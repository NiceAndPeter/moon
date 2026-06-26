/*
** Lua API
** See Copyright Notice in lua.h
*/

#define LUA_CORE

#include "lprefix.h"


#include <climits>
#include <cstdarg>
#include <cstring>

#include "lua.h"

#include "lapi.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lundump.h"
#include "lvirtualmachine.h"
#include "lvm.h"



const char lua_ident[] =
  "$LuaVersion: " LUA_COPYRIGHT " $"
  "$LuaAuthors: " LUA_AUTHORS " $";



/*
** NOTE: index2value() and index2stack() moved to LuaStack class (lstack.cpp)
** as indexToValue() and indexToStack() methods.
*/


LUA_API int lua_checkstack (lua_State *L, int n) {
  int res;
  CallInfo *ci;
  lua_lock(L);
  ci = L->getCI();
  api_check(L, n >= 0, "negative 'n'");
  if (L->getStackLast().p - L->getTop().p > n)  // stack large enough?
    res = 1;  // yes; check is OK
  else  // need to grow stack
    res = L->growStack(n, 0);
  if (res && ci->topRef().p < L->getTop().p + n)
    ci->topRef().p = L->getTop().p + n;  // adjust frame top
  lua_unlock(L);
  return res;
}


LUA_API void lua_xmove (lua_State *from, lua_State *to, int n) {
  if (from == to) return;
  lua_lock(to);
  api_checkpop(from, n);
  api_check(from, G(from) == G(to), "moving among independent states");
  api_check(from, to->getCI()->topRef().p - to->getTop().p >= n, "stack overflow");
  from->getStackSubsystem().popN(n);
  for (int i = 0; i < n; i++) {
    *s2v(to->getTop().p) = *s2v(from->getTop().p + i);  /* use operator= */
    to->getStackSubsystem().push();  // stack already checked by previous 'api_check'
  }
  lua_unlock(to);
}


LUA_API lua_CFunction lua_atpanic (lua_State *L, lua_CFunction panicf) {
  lua_CFunction old;
  lua_lock(L);
  old = G(L)->getPanic();
  G(L)->setPanic(panicf);
  lua_unlock(L);
  return old;
}


LUA_API lua_Number lua_version (lua_State *L) {
  UNUSED(L);
  return LUA_VERSION_NUM;
}



/*
** basic stack manipulation
*/


/*
** convert an acceptable stack index into an absolute index
*/
LUA_API int lua_absindex (lua_State *L, int idx) {
  return (idx > 0 || ispseudo(idx))
         ? idx
         : cast_int(L->getTop().p - L->getCI()->funcRef().p) + idx;
}


LUA_API int lua_gettop (lua_State *L) {
  return cast_int(L->getTop().p - (L->getCI()->funcRef().p + 1));
}


LUA_API void lua_settop (lua_State *L, int idx) {
  lua_lock(L);
  CallInfo *ci = L->getCI();
  auto func = ci->funcRef().p;
  ptrdiff_t diff;  // difference for new top
  if (idx >= 0) {
    api_check(L, idx <= ci->topRef().p - (func + 1), "new top too large");
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
    lua_assert(ci->callStatusRef() & CIST_TBC);
    newtop = luaF_close(L, newtop, CLOSEKTOP, 0);
  }
  L->getStackSubsystem().setTopPtr(newtop);  // correct top only after closing any upvalue
  lua_unlock(L);
}


LUA_API void lua_closeslot (lua_State *L, int idx) {
  lua_lock(L);
  StkId level = L->getStackSubsystem().indexToStack(L, idx);
  api_check(L, (L->getCI()->callStatusRef() & CIST_TBC) && (L->getTbclist().p == level),
     "no variable to close at given level");
  level = luaF_close(L, level, CLOSEKTOP, 0);
  setnilvalue(s2v(level));
  lua_unlock(L);
}


/*
** Reverse the stack segment from 'from' to 'to'
** (auxiliary to 'lua_rotate')
** Note that we move(copy) only the value inside the stack.
** (We do not move additional fields that may exist.)
*/
static void reverse (lua_State *L, StkId from, StkId to) {
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
LUA_API void lua_rotate (lua_State *L, int idx, int n) {
  lua_lock(L);
  auto t = L->getTop().p - 1;  // end of stack segment being rotated
  auto p = L->getStackSubsystem().indexToStack(L, idx);  // start of segment
  api_check(L, L->getTbclist().p < p, "moving a to-be-closed slot");
  api_check(L, (n >= 0 ? n : -n) <= (t - p + 1), "invalid 'n'");
  auto m = (n >= 0 ? t - n : p - n - 1);  // end of prefix
  reverse(L, p, m);  // reverse the prefix with length 'n'
  reverse(L, m + 1, t);  // reverse the suffix
  reverse(L, p, t);  // reverse the entire segment
  lua_unlock(L);
}


LUA_API void lua_copy (lua_State *L, int fromidx, int toidx) {
  lua_lock(L);
  TValue *fr = L->getStackSubsystem().indexToValue(L, fromidx);
  TValue *to = L->getStackSubsystem().indexToValue(L, toidx);
  api_check(L, isvalid(L, to), "invalid index");
  *to = *fr;
  if (isupvalue(toidx))  // function upvalue?
    luaC_barrier(L, clCvalue(s2v(L->getCI()->funcRef().p)), fr);
  /* LUA_REGISTRYINDEX does not need gc barrier
     (collector revisits it before finishing collection) */
  lua_unlock(L);
}


LUA_API void lua_pushvalue (lua_State *L, int idx) {
  lua_lock(L);
  L->getStackSubsystem().setSlot(L->getTop().p, L->getStackSubsystem().indexToValue(L, idx));
  api_incr_top(L);
  lua_unlock(L);
}



/*
** access functions (stack -> C)
*/


LUA_API int lua_type (lua_State *L, int idx) {
  const TValue *o = L->getStackSubsystem().indexToValue(L,idx);
  return (isvalid(L, o) ? ttype(o) : LUA_TNONE);
}


LUA_API const char *lua_typename (lua_State *L, int t) {
  UNUSED(L);
  api_check(L, LUA_TNONE <= t && t < LUA_NUMTYPES, "invalid type");
  return ttypename(t);
}


LUA_API int lua_iscfunction (lua_State *L, int idx) {
  const TValue *o = L->getStackSubsystem().indexToValue(L,idx);
  return (ttislcf(o) || (ttisCclosure(o)));
}


LUA_API int lua_isinteger (lua_State *L, int idx) {
  const TValue *o = L->getStackSubsystem().indexToValue(L,idx);
  return ttisinteger(o);
}


LUA_API int lua_isnumber (lua_State *L, int idx) {
  lua_Number n;
  const TValue *o = L->getStackSubsystem().indexToValue(L,idx);
  return tonumber(o, &n);
}


LUA_API int lua_isstring (lua_State *L, int idx) {
  const TValue *o = L->getStackSubsystem().indexToValue(L,idx);
  return (ttisstring(o) || cvt2str(o));
}


LUA_API int lua_isuserdata (lua_State *L, int idx) {
  const TValue *o = L->getStackSubsystem().indexToValue(L,idx);
  return (ttisfulluserdata(o) || ttislightuserdata(o));
}


LUA_API int lua_rawequal (lua_State *L, int index1, int index2) {
  const TValue *o1 = L->getStackSubsystem().indexToValue(L,index1);
  const TValue *o2 = L->getStackSubsystem().indexToValue(L,index2);
  return (isvalid(L, o1) && isvalid(L, o2)) ? VirtualMachine::rawequalObj(o1, o2) : 0;
}


LUA_API void lua_arith (lua_State *L, int op) {
  lua_lock(L);
  if (op != LUA_OPUNM && op != LUA_OPBNOT)
    api_checkpop(L, 2);  // all other operations expect two operands
  else {  // for unary operations, add fake 2nd operand
    api_checkpop(L, 1);
    *s2v(L->getTop().p) = *s2v(L->getTop().p - 1);  /* duplicate - use operator= */
    api_incr_top(L);
  }
  // first operand at top - 2, second at top - 1; result go to top - 2
  luaO_arith(L, op, s2v(L->getTop().p - 2), s2v(L->getTop().p - 1), L->getTop().p - 2);
  L->getStackSubsystem().pop();  // pop second operand
  lua_unlock(L);
}


LUA_API int lua_compare (lua_State *L, int index1, int index2, int op) {
  int i = 0;
  lua_lock(L);  // may call tag method
  const TValue *o1 = L->getStackSubsystem().indexToValue(L,index1);
  const TValue *o2 = L->getStackSubsystem().indexToValue(L,index2);
  if (isvalid(L, o1) && isvalid(L, o2)) {
    switch (op) {
      case LUA_OPEQ: i = L->getVM().equalObj(o1, o2); break;
      case LUA_OPLT: i = L->getVM().lessThan(o1, o2); break;
      case LUA_OPLE: i = L->getVM().lessEqual(o1, o2); break;
      default: api_check(L, 0, "invalid option");
    }
  }
  lua_unlock(L);
  return i;
}


LUA_API unsigned (lua_numbertocstring) (lua_State *L, int idx, char *buff) {
  const TValue *o = L->getStackSubsystem().indexToValue(L,idx);
  if (ttisnumber(o)) {
    unsigned len = luaO_tostringbuff(o, buff);
    buff[len++] = '\0';  // add final zero
    return len;
  }
  else
    return 0;
}


LUA_API size_t lua_stringtonumber (lua_State *L, const char *s) {
  size_t sz = luaO_str2num(s, s2v(L->getTop().p));
  if (sz != 0)
    api_incr_top(L);
  return sz;
}


LUA_API lua_Number lua_tonumberx (lua_State *L, int idx, int *pisnum) {
  const TValue *o = L->getStackSubsystem().indexToValue(L,idx);
  lua_Number n = 0;
  int isnum = tonumber(o, &n);
  if (pisnum)
    *pisnum = isnum;
  return n;
}


LUA_API lua_Integer lua_tointegerx (lua_State *L, int idx, int *pisnum) {
  const TValue *o = L->getStackSubsystem().indexToValue(L,idx);
  lua_Integer res = 0;
  int isnum = tointeger(o, &res);
  if (pisnum)
    *pisnum = isnum;
  return res;
}


LUA_API int lua_toboolean (lua_State *L, int idx) {
  const TValue *o = L->getStackSubsystem().indexToValue(L,idx);
  return !l_isfalse(o);
}


LUA_API const char *lua_tolstring (lua_State *L, int idx, size_t *len) {
  TValue *o;
  lua_lock(L);
  o = L->getStackSubsystem().indexToValue(L,idx);
  if (!ttisstring(o)) {
    if (!cvt2str(o)) {  // not convertible?
      if (len != nullptr) *len = 0;
      lua_unlock(L);
      return nullptr;
    }
    luaO_tostring(L, o);
    luaC_checkGC(L);
    o = L->getStackSubsystem().indexToValue(L,idx);  // previous call may reallocate the stack
  }
  lua_unlock(L);
  if (len != nullptr)
    return getStringWithLength(tsvalue(o), *len);
  else
    return getStringContents(tsvalue(o));
}


LUA_API lua_Unsigned lua_rawlen (lua_State *L, int idx) {
  const TValue *o = L->getStackSubsystem().indexToValue(L,idx);
  switch (ttypetag(o)) {
    case LuaT::SHRSTR: return static_cast<lua_Unsigned>(tsvalue(o)->length());
    case LuaT::LNGSTR: return static_cast<lua_Unsigned>(tsvalue(o)->length());
    case LuaT::USERDATA: return static_cast<lua_Unsigned>(uvalue(o)->getLen());
    case LuaT::TABLE: {
      lua_Unsigned res;
      lua_lock(L);
      res = hvalue(o)->getn(L);
      lua_unlock(L);
      return res;
    }
    default: return 0;
  }
}


LUA_API lua_CFunction lua_tocfunction (lua_State *L, int idx) {
  const TValue *o = L->getStackSubsystem().indexToValue(L,idx);
  if (ttislcf(o)) return fvalue(o);
  else if (ttisCclosure(o))
    return clCvalue(o)->getFunction();
  else return nullptr;  // not a C function
}


static inline void *touserdata (const TValue *o) {
  switch (ttype(o)) {
    case LUA_TUSERDATA: return uvalue(o)->getMemory();
    case LUA_TLIGHTUSERDATA: return pvalue(o);
    default: return nullptr;
  }
}


LUA_API void *lua_touserdata (lua_State *L, int idx) {
  const TValue *o = L->getStackSubsystem().indexToValue(L,idx);
  return touserdata(o);
}


LUA_API lua_State *lua_tothread (lua_State *L, int idx) {
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
LUA_API const void *lua_topointer (lua_State *L, int idx) {
  const TValue *o = L->getStackSubsystem().indexToValue(L,idx);
  switch (ttypetag(o)) {
    case LuaT::LCF: return cast_voidp(cast_sizet(fvalue(o)));
    case LuaT::USERDATA: case LuaT::LIGHTUSERDATA:
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


LUA_API void lua_pushnil (lua_State *L) {
  lua_lock(L);
  setnilvalue(s2v(L->getTop().p));
  api_incr_top(L);
  lua_unlock(L);
}


LUA_API void lua_pushnumber (lua_State *L, lua_Number n) {
  lua_lock(L);
  s2v(L->getTop().p)->setFloat(n);
  api_incr_top(L);
  lua_unlock(L);
}


LUA_API void lua_pushinteger (lua_State *L, lua_Integer n) {
  lua_lock(L);
  s2v(L->getTop().p)->setInt(n);
  api_incr_top(L);
  lua_unlock(L);
}


/*
** Pushes on the stack a string with given length. Avoid using 's' when
** 'len' == 0 (as 's' can be nullptr in that case), due to later use of
** 'memcmp' and 'memcpy'.
*/
LUA_API const char *lua_pushlstring (lua_State *L, const char *s, size_t len) {
  TString *ts;
  lua_lock(L);
  ts = (len == 0) ? TString::create(L, "") : TString::create(L, s, len);
  setsvalue2s(L, L->getTop().p, ts);
  api_incr_top(L);
  luaC_checkGC(L);
  lua_unlock(L);
  return getStringContents(ts);
}


LUA_API const char *lua_pushexternalstring (lua_State *L,
	        const char *s, size_t len, lua_Alloc falloc, void *ud) {
  TString *ts;
  lua_lock(L);
  api_check(L, len <= MAX_SIZE, "string too large");
  api_check(L, s[len] == '\0', "string not ending with zero");
  ts = TString::createExternal(L, s, len, falloc, ud);
  setsvalue2s(L, L->getTop().p, ts);
  api_incr_top(L);
  luaC_checkGC(L);
  lua_unlock(L);
  return getStringContents(ts);
}


LUA_API const char *lua_pushstring (lua_State *L, const char *s) {
  lua_lock(L);
  if (s == nullptr)
    setnilvalue(s2v(L->getTop().p));
  else {
    TString *ts;
    ts = TString::create(L, s);
    setsvalue2s(L, L->getTop().p, ts);
    s = getStringContents(ts);  // internal copy's address
  }
  api_incr_top(L);
  luaC_checkGC(L);
  lua_unlock(L);
  return s;
}


LUA_API const char *lua_pushvfstring (lua_State *L, const char *fmt,
                                      va_list argp) {
  const char *ret;
  lua_lock(L);
  ret = luaO_pushvfstring(L, fmt, argp);
  luaC_checkGC(L);
  lua_unlock(L);
  return ret;
}


LUA_API const char *lua_pushfstring (lua_State *L, const char *fmt, ...) {
  const char *ret;
  va_list argp;
  lua_lock(L);
  pushvfstring(L, argp, fmt, ret);
  luaC_checkGC(L);
  lua_unlock(L);
  return ret;
}


LUA_API void lua_pushcclosure (lua_State *L, lua_CFunction fn, int n) {
  lua_lock(L);
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
      lua_assert(iswhite(cl));
    }
    L->getStackSubsystem().popN(n);
    setclCvalue(L, s2v(L->getTop().p), cl);
    api_incr_top(L);
    luaC_checkGC(L);
  }
  lua_unlock(L);
}


LUA_API void lua_pushboolean (lua_State *L, int b) {
  lua_lock(L);
  if (b)
    setbtvalue(s2v(L->getTop().p));
  else
    setbfvalue(s2v(L->getTop().p));
  api_incr_top(L);
  lua_unlock(L);
}


LUA_API void lua_pushlightuserdata (lua_State *L, void *p) {
  lua_lock(L);
  setpvalue(s2v(L->getTop().p), p);
  api_incr_top(L);
  lua_unlock(L);
}


LUA_API int lua_pushthread (lua_State *L) {
  lua_lock(L);
  setthvalue(L, s2v(L->getTop().p), L);
  api_incr_top(L);
  lua_unlock(L);
  return (mainthread(G(L)) == L);
}



/*
** get functions (Lua -> stack)
*/


static int auxgetstr (lua_State *L, const TValue *t, const char *k) {
  LuaT tag;
  TString *str = TString::create(L, k);
  tag = L->getVM().fastget(t, str, s2v(L->getTop().p), [](Table* tbl, TString* strkey, TValue* res) { return tbl->getStr(strkey, res); });
  if (!tagisempty(tag))
    api_incr_top(L);
  else {
    setsvalue2s(L, L->getTop().p, str);
    api_incr_top(L);
    tag = L->getVM().finishGet(t, s2v(L->getTop().p - 1), L->getTop().p - 1, tag);
  }
  lua_unlock(L);
  return novariant(tag);
}


/*
** The following function assumes that the registry cannot be a weak
** table; so, an emergency collection while using the global table
** cannot collect it.
*/
static void getGlobalTable (lua_State *L, TValue *gt) {
  Table *registry = hvalue(G(L)->getRegistry());
  LuaT tag = registry->getInt(LUA_RIDX_GLOBALS, gt);
  (void)tag;  // avoid not-used warnings when checks are off
  api_check(L, novariant(tag) == LUA_TTABLE, "global table must exist");
}


LUA_API int lua_getglobal (lua_State *L, const char *name) {
  TValue gt;
  lua_lock(L);
  getGlobalTable(L, &gt);
  return auxgetstr(L, &gt, name);
}


LUA_API int lua_gettable (lua_State *L, int idx) {
  lua_lock(L);
  api_checkpop(L, 1);
  TValue *t = L->getStackSubsystem().indexToValue(L,idx);
  LuaT tag = L->getVM().fastget(t, s2v(L->getTop().p - 1), s2v(L->getTop().p - 1), [](Table* tbl, const TValue* key, TValue* res) { return tbl->get(key, res); });
  if (tagisempty(tag))
    tag = L->getVM().finishGet(t, s2v(L->getTop().p - 1), L->getTop().p - 1, tag);
  lua_unlock(L);
  return novariant(tag);
}


LUA_API int lua_getfield (lua_State *L, int idx, const char *k) {
  lua_lock(L);
  return auxgetstr(L, L->getStackSubsystem().indexToValue(L,idx), k);
}


LUA_API int lua_geti (lua_State *L, int idx, lua_Integer n) {
  lua_lock(L);
  TValue *t = L->getStackSubsystem().indexToValue(L,idx);
  LuaT tag;
  L->getVM().fastgeti(t, n, s2v(L->getTop().p), tag);
  if (tagisempty(tag)) {
    TValue key;
    key.setInt(n);
    tag = L->getVM().finishGet(t, &key, L->getTop().p, tag);
  }
  api_incr_top(L);
  lua_unlock(L);
  return novariant(tag);
}


static int finishrawget (lua_State *L, LuaT tag) {
  if (tagisempty(tag))  // avoid copying empty items to the stack
    setnilvalue(s2v(L->getTop().p));
  api_incr_top(L);
  lua_unlock(L);
  return novariant(tag);
}


static inline Table *gettable (lua_State *L, int idx) {
  TValue *t = L->getStackSubsystem().indexToValue(L,idx);
  api_check(L, ttistable(t), "table expected");
  return hvalue(t);
}


LUA_API int lua_rawget (lua_State *L, int idx) {
  lua_lock(L);
  api_checkpop(L, 1);
  Table *t = gettable(L, idx);
  LuaT tag = t->get(s2v(L->getTop().p - 1), s2v(L->getTop().p - 1));
  L->getStackSubsystem().pop();  // pop key
  return finishrawget(L, tag);
}


LUA_API int lua_rawgeti (lua_State *L, int idx, lua_Integer n) {
  lua_lock(L);
  Table *t = gettable(L, idx);
  LuaT tag;
  t->fastGeti(n, s2v(L->getTop().p), tag);
  return finishrawget(L, tag);
}


LUA_API int lua_rawgetp (lua_State *L, int idx, const void *p) {
  lua_lock(L);
  Table *t = gettable(L, idx);
  TValue k;
  setpvalue(&k, cast_voidp(p));
  return finishrawget(L, t->get(&k, s2v(L->getTop().p)));
}


LUA_API void lua_createtable (lua_State *L, int narray, int nrec) {
  lua_lock(L);
  Table *t = Table::create(L);
  sethvalue2s(L, L->getTop().p, t);
  api_incr_top(L);
  if (narray > 0 || nrec > 0)
    t->resize(L, cast_uint(narray), cast_uint(nrec));
  luaC_checkGC(L);
  lua_unlock(L);
}


LUA_API int lua_getmetatable (lua_State *L, int objindex) {
  const TValue *obj;
  Table *mt;
  int res = 0;
  lua_lock(L);
  obj = L->getStackSubsystem().indexToValue(L,objindex);
  switch (ttype(obj)) {
    case LUA_TTABLE:
      mt = hvalue(obj)->getMetatable();
      break;
    case LUA_TUSERDATA:
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
  lua_unlock(L);
  return res;
}


LUA_API int lua_getiuservalue (lua_State *L, int idx, int n) {
  TValue *o;
  int t;
  lua_lock(L);
  o = L->getStackSubsystem().indexToValue(L,idx);
  api_check(L, ttisfulluserdata(o), "full userdata expected");
  if (n <= 0 || n > uvalue(o)->getNumUserValues()) {
    setnilvalue(s2v(L->getTop().p));
    t = LUA_TNONE;
  }
  else {
    L->getStackSubsystem().setSlot(L->getTop().p, &uvalue(o)->getUserValue(n - 1)->uv);
    t = ttype(s2v(L->getTop().p));
  }
  api_incr_top(L);
  lua_unlock(L);
  return t;
}


/*
** set functions (stack -> Lua)
*/

/*
** t[k] = value at the top of the stack (where 'k' is a string)
*/
static void auxsetstr (lua_State *L, const TValue *t, const char *k) {
  int hres;
  TString *str = TString::create(L, k);
  api_checkpop(L, 1);
  hres = L->getVM().fastset(t, str, s2v(L->getTop().p - 1), [](Table* tbl, TString* strkey, TValue* val) { return tbl->psetStr(strkey, val); });
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
  lua_unlock(L);  // lock done by caller
}


LUA_API void lua_setglobal (lua_State *L, const char *name) {
  TValue gt;
  lua_lock(L);  // unlock done in 'auxsetstr'
  getGlobalTable(L, &gt);
  auxsetstr(L, &gt, name);
}


LUA_API void lua_settable (lua_State *L, int idx) {
  lua_lock(L);
  api_checkpop(L, 2);
  TValue *t = L->getStackSubsystem().indexToValue(L,idx);
  int hres = L->getVM().fastset(t, s2v(L->getTop().p - 2), s2v(L->getTop().p - 1), [](Table* tbl, const TValue* key, TValue* val) { return tbl->pset(key, val); });
  if (hres == HOK)
    L->getVM().finishfastset(t, s2v(L->getTop().p - 1));
  else
    L->getVM().finishSet(t, s2v(L->getTop().p - 2), s2v(L->getTop().p - 1), hres);
  L->getStackSubsystem().popN(2);  // pop index and value
  lua_unlock(L);
}


LUA_API void lua_setfield (lua_State *L, int idx, const char *k) {
  lua_lock(L);  // unlock done in 'auxsetstr'
  auxsetstr(L, L->getStackSubsystem().indexToValue(L,idx), k);
}


LUA_API void lua_seti (lua_State *L, int idx, lua_Integer n) {
  lua_lock(L);
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
  lua_unlock(L);
}


static void aux_rawset (lua_State *L, int idx, TValue *key, int n) {
  lua_lock(L);
  api_checkpop(L, n);
  Table *t = gettable(L, idx);
  t->set(L, key, s2v(L->getTop().p - 1));
  invalidateTMcache(t);
  luaC_barrierback(L, obj2gco(t), s2v(L->getTop().p - 1));
  L->getStackSubsystem().popN(n);
  lua_unlock(L);
}


LUA_API void lua_rawset (lua_State *L, int idx) {
  aux_rawset(L, idx, s2v(L->getTop().p - 2), 2);
}


LUA_API void lua_rawsetp (lua_State *L, int idx, const void *p) {
  TValue k;
  setpvalue(&k, cast_voidp(p));
  aux_rawset(L, idx, &k, 1);
}


LUA_API void lua_rawseti (lua_State *L, int idx, lua_Integer n) {
  lua_lock(L);
  api_checkpop(L, 1);
  Table *t = gettable(L, idx);
  t->setInt(L, n, s2v(L->getTop().p - 1));
  luaC_barrierback(L, obj2gco(t), s2v(L->getTop().p - 1));
  L->getStackSubsystem().pop();
  lua_unlock(L);
}


LUA_API int lua_setmetatable (lua_State *L, int objindex) {
  lua_lock(L);
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
    case LUA_TTABLE: {
      hvalue(obj)->setMetatable(mt);
      if (mt) {
        luaC_objbarrier(L, gcvalue(obj), mt);
        gcvalue(obj)->checkFinalizer(L, mt);      }
      break;
    }
    case LUA_TUSERDATA: {
      uvalue(obj)->setMetatable(mt);
      if (mt) {
        luaC_objbarrier(L, uvalue(obj), mt);
        gcvalue(obj)->checkFinalizer(L, mt);      }
      break;
    }
    default: {
      G(L)->setMetatable(ttype(obj), mt);
      break;
    }
  }
  L->getStackSubsystem().pop();
  lua_unlock(L);
  return 1;
}


LUA_API int lua_setiuservalue (lua_State *L, int idx, int n) {
  TValue *o;
  int res;
  lua_lock(L);
  api_checkpop(L, 1);
  o = L->getStackSubsystem().indexToValue(L,idx);
  api_check(L, ttisfulluserdata(o), "full userdata expected");
  if (!(cast_uint(n) - 1u < cast_uint(uvalue(o)->getNumUserValues())))
    res = 0;  // 'n' not in [1, uvalue(o)->getNumUserValues()]
  else {
    uvalue(o)->getUserValue(n - 1)->uv = *s2v(L->getTop().p - 1);
    luaC_barrierback(L, gcvalue(o), s2v(L->getTop().p - 1));
    res = 1;
  }
  L->getStackSubsystem().pop();
  lua_unlock(L);
  return res;
}


/*
** 'load' and 'call' functions (run Lua code)
*/


inline void checkresults(lua_State* L, int na, int nr) {
	api_check(L, (nr) == LUA_MULTRET
	          || (L->getCI()->topRef().p - L->getTop().p >= (nr) - (na)),
	          "results from function overflow current stack size");
	api_check(L, LUA_MULTRET <= (nr) && (nr) <= MAXRESULTS,
	          "invalid number of results");
}


LUA_API void lua_callk (lua_State *L, int nargs, int nresults,
                        lua_KContext ctx, lua_KFunction k) {
  StkId func;
  lua_lock(L);
  api_check(L, k == nullptr || !L->getCI()->isLua(),
    "cannot use continuations inside hooks");
  api_checkpop(L, nargs + 1);
  api_check(L, L->getStatus() == LUA_OK, "cannot do calls on non-normal thread");
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
  lua_unlock(L);
}



/*
** Execute a protected call.
*/
struct CallS {  // data to 'f_call'
  StkId func;
  int nresults;
};


static void f_call (lua_State *L, void *ud) {
  CallS *c = static_cast<CallS*>(ud);
  L->callNoYield( c->func, c->nresults);
}



LUA_API int lua_pcallk (lua_State *L, int nargs, int nresults, int errfunc,
                        lua_KContext ctx, lua_KFunction k) {
  lua_lock(L);
  api_check(L, k == nullptr || !L->getCI()->isLua(),
    "cannot use continuations inside hooks");
  api_checkpop(L, nargs + 1);
  api_check(L, L->getStatus() == LUA_OK, "cannot do calls on non-normal thread");
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
    CallInfo *ci = L->getCI();
    ci->setK(k);  // save continuation
    ci->setCtx(ctx);  // save context
    // save information for error recovery
    ci->setFuncIdx(cast_int(L->saveStack(c.func)));
    ci->setOldErrFunc(L->getErrFunc());
    L->setErrFunc(func);
    ci->setOAH(L->getAllowHook());  // save value of 'allowhook'
    ci->callStatusRef() |= CIST_YPCALL;  // function can do error recovery
    L->call( c.func, nresults);  // do the call
    ci->callStatusRef() &= ~CIST_YPCALL;
    L->setErrFunc(ci->getOldErrFunc());
    status = LUA_OK;  // if it is here, there were no errors
  }
  adjustresults(L, nresults);
  lua_unlock(L);
  return APIstatus(status);
}


LUA_API int lua_load (lua_State *L, lua_Reader reader, void *data,
                      const char *chunkname, const char *mode) {
  TStatus status;
  lua_lock(L);
  if (!chunkname) chunkname = "?";
  ZIO z(L, reader, data);
  status = L->protectedParser( &z, chunkname, mode);
  if (status == LUA_OK) {  // no errors?
    LClosure *f = clLvalue(s2v(L->getTop().p - 1));  // get new function
    if (f->getNumUpvalues() >= 1) {  // does it have an upvalue?
      // get global table from registry
      TValue gt;
      getGlobalTable(L, &gt);
      // set global table as 1st upvalue of 'f' (may be LUA_ENV)
      *f->getUpval(0)->getVP() = gt;
      luaC_barrier(L, f->getUpval(0), &gt);
    }
  }
  lua_unlock(L);
  return APIstatus(status);
}


/*
** Dump a Lua function, calling 'writer' to write its parts. Ensure
** the stack returns with its original size.
*/
LUA_API int lua_dump (lua_State *L, lua_Writer writer, void *data, int strip) {
  int status;
  ptrdiff_t otop = L->saveStack(L->getTop().p);  // original top
  TValue *f = s2v(L->getTop().p - 1);  // function to be dumped
  lua_lock(L);
  api_checkpop(L, 1);
  api_check(L, isLfunction(f), "Lua function expected");
  status = luaU_dump(L, clLvalue(f)->getProto(), writer, data, strip);
  L->getStackSubsystem().setTopPtr(L->restoreStack(otop));  // restore top
  lua_unlock(L);
  return status;
}


LUA_API int lua_status (lua_State *L) {
  return APIstatus(L->getStatus());
}


/*
** Garbage-collection function
*/
LUA_API int lua_gc (lua_State *L, int what, ...) {
  va_list argp;
  int res = 0;
  GlobalState *g = G(L);
  if (g->getGCStp() & (GCSTPGC | GCSTPCLS))  // internal stop?
    return -1;  // all options are invalid when stopped
  lua_lock(L);
  va_start(argp, what);
  switch (what) {
    case LUA_GCSTOP: {
      g->setGCStp(GCSTPUSR);  // stopped by the user
      break;
    }
    case LUA_GCRESTART: {
      luaE_setdebt(g, 0);
      g->setGCStp(0);  // (other bits must be zero here)
      break;
    }
    case LUA_GCCOLLECT: {
      luaC_fullgc(*L, 0);
      break;
    }
    case LUA_GCCOUNT: {
      // GC values are expressed in Kbytes: #bytes/2^10
      res = cast_int(g->getTotalBytes() >> 10);
      break;
    }
    case LUA_GCCOUNTB: {
      res = cast_int(g->getTotalBytes() & 0x3ff);
      break;
    }
    case LUA_GCSTEP: {
      lu_byte oldstp = g->getGCStp();
      l_mem n = static_cast<l_mem>(va_arg(argp, size_t));
      int work = 0;  // true if GC did some work
      g->setGCStp(0);  // allow GC to run (other bits must be zero here)
      if (n <= 0)
        n = g->getGCDebt();  // force to run one basic step
      luaE_setdebt(g, g->getGCDebt() - n);
      luaC_condGC(L, [](){}, [&work](){ work = 1; });
      if (work && g->getGCState() == GCState::Pause)  // end of cycle?
        res = 1;  // signal it
      g->setGCStp(oldstp);  // restore previous state
      break;
    }
    case LUA_GCISRUNNING: {
      res = g->isGCRunning();
      break;
    }
    case LUA_GCGEN: {
      res = (g->getGCKind() == GCKind::Incremental) ? LUA_GCINC : LUA_GCGEN;
      luaC_changemode(*L, GCKind::GenerationalMinor);
      break;
    }
    case LUA_GCINC: {
      res = (g->getGCKind() == GCKind::Incremental) ? LUA_GCINC : LUA_GCGEN;
      luaC_changemode(*L, GCKind::Incremental);
      break;
    }
    case LUA_GCPARAM: {
      int param = va_arg(argp, int);
      int value = va_arg(argp, int);
      api_check(L, 0 <= param && param < LUA_GCPN, "invalid parameter");
      res = cast_int(luaO_applyparam(g->getGCParam(param), 100));
      if (value >= 0)
        g->setGCParam(param, luaO_codeparam(cast_uint(value)));
      break;
    }
    default: res = -1;  // invalid option
  }
  va_end(argp);
  lua_unlock(L);
  return res;
}



/*
** miscellaneous functions
*/


LUA_API int lua_error (lua_State *L) {
  TValue *errobj;
  lua_lock(L);
  errobj = s2v(L->getTop().p - 1);
  api_checkpop(L, 1);
  // error object is the memory error message?
  if (ttisshrstring(errobj) && shortStringsEqual(tsvalue(errobj), G(L)->getMemErrMsg()))
    luaM_error(L);  // raise a memory error
  else
    luaG_errormsg(L);  // raise a regular error
  // code unreachable; will unlock when control actually leaves the kernel
  return 0;  // to avoid warnings
}


LUA_API int lua_next (lua_State *L, int idx) {
  lua_lock(L);
  api_checkpop(L, 1);
  Table *t = gettable(L, idx);
  int more = t->tableNext(L, L->getTop().p - 1);
  if (more)
    api_incr_top(L);
  else  // no more elements
    L->getStackSubsystem().pop();  // pop key
  lua_unlock(L);
  return more;
}


LUA_API void lua_toclose (lua_State *L, int idx) {
  lua_lock(L);
  StkId o = L->getStackSubsystem().indexToStack(L,idx);
  api_check(L, L->getTbclist().p < o, "given index below or equal a marked one");
  luaF_newtbcupval(L, o);  // create new to-be-closed upvalue
  L->getCI()->callStatusRef() |= CIST_TBC;  // mark that function has TBC slots
  lua_unlock(L);
}


LUA_API void lua_concat (lua_State *L, int n) {
  lua_lock(L);
  api_checknelems(L, n);
  if (n > 0) {
    L->getVM().concat(n);
    luaC_checkGC(L);
  }
  else {  // nothing to concatenate
    setsvalue2s(L, L->getTop().p, TString::create(L, "", 0));  // push empty string
    api_incr_top(L);
  }
  lua_unlock(L);
}


LUA_API void lua_len (lua_State *L, int idx) {
  lua_lock(L);
  TValue *t = L->getStackSubsystem().indexToValue(L,idx);
  L->getVM().objlen(L->getTop().p, t);
  api_incr_top(L);
  lua_unlock(L);
}


LUA_API lua_Alloc lua_getallocf (lua_State *L, void **ud) {
  lua_lock(L);
  if (ud) *ud = G(L)->getUd();
  lua_Alloc f = G(L)->getFrealloc();
  lua_unlock(L);
  return f;
}


LUA_API void lua_setallocf (lua_State *L, lua_Alloc f, void *ud) {
  lua_lock(L);
  G(L)->setUd(ud);
  G(L)->setFrealloc(f);
  lua_unlock(L);
}


void lua_setwarnf (lua_State *L, lua_WarnFunction f, void *ud) {
  lua_lock(L);
  G(L)->setUdWarn(ud);
  G(L)->setWarnF(f);
  lua_unlock(L);
}


void lua_warning (lua_State *L, const char *msg, int tocont) {
  lua_lock(L);
  luaE_warning(L, msg, tocont);
  lua_unlock(L);
}



LUA_API void *lua_newuserdatauv (lua_State *L, size_t size, int nuvalue) {
  lua_lock(L);
  api_check(L, 0 <= nuvalue && nuvalue < SHRT_MAX, "invalid value");
  Udata *u = luaS_newudata(L, size, static_cast<unsigned short>(nuvalue));
  setuvalue(L, s2v(L->getTop().p), u);
  api_incr_top(L);
  luaC_checkGC(L);
  lua_unlock(L);
  return u->getMemory();
}



static const char *aux_upvalue (TValue *fi, int n, TValue **val,
                                GCObject **owner) {
  switch (ttypetag(fi)) {
    case LuaT::CCL: {  // C closure
      CClosure *f = clCvalue(fi);
      if (!(cast_uint(n) - 1u < cast_uint(f->getNumUpvalues())))
        return nullptr;  // 'n' not in [1, f->getNumUpvalues()]
      *val = f->getUpvalue(n-1);
      if (owner) *owner = obj2gco(f);
      return "";
    }
    case LuaT::LCL: {  // Lua closure
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


LUA_API const char *lua_getupvalue (lua_State *L, int funcindex, int n) {
  const char *name;
  TValue *val = nullptr;  // to avoid warnings
  lua_lock(L);
  name = aux_upvalue(L->getStackSubsystem().indexToValue(L,funcindex), n, &val, nullptr);
  if (name) {
    L->getStackSubsystem().setSlot(L->getTop().p, val);
    api_incr_top(L);
  }
  lua_unlock(L);
  return name;
}


LUA_API const char *lua_setupvalue (lua_State *L, int funcindex, int n) {
  const char *name;
  TValue *val = nullptr;  // to avoid warnings
  GCObject *owner = nullptr;  // to avoid warnings
  TValue *fi;
  lua_lock(L);
  fi = L->getStackSubsystem().indexToValue(L,funcindex);
  api_checknelems(L, 1);
  name = aux_upvalue(fi, n, &val, &owner);
  if (name) {
    L->getStackSubsystem().pop();
    *val = *s2v(L->getTop().p);
    luaC_barrier(L, owner, val);
  }
  lua_unlock(L);
  return name;
}


static UpVal **getupvalref (lua_State *L, int fidx, int n, LClosure **pf) {
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


LUA_API void *lua_upvalueid (lua_State *L, int fidx, int n) {
  TValue *fi = L->getStackSubsystem().indexToValue(L,fidx);
  switch (ttypetag(fi)) {
    case LuaT::LCL: {  // lua closure
      return *getupvalref(L, fidx, n, nullptr);
    }
    case LuaT::CCL: {  // C closure
      CClosure *f = clCvalue(fi);
      if (1 <= n && n <= f->getNumUpvalues())
        return f->getUpvalue(n - 1);
      // else
    }  // FALLTHROUGH
    case LuaT::LCF:
      return nullptr;  // light C functions have no upvalues
    default: {
      api_check(L, 0, "function expected");
      return nullptr;
    }
  }
}


LUA_API void lua_upvaluejoin (lua_State *L, int fidx1, int n1,
                                            int fidx2, int n2) {
  LClosure *f1;
  UpVal **up1 = getupvalref(L, fidx1, n1, &f1);
  UpVal **up2 = getupvalref(L, fidx2, n2, nullptr);
  api_check(L, *up1 != nullptr && *up2 != nullptr, "invalid upvalue index");
  *up1 = *up2;
  luaC_objbarrier(L, f1, *up1);
}


