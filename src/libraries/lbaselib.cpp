/*
** Basic library
** See Copyright Notice in lua.h
*/

#define MOON_LIB

#include "lprefix.h"


#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"
#include "llimits.h"


static int moonB_print (moon_State *L) {
  int n = moon_gettop(L);  // number of arguments
  for (int i = 1; i <= n; i++) {  // for each argument
    size_t l;
    const char *s = moonL_tolstring(L, i, &l);  // convert it to string
    if (i > 1)  // not the first element?
      moon_writestring("\t", 1);  // add a tab before it
    moon_writestring(s, l);  // print it
    moon_pop(L, 1);  // pop result
  }
  moon_writeline();
  return 0;
}


/*
** Creates a warning with all given arguments.
** Check first for errors; otherwise an error may interrupt
** the composition of a warning, leaving it unfinished.
*/
static int moonB_warn (moon_State *L) {
  int n = moon_gettop(L);  // number of arguments
  moonL_checkstring(L, 1);  // at least one argument
  for (int i = 2; i <= n; i++)
    moonL_checkstring(L, i);  // make sure all arguments are strings
  for (int i = 1; i < n; i++)  // compose warning
    moon_warning(L, moon_tostring(L, i), 1);
  moon_warning(L, moon_tostring(L, n), 0);  // close warning
  return 0;
}


#define SPACECHARS	" \f\n\r\t\v"

static const char *b_str2int (const char *s, unsigned base, moon_Integer *pn) {
  moon_Unsigned n = 0;
  int neg = 0;
  s += strspn(s, SPACECHARS);  // skip initial spaces
  if (*s == '-') { s++; neg = 1; }  // handle sign
  else if (*s == '+') s++;
  if (!isalnum(cast_uchar(*s)))  // no digit?
    return nullptr;
  do {
    unsigned digit = cast_uint(isdigit(cast_uchar(*s))
                               ? *s - '0'
                               : (toupper(cast_uchar(*s)) - 'A') + 10);
    if (digit >= base) return nullptr;  // invalid numeral
    n = n * base + digit;
    s++;
  } while (isalnum(cast_uchar(*s)));
  s += strspn(s, SPACECHARS);  // skip trailing spaces
  *pn = (moon_Integer)((neg) ? (0u - n) : n);
  return s;
}


static int moonB_tonumber (moon_State *L) {
  if (moon_isnoneornil(L, 2)) {  // standard conversion?
    if (moon_type(L, 1) == MOON_TNUMBER) {  // already a number?
      moon_settop(L, 1);  // yes; return it
      return 1;
    }
    else {
      size_t l;
      const char *s = moon_tolstring(L, 1, &l);
      if (s != nullptr && moon_stringtonumber(L, s) == l + 1)
        return 1;  // successful conversion to number
      // else not a number
      moonL_checkany(L, 1);  // (but there must be some parameter)
    }
  }
  else {
    size_t l;
    moon_Integer n = 0;  // to avoid warnings
    moon_Integer base = moonL_checkinteger(L, 2);
    moonL_checktype(L, 1, MOON_TSTRING);  // no numbers as strings
    const char *s = moon_tolstring(L, 1, &l);
    moonL_argcheck(L, 2 <= base && base <= 36, 2, "base out of range");
    if (b_str2int(s, cast_uint(base), &n) == s + l) {
      moon_pushinteger(L, n);
      return 1;
    }  // else not a number
  }  // else not a number
  moonL_pushfail(L);  // not a number
  return 1;
}


static int moonB_error (moon_State *L) {
  int level = (int)moonL_optinteger(L, 2, 1);
  moon_settop(L, 1);
  if (moon_type(L, 1) == MOON_TSTRING && level > 0) {
    moonL_where(L, level);  // add extra information
    moon_pushvalue(L, 1);
    moon_concat(L, 2);
  }
  return moon_error(L);
}


static int moonB_getmetatable (moon_State *L) {
  moonL_checkany(L, 1);
  if (!moon_getmetatable(L, 1)) {
    moon_pushnil(L);
    return 1;  // no metatable
  }
  moonL_getmetafield(L, 1, "__metatable");
  return 1;  // returns either __metatable field (if present) or metatable
}


static int moonB_setmetatable (moon_State *L) {
  int t = moon_type(L, 2);
  moonL_checktype(L, 1, MOON_TTABLE);
  moonL_argexpected(L, t == MOON_TNIL || t == MOON_TTABLE, 2, "nil or table");
  if (l_unlikely(moonL_getmetafield(L, 1, "__metatable") != MOON_TNIL))
    return moonL_error(L, "cannot change a protected metatable");
  moon_settop(L, 2);
  moon_setmetatable(L, 1);
  return 1;
}


static int moonB_rawequal (moon_State *L) {
  moonL_checkany(L, 1);
  moonL_checkany(L, 2);
  moon_pushboolean(L, moon_rawequal(L, 1, 2));
  return 1;
}


static int moonB_rawlen (moon_State *L) {
  int t = moon_type(L, 1);
  moonL_argexpected(L, t == MOON_TTABLE || t == MOON_TSTRING, 1,
                      "table or string");
  moon_pushinteger(L, l_castU2S(moon_rawlen(L, 1)));
  return 1;
}


static int moonB_rawget (moon_State *L) {
  moonL_checktype(L, 1, MOON_TTABLE);
  moonL_checkany(L, 2);
  moon_settop(L, 2);
  moon_rawget(L, 1);
  return 1;
}

static int moonB_rawset (moon_State *L) {
  moonL_checktype(L, 1, MOON_TTABLE);
  moonL_checkany(L, 2);
  moonL_checkany(L, 3);
  moon_settop(L, 3);
  moon_rawset(L, 1);
  return 1;
}


static int pushmode (moon_State *L, int oldmode) {
  if (oldmode == -1)
    moonL_pushfail(L);  // invalid call to 'moon_gc'
  else
    moon_pushstring(L, (oldmode == MOON_GCINC) ? "incremental"
                                             : "generational");
  return 1;
}


/*
** check whether call to 'moon_gc' was valid (not inside a finalizer)
*/
#define checkvalres(res) { if (res == -1) break; }

static int moonB_collectgarbage (moon_State *L) {
  static const char *const opts[] = {"stop", "restart", "collect",
    "count", "step", "isrunning", "generational", "incremental",
    "param", nullptr};
  static const char optsnum[] = {MOON_GCSTOP, MOON_GCRESTART, MOON_GCCOLLECT,
    MOON_GCCOUNT, MOON_GCSTEP, MOON_GCISRUNNING, MOON_GCGEN, MOON_GCINC,
    MOON_GCPARAM};
  int o = optsnum[moonL_checkoption(L, 1, "collect", opts)];
  switch (o) {
    case MOON_GCCOUNT: {
      int k = moon_gc(L, o);
      int b = moon_gc(L, MOON_GCCOUNTB);
      checkvalres(k);
      moon_pushnumber(L, (moon_Number)k + ((moon_Number)b/1024));
      return 1;
    }
    case MOON_GCSTEP: {
      moon_Integer n = moonL_optinteger(L, 2, 0);
      int res = moon_gc(L, o, cast_sizet(n));
      checkvalres(res);
      moon_pushboolean(L, res);
      return 1;
    }
    case MOON_GCISRUNNING: {
      int res = moon_gc(L, o);
      checkvalres(res);
      moon_pushboolean(L, res);
      return 1;
    }
    case MOON_GCGEN: {
      return pushmode(L, moon_gc(L, o));
    }
    case MOON_GCINC: {
      return pushmode(L, moon_gc(L, o));
    }
    case MOON_GCPARAM: {
      static const char *const params[] = {
        "minormul", "majorminor", "minormajor",
        "pause", "stepmul", "stepsize", nullptr};
      static const char pnum[] = {
        MOON_GCPMINORMUL, MOON_GCPMAJORMINOR, MOON_GCPMINORMAJOR,
        MOON_GCPPAUSE, MOON_GCPSTEPMUL, MOON_GCPSTEPSIZE};
      int p = pnum[moonL_checkoption(L, 2, nullptr, params)];
      moon_Integer value = moonL_optinteger(L, 3, -1);
      moon_pushinteger(L, moon_gc(L, o, p, (int)value));
      return 1;
    }
    default: {
      int res = moon_gc(L, o);
      checkvalres(res);
      moon_pushinteger(L, res);
      return 1;
    }
  }
  moonL_pushfail(L);  // invalid call (inside a finalizer)
  return 1;
}


static int moonB_type (moon_State *L) {
  int t = moon_type(L, 1);
  moonL_argcheck(L, t != MOON_TNONE, 1, "value expected");
  moon_pushstring(L, moon_typename(L, t));
  return 1;
}


static int moonB_next (moon_State *L) {
  moonL_checktype(L, 1, MOON_TTABLE);
  moon_settop(L, 2);  // create a 2nd argument if there isn't one
  if (moon_next(L, 1))
    return 2;
  else {
    moon_pushnil(L);
    return 1;
  }
}


static int pairscont (moon_State *L, int status, moon_KContext k) {
  (void)L; (void)status; (void)k;  // unused
  return 3;
}

static int moonB_pairs (moon_State *L) {
  moonL_checkany(L, 1);
  if (moonL_getmetafield(L, 1, "__pairs") == MOON_TNIL) {  // no metamethod?
    moon_pushcfunction(L, moonB_next);  // will return generator,
    moon_pushvalue(L, 1);  // state,
    moon_pushnil(L);  // and initial value
  }
  else {
    moon_pushvalue(L, 1);  // argument 'self' to metamethod
    moon_callk(L, 1, 3, 0, pairscont);  // get 3 values from metamethod
  }
  return 3;
}


/*
** Traversal function for 'ipairs'
*/
static int ipairsaux (moon_State *L) {
  moon_Integer i = moonL_checkinteger(L, 2);
  i = moonL_intop(+, i, 1);
  moon_pushinteger(L, i);
  return (moon_geti(L, 1, i) == MOON_TNIL) ? 1 : 2;
}


/*
** 'ipairs' function. Returns 'ipairsaux', given "table", 0.
** (The given "table" may not be a table.)
*/
static int moonB_ipairs (moon_State *L) {
  moonL_checkany(L, 1);
  moon_pushcfunction(L, ipairsaux);  // iteration function
  moon_pushvalue(L, 1);  // state
  moon_pushinteger(L, 0);  // initial value
  return 3;
}


static int load_aux (moon_State *L, int status, int envidx) {
  if (l_likely(status == MOON_OK)) {
    if (envidx != 0) {  // 'env' parameter?
      moon_pushvalue(L, envidx);  // environment for loaded function
      if (!moon_setupvalue(L, -2, 1))  // set it as 1st upvalue
        moon_pop(L, 1);  // remove 'env' if not used by previous call
    }
    return 1;
  }
  else {  // error (message is on top of the stack)
    moonL_pushfail(L);
    moon_insert(L, -2);  // put before error message
    return 2;  // return fail plus error message
  }
}


static const char *getMode (moon_State *L, int idx) {
  const char *mode = moonL_optstring(L, idx, "bt");
  if (strchr(mode, 'B') != nullptr)  // Lua code cannot use fixed buffers
    moonL_argerror(L, idx, "invalid mode");
  return mode;
}


static int moonB_loadfile (moon_State *L) {
  const char *fname = moonL_optstring(L, 1, nullptr);
  const char *mode = getMode(L, 2);
  int env = (!moon_isnone(L, 3) ? 3 : 0);  // 'env' index or 0 if no 'env'
  int status = moonL_loadfilex(L, fname, mode);
  return load_aux(L, status, env);
}


/*
** {======================================================
** Generic Read function
** =======================================================
*/


/*
** reserved slot, above all arguments, to hold a copy of the returned
** string to avoid it being collected while parsed. 'load' has four
** optional arguments (chunk, source name, mode, and environment).
*/
#define RESERVEDSLOT	5


/*
** Reader for generic 'load' function: 'moon_load' uses the
** stack for internal stuff, so the reader cannot change the
** stack top. Instead, it keeps its resulting string in a
** reserved slot inside the stack.
*/
static const char *generic_reader (moon_State *L, void *ud, size_t *size) {
  (void)(ud);  // not used
  moonL_checkstack(L, 2, "too many nested functions");
  moon_pushvalue(L, 1);  // get function
  moon_call(L, 0, 1);  // call it
  if (moon_isnil(L, -1)) {
    moon_pop(L, 1);  // pop result
    *size = 0;
    return nullptr;
  }
  else if (l_unlikely(!moon_isstring(L, -1)))
    moonL_error(L, "reader function must return a string");
  moon_replace(L, RESERVEDSLOT);  // save string in reserved slot
  return moon_tolstring(L, RESERVEDSLOT, size);
}


static int moonB_load (moon_State *L) {
  int status;
  size_t l;
  const char *s = moon_tolstring(L, 1, &l);
  const char *mode = getMode(L, 3);
  int env = (!moon_isnone(L, 4) ? 4 : 0);  // 'env' index or 0 if no 'env'
  if (s != nullptr) {  // loading a string?
    const char *chunkname = moonL_optstring(L, 2, s);
    status = moonL_loadbufferx(L, s, l, chunkname, mode);
  }
  else {  // loading from a reader function
    const char *chunkname = moonL_optstring(L, 2, "=(load)");
    moonL_checktype(L, 1, MOON_TFUNCTION);
    moon_settop(L, RESERVEDSLOT);  // create reserved slot
    status = moon_load(L, generic_reader, nullptr, chunkname, mode);
  }
  return load_aux(L, status, env);
}

// }======================================================


static int dofilecont (moon_State *L, int d1, moon_KContext d2) {
  (void)d1;  (void)d2;  // only to match 'moon_Kfunction' prototype
  return moon_gettop(L) - 1;
}


static int moonB_dofile (moon_State *L) {
  const char *fname = moonL_optstring(L, 1, nullptr);
  moon_settop(L, 1);
  if (l_unlikely(moonL_loadfile(L, fname) != MOON_OK))
    return moon_error(L);
  moon_callk(L, 0, MOON_MULTRET, 0, dofilecont);
  return dofilecont(L, 0, 0);
}


static int moonB_assert (moon_State *L) {
  if (l_likely(moon_toboolean(L, 1)))  // condition is true?
    return moon_gettop(L);  // return all arguments
  else {  // error
    moonL_checkany(L, 1);  // there must be a condition
    moon_remove(L, 1);  // remove it
    moon_pushliteral(L, "assertion failed!");  // default message
    moon_settop(L, 1);  // leave only message (default if no other one)
    return moonB_error(L);  // call 'error'
  }
}


static int moonB_select (moon_State *L) {
  int n = moon_gettop(L);
  if (moon_type(L, 1) == MOON_TSTRING && *moon_tostring(L, 1) == '#') {
    moon_pushinteger(L, n-1);
    return 1;
  }
  else {
    moon_Integer i = moonL_checkinteger(L, 1);
    if (i < 0) i = n + i;
    else if (i > n) i = n;
    moonL_argcheck(L, 1 <= i, 1, "index out of range");
    return n - (int)i;
  }
}


/*
** Continuation function for 'pcall' and 'xpcall'. Both functions
** already pushed a 'true' before doing the call, so in case of success
** 'finishpcall' only has to return everything in the stack minus
** 'extra' values (where 'extra' is exactly the number of items to be
** ignored).
*/
static int finishpcall (moon_State *L, int status, moon_KContext extra) {
  if (l_unlikely(status != MOON_OK && status != MOON_YIELD)) {  // error?
    moon_pushboolean(L, 0);  // first result (false)
    moon_pushvalue(L, -2);  // error message
    return 2;  // return false, msg
  }
  else
    return moon_gettop(L) - (int)extra;  // return all results
}


static int moonB_pcall (moon_State *L) {
  moonL_checkany(L, 1);
  moon_pushboolean(L, 1);  // first result if no errors
  moon_insert(L, 1);  // put it in place
  const int status = moon_pcallk(L, moon_gettop(L) - 2, MOON_MULTRET, 0, 0, finishpcall);
  return finishpcall(L, status, 0);
}


/*
** Do a protected call with error handling. After 'moon_rotate', the
** stack will have <f, err, true, f, [args...]>; so, the function passes
** 2 to 'finishpcall' to skip the 2 first values when returning results.
*/
static int moonB_xpcall (moon_State *L) {
  int n = moon_gettop(L);
  moonL_checktype(L, 2, MOON_TFUNCTION);  // check error function
  moon_pushboolean(L, 1);  // first result
  moon_pushvalue(L, 1);  // function
  moon_rotate(L, 3, 2);  // move them below function's arguments
  const int status = moon_pcallk(L, n - 2, MOON_MULTRET, 2, 2, finishpcall);
  return finishpcall(L, status, 2);
}


static int moonB_tostring (moon_State *L) {
  moonL_checkany(L, 1);
  moonL_tolstring(L, 1, nullptr);
  return 1;
}


static const moonL_Reg base_funcs[] = {
  {"assert", moonB_assert},
  {"collectgarbage", moonB_collectgarbage},
  {"dofile", moonB_dofile},
  {"error", moonB_error},
  {"getmetatable", moonB_getmetatable},
  {"ipairs", moonB_ipairs},
  {"loadfile", moonB_loadfile},
  {"load", moonB_load},
  {"next", moonB_next},
  {"pairs", moonB_pairs},
  {"pcall", moonB_pcall},
  {"print", moonB_print},
  {"warn", moonB_warn},
  {"rawequal", moonB_rawequal},
  {"rawlen", moonB_rawlen},
  {"rawget", moonB_rawget},
  {"rawset", moonB_rawset},
  {"select", moonB_select},
  {"setmetatable", moonB_setmetatable},
  {"tonumber", moonB_tonumber},
  {"tostring", moonB_tostring},
  {"type", moonB_type},
  {"xpcall", moonB_xpcall},
  // placeholders
  {MOON_GNAME, nullptr},
  {"_VERSION", nullptr},
  {nullptr, nullptr}
};


MOONMOD_API int moonopen_base (moon_State *L) {
  // open lib into global table
  moon_pushglobaltable(L);
  moonL_setfuncs(L, base_funcs, 0);
  // set global _G
  moon_pushvalue(L, -1);
  moon_setfield(L, -2, MOON_GNAME);
  // set global _VERSION
  moon_pushliteral(L, MOON_VERSION);
  moon_setfield(L, -2, "_VERSION");
  return 1;
}

