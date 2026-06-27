/*
** Dynamic library loader for Lua
** See Copyright Notice in lua.h
**
** This module contains an implementation of loadlib for Unix systems
** that have dlfcn, an implementation for Windows, and a stub for other
** systems.
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
** MOON_CSUBSEP is the character that replaces dots in submodule names
** when searching for a C loader.
** MOON_LSUBSEP is the character that replaces dots in submodule names
** when searching for a Lua loader.
*/
#if !defined(MOON_CSUBSEP)
#define MOON_CSUBSEP		MOON_DIRSEP
#endif

#if !defined(MOON_LSUBSEP)
#define MOON_LSUBSEP		MOON_DIRSEP
#endif


// prefix for open functions in C libraries
#define MOON_POF		"moonopen_"

// separator for open functions in C libraries
#define MOON_OFSEP	"_"


/*
** key for table in the registry that keeps handles
** for all loaded C libraries
*/
static const char *const CLIBS = "_CLIBS";

#define LIB_FAIL	"open"


#define setprogdir(L)           ((void)0)


// cast void* to a Lua function
#define cast_Lfunc(p)	cast(moon_CFunction, cast_func(p))


/*
** system-dependent functions
*/

/*
** unload library 'lib'
*/
static void lsys_unloadlib (void *lib);

/*
** load C library in file 'path'. If 'seeglb', load with all names in
** the library global.
** Returns the library; in case of error, returns nullptr plus an
** error string in the stack.
*/
static void *lsys_load (moon_State *L, const char *path, int seeglb);

/*
** Try to find a function named 'sym' in library 'lib'.
** Returns the function; in case of error, returns nullptr plus an
** error string in the stack.
*/
static moon_CFunction lsys_sym (moon_State *L, void *lib, const char *sym);




#if defined(MOON_USE_DLOPEN)  // {
/*
** {========================================================================
** This is an implementation of loadlib based on the dlfcn interface,
** which is available in all POSIX systems.
** =========================================================================
*/

#include <dlfcn.h>


static void lsys_unloadlib (void *lib) {
  dlclose(lib);
}


static void *lsys_load (moon_State *L, const char *path, int seeglb) {
  void *lib = dlopen(path, RTLD_NOW | (seeglb ? RTLD_GLOBAL : RTLD_LOCAL));
  if (l_unlikely(lib == nullptr))
    moon_pushstring(L, dlerror());
  return lib;
}


static moon_CFunction lsys_sym (moon_State *L, void *lib, const char *sym) {
  moon_CFunction f = cast_Lfunc(dlsym(lib, sym));
  if (l_unlikely(f == nullptr))
    moon_pushstring(L, dlerror());
  return f;
}

// }======================================================



#elif defined(MOON_DL_DLL)  // }{
/*
** {======================================================================
** This is an implementation of loadlib for Windows using native functions.
** =======================================================================
*/

#include <windows.h>


/*
** optional flags for LoadLibraryEx
*/
#if !defined(MOON_LLE_FLAGS)
#define MOON_LLE_FLAGS	0
#endif


#undef setprogdir


/*
** Replace in the path (on the top of the stack) any occurrence
** of MOON_EXEC_DIR with the executable's path.
*/
static void setprogdir (moon_State *L) {
  char buff[MAX_PATH + 1];
  char *lb;
  DWORD nsize = sizeof(buff)/sizeof(char);
  DWORD n = GetModuleFileNameA(nullptr, buff, nsize);  // get exec. name
  if (n == 0 || n == nsize || (lb = strrchr(buff, '\\')) == nullptr)
    moonL_error(L, "unable to get ModuleFileName");
  else {
    *lb = '\0';  /* cut name on the last '\\' to get the path */
    moonL_gsub(L, moon_tostring(L, -1), MOON_EXEC_DIR, buff);
    moon_remove(L, -2);  // remove original string
  }
}




static void pusherror (moon_State *L) {
  int error = GetLastError();
  char buffer[128];
  if (FormatMessageA(FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM,
      nullptr, error, 0, buffer, sizeof(buffer)/sizeof(char), nullptr))
    moon_pushstring(L, buffer);
  else
    moon_pushfstring(L, "system error %d\n", error);
}

static void lsys_unloadlib (void *lib) {
  FreeLibrary((HMODULE)lib);
}


static void *lsys_load (moon_State *L, const char *path, int seeglb) {
  HMODULE lib = LoadLibraryExA(path, nullptr, MOON_LLE_FLAGS);
  (void)(seeglb);  // not used: symbols are 'global' by default
  if (lib == nullptr) pusherror(L);
  return lib;
}


static moon_CFunction lsys_sym (moon_State *L, void *lib, const char *sym) {
  moon_CFunction f = cast_Lfunc(GetProcAddress((HMODULE)lib, sym));
  if (f == nullptr) pusherror(L);
  return f;
}

// }======================================================


#else  // }{
/*
** {======================================================
** Fallback for other systems
** =======================================================
*/

#undef LIB_FAIL
#define LIB_FAIL	"absent"


#define DLMSG	"dynamic libraries not enabled; check your Lua installation"


static void lsys_unloadlib (void *lib) {
  (void)(lib);  // not used
}


static void *lsys_load (moon_State *L, const char *path, int seeglb) {
  (void)(path); (void)(seeglb);  // not used
  moon_pushliteral(L, DLMSG);
  return nullptr;
}


static moon_CFunction lsys_sym (moon_State *L, void *lib, const char *sym) {
  (void)(lib); (void)(sym);  // not used
  moon_pushliteral(L, DLMSG);
  return nullptr;
}

// }======================================================
#endif  // }


/*
** {==================================================================
** Set Paths
** ===================================================================
*/

/*
** MOON_PATH_VAR and MOON_CPATH_VAR are the names of the environment
** variables that Lua check to set its paths.
*/
#if !defined(MOON_PATH_VAR)
#define MOON_PATH_VAR    "MOON_PATH"
#endif

#if !defined(MOON_CPATH_VAR)
#define MOON_CPATH_VAR   "MOON_CPATH"
#endif



/*
** return registry.MOON_NOENV as a boolean
*/
static int noenv (moon_State *L) {
  moon_getfield(L, MOON_REGISTRYINDEX, "MOON_NOENV");
  const int b = moon_toboolean(L, -1);
  moon_pop(L, 1);  // remove value
  return b;
}


/*
** Set a path. (If using the default path, assume it is a string
** literal in C and create it as an external string.)
*/
static void setpath (moon_State *L, const char *fieldname,
                                   const char *envname,
                                   const char *dft) {
  const char *dftmark;
  const char *nver = moon_pushfstring(L, "%s%s", envname, MOON_VERSUFFIX);
  const char *path = getenv(nver);  // try versioned name
  if (path == nullptr)  // no versioned environment variable?
    path = getenv(envname);  // try unversioned name
  if (path == nullptr || noenv(L))  // no environment variable?
    moon_pushexternalstring(L, dft, strlen(dft), nullptr, nullptr);  // use default
  else if ((dftmark = strstr(path, MOON_PATH_SEP MOON_PATH_SEP)) == nullptr)
    moon_pushstring(L, path);  // nothing to change
  else {  // path contains a ";;": insert default path in its place
    size_t len = strlen(path);
    moonL_Buffer b;
    moonL_buffinit(L, &b);
    if (path < dftmark) {  // is there a prefix before ';;'?
      moonL_addlstring(&b, path, ct_diff2sz(dftmark - path));  // add it
      moonL_addchar(&b, *MOON_PATH_SEP);
    }
    moonL_addstring(&b, dft);  // add default
    if (dftmark < path + len - 2) {  // is there a suffix after ';;'?
      moonL_addchar(&b, *MOON_PATH_SEP);
      moonL_addlstring(&b, dftmark + 2, ct_diff2sz((path + len - 2) - dftmark));
    }
    moonL_pushresult(&b);
  }
  setprogdir(L);
  moon_setfield(L, -3, fieldname);  // package[fieldname] = path value
  moon_pop(L, 1);  // pop versioned variable name ('nver')
}

// }==================================================================


/*
** External strings created by DLLs may need the DLL code to be
** deallocated. This implies that a DLL can only be unloaded after all
** its strings were deallocated. To ensure that, we create a 'library
** string' to represent each DLL, and when this string is deallocated
** it closes its corresponding DLL.
** (The string itself is irrelevant; its userdata is the DLL pointer.)
*/


/*
** return registry.CLIBS[path]
*/
static void *checkclib (moon_State *L, const char *path) {
  void *plib;
  moon_getfield(L, MOON_REGISTRYINDEX, CLIBS);
  moon_getfield(L, -1, path);
  plib = moon_touserdata(L, -1);  // plib = CLIBS[path]
  moon_pop(L, 2);  // pop CLIBS table and 'plib'
  return plib;
}


/*
** Deallocate function for library strings.
** Unload the DLL associated with the string being deallocated.
*/
static void *freelib (void *ud, void *ptr, size_t osize, size_t nsize) {
  // string itself is irrelevant and static
  (void)ptr; (void)osize; (void)nsize;
  lsys_unloadlib(ud);  // unload library represented by the string
  return nullptr;
}


/*
** Create a library string that, when deallocated, will unload 'plib'
*/
static void createlibstr (moon_State *L, void *plib) {
  // common content for all library strings
  static const char dummy[] = "01234567890";
  moon_pushexternalstring(L, dummy, sizeof(dummy) - 1, freelib, plib);
}


/*
** registry.CLIBS[path] = plib          -- for queries.
** Also create a reference to strlib, so that the library string will
** only be collected when registry.CLIBS is collected.
*/
static void addtoclib (moon_State *L, const char *path, void *plib) {
  moon_getfield(L, MOON_REGISTRYINDEX, CLIBS);
  moon_pushlightuserdata(L, plib);
  moon_setfield(L, -2, path);  // CLIBS[path] = plib
  createlibstr(L, plib);
  moonL_ref(L, -2);  // keep library string in CLIBS
  moon_pop(L, 1);  // pop CLIBS table
}


// error codes for 'lookforfunc'
#define ERRLIB		1
#define ERRFUNC		2

/*
** Look for a C function named 'sym' in a dynamically loaded library
** 'path'.
** First, check whether the library is already loaded; if not, try
** to load it.
** Then, if 'sym' is '*', return true (as library has been loaded).
** Otherwise, look for symbol 'sym' in the library and push a
** C function with that symbol.
** Return 0 with 'true' or a function in the stack; in case of
** errors, return an error code with an error message in the stack.
*/
static int lookforfunc (moon_State *L, const char *path, const char *sym) {
  void *reg = checkclib(L, path);  // check loaded C libraries
  if (reg == nullptr) {  // must load library?
    reg = lsys_load(L, path, *sym == '*');  // global symbols if 'sym'=='*'
    if (reg == nullptr) return ERRLIB;  // unable to load library
    addtoclib(L, path, reg);
  }
  if (*sym == '*') {  // loading only library (no function)?
    moon_pushboolean(L, 1);  // return 'true'
    return 0;  // no errors
  }
  else {
    moon_CFunction f = lsys_sym(L, reg, sym);
    if (f == nullptr)
      return ERRFUNC;  // unable to find function
    moon_pushcfunction(L, f);  // else create new function
    return 0;  // no errors
  }
}


static int ll_loadlib (moon_State *L) {
  const char *path = moonL_checkstring(L, 1);
  const char *init = moonL_checkstring(L, 2);
  int stat = lookforfunc(L, path, init);
  if (l_likely(stat == 0))  // no errors?
    return 1;  // return the loaded function
  else {  // error; error message is on stack top
    moonL_pushfail(L);
    moon_insert(L, -2);
    moon_pushstring(L, (stat == ERRLIB) ?  LIB_FAIL : "init");
    return 3;  // return fail, error message, and where
  }
}



/*
** {======================================================
** 'require' function
** =======================================================
*/


static int readable (const char *filename) {
  FILE *f = fopen(filename, "r");  // try to open file
  if (f == nullptr) return 0;  // open failed
  fclose(f);
  return 1;
}


/*
** Get the next name in '*path' = 'name1;name2;name3;...', changing
** the ending ';' to '\0' to create a zero-terminated string. Return
** nullptr when list ends.
*/
static const char *getnextfilename (char **path, char *end) {
  char *sep;
  char *name = *path;
  if (name == end)
    return nullptr;  // no more names
  else if (*name == '\0') {  // from previous iteration?
    *name = *MOON_PATH_SEP;  /* restore separator */
    name++;  // skip it
  }
  sep = strchr(name, *MOON_PATH_SEP);  // find next separator
  if (sep == nullptr)  // separator not found?
    sep = end;  // name goes until the end
  *sep = '\0';  /* finish file name */
  *path = sep;  /* will start next search from here */
  return name;
}


/*
** Given a path such as ";blabla.so;blublu.so", pushes the string
**
** no file 'blabla.so'
**	no file 'blublu.so'
*/
static void pusherrornotfound (moon_State *L, const char *path) {
  moonL_Buffer b;
  moonL_buffinit(L, &b);
  moonL_addstring(&b, "no file '");
  moonL_addgsub(&b, path, MOON_PATH_SEP, "'\n\tno file '");
  moonL_addstring(&b, "'");
  moonL_pushresult(&b);
}


static const char *searchpath (moon_State *L, const char *name,
                                             const char *path,
                                             const char *sep,
                                             const char *dirsep) {
  moonL_Buffer buff;
  char *pathname;  // path with name inserted
  char *endpathname;  // its end
  const char *filename;
  // separator is non-empty and appears in 'name'?
  if (*sep != '\0' && strchr(name, *sep) != nullptr)
    name = moonL_gsub(L, name, sep, dirsep);  // replace it by 'dirsep'
  moonL_buffinit(L, &buff);
  // add path to the buffer, replacing marks ('?') with the file name
  moonL_addgsub(&buff, path, MOON_PATH_MARK, name);
  moonL_addchar(&buff, '\0');
  pathname = moonL_buffaddr(&buff);  // writable list of file names
  endpathname = pathname + moonL_bufflen(&buff) - 1;
  while ((filename = getnextfilename(&pathname, endpathname)) != nullptr) {
    if (readable(filename))  // does file exist and is readable?
      return moon_pushstring(L, filename);  // save and return name
  }
  moonL_pushresult(&buff);  // push path to create error message
  pusherrornotfound(L, moon_tostring(L, -1));  // create error message
  return nullptr;  // not found
}


static int ll_searchpath (moon_State *L) {
  const char *f = searchpath(L, moonL_checkstring(L, 1),
                                moonL_checkstring(L, 2),
                                moonL_optstring(L, 3, "."),
                                moonL_optstring(L, 4, MOON_DIRSEP));
  if (f != nullptr) return 1;
  else {  // error message is on top of the stack
    moonL_pushfail(L);
    moon_insert(L, -2);
    return 2;  // return fail + error message
  }
}


static const char *findfile (moon_State *L, const char *name,
                                           const char *pname,
                                           const char *dirsep) {
  const char *path;
  moon_getfield(L, moon_upvalueindex(1), pname);
  path = moon_tostring(L, -1);
  if (l_unlikely(path == nullptr))
    moonL_error(L, "'package.%s' must be a string", pname);
  return searchpath(L, name, path, ".", dirsep);
}


static int checkload (moon_State *L, int stat, const char *filename) {
  if (l_likely(stat)) {  // module loaded successfully?
    moon_pushstring(L, filename);  // will be 2nd argument to module
    return 2;  // return open function and file name
  }
  else
    return moonL_error(L, "error loading module '%s' from file '%s':\n\t%s",
                          moon_tostring(L, 1), filename, moon_tostring(L, -1));
}


static int searcher_Lua (moon_State *L) {
  const char *filename;
  const char *name = moonL_checkstring(L, 1);
  filename = findfile(L, name, "path", MOON_LSUBSEP);
  if (filename == nullptr) return 1;  // module not found in this path
  return checkload(L, (moonL_loadfile(L, filename) == MOON_OK), filename);
}


/*
** Try to find a load function for module 'modname' at file 'filename'.
** First, change '.' to '_' in 'modname'; then, if 'modname' has
** the form X-Y (that is, it has an "ignore mark"), build a function
** name "moonopen_X" and look for it. (For compatibility, if that
** fails, it also tries "moonopen_Y".) If there is no ignore mark,
** look for a function named "moonopen_modname".
*/
static int loadfunc (moon_State *L, const char *filename, const char *modname) {
  const char *openfunc;
  const char *mark;
  modname = moonL_gsub(L, modname, ".", MOON_OFSEP);
  mark = strchr(modname, *MOON_IGMARK);
  if (mark) {
    openfunc = moon_pushlstring(L, modname, ct_diff2sz(mark - modname));
    openfunc = moon_pushfstring(L, MOON_POF"%s", openfunc);
    const int stat = lookforfunc(L, filename, openfunc);
    if (stat != ERRFUNC) return stat;
    modname = mark + 1;  // else go ahead and try old-style name
  }
  openfunc = moon_pushfstring(L, MOON_POF"%s", modname);
  return lookforfunc(L, filename, openfunc);
}


static int searcher_C (moon_State *L) {
  const char *name = moonL_checkstring(L, 1);
  const char *filename = findfile(L, name, "cpath", MOON_CSUBSEP);
  if (filename == nullptr) return 1;  // module not found in this path
  return checkload(L, (loadfunc(L, filename, name) == 0), filename);
}


static int searcher_Croot (moon_State *L) {
  const char *filename;
  const char *name = moonL_checkstring(L, 1);
  const char *p = strchr(name, '.');
  if (p == nullptr) return 0;  // is root
  moon_pushlstring(L, name, ct_diff2sz(p - name));
  filename = findfile(L, moon_tostring(L, -1), "cpath", MOON_CSUBSEP);
  if (filename == nullptr) return 1;  // root not found
  if (int stat = loadfunc(L, filename, name); stat != 0) {
    if (stat != ERRFUNC)
      return checkload(L, 0, filename);  // real error
    else {  // open function not found
      moon_pushfstring(L, "no module '%s' in file '%s'", name, filename);
      return 1;
    }
  }
  moon_pushstring(L, filename);  // will be 2nd argument to module
  return 2;
}


static int searcher_preload (moon_State *L) {
  const char *name = moonL_checkstring(L, 1);
  moon_getfield(L, MOON_REGISTRYINDEX, MOON_PRELOAD_TABLE);
  if (moon_getfield(L, -1, name) == MOON_TNIL) {  // not found?
    moon_pushfstring(L, "no field package.preload['%s']", name);
    return 1;
  }
  else {
    moon_pushliteral(L, ":preload:");
    return 2;
  }
}


static void findloader (moon_State *L, const char *name) {
  moonL_Buffer msg;  // to build error message
  // push 'package.searchers' to index 3 in the stack
  if (l_unlikely(moon_getfield(L, moon_upvalueindex(1), "searchers")
                 != MOON_TTABLE))
    moonL_error(L, "'package.searchers' must be a table");
  moonL_buffinit(L, &msg);
  moonL_addstring(&msg, "\n\t");  // error-message prefix for first message
  // iterate over available searchers to find a loader
  for (int i = 1; ; i++) {
    if (l_unlikely(moon_rawgeti(L, 3, i) == MOON_TNIL)) {  // no more searchers?
      moon_pop(L, 1);  // remove nil
      moonL_buffsub(&msg, 2);  // remove last prefix
      moonL_pushresult(&msg);  // create error message
      moonL_error(L, "module '%s' not found:%s", name, moon_tostring(L, -1));
    }
    moon_pushstring(L, name);
    moon_call(L, 1, 2);  // call it
    if (moon_isfunction(L, -2))  // did it find a loader?
      return;  // module loader found
    else if (moon_isstring(L, -2)) {  // searcher returned error message?
      moon_pop(L, 1);  // remove extra return
      moonL_addvalue(&msg);  // concatenate error message
      moonL_addstring(&msg, "\n\t");  // prefix for next message
    }
    else  // no error message
      moon_pop(L, 2);  // remove both returns
  }
}


static int ll_require (moon_State *L) {
  const char *name = moonL_checkstring(L, 1);
  moon_settop(L, 1);  // LOADED table will be at index 2
  moon_getfield(L, MOON_REGISTRYINDEX, MOON_LOADED_TABLE);
  moon_getfield(L, 2, name);  // LOADED[name]
  if (moon_toboolean(L, -1))  // is it there?
    return 1;  // package is already loaded
  // else must load package
  moon_pop(L, 1);  // remove 'getfield' result
  findloader(L, name);
  moon_rotate(L, -2, 1);  // function <-> loader data
  moon_pushvalue(L, 1);  // name is 1st argument to module loader
  moon_pushvalue(L, -3);  // loader data is 2nd argument
  // stack: ...; loader data; loader function; mod. name; loader data
  moon_call(L, 2, 1);  // run loader to load module
  // stack: ...; loader data; result from loader
  if (!moon_isnil(L, -1))  // non-nil return?
    moon_setfield(L, 2, name);  // LOADED[name] = returned value
  else
    moon_pop(L, 1);  // pop nil
  if (moon_getfield(L, 2, name) == MOON_TNIL) {  // module set no value?
    moon_pushboolean(L, 1);  // use true as result
    moon_copy(L, -1, -2);  // replace loader result
    moon_setfield(L, 2, name);  // LOADED[name] = true
  }
  moon_rotate(L, -2, 1);  // loader data <-> module result
  return 2;  // return module result and loader data
}

// }======================================================




static const moonL_Reg pk_funcs[] = {
  {"loadlib", ll_loadlib},
  {"searchpath", ll_searchpath},
  // placeholders
  {"preload", nullptr},
  {"cpath", nullptr},
  {"path", nullptr},
  {"searchers", nullptr},
  {"loaded", nullptr},
  {nullptr, nullptr}
};


static const moonL_Reg ll_funcs[] = {
  {"require", ll_require},
  {nullptr, nullptr}
};


static void createsearcherstable (moon_State *L) {
  static const moon_CFunction searchers[] = {
    searcher_preload,
    searcher_Lua,
    searcher_C,
    searcher_Croot,
    nullptr
  };
  // create 'searchers' table
  moon_createtable(L, sizeof(searchers)/sizeof(searchers[0]) - 1, 0);
  // fill it with predefined searchers
  for (int i=0; searchers[i] != nullptr; i++) {
    moon_pushvalue(L, -2);  // set 'package' as upvalue for all searchers
    moon_pushcclosure(L, searchers[i], 1);
    moon_rawseti(L, -2, i+1);
  }
  moon_setfield(L, -2, "searchers");  // put it in field 'searchers'
}


MOONMOD_API int moonopen_package (moon_State *L) {
  moonL_getsubtable(L, MOON_REGISTRYINDEX, CLIBS);  // create CLIBS table
  moon_pop(L, 1);  // will not use it now
  moonL_newlib(L, pk_funcs);  // create 'package' table
  createsearcherstable(L);
  // set paths
  setpath(L, "path", MOON_PATH_VAR, MOON_PATH_DEFAULT);
  setpath(L, "cpath", MOON_CPATH_VAR, MOON_CPATH_DEFAULT);
  // store config information
  moon_pushliteral(L, MOON_DIRSEP "\n" MOON_PATH_SEP "\n" MOON_PATH_MARK "\n"
                     MOON_EXEC_DIR "\n" MOON_IGMARK "\n");
  moon_setfield(L, -2, "config");
  // set field 'loaded'
  moonL_getsubtable(L, MOON_REGISTRYINDEX, MOON_LOADED_TABLE);
  moon_setfield(L, -2, "loaded");
  // set field 'preload'
  moonL_getsubtable(L, MOON_REGISTRYINDEX, MOON_PRELOAD_TABLE);
  moon_setfield(L, -2, "preload");
  moon_pushglobaltable(L);
  moon_pushvalue(L, -2);  // set 'package' as upvalue for next lib
  moonL_setfuncs(L, ll_funcs, 1);  // open lib into global table
  moon_pop(L, 1);  // pop global table
  return 1;  // return 'package' table
}

