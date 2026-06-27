/*
** $Id: lualib.h $
** Lua standard libraries
** See Copyright Notice in lua.h
*/


#ifndef moonlib_h
#define moonlib_h

#include "lua.h"


#ifdef __cplusplus
extern "C" {
#endif


/* version suffix for environment variable names */
#define MOON_VERSUFFIX          "_" MOON_VERSION_MAJOR "_" MOON_VERSION_MINOR

#define MOON_GLIBK		1
MOONMOD_API int (moonopen_base) (moon_State *L);

#define MOON_LOADLIBNAME	"package"
#define MOON_LOADLIBK	(MOON_GLIBK << 1)
MOONMOD_API int (moonopen_package) (moon_State *L);


#define MOON_COLIBNAME	"coroutine"
#define MOON_COLIBK	(MOON_LOADLIBK << 1)
MOONMOD_API int (moonopen_coroutine) (moon_State *L);

#define MOON_DBLIBNAME	"debug"
#define MOON_DBLIBK	(MOON_COLIBK << 1)
MOONMOD_API int (moonopen_debug) (moon_State *L);

#define MOON_IOLIBNAME	"io"
#define MOON_IOLIBK	(MOON_DBLIBK << 1)
MOONMOD_API int (moonopen_io) (moon_State *L);

#define MOON_MATHLIBNAME	"math"
#define MOON_MATHLIBK	(MOON_IOLIBK << 1)
MOONMOD_API int (moonopen_math) (moon_State *L);

#define MOON_OSLIBNAME	"os"
#define MOON_OSLIBK	(MOON_MATHLIBK << 1)
MOONMOD_API int (moonopen_os) (moon_State *L);

#define MOON_STRLIBNAME	"string"
#define MOON_STRLIBK	(MOON_OSLIBK << 1)
MOONMOD_API int (moonopen_string) (moon_State *L);

#define MOON_TABLIBNAME	"table"
#define MOON_TABLIBK	(MOON_STRLIBK << 1)
MOONMOD_API int (moonopen_table) (moon_State *L);

#define MOON_UTF8LIBNAME	"utf8"
#define MOON_UTF8LIBK	(MOON_TABLIBK << 1)
MOONMOD_API int (moonopen_utf8) (moon_State *L);


/* open selected libraries */
MOONLIB_API void (moonL_openselectedlibs) (moon_State *L, int load, int preload);

/* open all libraries */
#define moonL_openlibs(L)	moonL_openselectedlibs(L, ~0, 0)


#ifdef __cplusplus
}
#endif

#endif
