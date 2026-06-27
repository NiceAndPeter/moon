/*
** Standard Operating System library
** See Copyright Notice in lua.h
*/

#define MOON_LIB

#include "lprefix.h"


#include <cerrno>
#include <clocale>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"
#include "llimits.h"


/*
** {==================================================================
** List of valid conversion specifiers for the 'strftime' function;
** options are grouped by length; group of length 2 start with '||'.
** ===================================================================
*/
#if !defined(MOON_STRFTIMEOPTIONS)  // {

#if defined(MOON_USE_WINDOWS)
#define MOON_STRFTIMEOPTIONS  "aAbBcdHIjmMpSUwWxXyYzZ%" \
    "||" "#c#x#d#H#I#j#m#M#S#U#w#W#y#Y"  // two-char options
#elif defined(MOON_USE_C89)  // C89 (only 1-char options)
#define MOON_STRFTIMEOPTIONS  "aAbBcdHIjmMpSUwWxXyYZ%"
#else  // C99 specification
#define MOON_STRFTIMEOPTIONS  "aAbBcCdDeFgGhHIjmMnprRStTuUVwWxXyYzZ%" \
    "||" "EcECExEXEyEY" "OdOeOHOIOmOMOSOuOUOVOwOWOy"  // two-char options
#endif

#endif  // }
// }==================================================================


/*
** {==================================================================
** Configuration for time-related stuff
** ===================================================================
*/

/*
** type to represent time_t in Lua
*/
#if !defined(MOON_NUMTIME)  // {

#define l_timet			moon_Integer
#define l_pushtime(L,t)		moon_pushinteger(L,(moon_Integer)(t))
#define l_gettime(L,arg)	moonL_checkinteger(L, arg)

#else  // }{

#define l_timet			moon_Number
#define l_pushtime(L,t)		moon_pushnumber(L,(moon_Number)(t))
#define l_gettime(L,arg)	moonL_checknumber(L, arg)

#endif  // }


#if !defined(l_gmtime)  // {
/*
** By default, Lua uses gmtime/localtime, except when POSIX is available,
** where it uses gmtime_r/localtime_r
*/

#if defined(MOON_USE_POSIX)  // {

#define l_gmtime(t,r)		gmtime_r(t,r)
#define l_localtime(t,r)	localtime_r(t,r)

#else  // }{

// ISO C definitions
#define l_gmtime(t,r)		((void)(r)->tm_sec, gmtime(t))
#define l_localtime(t,r)	((void)(r)->tm_sec, localtime(t))

#endif  // }

#endif  // }

// }==================================================================


/*
** {==================================================================
** Configuration for 'tmpnam':
** By default, Lua uses tmpnam except when POSIX is available, where
** it uses mkstemp.
** ===================================================================
*/
#if !defined(moon_tmpnam)  // {

#if defined(MOON_USE_POSIX)  // {

#include <unistd.h>

#define MOON_TMPNAMBUFSIZE	32

#if !defined(MOON_TMPNAMTEMPLATE)
#define MOON_TMPNAMTEMPLATE	"/tmp/moon_XXXXXX"
#endif

#define moon_tmpnam(b,e) { \
        strcpy(b, MOON_TMPNAMTEMPLATE); \
        e = mkstemp(b); \
        if (e != -1) close(e); \
        e = (e == -1); }

#else  // }{

// ISO C definitions
#define MOON_TMPNAMBUFSIZE	L_tmpnam
#define moon_tmpnam(b,e)		{ e = (tmpnam(b) == nullptr); }

#endif  // }

#endif  // }
// }==================================================================


#if !defined(l_system)
#if defined(MOON_USE_IOS)
// Despite claiming to be ISO C, iOS does not implement 'system'.
#define l_system(cmd) ((cmd) == nullptr ? 0 : -1)
#else
#define l_system(cmd)	system(cmd)  // default definition
#endif
#endif


static int os_execute (moon_State *L) {
  const char *cmd = moonL_optstring(L, 1, nullptr);
  errno = 0;
  const int stat = l_system(cmd);
  if (cmd != nullptr)
    return moonL_execresult(L, stat);
  else {
    moon_pushboolean(L, stat);  // true if there is a shell
    return 1;
  }
}


static int os_remove (moon_State *L) {
  const char *filename = moonL_checkstring(L, 1);
  errno = 0;
  return moonL_fileresult(L, remove(filename) == 0, filename);
}


static int os_rename (moon_State *L) {
  const char *fromname = moonL_checkstring(L, 1);
  const char *toname = moonL_checkstring(L, 2);
  errno = 0;
  return moonL_fileresult(L, rename(fromname, toname) == 0, nullptr);
}


static int os_tmpname (moon_State *L) {
  char buff[MOON_TMPNAMBUFSIZE];
  int err;
  moon_tmpnam(buff, err);
  if (l_unlikely(err))
    return moonL_error(L, "unable to generate a unique filename");
  moon_pushstring(L, buff);
  return 1;
}


static int os_getenv (moon_State *L) {
  moon_pushstring(L, getenv(moonL_checkstring(L, 1)));  // if nullptr push nil
  return 1;
}


static int os_clock (moon_State *L) {
  moon_pushnumber(L, ((moon_Number)clock())/(moon_Number)CLOCKS_PER_SEC);
  return 1;
}


/*
** {======================================================
** Time/Date operations
** { year=%Y, month=%m, day=%d, hour=%H, min=%M, sec=%S,
**   wday=%w+1, yday=%j, isdst=? }
** =======================================================
*/

/*
** About the overflow check: an overflow cannot occur when time
** is represented by a moon_Integer, because either moon_Integer is
** large enough to represent all int fields or it is not large enough
** to represent a time that cause a field to overflow.  However, if
** times are represented as doubles and moon_Integer is int, then the
** time 0x1.e1853b0d184f6p+55 would cause an overflow when adding 1900
** to compute the year.
*/
static void setfield (moon_State *L, const char *key, int value, int delta) {
  #if (defined(MOON_NUMTIME) && MOON_MAXINTEGER <= INT_MAX)
    if (l_unlikely(value > MOON_MAXINTEGER - delta))
      moonL_error(L, "field '%s' is out-of-bound", key);
  #endif
  moon_pushinteger(L, (moon_Integer)value + delta);
  moon_setfield(L, -2, key);
}


static void setboolfield (moon_State *L, const char *key, int value) {
  if (value < 0)  // undefined?
    return;  // does not set field
  moon_pushboolean(L, value);
  moon_setfield(L, -2, key);
}


/*
** Set all fields from structure 'tm' in the table on top of the stack
*/
static void setallfields (moon_State *L, struct tm *stm) {
  setfield(L, "year", stm->tm_year, 1900);
  setfield(L, "month", stm->tm_mon, 1);
  setfield(L, "day", stm->tm_mday, 0);
  setfield(L, "hour", stm->tm_hour, 0);
  setfield(L, "min", stm->tm_min, 0);
  setfield(L, "sec", stm->tm_sec, 0);
  setfield(L, "yday", stm->tm_yday, 1);
  setfield(L, "wday", stm->tm_wday, 1);
  setboolfield(L, "isdst", stm->tm_isdst);
}


static int getboolfield (moon_State *L, const char *key) {
  const int res = (moon_getfield(L, -1, key) == MOON_TNIL) ? -1 : moon_toboolean(L, -1);
  moon_pop(L, 1);
  return res;
}


static int getfield (moon_State *L, const char *key, int d, int delta) {
  int isnum;
  int t = moon_getfield(L, -1, key);  // get field and its type
  moon_Integer res = moon_tointegerx(L, -1, &isnum);
  if (!isnum) {  // field is not an integer?
    if (l_unlikely(t != MOON_TNIL))  // some other value?
      return moonL_error(L, "field '%s' is not an integer", key);
    else if (l_unlikely(d < 0))  // absent field; no default?
      return moonL_error(L, "field '%s' missing in date table", key);
    res = d;
  }
  else {
    if (!(res >= 0 ? res - delta <= std::numeric_limits<int>::max() : std::numeric_limits<int>::min() + delta <= res))
      return moonL_error(L, "field '%s' is out-of-bound", key);
    res -= delta;
  }
  moon_pop(L, 1);
  return (int)res;
}


static const char *checkoption (moon_State *L, const char *conv,
                                size_t convlen, char *buff) {
  const char *option = MOON_STRFTIMEOPTIONS;
  unsigned oplen = 1;  // length of options being checked
  for (; *option != '\0' && oplen <= convlen; option += oplen) {
    if (*option == '|')  // next block?
      oplen++;  // will check options with next length (+1)
    else if (memcmp(conv, option, oplen) == 0) {  // match?
      memcpy(buff, conv, oplen);  // copy valid option to buffer
      buff[oplen] = '\0';
      return conv + oplen;  // return next item
    }
  }
  moonL_argerror(L, 1,
    moon_pushfstring(L, "invalid conversion specifier '%%%s'", conv));
  return conv;  // to avoid warnings
}


static time_t l_checktime (moon_State *L, int arg) {
  l_timet t = l_gettime(L, arg);
  moonL_argcheck(L, (time_t)t == t, arg, "time out-of-bounds");
  return (time_t)t;
}


// maximum size for an individual 'strftime' item
#define SIZETIMEFMT	250


static int os_date (moon_State *L) {
  size_t slen;
  const char *s = moonL_optlstring(L, 1, "%c", &slen);
  time_t t = moonL_opt(L, l_checktime, 2, time(nullptr));
  const char *se = s + slen;  // 's' end
  struct tm tmr, *stm;
  if (*s == '!') {  // UTC?
    stm = l_gmtime(&t, &tmr);
    s++;  // skip '!'
  }
  else
    stm = l_localtime(&t, &tmr);
  if (stm == nullptr)  // invalid date?
    return moonL_error(L,
                 "date result cannot be represented in this installation");
  if (strcmp(s, "*t") == 0) {
    moon_createtable(L, 0, 9);  // 9 = number of fields
    setallfields(L, stm);
  }
  else {
    char cc[4];  // buffer for individual conversion specifiers
    moonL_Buffer b;
    cc[0] = '%';
    moonL_buffinit(L, &b);
    while (s < se) {
      if (*s != '%')  // not a conversion specifier?
        moonL_addchar(&b, *s++);
      else {
        char *buff = moonL_prepbuffsize(&b, SIZETIMEFMT);
        s++;  // skip '%'
        // copy specifier to 'cc'
        s = checkoption(L, s, ct_diff2sz(se - s), cc + 1);
        const size_t reslen = strftime(buff, SIZETIMEFMT, cc, stm);
        moonL_addsize(&b, reslen);
      }
    }
    moonL_pushresult(&b);
  }
  return 1;
}


static int os_time (moon_State *L) {
  time_t t;
  if (moon_isnoneornil(L, 1))  // called without args?
    t = time(nullptr);  // get current time
  else {
    struct tm ts;
    moonL_checktype(L, 1, MOON_TTABLE);
    moon_settop(L, 1);  // make sure table is at the top
    ts.tm_year = getfield(L, "year", -1, 1900);
    ts.tm_mon = getfield(L, "month", -1, 1);
    ts.tm_mday = getfield(L, "day", -1, 0);
    ts.tm_hour = getfield(L, "hour", 12, 0);
    ts.tm_min = getfield(L, "min", 0, 0);
    ts.tm_sec = getfield(L, "sec", 0, 0);
    ts.tm_isdst = getboolfield(L, "isdst");
    t = mktime(&ts);
    setallfields(L, &ts);  // update fields with normalized values
  }
  if (t != (time_t)(l_timet)t || t == (time_t)(-1))
    return moonL_error(L,
                  "time result cannot be represented in this installation");
  l_pushtime(L, t);
  return 1;
}


static int os_difftime (moon_State *L) {
  time_t t1 = l_checktime(L, 1);
  time_t t2 = l_checktime(L, 2);
  moon_pushnumber(L, (moon_Number)difftime(t1, t2));
  return 1;
}

// }======================================================


static int os_setlocale (moon_State *L) {
  static const int cat[] = {LC_ALL, LC_COLLATE, LC_CTYPE, LC_MONETARY,
                      LC_NUMERIC, LC_TIME};
  static const char *const catnames[] = {"all", "collate", "ctype", "monetary",
     "numeric", "time", nullptr};
  const char *l = moonL_optstring(L, 1, nullptr);
  int op = moonL_checkoption(L, 2, "all", catnames);
  moon_pushstring(L, setlocale(cat[op], l));
  return 1;
}


static int os_exit (moon_State *L) {
  int status;
  if (moon_isboolean(L, 1))
    status = (moon_toboolean(L, 1) ? EXIT_SUCCESS : EXIT_FAILURE);
  else
    status = (int)moonL_optinteger(L, 1, EXIT_SUCCESS);
  if (moon_toboolean(L, 2))
    moon_close(L);
  if (L) exit(status);  // 'if' to avoid warnings for unreachable 'return'
  return 0;
}


static const moonL_Reg syslib[] = {
  {"clock",     os_clock},
  {"date",      os_date},
  {"difftime",  os_difftime},
  {"execute",   os_execute},
  {"exit",      os_exit},
  {"getenv",    os_getenv},
  {"remove",    os_remove},
  {"rename",    os_rename},
  {"setlocale", os_setlocale},
  {"time",      os_time},
  {"tmpname",   os_tmpname},
  {nullptr, nullptr}
};

// }======================================================



MOONMOD_API int moonopen_os (moon_State *L) {
  moonL_newlib(L, syslib);
  return 1;
}

