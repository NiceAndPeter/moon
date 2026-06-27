/*
** Lua Parser - Utility Functions
** See Copyright Notice in lua.h
*/

#define MOON_CORE

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


void ExpDesc::init(expkind kind, int i) {
  setFalseList(NO_JUMP);
  setTrueList(NO_JUMP);
  setKind(kind);
  setInfo(i);
}


void ExpDesc::initString(TString *s) {
  setFalseList(NO_JUMP);
  setTrueList(NO_JUMP);
  setKind(VKSTR);
  setStringValue(s);
}


// External API wrapper
void moonY_checklimit (FuncState *funcState, int v, int l, const char *what) {
  funcState->checklimit(v, l, what);
}


// External API wrapper
lu_byte moonY_nvarstack (FuncState *funcState) {
  return funcState->nvarstack();
}


inline void enterlevel(LexState* lexState) {
	moonE_incCstack(lexState->getLuaState());
}

inline void leavelevel(LexState* lexState) noexcept {
	lexState->getLuaState()->getNumberOfCCallsRef()--;
}


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


// Priority table for binary operators is defined in parser.cpp

#define UNARY_PRIORITY	12  // priority for unary operators


/*
** structure to chain all variables in the left-hand side of an
** assignment
*/
struct LHS_assign {
  struct LHS_assign *prev;
  ExpDesc v;  // variable (global, local, upvalue, or indexed)
};


/*
** compiles the main function, which is a regular vararg function with an
** upvalue named MOON_ENV
*/
LClosure *moonY_parser (moon_State *L, ZIO *z, Mbuffer *buff,
                       Dyndata *dyd, const char *name, int firstchar) {
  LexState lexstate;
  LClosure *cl = LClosure::create(L, 1);  // create main closure
  setclLvalue2s(L, L->getTop().p, cl);  // anchor it (to avoid being collected)
  L->inctop();  lexstate.setTable(Table::create(L));  // create table for scanner
  sethvalue2s(L, L->getTop().p, lexstate.getTable());  // anchor it
  L->inctop();  Proto* proto = moonF_newproto(L);
  cl->setProto(proto);
  moonC_objbarrier(L, cl, cl->getProto());
  proto->setSource(TString::create(L, name));  // create and anchor TString
  moonC_objbarrier(L, proto, proto->getSource());
  FuncState funcstate(*proto, lexstate);
  lexstate.setBuffer(buff);
  lexstate.setDyndata(dyd);
  dyd->actvar().setN(0);
  dyd->gt.setN(0);
  dyd->label.setN(0);
  lexstate.setInput(L, z, funcstate.getProto().getSource(), firstchar);
  Parser parser(lexstate, nullptr);
  parser.mainfunc(&funcstate);
  moon_assert(!funcstate.getPrev() && funcstate.getNumUpvalues() == 1);
  // all scopes should be correctly finished
  moon_assert(dyd->actvar().getN() == 0 && dyd->gt.getN() == 0 && dyd->label.getN() == 0);
  L->getStackSubsystem().pop();  // remove scanner's table
  return cl;  // closure is on the stack, too
}
