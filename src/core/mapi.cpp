/*
** Lua API
** See Copyright Notice in lua.h
*/

#define MOON_CORE

#include "mprefix.h"


#include <climits>
#include <cstdarg>
#include <cstring>

#include "moon.h"

#include "mapi.h"
#include "mdebug.h"
#include "mdo.h"
#include "mfunc.h"
#include "mgc.h"
#include "mmem.h"
#include "mobject.h"
#include "mstate.h"
#include "mstring.h"
#include "mtable.h"
#include "mtm.h"
#include "mundump.h"
#include "mvirtualmachine.h"
#include "mvm.h"



const char moon_ident[] =
  "$MoonVersion: " MOON_COPYRIGHT " $"
  "$MoonAuthors: " MOON_AUTHORS " $";



/*
** NOTE: index2value() and index2stack() moved to MoonStack class (lstack.cpp)
** as indexToValue() and indexToStack() methods.
*/


MOON_API int moon_checkstack (moon_State *L, int n) {
  int res;
  moon_lock(L);
  CallInfo *callInfo = L->getCI();
  api_check(L, n >= 0, "negative 'n'");
  if (L->getStackLast().p - L->getTop().p > n)  // stack large enough?
    res = 1;  // yes; check is OK
  else  // need to grow stack
    res = L->growStack(n, 0);
  if (res && callInfo->topRef().p < L->getTop().p + n)
    callInfo->topRef().p = L->getTop().p + n;  // adjust frame top
  moon_unlock(L);
  return res;
}


MOON_API void moon_xmove (moon_State *from, moon_State *to, int n) {
  if (from == to) return;
  moon_lock(to);
  api_checkpop(from, n);
  api_check(from, G(from) == G(to), "moving among independent states");
  api_check(from, to->getCI()->topRef().p - to->getTop().p >= n, "stack overflow");
  from->getStackSubsystem().popN(n);
  for (int i = 0; i < n; i++) {
    *s2v(to->getTop().p) = *s2v(from->getTop().p + i);  /* use operator= */
    to->getStackSubsystem().push();  // stack already checked by previous 'api_check'
  }
  moon_unlock(to);
}


MOON_API moon_CFunction moon_atpanic (moon_State *L, moon_CFunction panicf) {
  moon_CFunction old;
  moon_lock(L);
  old = G(L)->getPanic();
  G(L)->setPanic(panicf);
  moon_unlock(L);
  return old;
}


MOON_API moon_Number moon_version (moon_State *L) {
  UNUSED(L);
  return MOON_VERSION_NUM;
}



/*
** basic stack manipulation
*/


/*
** convert an acceptable stack index into an absolute index
*/
MOON_API int moon_absindex (moon_State *L, int idx) {
  return (idx > 0 || ispseudo(idx))
         ? idx
         : cast_int(L->getTop().p - L->getCI()->funcRef().p) + idx;
}


MOON_API int moon_gettop (moon_State *L) {
  return cast_int(L->getTop().p - (L->getCI()->funcRef().p + 1));
}


MOON_API void moon_settop (moon_State *L, int idx) {
  moon_lock(L);
  CallInfo *callInfo = L->getCI();
  auto func = callInfo->funcRef().p;
  ptrdiff_t diff;  // difference for new top
  if (idx >= 0) {
    api_check(L, idx <= callInfo->topRef().p - (func + 1), "new top too large");
    diff = ((func + 1) + idx) - L->getTop().p;
    for (; diff > 0; diff--) {
      setnilvalue(s2v(L->getTop().p));  // clear new slot
      L->getStackSubsystem().push();
    }
  }
  else {
    api_check(L, -(idx+1) <= (L->getTop().p - (func + 1)), "invalid new top");
    diff = idx + 1;  // will "subtract" index (as it is negative)
  }
  StkId newtop = L->getTop().p + diff;
  if (diff < 0 && L->getTbclist().p >= newtop) {
    moon_assert(callInfo->callStatusRef() & CIST_TBC);
    newtop = moonF_close(L, newtop, CLOSEKTOP, 0);
  }
  L->getStackSubsystem().setTopPtr(newtop);  // correct top only after closing any upvalue
  moon_unlock(L);
}


MOON_API void moon_closeslot (moon_State *L, int idx) {
  moon_lock(L);
  StkId level = L->getStackSubsystem().indexToStack(L, idx);
  api_check(L, (L->getCI()->callStatusRef() & CIST_TBC) && (L->getTbclist().p == level),
     "no variable to close at given level");
  level = moonF_close(L, level, CLOSEKTOP, 0);
  setnilvalue(s2v(level));
  moon_unlock(L);
}


/*
** Reverse the stack segment from 'from' to 'to'
** (auxiliary to 'moon_rotate')
** Note that we move(copy) only the value inside the stack.
** (We do not move additional fields that may exist.)
*/
static void reverse (moon_State *L, StkId from, StkId to) {
  for (; from < to; from++, to--) {
    TValue temp;
    temp = *s2v(from);
    *s2v(from) = *s2v(to);  /* swap - use operator= */
    L->getStackSubsystem().setSlot(to, &temp);
  }
}


/*
** Let x = AB, where A is a prefix of length 'n'. Then,
** rotate x n == BA. But BA == (A^r . B^r)^r.
*/
MOON_API void moon_rotate (moon_State *L, int idx, int n) {
  moon_lock(L);
  auto t = L->getTop().p - 1;  // end of stack segment being rotated
  auto p = L->getStackSubsystem().indexToStack(L, idx);  // start of segment
  api_check(L, L->getTbclist().p < p, "moving a to-be-closed slot");
  api_check(L, (n >= 0 ? n : -n) <= (t - p + 1), "invalid 'n'");
  auto m = (n >= 0 ? t - n : p - n - 1);  // end of prefix
  reverse(L, p, m);  // reverse the prefix with length 'n'
  reverse(L, m + 1, t);  // reverse the suffix
  reverse(L, p, t);  // reverse the entire segment
  moon_unlock(L);
}


MOON_API void moon_copy (moon_State *L, int fromidx, int toidx) {
  moon_lock(L);
  TValue *fr = L->getStackSubsystem().indexToValue(L, fromidx);
  TValue *to = L->getStackSubsystem().indexToValue(L, toidx);
  api_check(L, isvalid(L, to), "invalid index");
  *to = *fr;
  if (isupvalue(toidx))  // function upvalue?
    moonC_barrier(L, clCvalue(s2v(L->getCI()->funcRef().p)), fr);
  /* MOON_REGISTRYINDEX does not need gc barrier
     (collector revisits it before finishing collection) */
  moon_unlock(L);
}


MOON_API void moon_pushvalue (moon_State *L, int idx) {
  moon_lock(L);
  L->getStackSubsystem().setSlot(L->getTop().p, L->getStackSubsystem().indexToValue(L, idx));
  api_incr_top(L);
  moon_unlock(L);
}



/*
** access functions (stack -> C)
*/


MOON_API int moon_type (moon_State *L, int idx) {
  const TValue *o = L->getStackSubsystem().indexToValue(L,idx);
  return (isvalid(L, o) ? ttype(o) : MOON_TNONE);
}


MOON_API const char *moon_typename (moon_State *L, int t) {
  UNUSED(L);
  api_check(L, MOON_TNONE <= t && t < MOON_NUMTYPES, "invalid type");
  return ttypename(t);
}


MOON_API int moon_iscfunction (moon_State *L, int idx) {
  const TValue *o = L->getStackSubsystem().indexToValue(L,idx);
  return (ttislcf(o) || (ttisCclosure(o)));
}


MOON_API int moon_isinteger (moon_State *L, int idx) {
  const TValue *o = L->getStackSubsystem().indexToValue(L,idx);
  return ttisinteger(o);
}


MOON_API int moon_isnumber (moon_State *L, int idx) {
  moon_Number n;
  const TValue *o = L->getStackSubsystem().indexToValue(L,idx);
  return tonumber(o, &n);
}


MOON_API int moon_isstring (moon_State *L, int idx) {
  const TValue *o = L->getStackSubsystem().indexToValue(L,idx);
  return (ttisstring(o) || cvt2str(o));
}


MOON_API int moon_isuserdata (moon_State *L, int idx) {
  const TValue *o = L->getStackSubsystem().indexToValue(L,idx);
  return (ttisfulluserdata(o) || ttislightuserdata(o));
}


MOON_API int moon_rawequal (moon_State *L, int index1, int index2) {
  const TValue *o1 = L->getStackSubsystem().indexToValue(L,index1);
  const TValue *o2 = L->getStackSubsystem().indexToValue(L,index2);
  return (isvalid(L, o1) && isvalid(L, o2)) ? VirtualMachine::rawequalObj(o1, o2) : 0;
}


MOON_API void moon_arith (moon_State *L, int op) {
  moon_lock(L);
  if (op != MOON_OPUNM && op != MOON_OPBNOT)
    api_checkpop(L, 2);  // all other operations expect two operands
  else {  // for unary operations, add fake 2nd operand
    api_checkpop(L, 1);
    *s2v(L->getTop().p) = *s2v(L->getTop().p - 1);  /* duplicate - use operator= */
    api_incr_top(L);
  }
  // first operand at top - 2, second at top - 1; result go to top - 2
  moonO_arith(L, op, s2v(L->getTop().p - 2), s2v(L->getTop().p - 1), L->getTop().p - 2);
  L->getStackSubsystem().pop();  // pop second operand
  moon_unlock(L);
}


MOON_API int moon_compare (moon_State *L, int index1, int index2, int op) {
  int i = 0;
  moon_lock(L);  // may call tag method
  const TValue *o1 = L->getStackSubsystem().indexToValue(L,index1);
  const TValue *o2 = L->getStackSubsystem().indexToValue(L,index2);
  if (isvalid(L, o1) && isvalid(L, o2)) {
    switch (op) {
      case MOON_OPEQ: i = L->getVM().equalObj(o1, o2); break;
      case MOON_OPLT: i = L->getVM().lessThan(o1, o2); break;
      case MOON_OPLE: i = L->getVM().lessEqual(o1, o2); break;
      default: api_check(L, 0, "invalid option");
    }
  }
  moon_unlock(L);
  return i;
}


MOON_API unsigned (moon_numbertocstring) (moon_State *L, int idx, char *buff) {
  const TValue *o = L->getStackSubsystem().indexToValue(L,idx);
  if (ttisnumber(o)) {
    unsigned len = moonO_tostringbuff(o, buff);
    buff[len++] = '\0';  // add final zero
    return len;
  }
  else
    return 0;
}


MOON_API size_t moon_stringtonumber (moon_State *L, const char *s) {
  size_t sz = moonO_str2num(s, s2v(L->getTop().p));
  if (sz != 0)
    api_incr_top(L);
  return sz;
}


MOON_API moon_Number moon_tonumberx (moon_State *L, int idx, int *pisnum) {
  const TValue *o = L->getStackSubsystem().indexToValue(L,idx);
  moon_Number n = 0;
  int isnum = tonumber(o, &n);
  if (pisnum)
    *pisnum = isnum;
  return n;
}


MOON_API moon_Integer moon_tointegerx (moon_State *L, int idx, int *pisnum) {
  const TValue *o = L->getStackSubsystem().indexToValue(L,idx);
  moon_Integer res = 0;
  int isnum = tointeger(o, &res);
  if (pisnum)
    *pisnum = isnum;
  return res;
}


MOON_API int moon_toboolean (moon_State *L, int idx) {
  const TValue *o = L->getStackSubsystem().indexToValue(L,idx);
  return !l_isfalse(o);
}


MOON_API const char *moon_tolstring (moon_State *L, int idx, size_t *len) {
  TValue *o;
  moon_lock(L);
  o = L->getStackSubsystem().indexToValue(L,idx);
  if (!ttisstring(o)) {
    if (!cvt2str(o)) {  // not convertible?
      if (len != nullptr) *len = 0;
      moon_unlock(L);
      return nullptr;
    }
    moonO_tostring(L, o);
    moonC_checkGC(L);
    o = L->getStackSubsystem().indexToValue(L,idx);  // previous call may reallocate the stack
  }
  moon_unlock(L);
  if (len != nullptr)
    return getStringWithLength(tsvalue(o), *len);
  else
    return getStringContents(tsvalue(o));
}


MOON_API moon_Unsigned moon_rawlen (moon_State *L, int idx) {
  const TValue *o = L->getStackSubsystem().indexToValue(L,idx);
  switch (ttypetag(o)) {
    case MoonT::SHRSTR: return static_cast<moon_Unsigned>(tsvalue(o)->length());
    case MoonT::LNGSTR: return static_cast<moon_Unsigned>(tsvalue(o)->length());
    case MoonT::USERDATA: return static_cast<moon_Unsigned>(uvalue(o)->getLen());
    case MoonT::TABLE: {
      moon_lock(L);
      const moon_Unsigned res = hvalue(o)->getn(L);
      moon_unlock(L);
      return res;
    }
    default: return 0;
  }
}


MOON_API moon_CFunction moon_tocfunction (moon_State *L, int idx) {
  const TValue *o = L->getStackSubsystem().indexToValue(L,idx);
  if (ttislcf(o)) return fvalue(o);
  else if (ttisCclosure(o))
    return clCvalue(o)->getFunction();
  else return nullptr;  // not a C function
}


static inline void *touserdata (const TValue *o) {
  switch (ttype(o)) {
    case MOON_TUSERDATA: return uvalue(o)->getMemory();
    case MOON_TLIGHTUSERDATA: return pvalue(o);
    default: return nullptr;
  }
}


MOON_API void *moon_touserdata (moon_State *L, int idx) {
  const TValue *o = L->getStackSubsystem().indexToValue(L,idx);
  return touserdata(o);
}


MOON_API moon_State *moon_tothread (moon_State *L, int idx) {
  const TValue *o = L->getStackSubsystem().indexToValue(L,idx);
  return (!ttisthread(o)) ? nullptr : thvalue(o);
}


/*
** Returns a pointer to the internal representation of an object.
** Note that ISO C does not allow the conversion of a pointer to
** function to a 'void*', so the conversion here goes through
** a 'size_t'. (As the returned pointer is only informative, this
** conversion should not be a problem.)
*/
MOON_API const void *moon_topointer (moon_State *L, int idx) {
  const TValue *o = L->getStackSubsystem().indexToValue(L,idx);
  switch (ttypetag(o)) {
    case MoonT::LCF: return cast_voidp(cast_sizet(fvalue(o)));
    case MoonT::USERDATA: case MoonT::LIGHTUSERDATA:
      return touserdata(o);
    default: {
      if (iscollectable(o))
        return gcvalue(o);
      else
        return nullptr;
    }
  }
}



/*
** push functions (C -> stack)
*/


MOON_API void moon_pushnil (moon_State *L) {
  moon_lock(L);
  setnilvalue(s2v(L->getTop().p));
  api_incr_top(L);
  moon_unlock(L);
}


MOON_API void moon_pushnumber (moon_State *L, moon_Number n) {
  moon_lock(L);
  s2v(L->getTop().p)->setFloat(n);
  api_incr_top(L);
  moon_unlock(L);
}


MOON_API void moon_pushinteger (moon_State *L, moon_Integer n) {
  moon_lock(L);
  s2v(L->getTop().p)->setInt(n);
  api_incr_top(L);
  moon_unlock(L);
}


/*
** Pushes on the stack a string with given length. Avoid using 's' when
** 'len' == 0 (as 's' can be nullptr in that case), due to later use of
** 'memcmp' and 'memcpy'.
*/
MOON_API const char *moon_pushlstring (moon_State *L, const char *s, size_t len) {
  TString *tstring;
  moon_lock(L);
  tstring = (len == 0) ? TString::create(L, "") : TString::create(L, s, len);
  setsvalue2s(L, L->getTop().p, tstring);
  api_incr_top(L);
  moonC_checkGC(L);
  moon_unlock(L);
  return getStringContents(tstring);
}


MOON_API const char *moon_pushexternalstring (moon_State *L,
	        const char *s, size_t len, moon_Alloc falloc, void *ud) {
  TString *tstring;
  moon_lock(L);
  api_check(L, len <= MAX_SIZE, "string too large");
  api_check(L, s[len] == '\0', "string not ending with zero");
  tstring = TString::createExternal(L, s, len, falloc, ud);
  setsvalue2s(L, L->getTop().p, tstring);
  api_incr_top(L);
  moonC_checkGC(L);
  moon_unlock(L);
  return getStringContents(tstring);
}


MOON_API const char *moon_pushstring (moon_State *L, const char *s) {
  moon_lock(L);
  if (s == nullptr)
    setnilvalue(s2v(L->getTop().p));
  else {
    TString *tstring;
    tstring = TString::create(L, s);
    setsvalue2s(L, L->getTop().p, tstring);
    s = getStringContents(tstring);  // internal copy's address
  }
  api_incr_top(L);
  moonC_checkGC(L);
  moon_unlock(L);
  return s;
}


MOON_API const char *moon_pushvfstring (moon_State *L, const char *fmt,
                                      va_list argp) {
  const char *ret;
  moon_lock(L);
  ret = moonO_pushvfstring(L, fmt, argp);
  moonC_checkGC(L);
  moon_unlock(L);
  return ret;
}


MOON_API const char *moon_pushfstring (moon_State *L, const char *fmt, ...) {
  const char *ret;
  va_list argp;
  moon_lock(L);
  pushvfstring(L, argp, fmt, ret);
  moonC_checkGC(L);
  moon_unlock(L);
  return ret;
}


MOON_API void moon_pushcclosure (moon_State *L, moon_CFunction fn, int n) {
  moon_lock(L);
  if (n == 0) {
    setfvalue(s2v(L->getTop().p), fn);
    api_incr_top(L);
  }
  else {
    api_checkpop(L, n);
    api_check(L, n <= MAXUPVAL, "upvalue index too large");
    CClosure *cl = CClosure::create(L, n);
    cl->setFunction(fn);
    for (int i = 0; i < n; i++) {
      *cl->getUpvalue(i) = *s2v(L->getTop().p - n + i);
      // does not need barrier because closure is white
      moon_assert(iswhite(cl));
    }
    L->getStackSubsystem().popN(n);
    setclCvalue(L, s2v(L->getTop().p), cl);
    api_incr_top(L);
    moonC_checkGC(L);
  }
  moon_unlock(L);
}


MOON_API void moon_pushboolean (moon_State *L, int b) {
  moon_lock(L);
  if (b)
    setbtvalue(s2v(L->getTop().p));
  else
    setbfvalue(s2v(L->getTop().p));
  api_incr_top(L);
  moon_unlock(L);
}


MOON_API void moon_pushlightuserdata (moon_State *L, void *p) {
  moon_lock(L);
  setpvalue(s2v(L->getTop().p), p);
  api_incr_top(L);
  moon_unlock(L);
}


MOON_API int moon_pushthread (moon_State *L) {
  moon_lock(L);
  setthvalue(L, s2v(L->getTop().p), L);
  api_incr_top(L);
  moon_unlock(L);
  return (mainthread(G(L)) == L);
}



/*
** get functions (Lua -> stack)
*/


static int auxgetstr (moon_State *L, const TValue *t, const char *k) {
  MoonT tag;
  TString *str = TString::create(L, k);
  tag = L->getVM().fastget(t, str, s2v(L->getTop().p), [](Table* tbl, TString* strkey, TValue* res) { return tbl->getStr(strkey, res); });
  if (!tagisempty(tag))
    api_incr_top(L);
  else {
    setsvalue2s(L, L->getTop().p, str);
    api_incr_top(L);
    tag = L->getVM().finishGet(t, s2v(L->getTop().p - 1), L->getTop().p - 1, tag);
  }
  moon_unlock(L);
  return novariant(tag);
}


/*
** The following function assumes that the registry cannot be a weak
** table; so, an emergency collection while using the global table
** cannot collect it.
*/
static void getGlobalTable (moon_State *L, TValue *gt) {
  Table *registry = hvalue(G(L)->getRegistry());
  MoonT tag = registry->getInt(MOON_RIDX_GLOBALS, gt);
  (void)tag;  // avoid not-used warnings when checks are off
  api_check(L, novariant(tag) == MOON_TTABLE, "global table must exist");
}


MOON_API int moon_getglobal (moon_State *L, const char *name) {
  TValue gt;
  moon_lock(L);
  getGlobalTable(L, &gt);
  return auxgetstr(L, &gt, name);
}


MOON_API int moon_gettable (moon_State *L, int idx) {
  moon_lock(L);
  api_checkpop(L, 1);
  TValue *t = L->getStackSubsystem().indexToValue(L,idx);
  MoonT tag = L->getVM().fastget(t, s2v(L->getTop().p - 1), s2v(L->getTop().p - 1), [](Table* tbl, const TValue* key, TValue* res) { return tbl->get(key, res); });
  if (tagisempty(tag))
    tag = L->getVM().finishGet(t, s2v(L->getTop().p - 1), L->getTop().p - 1, tag);
  moon_unlock(L);
  return novariant(tag);
}


MOON_API int moon_getfield (moon_State *L, int idx, const char *k) {
  moon_lock(L);
  return auxgetstr(L, L->getStackSubsystem().indexToValue(L,idx), k);
}


MOON_API int moon_geti (moon_State *L, int idx, moon_Integer n) {
  moon_lock(L);
  TValue *t = L->getStackSubsystem().indexToValue(L,idx);
  MoonT tag;
  L->getVM().fastgeti(t, n, s2v(L->getTop().p), tag);
  if (tagisempty(tag)) {
    TValue key;
    key.setInt(n);
    tag = L->getVM().finishGet(t, &key, L->getTop().p, tag);
  }
  api_incr_top(L);
  moon_unlock(L);
  return novariant(tag);
}


static int finishrawget (moon_State *L, MoonT tag) {
  if (tagisempty(tag))  // avoid copying empty items to the stack
    setnilvalue(s2v(L->getTop().p));
  api_incr_top(L);
  moon_unlock(L);
  return novariant(tag);
}


static inline Table *gettable (moon_State *L, int idx) {
  TValue *t = L->getStackSubsystem().indexToValue(L,idx);
  api_check(L, ttistable(t), "table expected");
  return hvalue(t);
}


MOON_API int moon_rawget (moon_State *L, int idx) {
  moon_lock(L);
  api_checkpop(L, 1);
  Table *t = gettable(L, idx);
  MoonT tag = t->get(s2v(L->getTop().p - 1), s2v(L->getTop().p - 1));
  L->getStackSubsystem().pop();  // pop key
  return finishrawget(L, tag);
}


MOON_API int moon_rawgeti (moon_State *L, int idx, moon_Integer n) {
  moon_lock(L);
  Table *t = gettable(L, idx);
  MoonT tag;
  t->fastGeti(n, s2v(L->getTop().p), tag);
  return finishrawget(L, tag);
}


MOON_API int moon_rawgetp (moon_State *L, int idx, const void *p) {
  moon_lock(L);
  Table *t = gettable(L, idx);
  TValue k;
  setpvalue(&k, cast_voidp(p));
  return finishrawget(L, t->get(&k, s2v(L->getTop().p)));
}


MOON_API void moon_createtable (moon_State *L, int narray, int nrec) {
  moon_lock(L);
  Table *t = Table::create(L);
  sethvalue2s(L, L->getTop().p, t);
  api_incr_top(L);
  if (narray > 0 || nrec > 0)
    t->resize(L, cast_uint(narray), cast_uint(nrec));
  moonC_checkGC(L);
  moon_unlock(L);
}


MOON_API int moon_getmetatable (moon_State *L, int objindex) {
  const TValue *obj;
  Table *mt;
  int res = 0;
  moon_lock(L);
  obj = L->getStackSubsystem().indexToValue(L,objindex);
  switch (ttype(obj)) {
    case MOON_TTABLE:
      mt = hvalue(obj)->getMetatable();
      break;
    case MOON_TUSERDATA:
      mt = uvalue(obj)->getMetatable();
      break;
    default:
      mt = G(L)->getMetatable(ttype(obj));
      break;
  }
  if (mt != nullptr) {
    sethvalue2s(L, L->getTop().p, mt);
    api_incr_top(L);
    res = 1;
  }
  moon_unlock(L);
  return res;
}


MOON_API int moon_getiuservalue (moon_State *L, int idx, int n) {
  int t;
  moon_lock(L);
  TValue *o = L->getStackSubsystem().indexToValue(L,idx);
  api_check(L, ttisfulluserdata(o), "full userdata expected");
  if (n <= 0 || n > uvalue(o)->getNumUserValues()) {
    setnilvalue(s2v(L->getTop().p));
    t = MOON_TNONE;
  }
  else {
    L->getStackSubsystem().setSlot(L->getTop().p, &uvalue(o)->getUserValue(n - 1)->value);
    t = ttype(s2v(L->getTop().p));
  }
  api_incr_top(L);
  moon_unlock(L);
  return t;
}


/*
** set functions (stack -> Lua)
*/

/*
** t[k] = value at the top of the stack (where 'k' is a string)
*/
static void auxsetstr (moon_State *L, const TValue *t, const char *k) {
  TString *str = TString::create(L, k);
  api_checkpop(L, 1);
  int hres = L->getVM().fastset(t, str, s2v(L->getTop().p - 1), [](Table* tbl, TString* strkey, TValue* val) { return tbl->psetStr(strkey, val); });
  if (hres == HOK) {
    L->getVM().finishfastset(t, s2v(L->getTop().p - 1));
    L->getStackSubsystem().pop();  // pop value
  }
  else {
    setsvalue2s(L, L->getTop().p, str);  // push 'str' (to make it a TValue)
    api_incr_top(L);
    L->getVM().finishSet(t, s2v(L->getTop().p - 1), s2v(L->getTop().p - 2), hres);
    L->getStackSubsystem().popN(2);  // pop value and key
  }
  moon_unlock(L);  // lock done by caller
}


MOON_API void moon_setglobal (moon_State *L, const char *name) {
  TValue gt;
  moon_lock(L);  // unlock done in 'auxsetstr'
  getGlobalTable(L, &gt);
  auxsetstr(L, &gt, name);
}


MOON_API void moon_settable (moon_State *L, int idx) {
  moon_lock(L);
  api_checkpop(L, 2);
  TValue *t = L->getStackSubsystem().indexToValue(L,idx);
  int hres = L->getVM().fastset(t, s2v(L->getTop().p - 2), s2v(L->getTop().p - 1), [](Table* tbl, const TValue* key, TValue* val) { return tbl->pset(key, val); });
  if (hres == HOK)
    L->getVM().finishfastset(t, s2v(L->getTop().p - 1));
  else
    L->getVM().finishSet(t, s2v(L->getTop().p - 2), s2v(L->getTop().p - 1), hres);
  L->getStackSubsystem().popN(2);  // pop index and value
  moon_unlock(L);
}


MOON_API void moon_setfield (moon_State *L, int idx, const char *k) {
  moon_lock(L);  // unlock done in 'auxsetstr'
  auxsetstr(L, L->getStackSubsystem().indexToValue(L,idx), k);
}


MOON_API void moon_seti (moon_State *L, int idx, moon_Integer n) {
  moon_lock(L);
  api_checkpop(L, 1);
  TValue *t = L->getStackSubsystem().indexToValue(L,idx);
  int hres;
  L->getVM().fastseti(t, n, s2v(L->getTop().p - 1), hres);
  if (hres == HOK)
    L->getVM().finishfastset(t, s2v(L->getTop().p - 1));
  else {
    TValue temp;
    temp.setInt(n);
    L->getVM().finishSet(t, &temp, s2v(L->getTop().p - 1), hres);
  }
  L->getStackSubsystem().pop();  // pop value
  moon_unlock(L);
}


static void aux_rawset (moon_State *L, int idx, TValue *key, int n) {
  moon_lock(L);
  api_checkpop(L, n);
  Table *t = gettable(L, idx);
  t->set(L, key, s2v(L->getTop().p - 1));
  invalidateTMcache(t);
  moonC_barrierback(L, obj2gco(t), s2v(L->getTop().p - 1));
  L->getStackSubsystem().popN(n);
  moon_unlock(L);
}


MOON_API void moon_rawset (moon_State *L, int idx) {
  aux_rawset(L, idx, s2v(L->getTop().p - 2), 2);
}


MOON_API void moon_rawsetp (moon_State *L, int idx, const void *p) {
  TValue k;
  setpvalue(&k, cast_voidp(p));
  aux_rawset(L, idx, &k, 1);
}


MOON_API void moon_rawseti (moon_State *L, int idx, moon_Integer n) {
  moon_lock(L);
  api_checkpop(L, 1);
  Table *t = gettable(L, idx);
  t->setInt(L, n, s2v(L->getTop().p - 1));
  moonC_barrierback(L, obj2gco(t), s2v(L->getTop().p - 1));
  L->getStackSubsystem().pop();
  moon_unlock(L);
}


MOON_API int moon_setmetatable (moon_State *L, int objindex) {
  moon_lock(L);
  api_checkpop(L, 1);
  TValue *obj = L->getStackSubsystem().indexToValue(L,objindex);
  Table *mt;
  if (ttisnil(s2v(L->getTop().p - 1)))
    mt = nullptr;
  else {
    api_check(L, ttistable(s2v(L->getTop().p - 1)), "table expected");
    mt = hvalue(s2v(L->getTop().p - 1));
  }
  switch (ttype(obj)) {
    case MOON_TTABLE: {
      hvalue(obj)->setMetatable(mt);
      if (mt) {
        moonC_objbarrier(L, gcvalue(obj), mt);
        gcvalue(obj)->checkFinalizer(L, mt);      }
      break;
    }
    case MOON_TUSERDATA: {
      uvalue(obj)->setMetatable(mt);
      if (mt) {
        moonC_objbarrier(L, uvalue(obj), mt);
        gcvalue(obj)->checkFinalizer(L, mt);      }
      break;
    }
    default: {
      G(L)->setMetatable(ttype(obj), mt);
      break;
    }
  }
  L->getStackSubsystem().pop();
  moon_unlock(L);
  return 1;
}


MOON_API int moon_setiuservalue (moon_State *L, int idx, int n) {
  int res;
  moon_lock(L);
  api_checkpop(L, 1);
  TValue *o = L->getStackSubsystem().indexToValue(L,idx);
  api_check(L, ttisfulluserdata(o), "full userdata expected");
  if (!(cast_uint(n) - 1u < cast_uint(uvalue(o)->getNumUserValues())))
    res = 0;  // 'n' not in [1, uvalue(o)->getNumUserValues()]
  else {
    uvalue(o)->getUserValue(n - 1)->value = *s2v(L->getTop().p - 1);
    moonC_barrierback(L, gcvalue(o), s2v(L->getTop().p - 1));
    res = 1;
  }
  L->getStackSubsystem().pop();
  moon_unlock(L);
  return res;
}


/*
** 'load' and 'call' functions (run Lua code)
*/


inline void checkresults(moon_State* L, int na, int nr) {
	api_check(L, (nr) == MOON_MULTRET
	          || (L->getCI()->topRef().p - L->getTop().p >= (nr) - (na)),
	          "results from function overflow current stack size");
	api_check(L, MOON_MULTRET <= (nr) && (nr) <= MAXRESULTS,
	          "invalid number of results");
}


MOON_API void moon_callk (moon_State *L, int nargs, int nresults,
                        moon_KContext ctx, moon_KFunction k) {
  StkId func;
  moon_lock(L);
  api_check(L, k == nullptr || !L->getCI()->isLua(),
    "cannot use continuations inside hooks");
  api_checkpop(L, nargs + 1);
  api_check(L, L->getStatus() == MOON_OK, "cannot do calls on non-normal thread");
  checkresults(L, nargs, nresults);
  func = L->getTop().p - (nargs+1);
  if (k != nullptr && yieldable(L)) {  // need to prepare continuation?
    L->getCI()->setK(k);  // save continuation
    L->getCI()->setCtx(ctx);  // save context
    L->call( func, nresults);  // do the call
  }
  else  // no continuation or no yieldable
    L->callNoYield( func, nresults);  // just do the call
  adjustresults(L, nresults);
  moon_unlock(L);
}



/*
** Execute a protected call.
*/
struct CallS {  // data to 'f_call'
  StkId func;
  int nresults;
};


static void f_call (moon_State *L, void *ud) {
  CallS *c = static_cast<CallS*>(ud);
  L->callNoYield( c->func, c->nresults);
}



MOON_API int moon_pcallk (moon_State *L, int nargs, int nresults, int errfunc,
                        moon_KContext ctx, moon_KFunction k) {
  moon_lock(L);
  api_check(L, k == nullptr || !L->getCI()->isLua(),
    "cannot use continuations inside hooks");
  api_checkpop(L, nargs + 1);
  api_check(L, L->getStatus() == MOON_OK, "cannot do calls on non-normal thread");
  checkresults(L, nargs, nresults);
  ptrdiff_t func;
  if (errfunc == 0)
    func = 0;
  else {
    StkId o = L->getStackSubsystem().indexToStack(L,errfunc);
    api_check(L, ttisfunction(s2v(o)), "error handler must be a function");
    func = L->saveStack(o);
  }
  struct CallS c;
  c.func = L->getTop().p - (nargs+1);  // function to be called
  TStatus status;
  if (k == nullptr || !yieldable(L)) {  // no continuation or no yieldable?
    c.nresults = nresults;  // do a 'conventional' protected call
    status = L->pCall( f_call, &c, L->saveStack(c.func), func);
  }
  else {  // prepare continuation (call is already protected by 'resume')
    CallInfo *callInfo = L->getCI();
    callInfo->setK(k);  // save continuation
    callInfo->setCtx(ctx);  // save context
    // save information for error recovery
    callInfo->setFuncIdx(cast_int(L->saveStack(c.func)));
    callInfo->setOldErrFunc(L->getErrFunc());
    L->setErrFunc(func);
    callInfo->setOAH(L->getAllowHook());  // save value of 'allowhook'
    callInfo->callStatusRef() |= CIST_YPCALL;  // function can do error recovery
    L->call( c.func, nresults);  // do the call
    callInfo->callStatusRef() &= ~CIST_YPCALL;
    L->setErrFunc(callInfo->getOldErrFunc());
    status = MOON_OK;  // if it is here, there were no errors
  }
  adjustresults(L, nresults);
  moon_unlock(L);
  return APIstatus(status);
}


MOON_API int moon_load (moon_State *L, moon_Reader reader, void *data,
                      const char *chunkname, const char *mode) {
  TStatus status;
  moon_lock(L);
  if (!chunkname) chunkname = "?";
  ZIO z(L, reader, data);
  status = L->protectedParser( &z, chunkname, mode);
  if (status == MOON_OK) {  // no errors?
    LClosure *f = clLvalue(s2v(L->getTop().p - 1));  // get new function
    if (f->getNumUpvalues() >= 1) {  // does it have an upvalue?
      // get global table from registry
      TValue gt;
      getGlobalTable(L, &gt);
      // set global table as 1st upvalue of 'f' (may be MOON_ENV)
      *f->getUpval(0)->getVP() = gt;
      moonC_barrier(L, f->getUpval(0), &gt);
    }
  }
  moon_unlock(L);
  return APIstatus(status);
}


/*
** Dump a Lua function, calling 'writer' to write its parts. Ensure
** the stack returns with its original size.
*/
MOON_API int moon_dump (moon_State *L, moon_Writer writer, void *data, int strip) {
  ptrdiff_t otop = L->saveStack(L->getTop().p);  // original top
  TValue *f = s2v(L->getTop().p - 1);  // function to be dumped
  moon_lock(L);
  api_checkpop(L, 1);
  api_check(L, isLfunction(f), "Lua function expected");
  const int status = moonU_dump(L, clLvalue(f)->getProto(), writer, data, strip);
  L->getStackSubsystem().setTopPtr(L->restoreStack(otop));  // restore top
  moon_unlock(L);
  return status;
}


MOON_API int moon_status (moon_State *L) {
  return APIstatus(L->getStatus());
}


/*
** Garbage-collection function
*/
MOON_API int moon_gc (moon_State *L, int what, ...) {
  va_list argp;
  int res = 0;
  GlobalState *g = G(L);
  if (g->getGCStp() & (GCSTPGC | GCSTPCLS))  // internal stop?
    return -1;  // all options are invalid when stopped
  moon_lock(L);
  va_start(argp, what);
  switch (what) {
    case MOON_GCSTOP: {
      g->setGCStp(GCSTPUSR);  // stopped by the user
      break;
    }
    case MOON_GCRESTART: {
      moonE_setdebt(g, 0);
      g->setGCStp(0);  // (other bits must be zero here)
      break;
    }
    case MOON_GCCOLLECT: {
      moonC_fullgc(*L, 0);
      break;
    }
    case MOON_GCCOUNT: {
      // GC values are expressed in Kbytes: #bytes/2^10
      res = cast_int(g->getTotalBytes() >> 10);
      break;
    }
    case MOON_GCCOUNTB: {
      res = cast_int(g->getTotalBytes() & 0x3ff);
      break;
    }
    case MOON_GCSTEP: {
      lu_byte oldstp = g->getGCStp();
      l_mem n = static_cast<l_mem>(va_arg(argp, size_t));
      int work = 0;  // true if GC did some work
      g->setGCStp(0);  // allow GC to run (other bits must be zero here)
      if (n <= 0)
        n = g->getGCDebt();  // force to run one basic step
      moonE_setdebt(g, g->getGCDebt() - n);
      moonC_condGC(L, [](){}, [&work](){ work = 1; });
      if (work && g->getGCState() == GCState::Pause)  // end of cycle?
        res = 1;  // signal it
      g->setGCStp(oldstp);  // restore previous state
      break;
    }
    case MOON_GCISRUNNING: {
      res = g->isGCRunning();
      break;
    }
    case MOON_GCGEN: {
      res = (g->getGCKind() == GCKind::Incremental) ? MOON_GCINC : MOON_GCGEN;
      moonC_changemode(*L, GCKind::GenerationalMinor);
      break;
    }
    case MOON_GCINC: {
      res = (g->getGCKind() == GCKind::Incremental) ? MOON_GCINC : MOON_GCGEN;
      moonC_changemode(*L, GCKind::Incremental);
      break;
    }
    case MOON_GCPARAM: {
      int param = va_arg(argp, int);
      int value = va_arg(argp, int);
      api_check(L, 0 <= param && param < MOON_GCPN, "invalid parameter");
      res = cast_int(moonO_applyparam(g->getGCParam(param), 100));
      if (value >= 0)
        g->setGCParam(param, moonO_codeparam(cast_uint(value)));
      break;
    }
    default: res = -1;  // invalid option
  }
  va_end(argp);
  moon_unlock(L);
  return res;
}



/*
** miscellaneous functions
*/


MOON_API int moon_error (moon_State *L) {
  TValue *errobj;
  moon_lock(L);
  errobj = s2v(L->getTop().p - 1);
  api_checkpop(L, 1);
  // error object is the memory error message?
  if (ttisshrstring(errobj) && shortStringsEqual(tsvalue(errobj), G(L)->getMemErrMsg()))
    moonM_error(L);  // raise a memory error
  else
    moonG_errormsg(L);  // raise a regular error
  // code unreachable; will unlock when control actually leaves the kernel
  return 0;  // to avoid warnings
}


MOON_API int moon_next (moon_State *L, int idx) {
  moon_lock(L);
  api_checkpop(L, 1);
  Table *t = gettable(L, idx);
  int more = t->tableNext(L, L->getTop().p - 1);
  if (more)
    api_incr_top(L);
  else  // no more elements
    L->getStackSubsystem().pop();  // pop key
  moon_unlock(L);
  return more;
}


MOON_API void moon_toclose (moon_State *L, int idx) {
  moon_lock(L);
  StkId o = L->getStackSubsystem().indexToStack(L,idx);
  api_check(L, L->getTbclist().p < o, "given index below or equal a marked one");
  moonF_newtbcupval(L, o);  // create new to-be-closed upvalue
  L->getCI()->callStatusRef() |= CIST_TBC;  // mark that function has TBC slots
  moon_unlock(L);
}


MOON_API void moon_concat (moon_State *L, int n) {
  moon_lock(L);
  api_checknelems(L, n);
  if (n > 0) {
    L->getVM().concat(n);
    moonC_checkGC(L);
  }
  else {  // nothing to concatenate
    setsvalue2s(L, L->getTop().p, TString::create(L, "", 0));  // push empty string
    api_incr_top(L);
  }
  moon_unlock(L);
}


MOON_API void moon_len (moon_State *L, int idx) {
  moon_lock(L);
  TValue *t = L->getStackSubsystem().indexToValue(L,idx);
  L->getVM().objlen(L->getTop().p, t);
  api_incr_top(L);
  moon_unlock(L);
}


MOON_API moon_Alloc moon_getallocf (moon_State *L, void **ud) {
  moon_lock(L);
  if (ud) *ud = G(L)->getUd();
  moon_Alloc f = G(L)->getFrealloc();
  moon_unlock(L);
  return f;
}


MOON_API void moon_setallocf (moon_State *L, moon_Alloc f, void *ud) {
  moon_lock(L);
  G(L)->setUd(ud);
  G(L)->setFrealloc(f);
  moon_unlock(L);
}


void moon_setwarnf (moon_State *L, moon_WarnFunction f, void *ud) {
  moon_lock(L);
  G(L)->setUdWarn(ud);
  G(L)->setWarnF(f);
  moon_unlock(L);
}


void moon_warning (moon_State *L, const char *msg, int tocont) {
  moon_lock(L);
  moonE_warning(L, msg, tocont);
  moon_unlock(L);
}



MOON_API void *moon_newuserdatauv (moon_State *L, size_t size, int nuvalue) {
  moon_lock(L);
  api_check(L, 0 <= nuvalue && nuvalue < SHRT_MAX, "invalid value");
  Udata *u = moonS_newudata(L, size, static_cast<unsigned short>(nuvalue));
  setuvalue(L, s2v(L->getTop().p), u);
  api_incr_top(L);
  moonC_checkGC(L);
  moon_unlock(L);
  return u->getMemory();
}



static const char *aux_upvalue (TValue *fi, int n, TValue **val,
                                GCObject **owner) {
  switch (ttypetag(fi)) {
    case MoonT::CCL: {  // C closure
      CClosure *f = clCvalue(fi);
      if (!(cast_uint(n) - 1u < cast_uint(f->getNumUpvalues())))
        return nullptr;  // 'n' not in [1, f->getNumUpvalues()]
      *val = f->getUpvalue(n-1);
      if (owner) *owner = obj2gco(f);
      return "";
    }
    case MoonT::LCL: {  // Lua closure
      LClosure *f = clLvalue(fi);
      TString *name;
      Proto *p = f->getProto();
      if (!(cast_uint(n) - 1u  < cast_uint(p->getUpvaluesSize())))
        return nullptr;  // 'n' not in [1, p->getUpvaluesSize()]
      *val = f->getUpval(n-1)->getVP();
      if (owner) *owner = obj2gco(f->getUpval(n - 1));
      name = p->getUpvalues()[n-1].getName();
      return (name == nullptr) ? "(no name)" : getStringContents(name);
    }
    default: return nullptr;  // not a closure
  }
}


MOON_API const char *moon_getupvalue (moon_State *L, int funcindex, int n) {
  const char *name;
  TValue *val = nullptr;  // to avoid warnings
  moon_lock(L);
  name = aux_upvalue(L->getStackSubsystem().indexToValue(L,funcindex), n, &val, nullptr);
  if (name) {
    L->getStackSubsystem().setSlot(L->getTop().p, val);
    api_incr_top(L);
  }
  moon_unlock(L);
  return name;
}


MOON_API const char *moon_setupvalue (moon_State *L, int funcindex, int n) {
  const char *name;
  TValue *val = nullptr;  // to avoid warnings
  GCObject *owner = nullptr;  // to avoid warnings
  TValue *fi;
  moon_lock(L);
  fi = L->getStackSubsystem().indexToValue(L,funcindex);
  api_checknelems(L, 1);
  name = aux_upvalue(fi, n, &val, &owner);
  if (name) {
    L->getStackSubsystem().pop();
    *val = *s2v(L->getTop().p);
    moonC_barrier(L, owner, val);
  }
  moon_unlock(L);
  return name;
}


static UpVal **getupvalref (moon_State *L, int fidx, int n, LClosure **pf) {
  static const UpVal *const nullup = nullptr;
  LClosure *f;
  TValue *fi = L->getStackSubsystem().indexToValue(L,fidx);
  api_check(L, ttisLclosure(fi), "Lua function expected");
  f = clLvalue(fi);
  if (pf) *pf = f;
  if (1 <= n && n <= f->getProto()->getUpvaluesSize())
    return f->getUpvalPtr(n - 1);  // get its upvalue pointer
  else
    return (UpVal**)&nullup;
}


MOON_API void *moon_upvalueid (moon_State *L, int fidx, int n) {
  TValue *fi = L->getStackSubsystem().indexToValue(L,fidx);
  switch (ttypetag(fi)) {
    case MoonT::LCL: {  // lua closure
      return *getupvalref(L, fidx, n, nullptr);
    }
    case MoonT::CCL: {  // C closure
      CClosure *f = clCvalue(fi);
      if (1 <= n && n <= f->getNumUpvalues())
        return f->getUpvalue(n - 1);
      // else
    }  // FALLTHROUGH
    case MoonT::LCF:
      return nullptr;  // light C functions have no upvalues
    default: {
      api_check(L, 0, "function expected");
      return nullptr;
    }
  }
}


MOON_API void moon_upvaluejoin (moon_State *L, int fidx1, int n1,
                                            int fidx2, int n2) {
  LClosure *f1;
  UpVal **up1 = getupvalref(L, fidx1, n1, &f1);
  UpVal **up2 = getupvalref(L, fidx2, n2, nullptr);
  api_check(L, *up1 != nullptr && *up2 != nullptr, "invalid upvalue index");
  *up1 = *up2;
  moonC_objbarrier(L, f1, *up1);
}


