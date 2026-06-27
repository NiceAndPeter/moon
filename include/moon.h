/*
** $Id: lua.h $
** Lua - A Scripting Language
** Lua.org, PUC-Rio, Brazil (www.lua.org)
** See Copyright Notice at the end of this file
*/


#ifndef moon_h
#define moon_h

#include <stdarg.h>
#include <stddef.h>


#ifdef __cplusplus
extern "C" {
#endif


#define MOON_COPYRIGHT	MOON_RELEASE "  Copyright (C) 1994-2025 Lua.org, PUC-Rio"
#define MOON_AUTHORS	"R. Ierusalimschy, L. H. de Figueiredo, W. Celes"


#define MOON_VERSION_MAJOR_N	5
#define MOON_VERSION_MINOR_N	5
#define MOON_VERSION_RELEASE_N	0

#define MOON_VERSION_NUM  (MOON_VERSION_MAJOR_N * 100 + MOON_VERSION_MINOR_N)
#define MOON_VERSION_RELEASE_NUM  (MOON_VERSION_NUM * 100 + MOON_VERSION_RELEASE_N)


#include "moonconf.h"


/* mark for precompiled code ('<esc>Lua') */
#define MOON_SIGNATURE	"\x1bLua"

/* option for multiple returns in 'moon_pcall' and 'moon_call' */
#define MOON_MULTRET	(-1)


/*
** Pseudo-indices
** (The stack size is limited to INT_MAX/2; we keep some free empty
** space after that to help overflow detection.)
*/
#define MOON_REGISTRYINDEX	(-(INT_MAX/2 + 1000))
#define moon_upvalueindex(i)	(MOON_REGISTRYINDEX - (i))


/* thread status */
#define MOON_OK		0
#define MOON_YIELD	1
#define MOON_ERRRUN	2
#define MOON_ERRSYNTAX	3
#define MOON_ERRMEM	4
#define MOON_ERRERR	5


typedef struct moon_State moon_State;


/*
** basic types
*/

#define MOON_TNONE		(-1)
#define MOON_TNIL		0
#define MOON_TBOOLEAN    		1
#define MOON_TLIGHTUSERDATA	2
#define MOON_TNUMBER		3
#define MOON_TSTRING		4
#define MOON_TTABLE		5
#define MOON_TFUNCTION		6
#define MOON_TUSERDATA		7
#define MOON_TTHREAD		8
#define MOON_NUMTYPES		9

#ifdef __cplusplus
  // C++ version: Use enum class for type safety
  enum class MoonType : int 
  {
    None           = MOON_TNONE,
    Nil            = MOON_TNIL,
    Boolean        = MOON_TBOOLEAN,
    LightUserdata  = MOON_TLIGHTUSERDATA,
    Number         = MOON_TNUMBER,
    String         = MOON_TSTRING,
    Table          = MOON_TTABLE,
    Function       = MOON_TFUNCTION,
    Userdata       = MOON_TUSERDATA,
    Thread         = MOON_TTHREAD,
    NumTypes       = MOON_NUMTYPES
  };
#endif



/* minimum Lua stack available to a C function */
#define MOON_MINSTACK	20


/* predefined values in the registry */
/* index 1 is reserved for the reference mechanism */
#define MOON_RIDX_GLOBALS	2
#define MOON_RIDX_MAINTHREAD	3
#define MOON_RIDX_LAST		3


/* type of numbers in Lua */
typedef MOON_NUMBER moon_Number;


/* type for integer functions */
typedef MOON_INTEGER moon_Integer;

/* unsigned integer type */
typedef MOON_UNSIGNED moon_Unsigned;

/* type for continuation-function contexts */
typedef MOON_KCONTEXT moon_KContext;


/*
** Type for C functions registered with Lua
*/
typedef int (*moon_CFunction) (moon_State *L);

/*
** Type for continuation functions
*/
typedef int (*moon_KFunction) (moon_State *L, int status, moon_KContext ctx);


/*
** Type for functions that read/write blocks when loading/dumping Lua chunks
*/
typedef const char * (*moon_Reader) (moon_State *L, void *ud, size_t *sz);

typedef int (*moon_Writer) (moon_State *L, const void *p, size_t sz, void *ud);


/*
** Type for memory-allocation functions
*/
typedef void * (*moon_Alloc) (void *ud, void *ptr, size_t osize, size_t nsize);


/*
** Type for warning functions
*/
typedef void (*moon_WarnFunction) (void *ud, const char *msg, int tocont);


/*
** Type used by the debug API to collect debug information
*/
typedef struct moon_Debug moon_Debug;


/*
** Functions to be called by the debugger in specific events
*/
typedef void (*moon_Hook) (moon_State *L, moon_Debug *ar);


/*
** generic extra include file
*/
#if defined(MOON_USER_H)
#include MOON_USER_H
#endif


/*
** RCS ident string
*/
extern const char moon_ident[];


/*
** state manipulation
*/
MOON_API moon_State *(moon_newstate) (moon_Alloc f, void *ud, unsigned seed);
MOON_API void       (moon_close) (moon_State *L);
MOON_API moon_State *(moon_newthread) (moon_State *L);
MOON_API int        (moon_closethread) (moon_State *L, moon_State *from);

MOON_API moon_CFunction (moon_atpanic) (moon_State *L, moon_CFunction panicf);


MOON_API moon_Number (moon_version) (moon_State *L);


/*
** basic stack manipulation
*/
MOON_API int   (moon_absindex) (moon_State *L, int idx);
MOON_API int   (moon_gettop) (moon_State *L);
MOON_API void  (moon_settop) (moon_State *L, int idx);
MOON_API void  (moon_pushvalue) (moon_State *L, int idx);
MOON_API void  (moon_rotate) (moon_State *L, int idx, int n);
MOON_API void  (moon_copy) (moon_State *L, int fromidx, int toidx);
MOON_API int   (moon_checkstack) (moon_State *L, int n);

MOON_API void  (moon_xmove) (moon_State *from, moon_State *to, int n);


/*
** access functions (stack -> C)
*/

MOON_API int             (moon_isnumber) (moon_State *L, int idx);
MOON_API int             (moon_isstring) (moon_State *L, int idx);
MOON_API int             (moon_iscfunction) (moon_State *L, int idx);
MOON_API int             (moon_isinteger) (moon_State *L, int idx);
MOON_API int             (moon_isuserdata) (moon_State *L, int idx);
MOON_API int             (moon_type) (moon_State *L, int idx);
MOON_API const char     *(moon_typename) (moon_State *L, int tp);

MOON_API moon_Number      (moon_tonumberx) (moon_State *L, int idx, int *isnum);
MOON_API moon_Integer     (moon_tointegerx) (moon_State *L, int idx, int *isnum);
MOON_API int             (moon_toboolean) (moon_State *L, int idx);
MOON_API const char     *(moon_tolstring) (moon_State *L, int idx, size_t *len);
MOON_API moon_Unsigned    (moon_rawlen) (moon_State *L, int idx);
MOON_API moon_CFunction   (moon_tocfunction) (moon_State *L, int idx);
MOON_API void	       *(moon_touserdata) (moon_State *L, int idx);
MOON_API moon_State      *(moon_tothread) (moon_State *L, int idx);
MOON_API const void     *(moon_topointer) (moon_State *L, int idx);


/*
** Comparison and arithmetic functions
*/

#define MOON_OPADD	0	/* ORDER TM, ORDER OP */
#define MOON_OPSUB	1
#define MOON_OPMUL	2
#define MOON_OPMOD	3
#define MOON_OPPOW	4
#define MOON_OPDIV	5
#define MOON_OPIDIV	6
#define MOON_OPBAND	7
#define MOON_OPBOR	8
#define MOON_OPBXOR	9
#define MOON_OPSHL	10
#define MOON_OPSHR	11
#define MOON_OPUNM	12
#define MOON_OPBNOT	13

MOON_API void  (moon_arith) (moon_State *L, int op);

#define MOON_OPEQ	0
#define MOON_OPLT	1
#define MOON_OPLE	2

MOON_API int   (moon_rawequal) (moon_State *L, int idx1, int idx2);
MOON_API int   (moon_compare) (moon_State *L, int idx1, int idx2, int op);


/*
** push functions (C -> stack)
*/
MOON_API void        (moon_pushnil) (moon_State *L);
MOON_API void        (moon_pushnumber) (moon_State *L, moon_Number n);
MOON_API void        (moon_pushinteger) (moon_State *L, moon_Integer n);
MOON_API const char *(moon_pushlstring) (moon_State *L, const char *s, size_t len);
MOON_API const char *(moon_pushexternalstring) (moon_State *L,
		const char *s, size_t len, moon_Alloc falloc, void *ud);
MOON_API const char *(moon_pushstring) (moon_State *L, const char *s);
MOON_API const char *(moon_pushvfstring) (moon_State *L, const char *fmt,
                                                      va_list argp);
MOON_API const char *(moon_pushfstring) (moon_State *L, const char *fmt, ...);
MOON_API void  (moon_pushcclosure) (moon_State *L, moon_CFunction fn, int n);
MOON_API void  (moon_pushboolean) (moon_State *L, int b);
MOON_API void  (moon_pushlightuserdata) (moon_State *L, void *p);
MOON_API int   (moon_pushthread) (moon_State *L);


/*
** get functions (Lua -> stack)
*/
MOON_API int (moon_getglobal) (moon_State *L, const char *name);
MOON_API int (moon_gettable) (moon_State *L, int idx);
MOON_API int (moon_getfield) (moon_State *L, int idx, const char *k);
MOON_API int (moon_geti) (moon_State *L, int idx, moon_Integer n);
MOON_API int (moon_rawget) (moon_State *L, int idx);
MOON_API int (moon_rawgeti) (moon_State *L, int idx, moon_Integer n);
MOON_API int (moon_rawgetp) (moon_State *L, int idx, const void *p);

MOON_API void  (moon_createtable) (moon_State *L, int narr, int nrec);
MOON_API void *(moon_newuserdatauv) (moon_State *L, size_t sz, int nuvalue);
MOON_API int   (moon_getmetatable) (moon_State *L, int objindex);
MOON_API int  (moon_getiuservalue) (moon_State *L, int idx, int n);


/*
** set functions (stack -> Lua)
*/
MOON_API void  (moon_setglobal) (moon_State *L, const char *name);
MOON_API void  (moon_settable) (moon_State *L, int idx);
MOON_API void  (moon_setfield) (moon_State *L, int idx, const char *k);
MOON_API void  (moon_seti) (moon_State *L, int idx, moon_Integer n);
MOON_API void  (moon_rawset) (moon_State *L, int idx);
MOON_API void  (moon_rawseti) (moon_State *L, int idx, moon_Integer n);
MOON_API void  (moon_rawsetp) (moon_State *L, int idx, const void *p);
MOON_API int   (moon_setmetatable) (moon_State *L, int objindex);
MOON_API int   (moon_setiuservalue) (moon_State *L, int idx, int n);


/*
** 'load' and 'call' functions (load and run Lua code)
*/
MOON_API void  (moon_callk) (moon_State *L, int nargs, int nresults,
                           moon_KContext ctx, moon_KFunction k);
#define moon_call(L,n,r)		moon_callk(L, (n), (r), 0, NULL)

MOON_API int   (moon_pcallk) (moon_State *L, int nargs, int nresults, int errfunc,
                            moon_KContext ctx, moon_KFunction k);
#define moon_pcall(L,n,r,f)	moon_pcallk(L, (n), (r), (f), 0, NULL)

MOON_API int   (moon_load) (moon_State *L, moon_Reader reader, void *dt,
                          const char *chunkname, const char *mode);

MOON_API int (moon_dump) (moon_State *L, moon_Writer writer, void *data, int strip);


/*
** coroutine functions
*/
MOON_API int  (moon_yieldk)     (moon_State *L, int nresults, moon_KContext ctx,
                               moon_KFunction k);
MOON_API int  (moon_resume)     (moon_State *L, moon_State *from, int narg,
                               int *nres);
MOON_API int  (moon_status)     (moon_State *L);
MOON_API int (moon_isyieldable) (moon_State *L);

#define moon_yield(L,n)		moon_yieldk(L, (n), 0, NULL)


/*
** Warning-related functions
*/
MOON_API void (moon_setwarnf) (moon_State *L, moon_WarnFunction f, void *ud);
MOON_API void (moon_warning)  (moon_State *L, const char *msg, int tocont);


/*
** garbage-collection options
*/

#define MOON_GCSTOP		0
#define MOON_GCRESTART		1
#define MOON_GCCOLLECT		2
#define MOON_GCCOUNT		3
#define MOON_GCCOUNTB		4
#define MOON_GCSTEP		5
#define MOON_GCISRUNNING		6
#define MOON_GCGEN		7
#define MOON_GCINC		8
#define MOON_GCPARAM		9


/*
** garbage-collection parameters
*/
/* parameters for generational mode */
#define MOON_GCPMINORMUL		0  /* control minor collections */
#define MOON_GCPMAJORMINOR	1  /* control shift major->minor */
#define MOON_GCPMINORMAJOR	2  /* control shift minor->major */

/* parameters for incremental mode */
#define MOON_GCPPAUSE		3  /* size of pause between successive GCs */
#define MOON_GCPSTEPMUL		4  /* GC "speed" */
#define MOON_GCPSTEPSIZE		5  /* GC granularity */

/* number of parameters */
#define MOON_GCPN		6


MOON_API int (moon_gc) (moon_State *L, int what, ...);


/*
** miscellaneous functions
*/

MOON_API int   (moon_error) (moon_State *L);

MOON_API int   (moon_next) (moon_State *L, int idx);

MOON_API void  (moon_concat) (moon_State *L, int n);
MOON_API void  (moon_len)    (moon_State *L, int idx);

#define MOON_N2SBUFFSZ	64
MOON_API unsigned  (moon_numbertocstring) (moon_State *L, int idx, char *buff);
MOON_API size_t  (moon_stringtonumber) (moon_State *L, const char *s);

MOON_API moon_Alloc (moon_getallocf) (moon_State *L, void **ud);
MOON_API void      (moon_setallocf) (moon_State *L, moon_Alloc f, void *ud);

MOON_API void (moon_toclose) (moon_State *L, int idx);
MOON_API void (moon_closeslot) (moon_State *L, int idx);


/*
** {==============================================================
** some useful macros
** ===============================================================
*/

#define moon_getextraspace(L)	((void *)((char *)(L) - MOON_EXTRASPACE))

#define moon_tonumber(L,i)	moon_tonumberx(L,(i),NULL)
#define moon_tointeger(L,i)	moon_tointegerx(L,(i),NULL)

#define moon_pop(L,n)		moon_settop(L, -(n)-1)

#define moon_newtable(L)		moon_createtable(L, 0, 0)

#define moon_register(L,n,f) (moon_pushcfunction(L, (f)), moon_setglobal(L, (n)))

#define moon_pushcfunction(L,f)	moon_pushcclosure(L, (f), 0)

#define moon_isfunction(L,n)	(moon_type(L, (n)) == MOON_TFUNCTION)
#define moon_istable(L,n)	(moon_type(L, (n)) == MOON_TTABLE)
#define moon_islightuserdata(L,n)	(moon_type(L, (n)) == MOON_TLIGHTUSERDATA)
#define moon_isnil(L,n)		(moon_type(L, (n)) == MOON_TNIL)
#define moon_isboolean(L,n)	(moon_type(L, (n)) == MOON_TBOOLEAN)
#define moon_isthread(L,n)	(moon_type(L, (n)) == MOON_TTHREAD)
#define moon_isnone(L,n)		(moon_type(L, (n)) == MOON_TNONE)
#define moon_isnoneornil(L, n)	(moon_type(L, (n)) <= 0)

#define moon_pushliteral(L, s)	moon_pushstring(L, "" s)

#define moon_pushglobaltable(L)  \
	((void)moon_rawgeti(L, MOON_REGISTRYINDEX, MOON_RIDX_GLOBALS))

#define moon_tostring(L,i)	moon_tolstring(L, (i), NULL)


#define moon_insert(L,idx)	moon_rotate(L, (idx), 1)

#define moon_remove(L,idx)	(moon_rotate(L, (idx), -1), moon_pop(L, 1))

#define moon_replace(L,idx)	(moon_copy(L, -1, (idx)), moon_pop(L, 1))

/* }============================================================== */


/*
** {==============================================================
** compatibility macros
** ===============================================================
*/

#define moon_newuserdata(L,s)	moon_newuserdatauv(L,s,1)
#define moon_getuservalue(L,idx)	moon_getiuservalue(L,idx,1)
#define moon_setuservalue(L,idx)	moon_setiuservalue(L,idx,1)

#define moon_resetthread(L)	moon_closethread(L,NULL)

/* }============================================================== */

/*
** {======================================================================
** Debug API
** =======================================================================
*/


/*
** Event codes
*/
#define MOON_HOOKCALL	0
#define MOON_HOOKRET	1
#define MOON_HOOKLINE	2
#define MOON_HOOKCOUNT	3
#define MOON_HOOKTAILCALL 4


/*
** Event masks
*/
#define MOON_MASKCALL	(1 << MOON_HOOKCALL)
#define MOON_MASKRET	(1 << MOON_HOOKRET)
#define MOON_MASKLINE	(1 << MOON_HOOKLINE)
#define MOON_MASKCOUNT	(1 << MOON_HOOKCOUNT)


MOON_API int (moon_getstack) (moon_State *L, int level, moon_Debug *ar);
MOON_API int (moon_getinfo) (moon_State *L, const char *what, moon_Debug *ar);
MOON_API const char *(moon_getlocal) (moon_State *L, const moon_Debug *ar, int n);
MOON_API const char *(moon_setlocal) (moon_State *L, const moon_Debug *ar, int n);
MOON_API const char *(moon_getupvalue) (moon_State *L, int funcindex, int n);
MOON_API const char *(moon_setupvalue) (moon_State *L, int funcindex, int n);

MOON_API void *(moon_upvalueid) (moon_State *L, int fidx, int n);
MOON_API void  (moon_upvaluejoin) (moon_State *L, int fidx1, int n1,
                                               int fidx2, int n2);

MOON_API void (moon_sethook) (moon_State *L, moon_Hook func, int mask, int count);
MOON_API moon_Hook (moon_gethook) (moon_State *L);
MOON_API int (moon_gethookmask) (moon_State *L);
MOON_API int (moon_gethookcount) (moon_State *L);


struct moon_Debug {
  int event;
  const char *name;	/* (n) */
  const char *namewhat;	/* (n) 'global', 'local', 'field', 'method' */
  const char *what;	/* (S) 'Lua', 'C', 'main', 'tail' */
  const char *source;	/* (S) */
  size_t srclen;	/* (S) */
  int currentline;	/* (l) */
  int linedefined;	/* (S) */
  int lastlinedefined;	/* (S) */
  unsigned char nups;	/* (u) number of upvalues */
  unsigned char nparams;/* (u) number of parameters */
  char isvararg;        /* (u) */
  unsigned char extraargs;  /* (t) number of extra arguments */
  char istailcall;	/* (t) */
  int ftransfer;   /* (r) index of first value transferred */
  int ntransfer;   /* (r) number of transferred values */
  char short_src[MOON_IDSIZE]; /* (S) */
  /* private part */
  struct CallInfo *i_ci;  /* active function */
};

/* }====================================================================== */


#define MOONI_TOSTRAUX(x)	#x
#define MOONI_TOSTR(x)		MOONI_TOSTRAUX(x)

#define MOON_VERSION_MAJOR	MOONI_TOSTR(MOON_VERSION_MAJOR_N)
#define MOON_VERSION_MINOR	MOONI_TOSTR(MOON_VERSION_MINOR_N)
#define MOON_VERSION_RELEASE	MOONI_TOSTR(MOON_VERSION_RELEASE_N)

#define MOON_VERSION	"Lua " MOON_VERSION_MAJOR "." MOON_VERSION_MINOR
#define MOON_RELEASE	MOON_VERSION "." MOON_VERSION_RELEASE


/******************************************************************************
* Copyright (C) 1994-2025 Lua.org, PUC-Rio.
*
* Permission is hereby granted, free of charge, to any person obtaining
* a copy of this software and associated documentation files (the
* "Software"), to deal in the Software without restriction, including
* without limitation the rights to use, copy, modify, merge, publish,
* distribute, sublicense, and/or sell copies of the Software, and to
* permit persons to whom the Software is furnished to do so, subject to
* the following conditions:
*
* The above copyright notice and this permission notice shall be
* included in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
******************************************************************************/


#ifdef __cplusplus
}
#endif

#endif
