/*
** $Id: lauxlib.h $
** Auxiliary functions for building Lua libraries
** See Copyright Notice in lua.h
*/


#ifndef lauxlib_h
#define lauxlib_h


#include <stddef.h>
#include <stdio.h>

#include "luaconf.h"
#include "lua.h"

#ifdef __cplusplus
#include <span>
#endif

#ifdef __cplusplus
extern "C" {
#endif


/* global table */
#define MOON_GNAME	"_G"


typedef struct moonL_Buffer moonL_Buffer;


/* extra error code for 'moonL_loadfilex' */
#define MOON_ERRFILE     (MOON_ERRERR+1)


/* key, in the registry, for table of loaded modules */
#define MOON_LOADED_TABLE	"_LOADED"


/* key, in the registry, for table of preloaded loaders */
#define MOON_PRELOAD_TABLE	"_PRELOAD"


typedef struct moonL_Reg {
  const char *name;
  moon_CFunction func;
} moonL_Reg;


#define MOONL_NUMSIZES	(sizeof(moon_Integer)*16 + sizeof(moon_Number))

MOONLIB_API void (moonL_checkversion_) (moon_State *L, moon_Number ver, size_t sz);
#define moonL_checkversion(L)  \
	  moonL_checkversion_(L, MOON_VERSION_NUM, MOONL_NUMSIZES)

MOONLIB_API int (moonL_getmetafield) (moon_State *L, int obj, const char *e);
MOONLIB_API int (moonL_callmeta) (moon_State *L, int obj, const char *e);
MOONLIB_API const char *(moonL_tolstring) (moon_State *L, int idx, size_t *len);
MOONLIB_API int (moonL_argerror) (moon_State *L, int arg, const char *extramsg);
MOONLIB_API int (moonL_typeerror) (moon_State *L, int arg, const char *tname);
MOONLIB_API const char *(moonL_checklstring) (moon_State *L, int arg,
                                                          size_t *l);
MOONLIB_API const char *(moonL_optlstring) (moon_State *L, int arg,
                                          const char *def, size_t *l);
MOONLIB_API moon_Number (moonL_checknumber) (moon_State *L, int arg);
MOONLIB_API moon_Number (moonL_optnumber) (moon_State *L, int arg, moon_Number def);

MOONLIB_API moon_Integer (moonL_checkinteger) (moon_State *L, int arg);
MOONLIB_API moon_Integer (moonL_optinteger) (moon_State *L, int arg,
                                          moon_Integer def);

MOONLIB_API void (moonL_checkstack) (moon_State *L, int sz, const char *msg);
MOONLIB_API void (moonL_checktype) (moon_State *L, int arg, int t);
MOONLIB_API void (moonL_checkany) (moon_State *L, int arg);

MOONLIB_API int   (moonL_newmetatable) (moon_State *L, const char *tname);
MOONLIB_API void  (moonL_setmetatable) (moon_State *L, const char *tname);
MOONLIB_API void *(moonL_testudata) (moon_State *L, int ud, const char *tname);
MOONLIB_API void *(moonL_checkudata) (moon_State *L, int ud, const char *tname);

MOONLIB_API void (moonL_where) (moon_State *L, int lvl);
MOONLIB_API int (moonL_error) (moon_State *L, const char *fmt, ...);

MOONLIB_API int (moonL_checkoption) (moon_State *L, int arg, const char *def,
                                   const char *const lst[]);

MOONLIB_API int (moonL_fileresult) (moon_State *L, int stat, const char *fname);
MOONLIB_API int (moonL_execresult) (moon_State *L, int stat);


/* predefined references */
#define MOON_NOREF       (-2)
#define MOON_REFNIL      (-1)

MOONLIB_API int (moonL_ref) (moon_State *L, int t);
MOONLIB_API void (moonL_unref) (moon_State *L, int t, int ref);

MOONLIB_API int (moonL_loadfilex) (moon_State *L, const char *filename,
                                               const char *mode);

#define moonL_loadfile(L,f)	moonL_loadfilex(L,f,NULL)

MOONLIB_API int (moonL_loadbufferx) (moon_State *L, const char *buff, size_t sz,
                                   const char *name, const char *mode);
MOONLIB_API int (moonL_loadstring) (moon_State *L, const char *s);

MOONLIB_API moon_State *(moonL_newstate) (void);

MOONLIB_API unsigned moonL_makeseed (moon_State *L);

MOONLIB_API moon_Integer (moonL_len) (moon_State *L, int idx);

MOONLIB_API void (moonL_addgsub) (moonL_Buffer *b, const char *s,
                                     const char *p, const char *r);
MOONLIB_API const char *(moonL_gsub) (moon_State *L, const char *s,
                                    const char *p, const char *r);

MOONLIB_API void (moonL_setfuncs) (moon_State *L, const moonL_Reg *l, int nup);

MOONLIB_API int (moonL_getsubtable) (moon_State *L, int idx, const char *fname);

MOONLIB_API void (moonL_traceback) (moon_State *L, moon_State *L1,
                                  const char *msg, int level);

MOONLIB_API void (moonL_requiref) (moon_State *L, const char *modname,
                                 moon_CFunction openf, int glb);

/*
** ===============================================================
** some useful macros
** ===============================================================
*/


#define moonL_newlibtable(L,l)	\
  moon_createtable(L, 0, sizeof(l)/sizeof((l)[0]) - 1)

#define moonL_newlib(L,l)  \
  (moonL_checkversion(L), moonL_newlibtable(L,l), moonL_setfuncs(L,l,0))

#define moonL_argcheck(L, cond,arg,extramsg)	\
	((void)(mooni_likely(cond) || moonL_argerror(L, (arg), (extramsg))))

#define moonL_argexpected(L,cond,arg,tname)	\
	((void)(mooni_likely(cond) || moonL_typeerror(L, (arg), (tname))))

#define moonL_checkstring(L,n)	(moonL_checklstring(L, (n), NULL))
#define moonL_optstring(L,n,d)	(moonL_optlstring(L, (n), (d), NULL))

#define moonL_typename(L,i)	moon_typename(L, moon_type(L,(i)))

#define moonL_dofile(L, fn) \
	(moonL_loadfile(L, fn) || moon_pcall(L, 0, MOON_MULTRET, 0))

#define moonL_dostring(L, s) \
	(moonL_loadstring(L, s) || moon_pcall(L, 0, MOON_MULTRET, 0))

#define moonL_getmetatable(L,n)	(moon_getfield(L, MOON_REGISTRYINDEX, (n)))

#define moonL_opt(L,f,n,d)	(moon_isnoneornil(L,(n)) ? (d) : f(L,(n)))

#define moonL_loadbuffer(L,s,sz,n)	moonL_loadbufferx(L,s,sz,n,NULL)


/*
** Perform arithmetic operations on moon_Integer values with wrap-around
** semantics, as the Lua core does.
*/
#define moonL_intop(op,v1,v2)  \
	((moon_Integer)((moon_Unsigned)(v1) op (moon_Unsigned)(v2)))


/* push the value used to represent failure/error */
#if defined(MOON_FAILISFALSE)
#define moonL_pushfail(L)	moon_pushboolean(L, 0)
#else
#define moonL_pushfail(L)	moon_pushnil(L)
#endif



/*
** {======================================================
** Generic Buffer manipulation
** =======================================================
*/

struct moonL_Buffer {
  char *b;  /* buffer address */
  size_t size;  /* buffer size */
  size_t n;  /* number of characters in buffer */
  moon_State *L;
  union {
    MOONI_MAXALIGN;  /* ensure maximum alignment for buffer */
    char b[MOONL_BUFFERSIZE];  /* initial buffer */
  } init;
};


#define moonL_bufflen(bf)	((bf)->n)
#define moonL_buffaddr(bf)	((bf)->b)


#define moonL_addchar(B,c) \
  ((void)((B)->n < (B)->size || moonL_prepbuffsize((B), 1)), \
   ((B)->b[(B)->n++] = (c)))

#define moonL_addsize(B,s)	((B)->n += (s))

#define moonL_buffsub(B,s)	((B)->n -= (s))

MOONLIB_API void (moonL_buffinit) (moon_State *L, moonL_Buffer *B);
MOONLIB_API char *(moonL_prepbuffsize) (moonL_Buffer *B, size_t sz);
MOONLIB_API void (moonL_addlstring) (moonL_Buffer *B, const char *s, size_t l);
MOONLIB_API void (moonL_addstring) (moonL_Buffer *B, const char *s);
MOONLIB_API void (moonL_addvalue) (moonL_Buffer *B);
MOONLIB_API void (moonL_pushresult) (moonL_Buffer *B);
MOONLIB_API void (moonL_pushresultsize) (moonL_Buffer *B, size_t sz);
MOONLIB_API char *(moonL_buffinitsize) (moon_State *L, moonL_Buffer *B, size_t sz);

#define moonL_prepbuffer(B)	moonL_prepbuffsize(B, MOONL_BUFFERSIZE)

/* }====================================================== */



/*
** {======================================================
** File handles for IO library
** =======================================================
*/

/*
** A file handle is a userdata with metatable 'MOON_FILEHANDLE' and
** initial structure 'moonL_Stream' (it may contain other fields
** after that initial structure).
*/

#define MOON_FILEHANDLE          "FILE*"


typedef struct moonL_Stream {
  FILE *f;  /* stream (NULL for incompletely created streams) */
  moon_CFunction closef;  /* to close stream (NULL for closed streams) */
} moonL_Stream;

/* }====================================================== */


/*
** {============================================================
** Compatibility with deprecated conversions
** =============================================================
*/
#if defined(MOON_COMPAT_APIINTCASTS)

#define moonL_checkunsigned(L,a)	((moon_Unsigned)moonL_checkinteger(L,a))
#define moonL_optunsigned(L,a,d)	\
	((moon_Unsigned)moonL_optinteger(L,a,(moon_Integer)(d)))

#define moonL_checkint(L,n)	((int)moonL_checkinteger(L, (n)))
#define moonL_optint(L,n,d)	((int)moonL_optinteger(L, (n), (d)))

#define moonL_checklong(L,n)	((long)moonL_checkinteger(L, (n)))
#define moonL_optlong(L,n,d)	((long)moonL_optinteger(L, (n), (d)))

#endif
/* }============================================================ */


#ifdef __cplusplus
}
#endif

/* Phase 115.1: C++ std::span overloads (outside extern "C") */
#ifdef __cplusplus
// std::span-based buffer functions (internal C++ API)
MOONLIB_API void moonL_addlstring (moonL_Buffer *B, std::span<const char> s);

// C-style wrapper for compatibility (inline, calls span version)
inline void moonL_addlstring (moonL_Buffer *B, const char *s, size_t l) {
	moonL_addlstring(B, std::span(s, l));
}
#endif

#endif


