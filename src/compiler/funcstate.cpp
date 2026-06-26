/*
** Lua Parser - FuncState Methods
** See Copyright Notice in lua.h
*/

#define LUA_CORE

#include "lprefix.h"


#include <climits>
#include <cstring>

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "llex.h"
#include "lmem.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lparser.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "../memory/lgc.h"


/* because all strings are unified by the scanner, the parser
   can use pointer equality for string equality */
inline bool eqstr(const TString& a, const TString& b) noexcept {
	return (&a) == (&b);
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


inline bool hasmultret(expkind k) noexcept {
	return (k) == VCALL || (k) == VVARARG;
}


typedef struct ConsControl {
  ExpDesc v;  // last list item read
  ExpDesc *t;  // table descriptor
  int nh;  // total number of 'record' elements
  int na;  // number of array elements already stored
  int tostore;  // number of array elements pending to be stored
  int maxtostore;  // maximum number of pending elements
} ConsControl;


l_noret FuncState::errorlimit(int limit, const char *what) {
  lua_State *L = getLexState().getLuaState();
  int line = getProto().getLineDefined();
  const char *where = (line == 0)
                      ? "main function"
                      : luaO_pushfstring(L, "function at line %d", line);
  const char *msg = luaO_pushfstring(L, "too many %s (limit is %d) in %s",
                             what, limit, where);
  getLexState().syntaxError(msg);
}


void FuncState::checklimit(int v, int l, const char *what) {
  if (l_unlikely(v > l)) errorlimit(l, what);
}


/*
** Register a new local variable in the active 'Proto' (for debug
** information).
*/
short FuncState::registerlocalvar(TString& varname) {
  Proto &proto = getProto();
  int oldsize = proto.getLocVarsSize();
  luaM_growvector<LocVar>(getLexState().getLuaState(), proto.getLocVarsRef(), getNumDebugVars(), proto.getLocVarsSizeRef(),
                  SHRT_MAX, "local variables");
  auto locVarsSpan = proto.getDebugInfo().getLocVarsSpan();
  while (oldsize < static_cast<int>(locVarsSpan.size()))
    locVarsSpan[oldsize++].setVarName(nullptr);
  locVarsSpan[getNumDebugVars()].setVarName(&varname);
  locVarsSpan[getNumDebugVars()].setStartPC(getPC());
  luaC_objbarrier(getLexState().getLuaState(), &proto, &varname);
  return postIncrementNumDebugVars();
}


/*
** Return the "variable description" (Vardesc) of a given variable.
** (Unless noted otherwise, all variables are referred to by their
** compiler indices.)
*/
Vardesc *FuncState::getlocalvardesc(int vidx) {
  return &getLexState().getDyndata()->actvar()[getFirstLocal() + vidx];
}


/*
** Convert 'nvar', a compiler index level, to its corresponding
** register. For that, search for the highest variable below that level
** that is in a register and uses its register index ('ridx') plus one.
*/
lu_byte FuncState::reglevel(int nvar) {
  while (nvar-- > 0) {
    Vardesc *vd = getlocalvardesc(nvar);  // get previous variable
    if (vd->isInReg())  // is in a register?
      return cast_byte(vd->vd.registerIndex + 1);
  }
  return 0;  // no variables in registers
}


/*
** Return the number of variables in the register stack for the given
** function.
*/
lu_byte FuncState::nvarstack() {
  return reglevel(getNumActiveVars());
}


/*
** Get the debug-information entry for current variable 'vidx'.
*/
LocVar *FuncState::localdebuginfo(int vidx) {
  Vardesc *vd = getlocalvardesc(vidx);
  if (!vd->isInReg())
    return nullptr;  // no debug info. for constants
  else {
    int idx = vd->vd.protoLocalVarIndex;
    lua_assert(idx < getNumDebugVars());
    return &getProto().getLocVars()[idx];
  }
}


/*
** Create an expression representing variable 'vidx'
*/
void FuncState::init_var(ExpDesc& e, int vidx) {
  e.setFalseList(NO_JUMP);
  e.setTrueList(NO_JUMP);
  e.setKind(VLOCAL);
  e.setLocalVarIndex(cast_short(vidx));
  e.setLocalRegister(getlocalvardesc(vidx)->vd.registerIndex);
}


/*
** Close the scope for all variables up to level 'tolevel'.
** (debug info.)
*/
void FuncState::removevars(int tolevel) {
  int current_n = getLexState().getDyndata()->actvar().getN();
  getLexState().getDyndata()->actvar().setN(current_n - (getNumActiveVars() - tolevel));
  while (getNumActiveVars() > tolevel) {
    LocVar *var = localdebuginfo(--getNumActiveVarsRef());
    if (var)  // does it have debug information?
      var->setEndPC(getPC());
  }
}


/*
** Search the upvalues of the function for one
** with the given 'name'.
*/
int FuncState::searchupvalue(TString& name) {
  auto upvaluesSpan = getProto().getUpvaluesSpan();
  for (size_t i = 0; i < static_cast<size_t>(getNumUpvalues()); i++) {
    if (eqstr(*upvaluesSpan[i].getName(), name)) return static_cast<int>(i);
  }
  return -1;  // not found
}


Upvaldesc *FuncState::allocupvalue() {
  Proto &proto = getProto();
  int oldsize = proto.getUpvaluesSize();
  checklimit(getNumUpvalues() + 1, MAXUPVAL, "upvalues");
  luaM_growvector<Upvaldesc>(getLexState().getLuaState(), proto.getUpvaluesRef(), getNumUpvalues(), proto.getUpvaluesSizeRef(),
                  MAXUPVAL, "upvalues");
  auto upvaluesSpan = proto.getUpvaluesSpan();
  while (oldsize < static_cast<int>(upvaluesSpan.size()))
    upvaluesSpan[oldsize++].setName(nullptr);
  return &upvaluesSpan[getNumUpvaluesRef()++];
}


int FuncState::newupvalue(TString& name, ExpDesc& v) {
  Upvaldesc *up = allocupvalue();
  FuncState *prevFunc = getPrev();
  if (v.getKind() == VLOCAL) {
    up->setInStack(1);
    up->setIndex(v.getLocalRegister());
    up->setKind(prevFunc->getlocalvardesc(v.getLocalVarIndex())->vd.kind);
    lua_assert(eqstr(name, *prevFunc->getlocalvardesc(v.getLocalVarIndex())->vd.name));
  }
  else {
    up->setInStack(0);
    up->setIndex(cast_byte(v.getInfo()));
    up->setKind(prevFunc->getProto().getUpvalues()[v.getInfo()].getKind());
    lua_assert(eqstr(name, *prevFunc->getProto().getUpvalues()[v.getInfo()].getName()));
  }
  up->setName(&name);
  luaC_objbarrier(getLexState().getLuaState(), &getProto(), &name);
  return getNumUpvalues() - 1;
}


/*
** Look for an active variable with the name 'n' in the function.
** If found, initialize 'var' with it and return its expression kind;
** otherwise return -1. While searching, var->u.info==-1 means that
** the preambular global declaration is active (the default while
** there is no other global declaration); var->u.info==-2 means there
** is no active collective declaration (some previous global declaration
** but no collective declaration); and var->u.info>=0 points to the
** inner-most (the first one found) collective declaration, if there is one.
*/
int FuncState::searchvar(TString& n, ExpDesc& var) {
  int nactive = static_cast<int>(getNumActiveVars());
  for (int i = nactive - 1; i >= 0; i--) {
    Vardesc *vd = getlocalvardesc(i);
    if (vd->isGlobal()) {  // global declaration?
      if (vd->vd.name == nullptr) {  // collective declaration?
        if (var.getInfo() < 0)  // no previous collective declaration?
          var.setInfo(getFirstLocal() + i);  // this is the first one
      }
      else {  // global name
        if (eqstr(n, *vd->vd.name)) {  // found?
          var.init(VGLOBAL, getFirstLocal() + i);
          return VGLOBAL;
        }
        else if (var.getInfo() == -1)  // active preambular declaration?
          var.setInfo(-2);  // invalidate preambular declaration
      }
    }
    else if (eqstr(n, *vd->vd.name)) {  // found?
      if (vd->vd.kind == RDKCTC)  // compile-time constant?
        var.init(VCONST, getFirstLocal() + i);
      else  // local variable
        init_var(var, i);
      return cast_int(var.getKind());
    }
  }
  return -1;  // not found
}


/*
** Mark block where variable at given level was defined
** (to emit close instructions later).
*/
void FuncState::markupval(int level) {
  BlockCnt *block = getBlock();
  while (block->numberOfActiveVariables > level)
    block = block->previous;
  block->upval = 1;
  setNeedClose(1);
}


/*
** Mark that current block has a to-be-closed variable.
*/
void FuncState::marktobeclosed() {
  BlockCnt *block = getBlock();
  block->upval = 1;
  block->insidetbc = 1;
  setNeedClose(1);
}


/*
** Find a variable with the given name 'n'. If it is an upvalue, add
** this upvalue into all intermediate functions. If it is a global, set
** 'var' as 'void' as a flag.
*/
void FuncState::singlevaraux(TString& n, ExpDesc& var, int base) {
  int v = searchvar(n, var);  // look up variables at current level
  if (v >= 0) {  // found?
    if (v == VLOCAL && !base)
      markupval(var.getLocalVarIndex());  // local will be used as an upval
  }
  else {  // not found at current level; try upvalues
    int idx = searchupvalue(n);  // try existing upvalues
    if (idx < 0) {  // not found?
      if (getPrev() != nullptr)  // more levels?
        getPrev()->singlevaraux(n, var, 0);  // try upper levels
      if (var.getKind() == VLOCAL || var.getKind() == VUPVAL)  // local or upvalue?
        idx = newupvalue(n, var);  // will be a new upvalue
      else  // it is a global or a constant
        return;  // don't need to do anything at this level
    }
    var.init(VUPVAL, idx);  // new or old upvalue
  }
}


/*
** Traverse the pending gotos of the finishing block checking whether
** each match some label of that block. Those that do not match are
** "exported" to the outer block, to be solved there. In particular,
** its 'numberOfActiveVariables' is updated with the level of the inner block,
** as the variables of the inner block are now out of scope.
*/
void FuncState::solvegotos(BlockCnt& blockCnt) {
  Labellist *gl = &lexState.getDyndata()->gt;
  int outlevel = reglevel(blockCnt.numberOfActiveVariables);  // level outside the block
  int igt = blockCnt.firstgoto;  // first goto in the finishing block
  while (igt < gl->getN()) {  // for each pending goto
    Labeldesc *gt = &(*gl)[igt];
    // search for a matching label in the current block
    Labeldesc *lb = lexState.findlabel(gt->name, blockCnt.firstlabel);
    if (lb != nullptr)  // found a match?
      lexState.closegoto(this, igt, lb, blockCnt.upval);  // close and remove goto
    else {  // adjust 'goto' for outer block
      /* block has variables to be closed and goto escapes the scope of
         some variable? */
      if (blockCnt.upval && reglevel(gt->numberOfActiveVariables) > outlevel)
        gt->close = 1;  // jump may need a close
      gt->numberOfActiveVariables = blockCnt.numberOfActiveVariables;  // correct level for outer block
      igt++;  // go to next goto
    }
  }
  lexState.getDyndata()->label.setN(blockCnt.firstlabel);  // remove local labels
}


void FuncState::enterblock(BlockCnt& blk, lu_byte isloop) {
  blk.isloop = isloop;
  blk.numberOfActiveVariables = getNumActiveVars();
  blk.firstlabel = getLexState().getDyndata()->label.getN();
  blk.firstgoto = getLexState().getDyndata()->gt.getN();
  blk.upval = 0;
  // inherit 'insidetbc' from enclosing block
  blk.insidetbc = (getBlock() != nullptr && getBlock()->insidetbc);
  blk.previous = getBlock();  // link block in function's block list
  setBlock(&blk);
  lua_assert(getFirstFreeRegister() == luaY_nvarstack(this));
}


void FuncState::leaveblock() {
  BlockCnt *blk = getBlock();
  LexState& lexstate = getLexState();
  lu_byte stklevel = reglevel(blk->numberOfActiveVariables);  // level outside block
  if (blk->previous && blk->upval)  // need a 'close'?
    codeABC(OP_CLOSE, stklevel, 0, 0);
  setFirstFreeRegister(stklevel);  // free registers
  removevars(blk->numberOfActiveVariables);  // remove block locals
  lua_assert(blk->numberOfActiveVariables == getNumActiveVars());  // back to level on entry
  if (blk->isloop == 2)  // has to fix pending breaks?
    lexstate.createlabel(this, lexstate.getBreakName(), 0, 0);
  solvegotos(*blk);
  if (blk->previous == nullptr) {  // was it the last block?
    if (blk->firstgoto < lexstate.getDyndata()->gt.getN())  // still pending gotos?
      lexstate.undefgoto(this, &lexstate.getDyndata()->gt[blk->firstgoto]);  // error
  }
  setBlock(blk->previous);  // current block now is previous one
}


void FuncState::closelistfield(ConsControl& cc) {
  lua_assert(cc.tostore > 0);
  exp2nextreg(cc.v);
  cc.v.setKind(VVOID);
  if (cc.tostore >= cc.maxtostore) {
    setlist(cc.t->getInfo(), cc.na, cc.tostore);  // flush
    cc.na += cc.tostore;
    cc.tostore = 0;  // no more items pending
  }
}


void FuncState::lastlistfield(ConsControl& cc) {
  if (cc.tostore == 0) return;
  if (hasmultret(cc.v.getKind())) {
    setreturns(cc.v, LUA_MULTRET);
    setlist(cc.t->getInfo(), cc.na, LUA_MULTRET);
    cc.na--;  // do not count last expression (unknown number of elements)
  }
  else {
    if (cc.v.getKind() != VVOID)
      exp2nextreg(cc.v);
    setlist(cc.t->getInfo(), cc.na, cc.tostore);
  }
  cc.na += cc.tostore;
}


/*
** Compute a limit for how many registers a constructor can use before
** emitting a 'SETLIST' instruction, based on how many registers are
** available.
*/
int FuncState::maxtostore() {
  int numfreeregs = MAX_FSTACK - getFirstFreeRegister();
  if (numfreeregs >= 160)  // "lots" of registers?
    return numfreeregs / 5;  // use up to 1/5 of them
  else if (numfreeregs >= 80)  // still "enough" registers?
    return 10;  // one 'SETLIST' instruction for each 10 values
  else  // save registers for potential more nesting
    return 1;
}


void FuncState::setvararg(int nparams) {
  getProto().setFlag(getProto().getFlag() | PF_ISVARARG);
  codeABC(OP_VARARGPREP, nparams, 0, 0);
}


// Create code to store the "top" register in 'var'
void FuncState::storevartop(ExpDesc& var) {
  ExpDesc e;
  e.init(VNONRELOC, getFirstFreeRegister() - 1);
  storevar(var, e);  // will also free the top register
}


/*
** Fix for instruction at position 'pcpos' to jump to 'dest'.
** (Jump addresses are relative in Lua). 'back' true means
** a back jump.
*/
void FuncState::fixforjump(int pcpos, int dest, int back) {
  Instruction *jmp = &getProto().getCode()[pcpos];
  int offset = dest - (pcpos + 1);
  if (back)
    offset = -offset;
  if (l_unlikely(offset > MAXARG_Bx))
    getLexState().syntaxError("control structure too long");
  SETARG_Bx(*jmp, static_cast<unsigned int>(offset));
}


void FuncState::checktoclose(int level) {
  if (level != -1) {  // is there a to-be-closed variable?
    marktobeclosed();
    codeABC(OP_TBC, reglevel(level), 0, 0);
  }
}
