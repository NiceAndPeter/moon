/*
** Auxiliary functions for building Lua libraries
** See Copyright Notice in lua.h
*/

#define MOON_LIB

#include "lprefix.h"


#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>


/*
** This file uses only the official API of Lua.
** Any function declared here could be written as an application function.
*/

#include "lua.h"

#include "lauxlib.h"
#include "llimits.h"


/*
** {======================================================
** Traceback
** =======================================================
*/


inline constexpr int LEVELS1 = 10;  // size of the first part of the stack
inline constexpr int LEVELS2 = 11;  // size of the second part of the stack



/*
** Search for 'objidx' in table at index -1. ('objidx' must be an
** absolute index.) Return 1 + string at top if it found a good name.
*/
static int findfield (moon_State *L, int objidx, int level) {
  if (level == 0 || !moon_istable(L, -1))
    return 0;  // not found
  moon_pushnil(L);  // start 'next' loop
  while (moon_next(L, -2)) {  // for each pair in table
    if (moon_type(L, -2) == MOON_TSTRING) {  // ignore non-string keys
      if (moon_rawequal(L, objidx, -1)) {  // found object?
        moon_pop(L, 1);  // remove value (but keep name)
        return 1;
      }
      else if (findfield(L, objidx, level - 1)) {  // try recursively
        // stack: lib_name, lib_table, field_name (top)
        moon_pushliteral(L, ".");  // place '.' between the two names
        moon_replace(L, -3);  // (in the slot occupied by table)
        moon_concat(L, 3);  // lib_name.field_name
        return 1;
      }
    }
    moon_pop(L, 1);  // remove value
  }
  return 0;  // not found
}


/*
** Search for a name for a function in all loaded modules
*/
static int pushglobalfuncname (moon_State *L, moon_Debug *ar) {
  int top = moon_gettop(L);
  moon_getinfo(L, "f", ar);  // push function
  moon_getfield(L, MOON_REGISTRYINDEX, MOON_LOADED_TABLE);
  moonL_checkstack(L, 6, "not enough stack");  // slots for 'findfield'
  if (findfield(L, top + 1, 2)) {
    const char *name = moon_tostring(L, -1);
    if (strncmp(name, MOON_GNAME ".", 3) == 0) {  // name start with '_G.'?
      moon_pushstring(L, name + 3);  // push name without prefix
      moon_remove(L, -2);  // remove original name
    }
    moon_copy(L, -1, top + 1);  // copy name to proper place
    moon_settop(L, top + 1);  // remove table "loaded" and name copy
    return 1;
  }
  else {
    moon_settop(L, top);  // remove function and global table
    return 0;
  }
}


static void pushfuncname (moon_State *L, moon_Debug *ar) {
  if (*ar->namewhat != '\0')  // is there a name from code?
    moon_pushfstring(L, "%s '%s'", ar->namewhat, ar->name);  // use it
  else if (*ar->what == 'm')  // main?
      moon_pushliteral(L, "main chunk");
  else if (pushglobalfuncname(L, ar)) {  // try a global name
    moon_pushfstring(L, "function '%s'", moon_tostring(L, -1));
    moon_remove(L, -2);  // remove name
  }
  else if (*ar->what != 'C')  // for Lua functions, use <file:line>
    moon_pushfstring(L, "function <%s:%d>", ar->short_src, ar->linedefined);
  else  // nothing left...
    moon_pushliteral(L, "?");
}


static int lastlevel (moon_State *L) {
  moon_Debug ar;
  int li = 1, le = 1;
  // find an upper bound
  while (moon_getstack(L, le, &ar)) { li = le; le *= 2; }
  // do a binary search
  while (li < le) {
    int m = (li + le)/2;
    if (moon_getstack(L, m, &ar)) li = m + 1;
    else le = m;
  }
  return le - 1;
}


MOONLIB_API void moonL_traceback (moon_State *L, moon_State *L1,
                                const char *msg, int level) {
  moonL_Buffer b;
  moon_Debug ar;
  int last = lastlevel(L1);
  int limit2show = (last - level > LEVELS1 + LEVELS2) ? LEVELS1 : -1;
  moonL_buffinit(L, &b);
  if (msg) {
    moonL_addstring(&b, msg);
    moonL_addchar(&b, '\n');
  }
  moonL_addstring(&b, "stack traceback:");
  while (moon_getstack(L1, level++, &ar)) {
    if (limit2show-- == 0) {  // too many levels?
      int n = last - level - LEVELS2 + 1;  // number of levels to skip
      moon_pushfstring(L, "\n\t...\t(skipping %d levels)", n);
      moonL_addvalue(&b);  // add warning about skip
      level += n;  // and skip to last levels
    }
    else {
      moon_getinfo(L1, "Slnt", &ar);
      if (ar.currentline <= 0)
        moon_pushfstring(L, "\n\t%s: in ", ar.short_src);
      else
        moon_pushfstring(L, "\n\t%s:%d: in ", ar.short_src, ar.currentline);
      moonL_addvalue(&b);
      pushfuncname(L, &ar);
      moonL_addvalue(&b);
      if (ar.istailcall)
        moonL_addstring(&b, "\n\t(...tail calls...)");
    }
  }
  moonL_pushresult(&b);
}

// }======================================================


/*
** {======================================================
** Error-report functions
** =======================================================
*/

MOONLIB_API int moonL_argerror (moon_State *L, int arg, const char *extramsg) {
  moon_Debug ar;
  const char *argword;
  if (!moon_getstack(L, 0, &ar))  // no stack frame?
    return moonL_error(L, "bad argument #%d (%s)", arg, extramsg);
  moon_getinfo(L, "nt", &ar);
  if (arg <= ar.extraargs)  // error in an extra argument?
    argword =  "extra argument";
  else {
    arg -= ar.extraargs;  // do not count extra arguments
    if (strcmp(ar.namewhat, "method") == 0) {  // colon syntax?
      arg--;  // do not count (extra) self argument
      if (arg == 0)  // error in self argument?
        return moonL_error(L, "calling '%s' on bad self (%s)",
                               ar.name, extramsg);
      // else go through; error in a regular argument
    }
    argword = "argument";
  }
  if (ar.name == nullptr)
    ar.name = (pushglobalfuncname(L, &ar)) ? moon_tostring(L, -1) : "?";
  return moonL_error(L, "bad %s #%d to '%s' (%s)",
                       argword, arg, ar.name, extramsg);
}


MOONLIB_API int moonL_typeerror (moon_State *L, int arg, const char *tname) {
  const char *msg;
  const char *typearg;  // name for the type of the actual argument
  if (moonL_getmetafield(L, arg, "__name") == MOON_TSTRING)
    typearg = moon_tostring(L, -1);  // use the given type name
  else if (moon_type(L, arg) == MOON_TLIGHTUSERDATA)
    typearg = "light userdata";  // special name for messages
  else
    typearg = moonL_typename(L, arg);  // standard name
  msg = moon_pushfstring(L, "%s expected, got %s", tname, typearg);
  return moonL_argerror(L, arg, msg);
}


static void tag_error (moon_State *L, int arg, int tag) {
  moonL_typeerror(L, arg, moon_typename(L, tag));
}


/*
** The use of 'moon_pushfstring' ensures this function does not
** need reserved stack space when called.
*/
MOONLIB_API void moonL_where (moon_State *L, int level) {
  moon_Debug ar;
  if (moon_getstack(L, level, &ar)) {  // check function at level
    moon_getinfo(L, "Sl", &ar);  // get info about it
    if (ar.currentline > 0) {  // is there info?
      moon_pushfstring(L, "%s:%d: ", ar.short_src, ar.currentline);
      return;
    }
  }
  moon_pushfstring(L, "");  // else, no information available...
}


/*
** Again, the use of 'moon_pushvfstring' ensures this function does
** not need reserved stack space when called. (At worst, it generates
** a memory error instead of the given message.)
*/
MOONLIB_API int moonL_error (moon_State *L, const char *fmt, ...) {
  va_list argp;
  va_start(argp, fmt);
  moonL_where(L, 1);
  moon_pushvfstring(L, fmt, argp);
  va_end(argp);
  moon_concat(L, 2);
  return moon_error(L);
}


MOONLIB_API int moonL_fileresult (moon_State *L, int stat, const char *fname) {
  int en = errno;  // calls to Lua API may change this value
  if (stat) {
    moon_pushboolean(L, 1);
    return 1;
  }
  else {
    const char *msg;
    moonL_pushfail(L);
    msg = (en != 0) ? strerror(en) : "(no extra info)";
    if (fname)
      moon_pushfstring(L, "%s: %s", fname, msg);
    else
      moon_pushstring(L, msg);
    moon_pushinteger(L, en);
    return 3;
  }
}


#if !defined(l_inspectstat)  // {

#if defined(MOON_USE_POSIX)

#include <sys/wait.h>

/*
** use appropriate macros to interpret 'pclose' return status
*/
#define l_inspectstat(stat,what)  \
   if (WIFEXITED(stat)) { stat = WEXITSTATUS(stat); } \
   else if (WIFSIGNALED(stat)) { stat = WTERMSIG(stat); what = "signal"; }

#else

#define l_inspectstat(stat,what)  // no op

#endif

#endif  // }


MOONLIB_API int moonL_execresult (moon_State *L, int stat) {
  if (stat == -1)  // error with an 'errno'?
    return moonL_fileresult(L, 0, nullptr);
  else {
    const char *what = "exit";  // type of termination
    l_inspectstat(stat, what);  // interpret result
    if (*what == 'e' && stat == 0)  // successful termination?
      moon_pushboolean(L, 1);
    else
      moonL_pushfail(L);
    moon_pushstring(L, what);
    moon_pushinteger(L, stat);
    return 3;  // return true/fail,what,code
  }
}

// }======================================================



/*
** {======================================================
** Userdata's metatable manipulation
** =======================================================
*/

MOONLIB_API int moonL_newmetatable (moon_State *L, const char *tname) {
  if (moonL_getmetatable(L, tname) != MOON_TNIL)  // name already in use?
    return 0;  // leave previous value on top, but return 0
  moon_pop(L, 1);
  moon_createtable(L, 0, 2);  // create metatable
  moon_pushstring(L, tname);
  moon_setfield(L, -2, "__name");  // metatable.__name = tname
  moon_pushvalue(L, -1);
  moon_setfield(L, MOON_REGISTRYINDEX, tname);  // registry.name = metatable
  return 1;
}


MOONLIB_API void moonL_setmetatable (moon_State *L, const char *tname) {
  moonL_getmetatable(L, tname);
  moon_setmetatable(L, -2);
}


MOONLIB_API void *moonL_testudata (moon_State *L, int ud, const char *tname) {
  void *p = moon_touserdata(L, ud);
  if (p != nullptr) {  // value is a userdata?
    if (moon_getmetatable(L, ud)) {  // does it have a metatable?
      moonL_getmetatable(L, tname);  // get correct metatable
      if (!moon_rawequal(L, -1, -2))  // not the same?
        p = nullptr;  // value is a userdata with wrong metatable
      moon_pop(L, 2);  // remove both metatables
      return p;
    }
  }
  return nullptr;  // value is not a userdata with a metatable
}


MOONLIB_API void *moonL_checkudata (moon_State *L, int ud, const char *tname) {
  void *p = moonL_testudata(L, ud, tname);
  moonL_argexpected(L, p != nullptr, ud, tname);
  return p;
}

// }======================================================


/*
** {======================================================
** Argument check functions
** =======================================================
*/

MOONLIB_API int moonL_checkoption (moon_State *L, int arg, const char *def,
                                 const char *const lst[]) {
  const char *name = (def) ? moonL_optstring(L, arg, def) :
                             moonL_checkstring(L, arg);
  for (int i=0; lst[i]; i++)
    if (strcmp(lst[i], name) == 0)
      return i;
  return moonL_argerror(L, arg,
                       moon_pushfstring(L, "invalid option '%s'", name));
}


/*
** Ensures the stack has at least 'space' extra slots, raising an error
** if it cannot fulfill the request. (The error handling needs a few
** extra slots to format the error message. In case of an error without
** this extra space, Lua will generate the same 'stack overflow' error,
** but without 'msg'.)
*/
MOONLIB_API void moonL_checkstack (moon_State *L, int space, const char *msg) {
  if (l_unlikely(!moon_checkstack(L, space))) {
    if (msg)
      moonL_error(L, "stack overflow (%s)", msg);
    else
      moonL_error(L, "stack overflow");
  }
}


MOONLIB_API void moonL_checktype (moon_State *L, int arg, int t) {
  if (l_unlikely(moon_type(L, arg) != t))
    tag_error(L, arg, t);
}


MOONLIB_API void moonL_checkany (moon_State *L, int arg) {
  if (l_unlikely(moon_type(L, arg) == MOON_TNONE))
    moonL_argerror(L, arg, "value expected");
}


MOONLIB_API const char *moonL_checklstring (moon_State *L, int arg, size_t *len) {
  const char *s = moon_tolstring(L, arg, len);
  if (l_unlikely(!s)) tag_error(L, arg, MOON_TSTRING);
  return s;
}


MOONLIB_API const char *moonL_optlstring (moon_State *L, int arg,
                                        const char *def, size_t *len) {
  if (moon_isnoneornil(L, arg)) {
    if (len)
      *len = (def ? strlen(def) : 0);
    return def;
  }
  else return moonL_checklstring(L, arg, len);
}


MOONLIB_API moon_Number moonL_checknumber (moon_State *L, int arg) {
  int isnum;
  moon_Number d = moon_tonumberx(L, arg, &isnum);
  if (l_unlikely(!isnum))
    tag_error(L, arg, MOON_TNUMBER);
  return d;
}


MOONLIB_API moon_Number moonL_optnumber (moon_State *L, int arg, moon_Number def) {
  return moonL_opt(L, moonL_checknumber, arg, def);
}


static void interror (moon_State *L, int arg) {
  if (moon_isnumber(L, arg))
    moonL_argerror(L, arg, "number has no integer representation");
  else
    tag_error(L, arg, MOON_TNUMBER);
}


MOONLIB_API moon_Integer moonL_checkinteger (moon_State *L, int arg) {
  int isnum;
  moon_Integer d = moon_tointegerx(L, arg, &isnum);
  if (l_unlikely(!isnum)) {
    interror(L, arg);
  }
  return d;
}


MOONLIB_API moon_Integer moonL_optinteger (moon_State *L, int arg,
                                                      moon_Integer def) {
  return moonL_opt(L, moonL_checkinteger, arg, def);
}

// }======================================================


/*
** {======================================================
** Generic Buffer manipulation
** =======================================================
*/

// userdata to box arbitrary data
typedef struct UBox {
  void *box;
  size_t bsize;
} UBox;


/* Resize the buffer used by a box. Optimize for the common case of
** resizing to the old size. (For instance, __gc will resize the box
** to 0 even after it was closed. 'pushresult' may also resize it to a
** final size that is equal to the one set when the buffer was created.)
*/
static void *resizebox (moon_State *L, int idx, size_t newsize) {
  UBox *box = static_cast<UBox *>(moon_touserdata(L, idx));
  if (box->bsize == newsize)  // not changing size?
    return box->box;  // keep the buffer
  else {
    void *ud;
    moon_Alloc allocf = moon_getallocf(L, &ud);
    void *temp = allocf(ud, box->box, box->bsize, newsize);
    if (l_unlikely(temp == nullptr && newsize > 0)) {  // allocation error?
      moon_pushliteral(L, "not enough memory");
      moon_error(L);  // raise a memory error
    }
    box->box = temp;
    box->bsize = newsize;
    return temp;
  }
}


static int boxgc (moon_State *L) {
  resizebox(L, 1, 0);
  return 0;
}


static const moonL_Reg boxmt[] = {  // box metamethods
  {"__gc", boxgc},
  {"__close", boxgc},
  {nullptr, nullptr}
};


static void newbox (moon_State *L) {
  UBox *box = static_cast<UBox *>(moon_newuserdatauv(L, sizeof(UBox), 0));
  box->box = nullptr;
  box->bsize = 0;
  if (moonL_newmetatable(L, "_UBOX*"))  // creating metatable?
    moonL_setfuncs(L, boxmt, 0);  // set its metamethods
  moon_setmetatable(L, -2);
}


/*
** check whether buffer is using a userdata on the stack as a temporary
** buffer
*/
inline bool buffonstack(const moonL_Buffer* B) noexcept {
    return B->b != B->init.b;
}


/*
** Whenever buffer is accessed, slot 'idx' must either be a box (which
** cannot be nullptr) or it is a placeholder for the buffer.
*/
#define checkbufferlevel(B,idx)  \
  moon_assert(buffonstack(B) ? moon_touserdata(B->L, idx) != nullptr  \
                            : moon_touserdata(B->L, idx) == static_cast<void*>(B))


/*
** Compute new size for buffer 'B', enough to accommodate extra 'sz'
** bytes plus one for a terminating zero.
*/
static size_t newbuffsize (moonL_Buffer *B, size_t sz) {
  size_t newsize = B->size;
  if (l_unlikely(sz >= MAX_SIZE - B->n))
    return cast_sizet(moonL_error(B->L, "resulting string too large"));
  // else  B->n + sz + 1 <= MAX_SIZE
  if (newsize <= MAX_SIZE/3 * 2)  // no overflow?
    newsize += (newsize >> 1);  // new size *= 1.5
  if (newsize < B->n + sz + 1)  // not big enough?
    newsize = B->n + sz + 1;
  return newsize;
}


/*
** Returns a pointer to a free area with at least 'sz' bytes in buffer
** 'B'. 'boxidx' is the relative position in the stack where is the
** buffer's box or its placeholder.
*/
static char *prepbuffsize (moonL_Buffer *B, size_t sz, int boxidx) {
  checkbufferlevel(B, boxidx);
  if (B->size - B->n >= sz)  // enough space?
    return B->b + B->n;
  else {
    moon_State *L = B->L;
    char *newbuff;
    size_t newsize = newbuffsize(B, sz);
    // create larger buffer
    if (buffonstack(B))  // buffer already has a box?
      newbuff = static_cast<char *>(resizebox(L, boxidx, newsize));  // resize it
    else {  // no box yet
      moon_remove(L, boxidx);  // remove placeholder
      newbox(L);  // create a new box
      moon_insert(L, boxidx);  // move box to its intended position
      moon_toclose(L, boxidx);
      newbuff = static_cast<char *>(resizebox(L, boxidx, newsize));
      std::copy_n(B->b, B->n, newbuff);  // copy original content
    }
    B->b = newbuff;
    B->size = newsize;
    return newbuff + B->n;
  }
}

/*
** returns a pointer to a free area with at least 'sz' bytes
*/
MOONLIB_API char *moonL_prepbuffsize (moonL_Buffer *B, size_t sz) {
  return prepbuffsize(B, sz, -1);
}


// std::span-based implementation
MOONLIB_API void moonL_addlstring (moonL_Buffer *B, std::span<const char> s) {
  size_t l = s.size();
  if (l > 0) {  // avoid 'std::copy_n' when 's' can be nullptr
    char *b = prepbuffsize(B, l, -1);
    std::copy_n(s.data(), l, b);
    moonL_addsize(B, l);
  }
}


MOONLIB_API void moonL_addstring (moonL_Buffer *B, const char *s) {
  moonL_addlstring(B, s, strlen(s));
}


MOONLIB_API void moonL_pushresult (moonL_Buffer *B) {
  moon_State *L = B->L;
  checkbufferlevel(B, -1);
  if (!buffonstack(B))  // using static buffer?
    moon_pushlstring(L, B->b, B->n);  // save result as regular string
  else {  // reuse buffer already allocated
    UBox *box = static_cast<UBox *>(moon_touserdata(L, -1));
    void *ud;
    moon_Alloc allocf = moon_getallocf(L, &ud);  // function to free buffer
    size_t len = B->n;  // final string length
    char *s;
    resizebox(L, -1, len + 1);  // adjust box size to content size
    s = static_cast<char*>(box->box);  // final buffer address
    s[len] = '\0';  // add ending zero
    // clear box, as Lua will take control of the buffer
    box->bsize = 0;  box->box = nullptr;
    moon_pushexternalstring(L, s, len, allocf, ud);
    moon_closeslot(L, -2);  // close the box
    moon_gc(L, MOON_GCSTEP, len);
  }
  moon_remove(L, -2);  // remove box or placeholder from the stack
}


MOONLIB_API void moonL_pushresultsize (moonL_Buffer *B, size_t sz) {
  moonL_addsize(B, sz);
  moonL_pushresult(B);
}


/*
** 'moonL_addvalue' is the only function in the Buffer system where the
** box (if existent) is not on the top of the stack. So, instead of
** calling 'moonL_addlstring', it replicates the code using -2 as the
** last argument to 'prepbuffsize', signaling that the box is (or will
** be) below the string being added to the buffer. (Box creation can
** trigger an emergency GC, so we should not remove the string from the
** stack before we have the space guaranteed.)
*/
MOONLIB_API void moonL_addvalue (moonL_Buffer *B) {
  moon_State *L = B->L;
  size_t len;
  const char *s = moon_tolstring(L, -1, &len);
  char *b = prepbuffsize(B, len, -2);
  std::copy_n(s, len, b);
  moonL_addsize(B, len);
  moon_pop(L, 1);  // pop string
}


MOONLIB_API void moonL_buffinit (moon_State *L, moonL_Buffer *B) {
  B->L = L;
  B->b = B->init.b;
  B->n = 0;
  B->size = MOONL_BUFFERSIZE;
  moon_pushlightuserdata(L, static_cast<void*>(B));  // push placeholder
}


MOONLIB_API char *moonL_buffinitsize (moon_State *L, moonL_Buffer *B, size_t sz) {
  moonL_buffinit(L, B);
  return prepbuffsize(B, sz, -1);
}

// }======================================================


/*
** {======================================================
** Reference system
** =======================================================
*/

/*
** The previously freed references form a linked list: t[1] is the index
** of a first free index, t[t[1]] is the index of the second element,
** etc. A zero signals the end of the list.
*/
MOONLIB_API int moonL_ref (moon_State *L, int t) {
  int ref;
  if (moon_isnil(L, -1)) {
    moon_pop(L, 1);  // remove from stack
    return MOON_REFNIL;  // 'nil' has a unique fixed reference
  }
  t = moon_absindex(L, t);
  if (moon_rawgeti(L, t, 1) == MOON_TNUMBER)  // already initialized?
    ref = (int)moon_tointeger(L, -1);  // ref = t[1]
  else {  // first access
    moon_assert(!moon_toboolean(L, -1));  // must be nil or false
    ref = 0;  // list is empty
    moon_pushinteger(L, 0);  // initialize as an empty list
    moon_rawseti(L, t, 1);  // ref = t[1] = 0
  }
  moon_pop(L, 1);  // remove element from stack
  if (ref != 0) {  // any free element?
    moon_rawgeti(L, t, ref);  // remove it from list
    moon_rawseti(L, t, 1);  // (t[1] = t[ref])
  }
  else  // no free elements
    ref = (int)moon_rawlen(L, t) + 1;  // get a new reference
  moon_rawseti(L, t, ref);
  return ref;
}


MOONLIB_API void moonL_unref (moon_State *L, int t, int ref) {
  if (ref >= 0) {
    t = moon_absindex(L, t);
    moon_rawgeti(L, t, 1);
    moon_assert(moon_isinteger(L, -1));
    moon_rawseti(L, t, ref);  // t[ref] = t[1]
    moon_pushinteger(L, ref);
    moon_rawseti(L, t, 1);  // t[1] = ref
  }
}

// }======================================================


/*
** {======================================================
** Load functions
** =======================================================
*/

typedef struct LoadF {
  unsigned n;  // number of pre-read characters
  FILE *f;  // file being read
  char buff[BUFSIZ];  // area for reading file
} LoadF;


static const char *getF (moon_State *L, void *ud, size_t *size) {
  LoadF *lf = static_cast<LoadF *>(ud);
  (void)L;  // not used
  if (lf->n > 0) {  // are there pre-read characters to be read?
    *size = lf->n;  /* return them (chars already in buffer) */
    lf->n = 0;  // no more pre-read characters
  }
  else {  // read a block from file
    /* 'fread' can return > 0 *and* set the EOF flag. If next call to
       'getF' called 'fread', it might still wait for user input.
       The next check avoids this problem. */
    if (feof(lf->f)) return nullptr;
    *size = fread(lf->buff, 1, sizeof(lf->buff), lf->f);  /* read block */
  }
  return lf->buff;
}


static int errfile (moon_State *L, const char *what, int fnameindex) {
  int err = errno;
  const char *filename = moon_tostring(L, fnameindex) + 1;
  if (err != 0)
    moon_pushfstring(L, "cannot %s %s: %s", what, filename, strerror(err));
  else
    moon_pushfstring(L, "cannot %s %s", what, filename);
  moon_remove(L, fnameindex);
  return MOON_ERRFILE;
}


/*
** Skip an optional BOM at the start of a stream. If there is an
** incomplete BOM (the first character is correct but the rest is
** not), returns the first character anyway to force an error
** (as no chunk can start with 0xEF).
*/
static int skipBOM (FILE *f) {
  int c = getc(f);  // read first character
  if (c == 0xEF && getc(f) == 0xBB && getc(f) == 0xBF)  // correct BOM?
    return getc(f);  // ignore BOM and return next char
  else  // no (valid) BOM
    return c;  // return first character
}


/*
** reads the first character of file 'f' and skips an optional BOM mark
** in its beginning plus its first line if it starts with '#'. Returns
** true if it skipped the first line.  In any case, '*cp' has the
** first "valid" character of the file (after the optional BOM and
** a first-line comment).
*/
static int skipcomment (FILE *f, int *cp) {
  int c = *cp = skipBOM(f);
  if (c == '#') {  // first line is a comment (Unix exec. file)?
    do {  // skip first line
      c = getc(f);
    } while (c != EOF && c != '\n');
    *cp = getc(f);  /* next character after comment, if present */
    return 1;  // there was a comment
  }
  else return 0;  // no comment
}


MOONLIB_API int moonL_loadfilex (moon_State *L, const char *filename,
                                             const char *mode) {
  LoadF lf;
  int status, readstatus;
  int c;
  int fnameindex = moon_gettop(L) + 1;  // index of filename on the stack
  if (filename == nullptr) {
    moon_pushliteral(L, "=stdin");
    lf.f = stdin;
  }
  else {
    moon_pushfstring(L, "@%s", filename);
    errno = 0;
    lf.f = fopen(filename, "r");
    if (lf.f == nullptr) return errfile(L, "open", fnameindex);
  }
  lf.n = 0;
  if (skipcomment(lf.f, &c))  // read initial portion
    lf.buff[lf.n++] = '\n';  // add newline to correct line numbers
  if (c == MOON_SIGNATURE[0]) {  // binary file?
    lf.n = 0;  // remove possible newline
    if (filename) {  // "real" file?
      errno = 0;
      lf.f = freopen(filename, "rb", lf.f);  // reopen in binary mode
      if (lf.f == nullptr) return errfile(L, "reopen", fnameindex);
      skipcomment(lf.f, &c);  // re-read initial portion
    }
  }
  if (c != EOF)
    lf.buff[lf.n++] = cast_char(c);  // 'c' is the first character
  status = moon_load(L, getF, &lf, moon_tostring(L, -1), mode);
  readstatus = ferror(lf.f);
  errno = 0;  // no useful error number until here
  if (filename) fclose(lf.f);  // close file (even in case of errors)
  if (readstatus) {
    moon_settop(L, fnameindex);  // ignore results from 'moon_load'
    return errfile(L, "read", fnameindex);
  }
  moon_remove(L, fnameindex);
  return status;
}


typedef struct LoadS {
  const char *s;
  size_t size;
} LoadS;


static const char *getS (moon_State *L, void *ud, size_t *size) {
  LoadS *ls = static_cast<LoadS *>(ud);
  (void)L;  // not used
  if (ls->size == 0) return nullptr;
  *size = ls->size;
  ls->size = 0;
  return ls->s;
}


MOONLIB_API int moonL_loadbufferx (moon_State *L, const char *buff, size_t size,
                                 const char *name, const char *mode) {
  LoadS ls;
  ls.s = buff;
  ls.size = size;
  return moon_load(L, getS, &ls, name, mode);
}


MOONLIB_API int moonL_loadstring (moon_State *L, const char *s) {
  return moonL_loadbuffer(L, s, strlen(s), s);
}

// }======================================================



MOONLIB_API int moonL_getmetafield (moon_State *L, int obj, const char *event) {
  if (!moon_getmetatable(L, obj))  // no metatable?
    return MOON_TNIL;
  else {
    moon_pushstring(L, event);
    const int tt = moon_rawget(L, -2);
    if (tt == MOON_TNIL)  // is metafield nil?
      moon_pop(L, 2);  // remove metatable and metafield
    else
      moon_remove(L, -2);  // remove only metatable
    return tt;  // return metafield type
  }
}


MOONLIB_API int moonL_callmeta (moon_State *L, int obj, const char *event) {
  obj = moon_absindex(L, obj);
  if (moonL_getmetafield(L, obj, event) == MOON_TNIL)  // no metafield?
    return 0;
  moon_pushvalue(L, obj);
  moon_call(L, 1, 1);
  return 1;
}


MOONLIB_API moon_Integer moonL_len (moon_State *L, int idx) {
  int isnum;
  moon_len(L, idx);
  const moon_Integer l = moon_tointegerx(L, -1, &isnum);
  if (l_unlikely(!isnum))
    moonL_error(L, "object length is not an integer");
  moon_pop(L, 1);  // remove object
  return l;
}


MOONLIB_API const char *moonL_tolstring (moon_State *L, int idx, size_t *len) {
  idx = moon_absindex(L,idx);
  if (moonL_callmeta(L, idx, "__tostring")) {  // metafield?
    if (!moon_isstring(L, -1))
      moonL_error(L, "'__tostring' must return a string");
  }
  else {
    switch (moon_type(L, idx)) {
      case MOON_TNUMBER: {
        char buff[MOON_N2SBUFFSZ];
        moon_numbertocstring(L, idx, buff);
        moon_pushstring(L, buff);
        break;
      }
      case MOON_TSTRING:
        moon_pushvalue(L, idx);
        break;
      case MOON_TBOOLEAN:
        moon_pushstring(L, (moon_toboolean(L, idx) ? "true" : "false"));
        break;
      case MOON_TNIL:
        moon_pushliteral(L, "nil");
        break;
      default: {
        int tt = moonL_getmetafield(L, idx, "__name");  // try name
        const char *kind = (tt == MOON_TSTRING) ? moon_tostring(L, -1) :
                                                 moonL_typename(L, idx);
        moon_pushfstring(L, "%s: %p", kind, moon_topointer(L, idx));
        if (tt != MOON_TNIL)
          moon_remove(L, -2);  // remove '__name'
        break;
      }
    }
  }
  return moon_tolstring(L, -1, len);
}


/*
** set functions from list 'l' into table at top - 'nup'; each
** function gets the 'nup' elements at the top as upvalues.
** Returns with only the table at the stack.
*/
MOONLIB_API void moonL_setfuncs (moon_State *L, const moonL_Reg *l, int nup) {
  moonL_checkstack(L, nup, "too many upvalues");
  for (; l->name != nullptr; l++) {  // fill the table with given functions
    if (l->func == nullptr)  // placeholder?
      moon_pushboolean(L, 0);
    else {
      for (int i = 0; i < nup; i++)  // copy upvalues to the top
        moon_pushvalue(L, -nup);
      moon_pushcclosure(L, l->func, nup);  // closure with those upvalues
    }
    moon_setfield(L, -(nup + 2), l->name);
  }
  moon_pop(L, nup);  // remove upvalues
}


/*
** ensure that stack[idx][fname] has a table and push that table
** into the stack
*/
MOONLIB_API int moonL_getsubtable (moon_State *L, int idx, const char *fname) {
  if (moon_getfield(L, idx, fname) == MOON_TTABLE)
    return 1;  // table already there
  else {
    moon_pop(L, 1);  // remove previous result
    idx = moon_absindex(L, idx);
    moon_newtable(L);
    moon_pushvalue(L, -1);  // copy to be left at top
    moon_setfield(L, idx, fname);  // assign new table to field
    return 0;  // false, because did not find table there
  }
}


/*
** Stripped-down 'require': After checking "loaded" table, calls 'openf'
** to open a module, registers the result in 'package.loaded' table and,
** if 'glb' is true, also registers the result in the global table.
** Leaves resulting module on the top.
*/
MOONLIB_API void moonL_requiref (moon_State *L, const char *modname,
                               moon_CFunction openf, int glb) {
  moonL_getsubtable(L, MOON_REGISTRYINDEX, MOON_LOADED_TABLE);
  moon_getfield(L, -1, modname);  // LOADED[modname]
  if (!moon_toboolean(L, -1)) {  // package not already loaded?
    moon_pop(L, 1);  // remove field
    moon_pushcfunction(L, openf);
    moon_pushstring(L, modname);  // argument to open function
    moon_call(L, 1, 1);  // call 'openf' to open module
    moon_pushvalue(L, -1);  // make copy of module (call result)
    moon_setfield(L, -3, modname);  // LOADED[modname] = module
  }
  moon_remove(L, -2);  // remove LOADED table
  if (glb) {
    moon_pushvalue(L, -1);  // copy of module
    moon_setglobal(L, modname);  // _G[modname] = module
  }
}


MOONLIB_API void moonL_addgsub (moonL_Buffer *b, const char *s,
                                     const char *p, const char *r) {
  const char *wild;
  size_t l = strlen(p);
  while ((wild = strstr(s, p)) != nullptr) {
    moonL_addlstring(b, s, ct_diff2sz(wild - s));  // push prefix
    moonL_addstring(b, r);  // push replacement in place of pattern
    s = wild + l;  // continue after 'p'
  }
  moonL_addstring(b, s);  // push last suffix
}


MOONLIB_API const char *moonL_gsub (moon_State *L, const char *s,
                                  const char *p, const char *r) {
  moonL_Buffer b;
  moonL_buffinit(L, &b);
  moonL_addgsub(&b, s, p, r);
  moonL_pushresult(&b);
  return moon_tostring(L, -1);
}


static void *l_alloc (void *ud, void *ptr, size_t osize, size_t nsize) {
  (void)ud; (void)osize;  // not used
  if (nsize == 0) {
    free(ptr);
    return nullptr;
  }
  else
    return realloc(ptr, nsize);
}


/*
** Standard panic function just prints an error message. The test
** with 'moon_type' avoids possible memory errors in 'moon_tostring'.
*/
static int panic (moon_State *L) {
  const char *msg = (moon_type(L, -1) == MOON_TSTRING)
                  ? moon_tostring(L, -1)
                  : "error object is not a string";
  moon_writestringerror("PANIC: unprotected error in call to Lua API (%s)\n",
                        msg);
  return 0;  // return to Lua to abort
}


/*
** Warning functions:
** warnfoff: warning system is off
** warnfon: ready to start a new message
** warnfcont: previous message is to be continued
*/
static void warnfoff (void *ud, const char *message, int tocont);
static void warnfon (void *ud, const char *message, int tocont);
static void warnfcont (void *ud, const char *message, int tocont);


/*
** Check whether message is a control message. If so, execute the
** control or ignore it if unknown.
*/
static int checkcontrol (moon_State *L, const char *message, int tocont) {
  if (tocont || *(message++) != '@')  // not a control message?
    return 0;
  else {
    if (strcmp(message, "off") == 0)
      moon_setwarnf(L, warnfoff, L);  // turn warnings off
    else if (strcmp(message, "on") == 0)
      moon_setwarnf(L, warnfon, L);  // turn warnings on
    return 1;  // it was a control message
  }
}


static void warnfoff (void *ud, const char *message, int tocont) {
  checkcontrol(static_cast<moon_State *>(ud), message, tocont);
}


/*
** Writes the message and handle 'tocont', finishing the message
** if needed and setting the next warn function.
*/
static void warnfcont (void *ud, const char *message, int tocont) {
  moon_State *L = static_cast<moon_State *>(ud);
  moon_writestringerror("%s", message);  // write message
  if (tocont)  // not the last part?
    moon_setwarnf(L, warnfcont, L);  // to be continued
  else {  // last part
    moon_writestringerror("%s", "\n");  // finish message with end-of-line
    moon_setwarnf(L, warnfon, L);  // next call is a new message
  }
}


static void warnfon (void *ud, const char *message, int tocont) {
  if (checkcontrol(static_cast<moon_State *>(ud), message, tocont))  // control message?
    return;  // nothing else to be done
  moon_writestringerror("%s", "Lua warning: ");  // start a new warning
  warnfcont(ud, message, tocont);  // finish processing
}



/*
** A function to compute an unsigned int with some level of
** randomness. Rely on Address Space Layout Randomization (if present)
** and the current time.
*/
#if !defined(mooni_makeseed)

#include <time.h>


// Size for the buffer, in bytes
inline constexpr size_t BUFSEEDB = sizeof(void*) + sizeof(time_t);

// Size for the buffer in int's, rounded up
inline constexpr size_t BUFSEED = (BUFSEEDB + sizeof(int) - 1) / sizeof(int);

/* Copy the contents of variable 'v' into the buffer pointed by 'b'.
** (The '&b[0]' disguises 'b' to fix an absurd warning from clang.)
*/
template<typename T>
inline void addbuff(char*& b, const T& v) noexcept {
	memcpy(&b[0], &v, sizeof(v));
	b += sizeof(v);
}


static unsigned int mooni_makeseed (void) {
  unsigned int buff[BUFSEED];
  unsigned int res;
  unsigned int i;
  time_t t = time(nullptr);
  char *b = reinterpret_cast<char*>(buff);
  addbuff(b, b);  // local variable's address
  addbuff(b, t);  // time
  // fill (rare but possible) remain of the buffer with zeros
  memset(b, 0, sizeof(buff) - BUFSEEDB);
  res = buff[0];
  for (i = 1; i < BUFSEED; i++)
    res ^= (res >> 3) + (res << 7) + buff[i];
  return res;
}

#endif


MOONLIB_API unsigned int moonL_makeseed (moon_State *L) {
  (void)L;  // unused
  return mooni_makeseed();
}


/*
** Use the name with parentheses so that headers can redefine it
** as a macro.
*/
MOONLIB_API moon_State *(moonL_newstate) (void) {
  moon_State *L = moon_newstate(l_alloc, nullptr, mooni_makeseed());
  if (l_likely(L)) {
    moon_atpanic(L, &panic);
    moon_setwarnf(L, warnfoff, L);  // default is warnings off
  }
  return L;
}


MOONLIB_API void moonL_checkversion_ (moon_State *L, moon_Number ver, size_t sz) {
  moon_Number v = moon_version(L);
  if (sz != MOONL_NUMSIZES)  // check numeric types
    moonL_error(L, "core and library have incompatible numeric types");
  else if (v != ver)
    moonL_error(L, "version mismatch: app. needs %f, Lua core provides %f",
                  (MOONI_UACNUMBER)ver, (MOONI_UACNUMBER)v);
}

