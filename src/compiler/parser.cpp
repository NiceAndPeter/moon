/*
** Lua Parser - Parser Class Methods
** See Copyright Notice in lua.h
*/

#define MOON_CORE

#include "mprefix.h"


#include <climits>
#include <cstring>

#include "moon.h"

#include "mdebug.h"
#include "mdo.h"
#include "mfunc.h"
#include "mlex.h"
#include "mmem.h"
#include "mobject.h"
#include "mopcodes.h"
#include "mparser.h"
#include "mstate.h"
#include "mstring.h"
#include "mtable.h"
#include "../memory/mgc.h"


/* maximum number of variable declarations per function (must be
   smaller than 250, due to the bytecode format) */
#define MAXVARS		200


inline bool hasmultret(expkind k) noexcept {
	return (k) == VCALL || (k) == VVARARG;
}


/* because all strings are unified by the scanner, the parser
   can use pointer equality for string equality */
inline bool eqstr(const TString& a, const TString& b) noexcept {
	return (&a) == (&b);
}


inline void check_condition(Parser* parser, bool c, const char* msg) {
	if (!c) parser->getLexState().syntaxError(msg);
}


inline void enterlevel(LexState* lexState) {
	moonE_incCstack(lexState->getLuaState());
}

inline void leavelevel(LexState* lexState) noexcept {
	lexState->getLuaState()->getNumberOfCCallsRef()--;
}


/*
** nodes for block list (list of active blocks)
*/
typedef struct BlockCnt {
  struct BlockCnt *previous;  // chain
  int firstlabel;  // index of first label in this block
  int firstgoto;  // index of first pending goto in this block
  short numberOfActiveVariables;  // number of active declarations at block entry
  lu_byte upval;  // true if some variable in the block is an upvalue
  lu_byte isloop;  // 1 if 'block' is a loop; 2 if it has pending breaks
  lu_byte insidetbc;  // true if inside the scope of a to-be-closed var.
} BlockCnt;


typedef struct ConsControl {
  ExpDesc v;  // last list item read
  ExpDesc *t;  // table descriptor
  int nh;  // total number of 'record' elements
  int na;  // number of array elements already stored
  int tostore;  // number of array elements pending to be stored
  int maxtostore;  // maximum number of pending elements
} ConsControl;


/*
** Maximum number of elements in a constructor, to control the following:
** * counter overflows;
** * overflows in 'extra' for OP_NEWTABLE and OP_SETLIST;
** * overflows when adding multiple returns in OP_SETLIST.
*/
#define MAX_CNST	(INT_MAX/2)
#if MAX_CNST/(MAXARG_vC + 1) > MAXARG_Ax
#undef MAX_CNST
#define MAX_CNST	(MAXARG_Ax * (MAXARG_vC + 1))
#endif


inline UnOpr getunopr (int op) noexcept {
  switch (op) {
    case static_cast<int>(RESERVED::TK_NOT): return UnOpr::OPR_NOT;
    case '-': return UnOpr::OPR_MINUS;
    case '~': return UnOpr::OPR_BNOT;
    case '#': return UnOpr::OPR_LEN;
    default: return UnOpr::OPR_NOUNOPR;
  }
}


inline BinOpr getbinopr (int op) noexcept {
  switch (op) {
    case '+': return BinOpr::OPR_ADD;
    case '-': return BinOpr::OPR_SUB;
    case '*': return BinOpr::OPR_MUL;
    case '%': return BinOpr::OPR_MOD;
    case '^': return BinOpr::OPR_POW;
    case '/': return BinOpr::OPR_DIV;
    case static_cast<int>(RESERVED::TK_IDIV): return BinOpr::OPR_IDIV;
    case '&': return BinOpr::OPR_BAND;
    case '|': return BinOpr::OPR_BOR;
    case '~': return BinOpr::OPR_BXOR;
    case static_cast<int>(RESERVED::TK_SHL): return BinOpr::OPR_SHL;
    case static_cast<int>(RESERVED::TK_SHR): return BinOpr::OPR_SHR;
    case static_cast<int>(RESERVED::TK_CONCAT): return BinOpr::OPR_CONCAT;
    case static_cast<int>(RESERVED::TK_NE): return BinOpr::OPR_NE;
    case static_cast<int>(RESERVED::TK_EQ): return BinOpr::OPR_EQ;
    case '<': return BinOpr::OPR_LT;
    case static_cast<int>(RESERVED::TK_LE): return BinOpr::OPR_LE;
    case '>': return BinOpr::OPR_GT;
    case static_cast<int>(RESERVED::TK_GE): return BinOpr::OPR_GE;
    case static_cast<int>(RESERVED::TK_AND): return BinOpr::OPR_AND;
    case static_cast<int>(RESERVED::TK_OR): return BinOpr::OPR_OR;
    default: return BinOpr::OPR_NOBINOPR;
  }
}


/*
** Priority table for binary operators.
*/
static const struct {
  lu_byte left;  // left priority for each binary operator
  lu_byte right;  // right priority
} priority[] = {  // ORDER OPR
   {10, 10}, {10, 10},  // '+' '-'
   {11, 11}, {11, 11},  // '*' '%'
   {14, 13},  // '^' (right associative)
   {11, 11}, {11, 11},  // '/' '//'
   {6, 6}, {4, 4}, {5, 5},  // '&' '|' '~'
   {7, 7}, {7, 7},  // '<<' '>>'
   {9, 8},  // '..' (right associative)
   {3, 3}, {3, 3}, {3, 3},  // ==, <, <=
   {3, 3}, {3, 3}, {3, 3},  // ~=, >, >=
   {2, 2}, {1, 1}  // and, or
};

#define UNARY_PRIORITY	12  // priority for unary operators


/*
** structure to chain all variables in the left-hand side of an
** assignment
*/
struct LHS_assign {
  struct LHS_assign *prev;
  ExpDesc v;  // variable (global, local, upvalue, or indexed)
};


l_noret Parser::error_expected(int token) {
  lexState.syntaxError(
      moonO_pushfstring(lexState.getLuaState(), "%s expected", lexState.tokenToStr(token)));
}


int Parser::testnext(int c) {
  if (lexState.getToken() == c) {
    lexState.nextToken();
    return 1;
  }
  else return 0;
}


/*
** Check that next token is 'c'.
*/
void Parser::check(int c) {
  if (lexState.getToken() != c)
    error_expected(c);
}


/*
** Check that next token is 'c' and skip it.
*/
void Parser::checknext(int c) {
  check(c);
  lexState.nextToken();
}


/*
** Check that next token is 'what' and skip it. In case of error,
** raise an error that the expected 'what' should match a 'who'
** in line 'where' (if that is not the current line).
*/
void Parser::check_match(int what, int who, int where) {
  if (l_unlikely(!testnext(what))) {
    if (where == lexState.getLineNumber())  // all in the same line?
      error_expected(what);  // do not need a complex message
    else {
      lexState.syntaxError(moonO_pushfstring(lexState.getLuaState(),
             "%s expected (to close %s at line %d)",
              lexState.tokenToStr(what), lexState.tokenToStr(who), where));
    }
  }
}


TString *Parser::str_checkname() {
  TString *tstring;
  check(static_cast<int>(RESERVED::TK_NAME));
  tstring = lexState.getSemInfo().tstring;
  lexState.nextToken();
  return tstring;
}


void Parser::codename(ExpDesc& expr) {
  expr.initString(str_checkname());
}


/*
** Register a new local variable in the active 'Proto' (for debug
** information).
*/
int Parser::new_varkind(TString* name, lu_byte kind) {
  Dyndata *dynData = lexState.getDyndata();
  Vardesc *var;
  var = dynData->actvar().allocateNew();  // MoonVector automatically grows
  var->vd.kind = kind;  // default
  var->vd.name = name;
  return dynData->actvar().getN() - 1 - funcState->getFirstLocal();
}


/*
** Create a new local variable with the given 'name' and regular kind.
*/
int Parser::new_localvar(TString& name) {
  return new_varkind(&name, VDKREG);
}


/*
** Return the "variable description" (Vardesc) of a given variable.
** (Unless noted otherwise, all variables are referred to by their
** compiler indices.)
*/
void Parser::check_readonly(ExpDesc& expr) {
  // FuncState passed as parameter
  TString *variableName = nullptr;  // to be set if variable is const
  switch (expr.getKind()) {
    case VCONST: {
      variableName = lexState.getDyndata()->actvar()[expr.getInfo()].vd.name;
      break;
    }
    case VLOCAL: {
      Vardesc *vardesc = funcState->getlocalvardesc(expr.getLocalVarIndex());
      if (vardesc->vd.kind != VDKREG)  // not a regular variable?
        variableName = vardesc->vd.name;
      break;
    }
    case VUPVAL: {
      Upvaldesc *up = &funcState->getProto().getUpvalues()[expr.getInfo()];
      if (up->getKind() != VDKREG)
        variableName = up->getName();
      break;
    }
    case VINDEXUP: case VINDEXSTR: case VINDEXED: {  // global variable
      if (expr.isIndexedReadOnly())  // read-only?
        variableName = tsvalue(&funcState->getProto().getConstants()[expr.getIndexedStringKeyIndex()]);
      break;
    }
    default:
      moon_assert(expr.getKind() == VINDEXI);  // this one doesn't need any check
      return;  // integer index cannot be read-only
  }
  if (variableName)
    lexState.semerror("attempt to assign to const variable '%s'", getStringContents(variableName));
}


/*
** Start the scope for the last 'nvars' created variables.
*/
void Parser::adjustlocalvars(int nvars) {
  // FuncState passed as parameter
  auto regLevel = funcState->nvarstack();
  for (int i = 0; i < nvars; i++) {
    auto vidx = funcState->getNumActiveVarsRef()++;
    Vardesc *var = funcState->getlocalvardesc(vidx);
    var->vd.registerIndex = cast_byte(regLevel++);
    var->vd.protoLocalVarIndex = funcState->registerlocalvar(*var->vd.name);
    funcState->checklimit(regLevel, MAXVARS, "local variables");
  }
}


/*
** Close the scope for all variables up to level 'tolevel'.
** (debug info.)
*/
void Parser::buildglobal(TString& varname, ExpDesc& var) {
  // FuncState passed as parameter
  var.init(VGLOBAL, -1);  // global by default
  funcState->singlevaraux(*lexState.getEnvName(), var, 1);  // get environment variable
  if (var.getKind() == VGLOBAL)
    lexState.semerror("_ENV is global when accessing variable '%s'", getStringContents(&varname));
  funcState->exp2anyregup(var);  // _ENV could be a constant
  ExpDesc key;
  key.initString(&varname);  // key is variable name
  funcState->indexed(var, key);  // 'var' represents _ENV[varname]
}


/*
** Find a variable with the given name, handling global variables too.
*/
void Parser::buildvar(TString& varname, ExpDesc& var) {
  // FuncState passed as parameter
  var.init(VGLOBAL, -1);  // global by default
  funcState->singlevaraux(varname, var, 1);
  if (var.getKind() == VGLOBAL) {  // global name?
    auto info = var.getInfo();
    // global by default in the scope of a global declaration?
    if (info == -2)
      lexState.semerror("variable '%s' not declared", getStringContents(&varname));
    buildglobal(varname, var);
    if (info != -1 && lexState.getDyndata()->actvar()[info].vd.kind == GDKCONST)
      var.setIndexedReadOnly(1);  // mark variable as read-only
    else  // anyway must be a global
      moon_assert(info == -1 || lexState.getDyndata()->actvar()[info].vd.kind == GDKREG);
  }
}


void Parser::singlevar(ExpDesc& var) {
  buildvar(*str_checkname(), var);
}


/*
** Adjust the number of results from an expression list 'e' with 'nexps'
** expressions to 'nvars' values.
*/
void Parser::adjust_assign(int variableCount, int expressionCount, ExpDesc& lastExpr) {
  // FuncState passed as parameter
  auto needed = variableCount - expressionCount;  // extra values needed
  if (hasmultret(lastExpr.getKind())) {  // last expression has multiple returns?
    auto extra = needed + 1;  // discount last expression itself
    if (extra < 0)
      extra = 0;
    funcState->setreturns(lastExpr, extra);  // last exp. provides the difference
  }
  else {
    if (lastExpr.getKind() != VVOID)  // at least one expression?
      funcState->exp2nextreg(lastExpr);  // close last expression
    if (needed > 0)  // missing values?
      funcState->nil(funcState->getFirstFreeRegister(), needed);  // complete with nils
  }
  if (needed > 0)
    funcState->reserveregs(needed);  // registers for extra values
  else  // adding 'needed' is actually a subtraction
    funcState->setFirstFreeRegister(cast_byte(funcState->getFirstFreeRegister() + needed));  // remove extra values
}


int Parser::newgotoentry(TString& name, int line) {
  // FuncState passed as parameter
  auto pc = funcState->jump();  // create jump
  funcState->codeABC(OP_CLOSE, 0, 1, 0);  // spaceholder, marked as dead
  return lexState.newlabelentry(funcState, &lexState.getDyndata()->gt, &name, line, pc);
}


/*
** Create a new label with the given 'name' at the given 'line'.
** 'last' tells whether label is the last non-op statement in its
** block. Solves all pending gotos to this new label and adds
** a close instruction if necessary.
** Returns true iff it added a close instruction.
*/
Proto *Parser::addprototype() {
  Proto *clp;
  moon_State *state = lexState.getLuaState();
  FuncState *funcstate = funcState;
  Proto &proto = funcstate->getProto();  // prototype of current function
  if (funcstate->getNumberOfNestedPrototypes() >= proto.getProtosSize()) {
    auto oldsize = proto.getProtosSize();
    moonM_growvector<Proto*>(state, proto.getProtosRef(), funcstate->getNumberOfNestedPrototypes(), proto.getProtosSizeRef(), MAXARG_Bx, "functions");
    auto protosSpan = proto.getProtosSpan();
    while (oldsize < static_cast<int>(protosSpan.size()))
      protosSpan[oldsize++] = nullptr;
  }
  proto.getProtosSpan()[funcstate->getNumberOfNestedPrototypesRef()++] = clp = moonF_newproto(state);
  moonC_objbarrier(state, &proto, clp);
  return clp;
}


/*
** codes instruction to create new closure in parent function.
** The OP_CLOSURE instruction uses the last available register,
** so that, if it invokes the GC, the GC knows which registers
** are in use at that time.

*/
void Parser::codeclosure( ExpDesc& v) {
  FuncState *funcstate = funcState->getPrev();
  v.init(VRELOC, funcstate->codeABx(OP_CLOSURE, 0, funcstate->getNumberOfNestedPrototypes() - 1));
  funcstate->exp2nextreg(v);  // fix it at the last register
}


void Parser::open_func(FuncState *funcstate, BlockCnt& bl) {
  moon_State *state = lexState.getLuaState();
  Proto& f = funcstate->getProto();
  funcstate->setPrev(funcState);  // linked list of funcstates
  setFuncState(funcstate);
  funcstate->setPC(0);
  funcstate->setPreviousLine(f.getLineDefined());
  funcstate->setInstructionsSinceAbsoluteLineInfo(0);
  funcstate->setLastTarget(0);
  funcstate->setFirstFreeRegister(0);
  funcstate->setNumberOfConstants(0);
  funcstate->setNumberOfAbsoluteLineInfo(0);
  funcstate->setNumberOfNestedPrototypes(0);
  funcstate->setNumUpvalues(0);
  funcstate->setNumDebugVars(0);
  funcstate->setNumActiveVars(0);
  funcstate->setNeedClose(0);
  funcstate->setFirstLocal(lexState.getDyndata()->actvar().getN());
  funcstate->setFirstLabel(lexState.getDyndata()->label.getN());
  funcstate->setBlock(nullptr);
  f.setSource(lexState.getSource());
  moonC_objbarrier(state, &f, f.getSource());
  f.setMaxStackSize(2);  // registers 0/1 are always valid
  funcstate->setKCache(Table::create(state));  // create table for function
  sethvalue2s(state, state->getTop().p, funcstate->getKCache());  // anchor it
  state->inctop();
  funcstate->enterblock(bl, 0);
}


void Parser::close_func() {
  moon_State *state = lexState.getLuaState();
  FuncState *funcstate = funcState;
  Proto &f = funcstate->getProto();
  funcstate->ret(moonY_nvarstack(funcstate), 0);  // final return
  funcstate->leaveblock();
  moon_assert(funcstate->getBlock() == nullptr);
  funcstate->finish();
  moonM_shrinkvector<Instruction>(state, f.getCodeRef(), f.getCodeSizeRef(), funcstate->getPC());
  moonM_shrinkvector<ls_byte>(state, f.getLineInfoRef(), f.getLineInfoSizeRef(), funcstate->getPC());
  moonM_shrinkvector<AbsLineInfo>(state, f.getAbsLineInfoRef(), f.getAbsLineInfoSizeRef(),
                       funcstate->getNumberOfAbsoluteLineInfo());
  moonM_shrinkvector<TValue>(state, f.getConstantsRef(), f.getConstantsSizeRef(), funcstate->getNumberOfConstants());
  moonM_shrinkvector<Proto*>(state, f.getProtosRef(), f.getProtosSizeRef(), funcstate->getNumberOfNestedPrototypes());
  moonM_shrinkvector<LocVar>(state, f.getLocVarsRef(), f.getLocVarsSizeRef(), funcstate->getNumDebugVars());
  moonM_shrinkvector<Upvaldesc>(state, f.getUpvaluesRef(), f.getUpvaluesSizeRef(), funcstate->getNumUpvalues());
  setFuncState(funcstate->getPrev());
  state->getStackSubsystem().pop();  // pop kcache table
  moonC_checkGC(state);
}


/*
** {======================================================================
** GRAMMAR RULES
** =======================================================================
*/


/*
** check whether current token is in the follow set of a block.
** 'until' closes syntactical blocks, but do not close scope,
** so it is handled in separate.
*/
int Parser::block_follow( int withuntil) {
  switch (lexState.getToken()) {
    case static_cast<int>(RESERVED::TK_ELSE): case static_cast<int>(RESERVED::TK_ELSEIF):
    case static_cast<int>(RESERVED::TK_END): case static_cast<int>(RESERVED::TK_EOS):
      return 1;
    case static_cast<int>(RESERVED::TK_UNTIL): return withuntil;
    default: return 0;
  }
}


void Parser::statlist() {
  // statlist -> { stat [';'] }
  while (!block_follow(1)) {
    if (lexState.getToken() == static_cast<int>(RESERVED::TK_RETURN)) {
      statement();
      return;  // 'return' must be last statement
    }
    statement();
  }
}


void Parser::fieldsel( ExpDesc& v) {
  // fieldsel -> ['.' | ':'] NAME
  FuncState *funcstate = funcState;
  funcstate->exp2anyregup(v);
  lexState.nextToken();  // skip the dot or colon
  ExpDesc key;
  codename(key);
  funcstate->indexed(v, key);
}


void Parser::yindex( ExpDesc& v) {
  // index -> '[' expr ']'
  lexState.nextToken();  // skip the '['
  expr(v);
  funcState->exp2val(v);
  checknext( ']');
}


/*
** {======================================================================
** Rules for Constructors
** =======================================================================
*/

void Parser::recfield( ConsControl& cc) {
  // recfield -> (NAME | '['exp']') = exp
  FuncState *funcstate = funcState;
  lu_byte reg = funcState->getFirstFreeRegister();
  ExpDesc tab, key, val;
  if (lexState.getToken() == static_cast<int>(RESERVED::TK_NAME))
    codename(key);
  else  // lexState.getToken() == '['
    yindex(key);
  cc.nh++;
  checknext( '=');
  tab = *cc.t;
  funcstate->indexed(tab, key);
  expr(val);
  funcstate->storevar(tab, val);
  funcstate->setFirstFreeRegister(reg);  // free registers
}


void Parser::listfield( ConsControl& cc) {
  // listfield -> exp
  expr(cc.v);
  cc.tostore++;
}


void Parser::field( ConsControl& cc) {
  // field -> listfield | recfield
  switch(lexState.getToken()) {
    case static_cast<int>(RESERVED::TK_NAME): {  // may be 'listfield' or 'recfield'
      if (lexState.lookaheadToken() != '=')  // expression?
        listfield(cc);
      else
        recfield(cc);
      break;
    }
    case '[': {
      recfield(cc);
      break;
    }
    default: {
      listfield(cc);
      break;
    }
  }
}


/*
** Compute a limit for how many registers a constructor can use before
** emitting a 'SETLIST' instruction, based on how many registers are
** available.
*/
void Parser::constructor( ExpDesc& table_exp) {
  /* constructor -> '{' [ field { sep field } [sep] ] '}'
     sep -> ',' | ';' */
  FuncState *funcstate = funcState;
  auto line = lexState.getLineNumber();
  auto pc = funcstate->codevABCk(OP_NEWTABLE, 0, 0, 0, 0);
  ConsControl cc;
  funcstate->code(0);  // space for extra arg.
  cc.na = cc.nh = cc.tostore = 0;
  cc.t = &table_exp;
  table_exp.init(VNONRELOC, funcstate->getFirstFreeRegister());  // table will be at stack top
  funcstate->reserveregs(1);
  cc.v.init(VVOID, 0);  // no value (yet)
  checknext( '{' /*}*/);
  cc.maxtostore = funcstate->maxtostore();
  do {
    if (lexState.getToken() == /*{*/ '}') break;
    if (cc.v.getKind() != VVOID)  // is there a previous list item?
      funcstate->closelistfield(cc);  // close it
    field(cc);
    moonY_checklimit(funcstate, cc.tostore + cc.na + cc.nh, MAX_CNST,
                    "items in a constructor");
  } while (testnext( ',') || testnext( ';'));
  check_match( /*{*/ '}', '{' /*}*/, line);
  funcstate->lastlistfield(cc);
  funcstate->settablesize(pc, static_cast<unsigned>(table_exp.getInfo()), static_cast<unsigned>(cc.na), static_cast<unsigned>(cc.nh));
}

// }======================================================================


void Parser::parlist() {
  // parlist -> [ {NAME ','} (NAME | '...') ]
  FuncState *funcstate = funcState;
  Proto &f = funcstate->getProto();
  int nparams = 0;
  int isvararg = 0;
  if (lexState.getToken() != ')') {  // is 'parlist' not empty?
    do {
      switch (lexState.getToken()) {
        case static_cast<int>(RESERVED::TK_NAME): {
          new_localvar(*str_checkname());
          nparams++;
          break;
        }
        case static_cast<int>(RESERVED::TK_DOTS): {
          lexState.nextToken();
          isvararg = 1;
          break;
        }
        default: lexState.syntaxError( "<name> or '...' expected");
      }
    } while (!isvararg && testnext( ','));
  }
  adjustlocalvars(nparams);
  f.setNumParams(cast_byte(funcstate->getNumActiveVars()));
  if (isvararg)
    funcstate->setvararg(f.getNumParams());  // declared vararg
  funcstate->reserveregs(funcstate->getNumActiveVars());  // reserve registers for parameters
}


void Parser::body( ExpDesc& funcExpr, int isMethod, int line) {
  // body ->  '(' parlist ')' block END
  Proto* proto = addprototype();
  proto->setLineDefined(line);
  FuncState new_fs(*proto, lexState);
  BlockCnt bl;
  open_func(&new_fs, bl);
  checknext( '(');
  if (isMethod) {
    new_localvarliteral("self");  // create 'self' parameter
    adjustlocalvars(1);
  }
  parlist();
  checknext( ')');
  statlist();
  new_fs.getProto().setLastLineDefined(lexState.getLineNumber());
  check_match(static_cast<int>(RESERVED::TK_END), static_cast<int>(RESERVED::TK_FUNCTION), line);
  codeclosure(funcExpr);
  close_func();
}


int Parser::explist( ExpDesc& v) {
  // explist -> expr { ',' expr }
  int n = 1;  // at least one expression
  expr(v);
  while (testnext( ',')) {
    funcState->exp2nextreg(v);
    expr(v);
    n++;
  }
  return n;
}


void Parser::funcargs( ExpDesc& f) {
  FuncState *funcstate = funcState;
  ExpDesc args;
  int base, nparams;
  auto line = lexState.getLineNumber();
  switch (lexState.getToken()) {
    case '(': {  // funcargs -> '(' [ explist ] ')'
      lexState.nextToken();
      if (lexState.getToken() == ')')  // arg list is empty?
        args.setKind(VVOID);
      else {
        explist(args);
        if (hasmultret(args.getKind()))
          funcstate->setreturns(args, MOON_MULTRET);
      }
      check_match( ')', '(', line);
      break;
    }
    case '{' /*}*/: {  /* funcargs -> constructor */
      constructor(args);
      break;
    }
    case static_cast<int>(RESERVED::TK_STRING): {  // funcargs -> STRING
      args.initString(lexState.getSemInfo().tstring);
      lexState.nextToken();  // must use 'seminfo' before 'next'
      break;
    }
    default: {
      lexState.syntaxError( "function arguments expected");
    }
  }
  moon_assert(f.getKind() == VNONRELOC);
  base = f.getInfo();  // base register for call
  if (hasmultret(args.getKind()))
    nparams = MOON_MULTRET;  // open call
  else {
    if (args.getKind() != VVOID)
      funcstate->exp2nextreg(args);  // close last argument
    nparams = funcstate->getFirstFreeRegister() - (base+1);
  }
  f.init(VCALL, funcstate->codeABC(OP_CALL, base, nparams+1, 2));
  funcstate->fixline(line);
  /* call removes function and arguments and leaves one result (unless
     changed later) */
  funcstate->setFirstFreeRegister(cast_byte(base + 1));
}




/*
** {======================================================================
** Expression parsing
** =======================================================================
*/


void Parser::primaryexp( ExpDesc& v) {
  // primaryexp -> NAME | '(' expr ')'
  switch (lexState.getToken()) {
    case '(': {
      auto line = lexState.getLineNumber();
      lexState.nextToken();
      expr(v);
      check_match( ')', '(', line);
      funcState->dischargevars(v);
      return;
    }
    case static_cast<int>(RESERVED::TK_NAME): {
      singlevar(v);
      return;
    }
    default: {
      lexState.syntaxError( "unexpected symbol");
    }
  }
}


void Parser::suffixedexp( ExpDesc& v) {
  /* suffixedexp ->
       primaryexp { '.' NAME | '[' exp ']' | ':' NAME funcargs | funcargs } */
  FuncState *funcstate = funcState;
  primaryexp(v);
  for (;;) {
    switch (lexState.getToken()) {
      case '.': {  // fieldsel
        fieldsel(v);
        break;
      }
      case '[': {  // '[' exp ']'
        ExpDesc key;
        funcstate->exp2anyregup(v);
        yindex(key);
        funcstate->indexed(v, key);
        break;
      }
      case ':': {  // ':' NAME funcargs
        ExpDesc key;
        lexState.nextToken();
        codename(key);
        funcstate->self(v, key);
        funcargs(v);
        break;
      }
      case '(': case static_cast<int>(RESERVED::TK_STRING): case '{' /*}*/: {  /* funcargs */
        funcstate->exp2nextreg(v);
        funcargs(v);
        break;
      }
      default: return;
    }
  }
}


void Parser::simpleexp( ExpDesc& v) {
  /* simpleexp -> FLT | INT | STRING | NIL | TRUE | FALSE | ... |
                  constructor | FUNCTION body | suffixedexp */
  switch (lexState.getToken()) {
    case static_cast<int>(RESERVED::TK_FLT): {
      v.init(VKFLT, 0);
      v.setFloatValue(lexState.getSemInfo().r);
      break;
    }
    case static_cast<int>(RESERVED::TK_INT): {
      v.init(VKINT, 0);
      v.setIntValue(lexState.getSemInfo().i);
      break;
    }
    case static_cast<int>(RESERVED::TK_STRING): {
      v.initString(lexState.getSemInfo().tstring);
      break;
    }
    case static_cast<int>(RESERVED::TK_NIL): {
      v.init(VNIL, 0);
      break;
    }
    case static_cast<int>(RESERVED::TK_TRUE): {
      v.init(VTRUE, 0);
      break;
    }
    case static_cast<int>(RESERVED::TK_FALSE): {
      v.init(VFALSE, 0);
      break;
    }
    case static_cast<int>(RESERVED::TK_DOTS): {  // vararg
      FuncState *funcstate = funcState;
      check_condition(this, funcstate->getProto().getFlag() & PF_ISVARARG,
                      "cannot use '...' outside a vararg function");
      v.init(VVARARG, funcstate->codeABC(OP_VARARG, 0, 0, 1));
      break;
    }
    case '{' /*}*/: {  /* constructor */
      constructor(v);
      return;
    }
    case static_cast<int>(RESERVED::TK_FUNCTION): {
      lexState.nextToken();
      body(v, 0, lexState.getLineNumber());
      return;
    }
    default: {
      suffixedexp(v);
      return;
    }
  }
  lexState.nextToken();
}


BinOpr Parser::subexpr( ExpDesc& v, int limit) {
  BinOpr op;
  UnOpr uop;
  enterlevel(&lexState);
  uop = getunopr(lexState.getToken());
  if (uop != UnOpr::OPR_NOUNOPR) {  // prefix (unary) operator?
    int line = lexState.getLineNumber();
    lexState.nextToken();  // skip operator
    subexpr(v, UNARY_PRIORITY);
    funcState->prefix(uop, v, line);
  }
  else simpleexp(v);
  // expand while operators have priorities higher than 'limit'
  op = getbinopr(lexState.getToken());
  while (op != BinOpr::OPR_NOBINOPR && priority[static_cast<int>(op)].left > limit) {
    ExpDesc v2;
    BinOpr nextop;
    int line = lexState.getLineNumber();
    lexState.nextToken();  // skip operator
    funcState->infix(op, v);
    // read sub-expression with higher priority
    nextop = subexpr(v2, priority[static_cast<int>(op)].right);
    funcState->posfix(op, v, v2, line);
    op = nextop;
  }
  leavelevel(&lexState);
  return op;  // return first untreated operator
}


void Parser::expr( ExpDesc& v) {
  subexpr(v, 0);
}

// }====================================================================



/*
** {======================================================================
** Rules for Statements
** =======================================================================
*/


void Parser::block() {
  // block -> statlist
  FuncState *funcstate = funcState;
  BlockCnt bl;
  funcstate->enterblock(bl, 0);
  statlist();
  funcstate->leaveblock();
}


/*
** check whether, in an assignment to an upvalue/local variable, the
** upvalue/local variable is begin used in a previous assignment to a
** table. If so, save original upvalue/local value in a safe place and
** use this safe copy in the previous assignment.
*/
void Parser::check_conflict( struct LHS_assign *lh, ExpDesc& v) {
  FuncState *funcstate = funcState;
  lu_byte extra = funcstate->getFirstFreeRegister();  // eventual position to save local variable
  int conflict = 0;
  for (; lh; lh = lh->prev) {  // check all previous assignments
    if (ExpDesc::isIndexed(lh->v.getKind())) {  // assignment to table field?
      if (lh->v.getKind() == VINDEXUP) {  // is table an upvalue?
        if (v.getKind() == VUPVAL && lh->v.getIndexedTableReg() == v.getInfo()) {
          conflict = 1;  // table is the upvalue being assigned now
          lh->v.setKind(VINDEXSTR);
          lh->v.setIndexedTableReg(extra);  // assignment will use safe copy
        }
      }
      else {  // table is a register
        if (v.getKind() == VLOCAL && lh->v.getIndexedTableReg() == v.getLocalRegister()) {
          conflict = 1;  // table is the local being assigned now
          lh->v.setIndexedTableReg(extra);  // assignment will use safe copy
        }
        // is index the local being assigned?
        if (lh->v.getKind() == VINDEXED && v.getKind() == VLOCAL &&
            lh->v.getIndexedKeyIndex() == v.getLocalRegister()) {
          conflict = 1;
          lh->v.setIndexedKeyIndex(extra);  // previous assignment will use safe copy
        }
      }
    }
  }
  if (conflict) {
    // copy upvalue/local value to a temporary (in position 'extra')
    if (v.getKind() == VLOCAL)
      funcstate->codeABC(OP_MOVE, extra, v.getLocalRegister(), 0);
    else
      funcstate->codeABC(OP_GETUPVAL, extra, v.getInfo(), 0);
    funcstate->reserveregs(1);
  }
}


// Create code to store the "top" register in 'var'
void Parser::restassign( struct LHS_assign *lh, int nvars) {
  ExpDesc e;
  check_condition(this, ExpDesc::isVar(lh->v.getKind()), "syntax error");
  check_readonly(lh->v);
  if (testnext( ',')) {  // restassign -> ',' suffixedexp restassign
    struct LHS_assign nv;
    nv.prev = lh;
    suffixedexp(nv.v);
    if (!ExpDesc::isIndexed(nv.v.getKind()))
      check_conflict(lh, nv.v);
    enterlevel(&lexState);  // control recursion depth
    restassign(&nv, nvars+1);
    leavelevel(&lexState);
  }
  else {  // restassign -> '=' explist
    int nexps;
    checknext( '=');
    nexps = explist(e);
    if (nexps != nvars)
      adjust_assign(nvars, nexps, e);
    else {
      funcState->setoneret(e);  // close last expression
      funcState->storevar(lh->v, e);
      return;  // avoid default
    }
  }
  funcState->storevartop(lh->v);  // default assignment
}


int Parser::cond() {
  // cond -> exp
  ExpDesc v;
  expr(v);  // read condition
  if (v.getKind() == VNIL) v.setKind(VFALSE);  // 'falses' are all equal here
  funcState->goiftrue(v);
  return v.getFalseList();
}


void Parser::gotostat( int line) {
  TString *name = str_checkname();  // label's name
  newgotoentry(*name, line);
}


/*
** Break statement. Semantically equivalent to "goto break".
*/
void Parser::breakstat( int line) {
  BlockCnt *bl;  // to look for an enclosing loop
  for (bl = funcState->getBlock(); bl != nullptr; bl = bl->previous) {
    if (bl->isloop)  // found one?
      goto ok;
  }
  lexState.syntaxError( "break outside loop");
 ok:
  bl->isloop = 2;  // signal that block has pending breaks
  lexState.nextToken();  // skip break
  newgotoentry(*lexState.getBreakName(), line);
}


/*
** Check whether there is already a label with the given 'name' at
** current function.
*/
void Parser::checkrepeated(TString& name) {
  Labeldesc *lb = lexState.findlabel(&name, funcState->getFirstLabel());
  if (l_unlikely(lb != nullptr))  // already defined?
    lexState.semerror( "label '%s' already defined on line %d",
                      getStringContents(&name), lb->line);  // error
}


void Parser::labelstat(TString& name, int line) {
  // label -> '::' NAME '::'
  checknext(static_cast<int>(RESERVED::TK_DBCOLON));  // skip double colon
  while (lexState.getToken() == ';' || lexState.getToken() == static_cast<int>(RESERVED::TK_DBCOLON))
    statement();  // skip other no-op statements
  checkrepeated(name);  // check for repeated labels
  lexState.createlabel(funcState, &name, line, block_follow(0));
}


void Parser::whilestat( int line) {
  // whilestat -> WHILE cond DO block END
  FuncState *funcstate = funcState;
  BlockCnt bl;
  lexState.nextToken();  // skip WHILE
  auto whileinit = funcstate->getlabel();
  auto condexit = cond();
  funcstate->enterblock(bl, 1);
  checknext(static_cast<int>(RESERVED::TK_DO));
  block();
  funcstate->patchlist(funcstate->jump(), whileinit);
  check_match(static_cast<int>(RESERVED::TK_END), static_cast<int>(RESERVED::TK_WHILE), line);
  funcstate->leaveblock();
  funcstate->patchtohere(condexit);  // false conditions finish the loop
}


void Parser::repeatstat( int line) {
  // repeatstat -> REPEAT block UNTIL cond
  FuncState *funcstate = funcState;
  auto repeat_init = funcstate->getlabel();
  BlockCnt bl1, bl2;
  funcstate->enterblock(bl1, 1);  // loop block
  funcstate->enterblock(bl2, 0);  // scope block
  lexState.nextToken();  // skip REPEAT
  statlist();
  check_match(static_cast<int>(RESERVED::TK_UNTIL), static_cast<int>(RESERVED::TK_REPEAT), line);
  auto condexit = cond();  // read condition (inside scope block)
  funcstate->leaveblock();  // finish scope
  if (bl2.upval) {  // upvalues?
    int exit = funcstate->jump();  // normal exit must jump over fix
    funcstate->patchtohere(condexit);  // repetition must close upvalues
    funcstate->codeABC(OP_CLOSE, funcstate->reglevel(bl2.numberOfActiveVariables), 0, 0);
    condexit = funcstate->jump();  // repeat after closing upvalues
    funcstate->patchtohere(exit);  // normal exit comes to here
  }
  funcstate->patchlist(condexit, repeat_init);  // close the loop
  funcstate->leaveblock();  // finish loop
}


/*
** Read an expression and generate code to put its results in next
** stack slot.
**
*/
void Parser::exp1() {
  ExpDesc e;
  expr(e);
  funcState->exp2nextreg(e);
  moon_assert(e.getKind() == VNONRELOC);
}


/*
** Fix for instruction at position 'pcpos' to jump to 'dest'.
** (Jump addresses are relative in Lua). 'back' true means
** a back jump.
*/
void Parser::forbody( int base, int line, int nvars, int isgen) {
  // forbody -> DO block
  static const OpCode forprep[2] = {OP_FORPREP, OP_TFORPREP};
  static const OpCode forloop[2] = {OP_FORLOOP, OP_TFORLOOP};
  BlockCnt bl;
  FuncState *funcstate = funcState;
  int prep, endfor;
  checknext(static_cast<int>(RESERVED::TK_DO));
  prep = funcstate->codeABx(forprep[isgen], base, 0);
  funcstate->getFirstFreeRegisterRef()--;  // both 'forprep' remove one register from the stack
  funcstate->enterblock(bl, 0);  // scope for declared variables
  adjustlocalvars(nvars);
  funcstate->reserveregs(nvars);
  block();
  funcstate->leaveblock();  // end of scope for declared variables
  funcstate->fixforjump(prep, funcstate->getlabel(), 0);
  if (isgen) {  // generic for?
    funcstate->codeABC(OP_TFORCALL, base, 0, nvars);
    funcstate->fixline(line);
  }
  endfor = funcstate->codeABx(forloop[isgen], base, 0);
  funcstate->fixforjump(endfor, prep + 1, 1);
  funcstate->fixline(line);
}


void Parser::fornum(TString& varname, int line) {
  // fornum -> NAME = exp,exp[,exp] forbody
  FuncState *funcstate = funcState;
  int base = funcstate->getFirstFreeRegister();
  new_localvarliteral("(for state)");
  new_localvarliteral("(for state)");
  new_varkind(&varname, RDKCONST);  // control variable
  checknext( '=');
  exp1();  // initial value
  checknext( ',');
  exp1();  // limit
  if (testnext( ','))
    exp1();  // optional step
  else {  // default step = 1
    funcstate->intCode(funcstate->getFirstFreeRegister(), 1);
    funcstate->reserveregs(1);
  }
  adjustlocalvars(2);  // start scope for internal variables
  forbody(base, line, 1, 0);
}


void Parser::forlist(TString& indexname) {
  // forlist -> NAME {,NAME} IN explist forbody
  FuncState *funcstate = funcState;
  ExpDesc e;
  int nvars = 4;  // function, state, closing, control
  int base = funcstate->getFirstFreeRegister();
  // create internal variables
  new_localvarliteral("(for state)");  // iterator function
  new_localvarliteral("(for state)");  // state
  new_localvarliteral("(for state)");  // closing var. (after swap)
  new_varkind(&indexname, RDKCONST);  // control variable
  // other declared variables
  while (testnext( ',')) {
    new_localvar(*str_checkname());
    nvars++;
  }
  checknext(static_cast<int>(RESERVED::TK_IN));
  int line = lexState.getLineNumber();
  adjust_assign(4, explist(e), e);
  adjustlocalvars(3);  // start scope for internal variables
  funcstate->marktobeclosed();  // last internal var. must be closed
  funcstate->checkstack(2);  // extra space to call iterator
  forbody(base, line, nvars - 3, 1);
}


void Parser::forstat( int line) {
  // forstat -> FOR (fornum | forlist) END
  FuncState *funcstate = funcState;
  TString *varname;
  BlockCnt bl;
  funcstate->enterblock(bl, 1);  // scope for loop and control variables
  lexState.nextToken();  // skip 'for'
  varname = str_checkname();  // first variable name
  switch (lexState.getToken()) {
    case '=': fornum(*varname, line); break;
    case ',': case static_cast<int>(RESERVED::TK_IN): forlist(*varname); break;
    default: lexState.syntaxError( "'=' or 'in' expected");
  }
  check_match(static_cast<int>(RESERVED::TK_END), static_cast<int>(RESERVED::TK_FOR), line);
  funcstate->leaveblock();  // loop scope ('break' jumps to this point)
}


void Parser::test_then_block( int *escapelist) {
  // test_then_block -> [IF | ELSEIF] cond THEN block
  FuncState *funcstate = funcState;
  lexState.nextToken();  // skip IF or ELSEIF
  int condtrue = cond();  // read condition
  checknext(static_cast<int>(RESERVED::TK_THEN));
  block();  // 'then' part
  if (lexState.getToken() == static_cast<int>(RESERVED::TK_ELSE) ||
      lexState.getToken() == static_cast<int>(RESERVED::TK_ELSEIF))  // followed by 'else'/'elseif'?
    funcstate->concat(escapelist, funcstate->jump());  // must jump over it
  funcstate->patchtohere(condtrue);
}


void Parser::ifstat( int line) {
  // ifstat -> IF cond THEN block {ELSEIF cond THEN block} [ELSE block] END
  FuncState *funcstate = funcState;
  int escapelist = NO_JUMP;  // exit list for finished parts
  test_then_block(&escapelist);  // IF cond THEN block
  while (lexState.getToken() == static_cast<int>(RESERVED::TK_ELSEIF))
    test_then_block(&escapelist);  // ELSEIF cond THEN block
  if (testnext(static_cast<int>(RESERVED::TK_ELSE)))
    block();  // 'else' part
  check_match(static_cast<int>(RESERVED::TK_END), static_cast<int>(RESERVED::TK_IF), line);
  funcstate->patchtohere(escapelist);  // patch escape list to 'if' end
}


void Parser::localfunc() {
  ExpDesc b;
  FuncState *funcstate = funcState;
  int fvar = funcstate->getNumActiveVars();  // function's variable index
  new_localvar(*str_checkname());  // new local variable
  adjustlocalvars(1);  // enter its scope
  body(b, 0, lexState.getLineNumber());  // function created in next register
  // debug information will only see the variable after this point!
  funcstate->localdebuginfo( fvar)->setStartPC(funcstate->getPC());
}


lu_byte Parser::getvarattribute( lu_byte df) {
  // attrib -> ['<' NAME '>']
  if (testnext( '<')) {
    TString *tstring = str_checkname();
    const char *attr = getStringContents(tstring);
    checknext( '>');
    if (strcmp(attr, "const") == 0)
      return RDKCONST;  // read-only variable
    else if (strcmp(attr, "close") == 0)
      return RDKTOCLOSE;  // to-be-closed variable
    else
      lexState.semerror( "unknown attribute '%s'", attr);
  }
  return df;  // return default value
}


void Parser::localstat() {
  // stat -> LOCAL NAME attrib { ',' NAME attrib } ['=' explist]
  FuncState *funcstate = funcState;
  int toclose = -1;  // index of to-be-closed variable (if any)
  int vidx;  // index of last variable
  int nvars = 0;
  // get prefixed attribute (if any); default is regular local variable
  lu_byte defkind = getvarattribute(VDKREG);
  do {  // for each variable
    TString *vname = str_checkname();  // get its name
    lu_byte kind = getvarattribute(defkind);  // postfixed attribute
    vidx = new_varkind(vname, kind);  // predeclare it
    if (kind == RDKTOCLOSE) {  // to-be-closed?
      if (toclose != -1)  // one already present?
        lexState.semerror( "multiple to-be-closed variables in local list");
      toclose = funcstate->getNumActiveVars() + nvars;
    }
    nvars++;
  } while (testnext( ','));
  ExpDesc e;
  int nexps;
  if (testnext( '='))  // initialization?
    nexps = explist(e);
  else {
    e.setKind(VVOID);
    nexps = 0;
  }
  Vardesc *var = funcstate->getlocalvardesc( vidx);  // retrieve last variable
  if (nvars == nexps &&  // no adjustments?
      var->vd.kind == RDKCONST &&  // last variable is const?
      funcstate->exp2const(e, &var->k)) {  // compile-time constant?
    var->vd.kind = RDKCTC;  // variable is a compile-time constant
    adjustlocalvars(nvars - 1);  // exclude last variable
    funcstate->getNumActiveVarsRef()++;  // but count it
  }
  else {
    adjust_assign(nvars, nexps, e);
    adjustlocalvars(nvars);
  }
  funcstate->checktoclose(toclose);
}


lu_byte Parser::getglobalattribute( lu_byte df) {
  lu_byte kind = getvarattribute(df);
  switch (kind) {
    case RDKTOCLOSE:
      lexState.semerror( "global variables cannot be to-be-closed");
      return kind;  // to avoid warnings
    case RDKCONST:
      return GDKCONST;  // adjust kind for global variable
    default:
      return kind;
  }
}


void Parser::globalnames( lu_byte defkind) {
  FuncState *funcstate = funcState;
  int nvars = 0;
  int lastidx;  // index of last registered variable
  do {  // for each name
    TString *vname = str_checkname();
    lu_byte kind = getglobalattribute(defkind);
    lastidx = new_varkind( vname, kind);
    nvars++;
  } while (testnext( ','));
  if (testnext( '=')) {  // initialization?
    ExpDesc e;
    int i;
    int nexps = explist(e);  // read list of expressions
    adjust_assign(nvars, nexps, e);
    for (i = 0; i < nvars; i++) {  // for each variable
      ExpDesc var;
      TString *varname = funcstate->getlocalvardesc(lastidx - i)->vd.name;
      buildglobal(*varname, var);  // create global variable in 'var'
      funcstate->storevartop(var);
    }
  }
  funcstate->setNumActiveVars(cast_short(funcstate->getNumActiveVars() + nvars));  // activate declaration
}


void Parser::globalstat() {
  /* globalstat -> (GLOBAL) attrib '*'
     globalstat -> (GLOBAL) attrib NAME attrib {',' NAME attrib} */
  FuncState *funcstate = funcState;
  // get prefixed attribute (if any); default is regular global variable
  lu_byte defkind = getglobalattribute(GDKREG);
  if (!testnext( '*'))
    globalnames(defkind);
  else {
    // use nullptr as name to represent '*' entries
    new_varkind( nullptr, defkind);
    funcstate->getNumActiveVarsRef()++;  // activate declaration
  }
}


void Parser::globalfunc( int line) {
  // globalfunc -> (GLOBAL FUNCTION) NAME body
  ExpDesc var, b;
  FuncState *funcstate = funcState;
  TString *fname = str_checkname();
  new_varkind( fname, GDKREG);  // declare global variable
  funcstate->getNumActiveVarsRef()++;  // enter its scope
  buildglobal(*fname, var);
  body(b, 0, lexState.getLineNumber());  // compile and return closure in 'b'
  funcstate->storevar(var, b);
  funcstate->fixline(line);  // definition "happens" in the first line
}


void Parser::globalstatfunc( int line) {
  // stat -> GLOBAL globalfunc | GLOBAL globalstat
  lexState.nextToken();  // skip 'global'
  if (testnext(static_cast<int>(RESERVED::TK_FUNCTION)))
    globalfunc(line);
  else
    globalstat();
}


int Parser::funcname( ExpDesc& v) {
  // funcname -> NAME {fieldsel} [':' NAME]
  int ismethod = 0;
  singlevar(v);
  while (lexState.getToken() == '.')
    fieldsel(v);
  if (lexState.getToken() == ':') {
    ismethod = 1;
    fieldsel(v);
  }
  return ismethod;
}


void Parser::funcstat( int line) {
  // funcstat -> FUNCTION funcname body
  ExpDesc v, b;
  lexState.nextToken();  // skip FUNCTION
  int ismethod = funcname(v);
  check_readonly(v);
  body(b, ismethod, line);
  funcState->storevar(v, b);
  funcState->fixline(line);  // definition "happens" in the first line
}


void Parser::exprstat() {
  // stat -> func | assignment
  FuncState *funcstate = funcState;
  struct LHS_assign v;
  suffixedexp(v.v);
  if (lexState.getToken() == '=' || lexState.getToken() == ',') {  // stat -> assignment ?
    v.prev = nullptr;
    restassign(&v, 1);
  }
  else {  // stat -> func
    Instruction *inst;
    check_condition(this, v.v.getKind() == VCALL, "syntax error");
    inst = &getinstruction(funcstate, v.v);
    SETARG_C(*inst, 1);  // call statement uses no results
  }
}


void Parser::retstat() {
  // stat -> RETURN [explist] [';']
  FuncState *funcstate = funcState;
  ExpDesc e;
  int nret;  // number of values being returned
  int first = moonY_nvarstack(funcstate);  // first slot to be returned
  if (block_follow(1) || lexState.getToken() == ';')
    nret = 0;  // return no values
  else {
    nret = explist(e);  // optional return values
    if (hasmultret(e.getKind())) {
      funcstate->setreturns(e, MOON_MULTRET);
      if (e.getKind() == VCALL && nret == 1 && !funcstate->getBlock()->insidetbc) {  // tail call?
        SET_OPCODE(getinstruction(funcstate,e), OP_TAILCALL);
        moon_assert(InstructionView(getinstruction(funcstate,e)).a() == moonY_nvarstack(funcstate));
      }
      nret = MOON_MULTRET;  // return all values
    }
    else {
      if (nret == 1)  // only one single value?
        first = funcstate->exp2anyreg(e);  // can use original slot
      else {  // values must go to the top of the stack
        funcstate->exp2nextreg(e);
        moon_assert(nret == funcstate->getFirstFreeRegister() - first);
      }
    }
  }
  funcstate->ret(first, nret);
  testnext( ';');  // skip optional semicolon
}


void Parser::statement() {
  int line = lexState.getLineNumber();  // may be needed for error messages
  enterlevel(&lexState);
  switch (lexState.getToken()) {
    case ';': {  // stat -> ';' (empty statement)
      lexState.nextToken();  // skip ';'
      break;
    }
    case static_cast<int>(RESERVED::TK_IF): {  // stat -> ifstat
      ifstat(line);
      break;
    }
    case static_cast<int>(RESERVED::TK_WHILE): {  // stat -> whilestat
      whilestat(line);
      break;
    }
    case static_cast<int>(RESERVED::TK_DO): {  // stat -> DO block END
      lexState.nextToken();  // skip DO
      block();
      check_match(static_cast<int>(RESERVED::TK_END), static_cast<int>(RESERVED::TK_DO), line);
      break;
    }
    case static_cast<int>(RESERVED::TK_FOR): {  // stat -> forstat
      forstat(line);
      break;
    }
    case static_cast<int>(RESERVED::TK_REPEAT): {  // stat -> repeatstat
      repeatstat(line);
      break;
    }
    case static_cast<int>(RESERVED::TK_FUNCTION): {  // stat -> funcstat
      funcstat(line);
      break;
    }
    case static_cast<int>(RESERVED::TK_LOCAL): {  // stat -> localstat
      lexState.nextToken();  // skip LOCAL
      if (testnext(static_cast<int>(RESERVED::TK_FUNCTION)))  // local function?
        localfunc();
      else
        localstat();
      break;
    }
    case static_cast<int>(RESERVED::TK_GLOBAL): {  // stat -> globalstatfunc
      globalstatfunc(line);
      break;
    }
    case static_cast<int>(RESERVED::TK_DBCOLON): {  // stat -> label
      lexState.nextToken();  // skip double colon
      labelstat(*str_checkname(), line);
      break;
    }
    case static_cast<int>(RESERVED::TK_RETURN): {  // stat -> retstat
      lexState.nextToken();  // skip RETURN
      retstat();
      break;
    }
    case static_cast<int>(RESERVED::TK_BREAK): {  // stat -> breakstat
      breakstat(line);
      break;
    }
    case static_cast<int>(RESERVED::TK_GOTO): {  // stat -> 'goto' NAME
      lexState.nextToken();  // skip 'goto'
      gotostat(line);
      break;
    }
#if defined(MOON_COMPAT_GLOBAL)
    case static_cast<int>(RESERVED::TK_NAME): {
      /* compatibility code to parse global keyword when "global"
         is not reserved */
      if (lexState.getSemInfo().tstring == lexState.getGlobalName()) {  // current = "global"?
        int lk = lexState.lookaheadToken();
        if (lk == '<' || lk == static_cast<int>(RESERVED::TK_NAME) || lk == '*' || lk == static_cast<int>(RESERVED::TK_FUNCTION)) {
          /* 'global <attrib>' or 'global name' or 'global *' or
             'global function' */
          globalstatfunc(line);
          break;
        }
      }  // else...
    }
#endif
    // FALLTHROUGH
    default: {  // stat -> func | assignment
      exprstat();
      break;
    }
  }
  moon_assert(funcState->getProto().getMaxStackSize() >= funcState->getFirstFreeRegister() &&
             funcState->getFirstFreeRegister() >= moonY_nvarstack(funcState));
  funcState->setFirstFreeRegister(moonY_nvarstack(funcState));  // free registers
  leavelevel(&lexState);
}

// }======================================================================

// }======================================================================


/*
** compiles the main function, which is a regular vararg function with an
** upvalue named MOON_ENV
*/
void Parser::mainfunc(FuncState *funcstate) {
  BlockCnt bl;
  Upvaldesc *env;
  open_func(funcstate, bl);
  funcstate->setvararg(0);  // main function is always declared vararg
  env = funcstate->allocupvalue();  // ...set environment upvalue
  env->setInStack(1);
  env->setIndex(0);
  env->setKind(VDKREG);
  env->setName(lexState.getEnvName());
  moonC_objbarrier(lexState.getLuaState(), &funcstate->getProto(), env->getName());
  lexState.nextToken();  // read first token
  statlist();  // parse main body
  check(static_cast<int>(RESERVED::TK_EOS));
  close_func();
}


