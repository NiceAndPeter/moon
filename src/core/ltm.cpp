/*
** Tag methods
** See Copyright Notice in lua.h
*/

#define MOON_CORE

#include "lprefix.h"


#include <array>
#include <cstring>

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lvm.h"


static const char udatatypename[] = "userdata";

MOONI_DDEF const TypeNamesArray moonT_typenames_ = {
  "no value",
  "nil", "boolean", udatatypename, "number",
  "string", "table", "function", udatatypename, "thread",
  "upvalue", "proto"  // these last cases are used for tests only
};


void moonT_init (moon_State *L) {
  static constexpr std::array<const char*, 25> moonT_eventname = {  // ORDER TM
    "__index", "__newindex",
    "__gc", "__mode", "__len", "__eq",
    "__add", "__sub", "__mul", "__mod", "__pow",
    "__div", "__idiv",
    "__band", "__bor", "__bxor", "__shl", "__shr",
    "__unm", "__bnot", "__lt", "__le",
    "__concat", "__call", "__close"
  };
  for (int i=0; i<static_cast<int>(TMS::TM_N); i++) {
    G(L)->setTMName(i, TString::create(L, moonT_eventname[static_cast<size_t>(i)]));
    obj2gco(G(L)->getTMName(i))->fix(L);  // never collect these names
  }
}


/*
** function to be used with macro "fasttm": optimized for absence of
** tag methods
*/
const TValue *moonT_gettm (const Table *events, TMS event, TString *ename) {
  const TValue *metamethod = events->HgetShortStr(ename);
  moon_assert(event <= TMS::TM_EQ);
  if (notm(metamethod)) {  // no tag method?
    events->setFlagBits(1 << static_cast<int>(event));  // cache this fact (flags is mutable)
    return nullptr;
  }
  else return metamethod;
}


const TValue *moonT_gettmbyobj (moon_State *L, const TValue *o, TMS event) {
  Table *mt;
  switch (ttype(o)) {
    case MOON_TTABLE:
      mt = hvalue(o)->getMetatable();
      break;
    case MOON_TUSERDATA:
      mt = uvalue(o)->getMetatable();
      break;
    default:
      mt = G(L)->getMetatable(ttype(o));
  }
  return (mt ? mt->HgetShortStr(G(L)->getTMName(static_cast<int>(event))) : G(L)->getNilValue());
}


/*
** Return the name of the type of an object. For tables and userdata
** with metatable, use their '__name' metafield, if present.
*/
const char *moonT_objtypename (moon_State *L, const TValue *o) {
  Table *mt;
  if ((ttistable(o) && (mt = hvalue(o)->getMetatable()) != nullptr) ||
      (ttisfulluserdata(o) && (mt = uvalue(o)->getMetatable()) != nullptr)) {
    const TValue *name = mt->HgetShortStr(TString::create(L, "__name"));
    if (ttisstring(name))  // is '__name' a string?
      return getStringContents(tsvalue(name));  // use it as type name
  }
  return ttypename(ttype(o));  // else use standard type name
}


void moonT_callTM (moon_State *L, const TValue *f, const TValue *p1,
                  const TValue *p2, const TValue *p3) {
  StkId func = L->getTop().p;
  auto& stack = L->getStackSubsystem();
  stack.setSlot(func, f);  // push function (assume EXTRA_STACK)
  stack.setSlot(func + 1, p1);  // 1st argument
  stack.setSlot(func + 2, p2);  // 2nd argument
  stack.setSlot(func + 3, p3);  // 3rd argument
  stack.adjust(4);
  // metamethod may yield only when called from Lua code
  if (L->getCI()->isLuaCode())
    L->call(func, 0);
  else
    L->callNoYield( func, 0);
}


MoonT moonT_callTMres (moon_State *L, const TValue *f, const TValue *p1,
                        const TValue *p2, StkId res) {
  ptrdiff_t result = L->saveStack(res);
  StkId func = L->getTop().p;
  auto& stack = L->getStackSubsystem();
  stack.setSlot(func, f);  // push function (assume EXTRA_STACK)
  stack.setSlot(func + 1, p1);  // 1st argument
  stack.setSlot(func + 2, p2);  // 2nd argument
  stack.adjust(3);
  // metamethod may yield only when called from Lua code
  if (L->getCI()->isLuaCode())
    L->call(func, 1);
  else
    L->callNoYield( func, 1);
  res = L->restoreStack(result);
  *s2v(res) = *s2v(--L->getTop().p);  /* move result to its place - use operator= */
  return ttypetag(s2v(res));  // return tag of the result
}


static int callbinTM (moon_State *L, const TValue *p1, const TValue *p2,
                      StkId res, TMS event) {
  const TValue *metamethod = moonT_gettmbyobj(L, p1, event);  // try first operand
  if (notm(metamethod))
    metamethod = moonT_gettmbyobj(L, p2, event);  // try second operand
  if (notm(metamethod))
    return -1;  // tag method not found
  else  // call tag method and return the tag of the result
    return static_cast<int>(moonT_callTMres(L, metamethod, p1, p2, res));
}


void moonT_trybinTM (moon_State *L, const TValue *p1, const TValue *p2,
                    StkId res, TMS event) {
  if (l_unlikely(callbinTM(L, p1, p2, res, event) < 0)) {
    switch (event) {
      case TMS::TM_BAND: case TMS::TM_BOR: case TMS::TM_BXOR:
      case TMS::TM_SHL: case TMS::TM_SHR: case TMS::TM_BNOT: {
        if (ttisnumber(p1) && ttisnumber(p2))
          moonG_tointerror(L, p1, p2);
        else
          moonG_opinterror(L, p1, p2, "perform bitwise operation on");
      }
      /* calls never return, but to avoid warnings: *//* FALLTHROUGH */
      default:
        moonG_opinterror(L, p1, p2, "perform arithmetic on");
    }
  }
}


/*
** The use of 'p1' after 'callbinTM' is safe because, when a tag
** method is not found, 'callbinTM' cannot change the stack.
*/
void moonT_tryconcatTM (moon_State *L) {
  StkId p1 = L->getTop().p - 2;  // first argument
  if (l_unlikely(callbinTM(L, s2v(p1), s2v(p1 + 1), p1, TMS::TM_CONCAT) < 0))
    moonG_concaterror(L, s2v(p1), s2v(p1 + 1));
}


void moonT_trybinassocTM (moon_State *L, const TValue *p1, const TValue *p2,
                                       int flip, StkId res, TMS event) {
  if (flip)
    moonT_trybinTM(L, p2, p1, res, event);
  else
    moonT_trybinTM(L, p1, p2, res, event);
}


void moonT_trybiniTM (moon_State *L, const TValue *p1, moon_Integer i2,
                                   int flip, StkId res, TMS event) {
  TValue aux;
  aux.setInt(i2);
  moonT_trybinassocTM(L, p1, &aux, flip, res, event);
}


/*
** Calls an order tag method.
*/
int moonT_callorderTM (moon_State *L, const TValue *p1, const TValue *p2,
                      TMS event) {
  int tag = callbinTM(L, p1, p2, L->getTop().p, event);  // try original event
  if (tag >= 0)  // found tag method?
    return !tagisfalse(tag);
  moonG_ordererror(L, p1, p2);  // no metamethod found
  return 0;  // to avoid warnings
}


int moonT_callorderiTM (moon_State *L, const TValue *p1, int v2,
                       int flip, int isfloat, TMS event) {
  TValue aux; const TValue *p2;
  if (isfloat) {
    aux.setFloat(cast_num(v2));
  }
  else
    aux.setInt(v2);
  if (flip) {  // arguments were exchanged?
    p2 = p1; p1 = &aux;  // correct them
  }
  else
    p2 = &aux;
  return moonT_callorderTM(L, p1, p2, event);
}


void moonT_adjustvarargs (moon_State *L, int nfixparams, CallInfo *callInfo,
                         const Proto *p) {
  int i;
  int actual = cast_int(L->getTop().p - callInfo->funcRef().p) - 1;  // number of arguments
  int nextra = actual - nfixparams;  // number of extra arguments
  callInfo->setExtraArgs(nextra);
  moonD_checkstack(L, p->getMaxStackSize() + 1);
  // copy function to the top of the stack
  *s2v(L->getTop().p) = *s2v(callInfo->funcRef().p);  /* use operator= */
  L->getStackSubsystem().push();
  // move fixed parameters to the top of the stack
  for (i = 1; i <= nfixparams; i++) {
    *s2v(L->getTop().p) = *s2v(callInfo->funcRef().p + i);  /* use operator= */
    L->getStackSubsystem().push();
    setnilvalue(s2v(callInfo->funcRef().p + i));  // erase original parameter (for GC)
  }
  callInfo->funcRef().p += actual + 1;
  callInfo->topRef().p += actual + 1;
  moon_assert(L->getTop().p <= callInfo->topRef().p && callInfo->topRef().p <= L->getStackLast().p);
}


void moonT_getvarargs (moon_State *L, CallInfo *callInfo, StkId where, int wanted) {
  int i;
  int nextra = callInfo->getExtraArgs();
  if (wanted < 0) {
    wanted = nextra;  // get all extra arguments available
    checkstackp(L, nextra, where);  // ensure stack space
    L->getStackSubsystem().setTopPtr(where + nextra);  // next instruction will need top
  }
  for (i = 0; i < wanted && i < nextra; i++)
    *s2v(where + i) = *s2v(callInfo->funcRef().p - nextra + i);  /* use operator= */
  for (; i < wanted; i++)  // complete required results with nil
    setnilvalue(s2v(where + i));
}

