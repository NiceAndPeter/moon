/*
** Initialization of libraries for lua.c and other clients
** See Copyright Notice in lua.h
*/


#define MOON_LIB


#include "mprefix.h"


#include <cstddef>

#include "moon.h"

#include "moonlib.h"
#include "mauxlib.h"
#include "mlimits.h"


/*
** Standard Libraries. (Must be listed in the same ORDER of their
** respective constants MOON_<libname>K.)
*/
static const moonL_Reg stdlibs[] = {
  {MOON_GNAME, moonopen_base},
  {MOON_LOADLIBNAME, moonopen_package},
  {MOON_COLIBNAME, moonopen_coroutine},
  {MOON_DBLIBNAME, moonopen_debug},
  {MOON_IOLIBNAME, moonopen_io},
  {MOON_MATHLIBNAME, moonopen_math},
  {MOON_OSLIBNAME, moonopen_os},
  {MOON_STRLIBNAME, moonopen_string},
  {MOON_TABLIBNAME, moonopen_table},
  {MOON_UTF8LIBNAME, moonopen_utf8},
  {nullptr, nullptr}
};


/*
** require and preload selected standard libraries
*/
MOONLIB_API void moonL_openselectedlibs (moon_State *L, int load, int preload) {
  int mask;
  const moonL_Reg *lib;
  moonL_getsubtable(L, MOON_REGISTRYINDEX, MOON_PRELOAD_TABLE);
  for (lib = stdlibs, mask = 1; lib->name != nullptr; lib++, mask <<= 1) {
    if (load & mask) {  // selected?
      moonL_requiref(L, lib->name, lib->func, 1);  // require library
      moon_pop(L, 1);  // remove result from the stack
    }
    else if (preload & mask) {  // selected?
      moon_pushcfunction(L, lib->func);
      moon_setfield(L, -2, lib->name);  // add library to PRELOAD table
    }
  }
  moon_assert((mask >> 1) == MOON_UTF8LIBK);
  moon_pop(L, 1);  // remove PRELOAD table
}

