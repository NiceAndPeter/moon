/*
** Interface from Lua to its debug API
** See Copyright Notice in lua.h
*/

#define MOON_LIB

#include "mprefix.h"


#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "moon.h"

#include "mauxlib.h"
#include "moonlib.h"
#include "mlimits.h"


/*
** The hook table at registry[HOOKKEY] maps threads to their current
** hook function.
*/
static const char *const HOOKKEY = "_HOOKKEY";


/*
** If L1 != L, L1 can be in any state, and therefore there are no
** guarantees about its stack space; any push in L1 must be
** checked.
*/
static void checkstack (moon_State *L, moon_State *L1, int n) {
  if (l_unlikely(L != L1 && !moon_checkstack(L1, n)))
    moonL_error(L, "stack overflow");
}


static int db_getregistry (moon_State *L) {
  moon_pushvalue(L, MOON_REGISTRYINDEX);
  return 1;
}


static int db_getmetatable (moon_State *L) {
  moonL_checkany(L, 1);
  if (!moon_getmetatable(L, 1)) {
    moon_pushnil(L);  // no metatable
  }
  return 1;
}


static int db_setmetatable (moon_State *L) {
  int t = moon_type(L, 2);
  moonL_argexpected(L, t == MOON_TNIL || t == MOON_TTABLE, 2, "nil or table");
  moon_settop(L, 2);
  moon_setmetatable(L, 1);
  return 1;  // return 1st argument
}


static int db_getuservalue (moon_State *L) {
  int n = (int)moonL_optinteger(L, 2, 1);
  if (moon_type(L, 1) != MOON_TUSERDATA)
    moonL_pushfail(L);
  else if (moon_getiuservalue(L, 1, n) != MOON_TNONE) {
    moon_pushboolean(L, 1);
    return 2;
  }
  return 1;
}


static int db_setuservalue (moon_State *L) {
  int n = (int)moonL_optinteger(L, 3, 1);
  moonL_checktype(L, 1, MOON_TUSERDATA);
  moonL_checkany(L, 2);
  moon_settop(L, 2);
  if (!moon_setiuservalue(L, 1, n))
    moonL_pushfail(L);
  return 1;
}


/*
** Auxiliary function used by several library functions: check for
** an optional thread as function's first argument and set 'arg' with
** 1 if this argument is present (so that functions can skip it to
** access their other arguments)
*/
static moon_State *getthread (moon_State *L, int *arg) {
  if (moon_isthread(L, 1)) {
    *arg = 1;
    return moon_tothread(L, 1);
  }
  else {
    *arg = 0;
    return L;  // function will operate over current thread
  }
}


/*
** Variations of 'moon_settable', used by 'db_getinfo' to put results
** from 'moon_getinfo' into result table. Key is always a string;
** value can be a string, an int, or a boolean.
*/
static void settabss (moon_State *L, const char *k, const char *v) {
  moon_pushstring(L, v);
  moon_setfield(L, -2, k);
}

static void settabsi (moon_State *L, const char *k, int v) {
  moon_pushinteger(L, v);
  moon_setfield(L, -2, k);
}

static void settabsb (moon_State *L, const char *k, int v) {
  moon_pushboolean(L, v);
  moon_setfield(L, -2, k);
}


/*
** In function 'db_getinfo', the call to 'moon_getinfo' may push
** results on the stack; later it creates the result table to put
** these objects. Function 'treatstackoption' puts the result from
** 'moon_getinfo' on top of the result table so that it can call
** 'moon_setfield'.
*/
static void treatstackoption (moon_State *L, moon_State *L1, const char *fname) {
  if (L == L1)
    moon_rotate(L, -2, 1);  // exchange object and table
  else
    moon_xmove(L1, L, 1);  // move object to the "main" stack
  moon_setfield(L, -2, fname);  // put object into table
}


/*
** Calls 'moon_getinfo' and collects all results in a new table.
** L1 needs stack space for an optional input (function) plus
** two optional outputs (function and line table) from function
** 'moon_getinfo'.
*/
static int db_getinfo (moon_State *L) {
  moon_Debug ar;
  int arg;
  moon_State *L1 = getthread(L, &arg);
  const char *options = moonL_optstring(L, arg+2, "flnSrtu");
  checkstack(L, L1, 3);
  moonL_argcheck(L, options[0] != '>', arg + 2, "invalid option '>'");
  if (moon_isfunction(L, arg + 1)) {  // info about a function?
    options = moon_pushfstring(L, ">%s", options);  // add '>' to 'options'
    moon_pushvalue(L, arg + 1);  // move function to 'L1' stack
    moon_xmove(L, L1, 1);
  }
  else {  // stack level
    if (!moon_getstack(L1, (int)moonL_checkinteger(L, arg + 1), &ar)) {
      moonL_pushfail(L);  // level out of range
      return 1;
    }
  }
  if (!moon_getinfo(L1, options, &ar))
    return moonL_argerror(L, arg+2, "invalid option");
  moon_newtable(L);  // table to collect results
  if (strchr(options, 'S')) {
    moon_pushlstring(L, ar.source, ar.srclen);
    moon_setfield(L, -2, "source");
    settabss(L, "short_src", ar.short_src);
    settabsi(L, "linedefined", ar.linedefined);
    settabsi(L, "lastlinedefined", ar.lastlinedefined);
    settabss(L, "what", ar.what);
  }
  if (strchr(options, 'l'))
    settabsi(L, "currentline", ar.currentline);
  if (strchr(options, 'u')) {
    settabsi(L, "nups", ar.nups);
    settabsi(L, "nparams", ar.nparams);
    settabsb(L, "isvararg", ar.isvararg);
  }
  if (strchr(options, 'n')) {
    settabss(L, "name", ar.name);
    settabss(L, "namewhat", ar.namewhat);
  }
  if (strchr(options, 'r')) {
    settabsi(L, "ftransfer", ar.ftransfer);
    settabsi(L, "ntransfer", ar.ntransfer);
  }
  if (strchr(options, 't')) {
    settabsb(L, "istailcall", ar.istailcall);
    settabsi(L, "extraargs", ar.extraargs);
  }
  if (strchr(options, 'L'))
    treatstackoption(L, L1, "activelines");
  if (strchr(options, 'f'))
    treatstackoption(L, L1, "func");
  return 1;  // return table
}


static int db_getlocal (moon_State *L) {
  int arg;
  moon_State *L1 = getthread(L, &arg);
  int nvar = (int)moonL_checkinteger(L, arg + 2);  // local-variable index
  if (moon_isfunction(L, arg + 1)) {  // function argument?
    moon_pushvalue(L, arg + 1);  // push function
    moon_pushstring(L, moon_getlocal(L, nullptr, nvar));  // push local name
    return 1;  // return only name (there is no value)
  }
  else {  // stack-level argument
    moon_Debug ar;
    const char *name;
    int level = (int)moonL_checkinteger(L, arg + 1);
    if (l_unlikely(!moon_getstack(L1, level, &ar)))  // out of range?
      return moonL_argerror(L, arg+1, "level out of range");
    checkstack(L, L1, 1);
    name = moon_getlocal(L1, &ar, nvar);
    if (name) {
      moon_xmove(L1, L, 1);  // move local value
      moon_pushstring(L, name);  // push name
      moon_rotate(L, -2, 1);  // re-order
      return 2;
    }
    else {
      moonL_pushfail(L);  // no name (nor value)
      return 1;
    }
  }
}


static int db_setlocal (moon_State *L) {
  int arg;
  const char *name;
  moon_State *L1 = getthread(L, &arg);
  moon_Debug ar;
  int level = (int)moonL_checkinteger(L, arg + 1);
  int nvar = (int)moonL_checkinteger(L, arg + 2);
  if (l_unlikely(!moon_getstack(L1, level, &ar)))  // out of range?
    return moonL_argerror(L, arg+1, "level out of range");
  moonL_checkany(L, arg+3);
  moon_settop(L, arg+3);
  checkstack(L, L1, 1);
  moon_xmove(L, L1, 1);
  name = moon_setlocal(L1, &ar, nvar);
  if (name == nullptr)
    moon_pop(L1, 1);  // pop value (if not popped by 'moon_setlocal')
  moon_pushstring(L, name);
  return 1;
}


/*
** get (if 'get' is true) or set an upvalue from a closure
*/
static int auxupvalue (moon_State *L, int get) {
  const char *name;
  int n = (int)moonL_checkinteger(L, 2);  // upvalue index
  moonL_checktype(L, 1, MOON_TFUNCTION);  // closure
  name = get ? moon_getupvalue(L, 1, n) : moon_setupvalue(L, 1, n);
  if (name == nullptr) return 0;
  moon_pushstring(L, name);
  moon_insert(L, -(get+1));  // no-op if get is false
  return get + 1;
}


static int db_getupvalue (moon_State *L) {
  return auxupvalue(L, 1);
}


static int db_setupvalue (moon_State *L) {
  moonL_checkany(L, 3);
  return auxupvalue(L, 0);
}


/*
** Check whether a given upvalue from a given closure exists and
** returns its index
*/
static void *checkupval (moon_State *L, int argf, int argnup, int *pnup) {
  void *id;
  int nup = (int)moonL_checkinteger(L, argnup);  // upvalue index
  moonL_checktype(L, argf, MOON_TFUNCTION);  // closure
  id = moon_upvalueid(L, argf, nup);
  if (pnup) {
    moonL_argcheck(L, id != nullptr, argnup, "invalid upvalue index");
    *pnup = nup;
  }
  return id;
}


static int db_upvalueid (moon_State *L) {
  void *id = checkupval(L, 1, 2, nullptr);
  if (id != nullptr)
    moon_pushlightuserdata(L, id);
  else
    moonL_pushfail(L);
  return 1;
}


static int db_upvaluejoin (moon_State *L) {
  int n1, n2;
  checkupval(L, 1, 2, &n1);
  checkupval(L, 3, 4, &n2);
  moonL_argcheck(L, !moon_iscfunction(L, 1), 1, "Lua function expected");
  moonL_argcheck(L, !moon_iscfunction(L, 3), 3, "Lua function expected");
  moon_upvaluejoin(L, 1, n1, 3, n2);
  return 0;
}


/*
** Call hook function registered at hook table for the current
** thread (if there is one)
*/
static void hookf (moon_State *L, moon_Debug *ar) {
  static const char *const hooknames[] =
    {"call", "return", "line", "count", "tail call"};
  moon_getfield(L, MOON_REGISTRYINDEX, HOOKKEY);
  moon_pushthread(L);
  if (moon_rawget(L, -2) == MOON_TFUNCTION) {  // is there a hook function?
    moon_pushstring(L, hooknames[(int)ar->event]);  // push event name
    if (ar->currentline >= 0)
      moon_pushinteger(L, ar->currentline);  // push current line
    else moon_pushnil(L);
    moon_assert(moon_getinfo(L, "lS", ar));
    moon_call(L, 2, 0);  // call hook function
  }
}


/*
** Convert a string mask (for 'sethook') into a bit mask
*/
static int makemask (const char *smask, int count) {
  int mask = 0;
  if (strchr(smask, 'c')) mask |= MOON_MASKCALL;
  if (strchr(smask, 'r')) mask |= MOON_MASKRET;
  if (strchr(smask, 'l')) mask |= MOON_MASKLINE;
  if (count > 0) mask |= MOON_MASKCOUNT;
  return mask;
}


/*
** Convert a bit mask (for 'gethook') into a string mask
*/
static char *unmakemask (int mask, char *smask) {
  int i = 0;
  if (mask & MOON_MASKCALL) smask[i++] = 'c';
  if (mask & MOON_MASKRET) smask[i++] = 'r';
  if (mask & MOON_MASKLINE) smask[i++] = 'l';
  smask[i] = '\0';
  return smask;
}


static int db_sethook (moon_State *L) {
  int arg, mask, count;
  moon_Hook func;
  moon_State *L1 = getthread(L, &arg);
  if (moon_isnoneornil(L, arg+1)) {  // no hook?
    moon_settop(L, arg+1);
    func = nullptr; mask = 0; count = 0;  // turn off hooks
  }
  else {
    const char *smask = moonL_checkstring(L, arg+2);
    moonL_checktype(L, arg+1, MOON_TFUNCTION);
    count = (int)moonL_optinteger(L, arg + 3, 0);
    func = hookf; mask = makemask(smask, count);
  }
  if (!moonL_getsubtable(L, MOON_REGISTRYINDEX, HOOKKEY)) {
    // table just created; initialize it
    moon_pushliteral(L, "k");
    moon_setfield(L, -2, "__mode");  // * hooktable.__mode = "k"
    moon_pushvalue(L, -1);
    moon_setmetatable(L, -2);  // metatable(hooktable) = hooktable
  }
  checkstack(L, L1, 1);
  moon_pushthread(L1); moon_xmove(L1, L, 1);  // key (thread)
  moon_pushvalue(L, arg + 1);  // value (hook function)
  moon_rawset(L, -3);  // hooktable[L1] = new Lua hook
  moon_sethook(L1, func, mask, count);
  return 0;
}


static int db_gethook (moon_State *L) {
  int arg;
  moon_State *L1 = getthread(L, &arg);
  char buff[5];
  int mask = moon_gethookmask(L1);
  moon_Hook hook = moon_gethook(L1);
  if (hook == nullptr) {  // no hook?
    moonL_pushfail(L);
    return 1;
  }
  else if (hook != hookf)  // external hook?
    moon_pushliteral(L, "external hook");
  else {  // hook table must exist
    moon_getfield(L, MOON_REGISTRYINDEX, HOOKKEY);
    checkstack(L, L1, 1);
    moon_pushthread(L1); moon_xmove(L1, L, 1);
    moon_rawget(L, -2);  // 1st result = hooktable[L1]
    moon_remove(L, -2);  // remove hook table
  }
  moon_pushstring(L, unmakemask(mask, buff));  // 2nd result = mask
  moon_pushinteger(L, moon_gethookcount(L1));  // 3rd result = count
  return 3;
}


static int db_debug (moon_State *L) {
  for (;;) {
    char buffer[250];
    moon_writestringerror("%s", "moon_debug> ");
    if (fgets(buffer, sizeof(buffer), stdin) == nullptr ||
        strcmp(buffer, "cont\n") == 0)
      return 0;
    if (moonL_loadbuffer(L, buffer, strlen(buffer), "=(debug command)") ||
        moon_pcall(L, 0, 0, 0))
      moon_writestringerror("%s\n", moonL_tolstring(L, -1, nullptr));
    moon_settop(L, 0);  // remove eventual returns
  }
}


static int db_traceback (moon_State *L) {
  int arg;
  moon_State *L1 = getthread(L, &arg);
  const char *msg = moon_tostring(L, arg + 1);
  if (msg == nullptr && !moon_isnoneornil(L, arg + 1))  // non-string 'msg'?
    moon_pushvalue(L, arg + 1);  // return it untouched
  else {
    int level = (int)moonL_optinteger(L, arg + 2, (L == L1) ? 1 : 0);
    moonL_traceback(L, L1, msg, level);
  }
  return 1;
}


static const moonL_Reg dblib[] = {
  {"debug", db_debug},
  {"getuservalue", db_getuservalue},
  {"gethook", db_gethook},
  {"getinfo", db_getinfo},
  {"getlocal", db_getlocal},
  {"getregistry", db_getregistry},
  {"getmetatable", db_getmetatable},
  {"getupvalue", db_getupvalue},
  {"upvaluejoin", db_upvaluejoin},
  {"upvalueid", db_upvalueid},
  {"setuservalue", db_setuservalue},
  {"sethook", db_sethook},
  {"setlocal", db_setlocal},
  {"setmetatable", db_setmetatable},
  {"setupvalue", db_setupvalue},
  {"traceback", db_traceback},
  {nullptr, nullptr}
};


MOONMOD_API int moonopen_debug (moon_State *L) {
  moonL_newlib(L, dblib);
  return 1;
}

