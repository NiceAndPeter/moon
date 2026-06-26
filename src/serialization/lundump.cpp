/*
** load precompiled Lua chunks
** See Copyright Notice in lua.h
*/

#define LUA_CORE

#include "lprefix.h"


#include <algorithm>
#include <climits>
#include <cstring>

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstring.h"
#include "ltable.h"
#include "lundump.h"
#include "lzio.h"
#include "../memory/lgc.h"


#if !defined(luai_verifycode)
#define luai_verifycode(L,f)  // empty
#endif


typedef struct {
  lua_State *L;
  ZIO *Z;
  const char *name;
  Table *h;  // list for string reuse
  size_t offset;  // current position relative to beginning of dump
  lua_Unsigned nstr;  // number of strings in the list
  lu_byte fixed;  // dump is fixed in memory
} LoadState;


static l_noret error (LoadState *S, const char *why) {
  luaO_pushfstring(S->L, "%s: bad binary format (%s)", S->name, why);
  S->L->doThrow( LUA_ERRSYNTAX);
}


/*
** All high-level loads go through loadVector; you can change it to
** adapt to the endianness of the input
*/
template<typename T>
inline void loadVector(LoadState* S, T* b, size_t n) {
	loadBlock(S, b, cast_sizet(n) * sizeof(T));
}

static void loadBlock (LoadState *S, void *b, size_t size) {
  if (luaZ_read(S->Z, b, size) != 0)
    error(S, "truncated chunk");
  S->offset += size;
}


static void loadAlign (LoadState *S, unsigned align) {
  unsigned padding = align - cast_uint(S->offset % align);
  if (padding < align) {  // (padding == align) means no padding
    lua_Integer paddingContent;
    loadBlock(S, &paddingContent, padding);
    lua_assert(S->offset % align == 0);
  }
}


static const void *getaddr_ (LoadState *S, size_t size) {
  const void *block = luaZ_getaddr(S->Z, size);
  S->offset += size;
  if (block == nullptr)
    error(S, "truncated fixed buffer");
  return block;
}

/* Note: Returns non-const pointer for compatibility with existing code
** that stores pointers to fixed buffer data. The buffer is read-only but
** the type system doesn't enforce this for historical reasons.
*/
template<typename T>
inline T* getaddr(LoadState* S, size_t n) {
	return const_cast<T*>(cast(const T*, getaddr_(S, cast_sizet(n * sizeof(T)))));
}


template<typename T>
inline void loadVar(LoadState* S, T& x) {
	loadVector(S, &x, 1);
}


static lu_byte loadByte (LoadState *S) {
  int b = zgetc(S->Z);
  if (b == EOZ)
    error(S, "truncated chunk");
  S->offset++;
  return cast_byte(b);
}


static lua_Unsigned loadVarint (LoadState *S, lua_Unsigned limit) {
  lua_Unsigned x = 0;
  int b;
  limit >>= 7;
  do {
    b = loadByte(S);
    if (x > limit)
      error(S, "integer overflow");
    x = (x << 7) | (b & 0x7f);
  } while ((b & 0x80) != 0);
  return x;
}


static size_t loadSize (LoadState *S) {
  return cast_sizet(loadVarint(S, MAX_SIZE));
}


static int loadInt (LoadState *S) {
  return cast_int(loadVarint(S, cast_sizet(std::numeric_limits<int>::max())));
}



static lua_Number loadNumber (LoadState *S) {
  lua_Number x;
  loadVar(S, x);
  return x;
}


static lua_Integer loadInteger (LoadState *S) {
  lua_Unsigned cx = loadVarint(S, LUA_MAXUNSIGNED);
  // decode unsigned to signed
  if ((cx & 1) != 0)
    return l_castU2S(~(cx >> 1));
  else
    return l_castU2S(cx >> 1);
}


/*
** Load a nullable string into slot 'sl' from prototype 'p'. The
** assignment to the slot and the barrier must be performed before any
** possible GC activity, to anchor the string. (Both 'loadVector' and
** 'luaH_setint' can call the GC.)
*/
static void loadString (LoadState *S, Proto& p, TString **sl) {
  lua_State *L = S->L;
  TString *ts;
  TValue sv;
  size_t size = loadSize(S);
  if (size == 0) {  // no string?
    lua_assert(*sl == nullptr);  // must be prefilled
    return;
  }
  else if (size == 1) {  // previously saved string?
    lua_Unsigned idx = loadVarint(S, LUA_MAXUNSIGNED);  // get its index
    TValue stv;
    if (novariant(S->h->getInt(l_castU2S(idx), &stv)) != LUA_TSTRING)
      error(S, "invalid string index");
    *sl = ts = tsvalue(&stv);  /* get its value */
    luaC_objbarrier(L, &p, ts);
    return;  // do not save it again
  }
  else if ((size -= 2) <= LUAI_MAXSHORTLEN) {  // short string?
    char buff[LUAI_MAXSHORTLEN + 1];  // extra space for '\0'
    loadVector(S, buff, size + 1);  // load string into buffer
    *sl = ts = TString::create(L, buff, size);  /* create string */
    luaC_objbarrier(L, &p, ts);
  }
  else if (S->fixed) {  // for a fixed buffer, use a fixed string
    const char *s = getaddr<char>(S, size + 1);  // get content address
    *sl = ts = TString::createExternal(L, s, size, nullptr, nullptr);
    luaC_objbarrier(L, &p, ts);
  }
  else {  // create internal copy
    *sl = ts = TString::createLongString(L, size);  /* create string */
    luaC_objbarrier(L, &p, ts);
    loadVector(S, getLongStringContents(ts), size + 1);  // load directly in final place
  }
  // add string to list of saved strings
  S->nstr++;
  setsvalue(L, &sv, ts);
  S->h->setInt(L, l_castU2S(S->nstr), &sv);
  luaC_objbarrierback(L, obj2gco(S->h), ts);
}


// Use span accessors
static void loadCode (LoadState *S, Proto& f) {
  int n = loadInt(S);
  loadAlign(S, sizeof(Instruction));
  if (S->fixed) {
    f.setCode(getaddr<Instruction>(S, n));
    f.setCodeSize(n);
  }
  else {
    f.setCode(luaM_newvectorchecked<Instruction>(S->L, n));
    f.setCodeSize(n);
    auto codeSpan = f.getCodeSpan();  // Get span after allocation
    loadVector(S, codeSpan.data(), codeSpan.size());
  }
}


static void loadFunction(LoadState *S, Proto& f);


// Use span accessors
static void loadConstants (LoadState *S, Proto& f) {
  int n = loadInt(S);
  f.setConstants(luaM_newvectorchecked<TValue>(S->L, n));
  f.setConstantsSize(n);
  auto constantsSpan = f.getConstantsSpan();
  for (TValue& v : constantsSpan) {
    setnilvalue(&v);
  }
  for (TValue& o_ref : constantsSpan) {
    TValue *o = &o_ref;
    LuaT t = static_cast<LuaT>(loadByte(S));
    switch (t) {
      case LuaT::NIL:
        setnilvalue(o);
        break;
      case LuaT::VFALSE:
        setbfvalue(o);
        break;
      case LuaT::VTRUE:
        setbtvalue(o);
        break;
      case LuaT::NUMFLT:
        o->setFloat(loadNumber(S));
        break;
      case LuaT::NUMINT:
        o->setInt(loadInteger(S));
        break;
      case LuaT::SHRSTR:
      case LuaT::LNGSTR: {
        lua_assert(f.getSource() == nullptr);
        loadString(S, f, f.getSourcePtr());  // use 'source' to anchor string
        if (f.getSource() == nullptr)
          error(S, "bad format for constant string");
        setsvalue2n(S->L, o, f.getSource());  // save it in the right place
        f.setSource(nullptr);
        break;
      }
      default: error(S, "invalid constant");
    }
  }
}


static void loadProtos (LoadState *S, Proto& f) {
  int i;
  int n = loadInt(S);
  f.setProtos(luaM_newvectorchecked<Proto*>(S->L, n));
  f.setProtosSize(n);
  std::fill_n(f.getProtos(), n, nullptr);
  for (i = 0; i < n; i++) {
    f.getProtos()[i] = luaF_newproto(S->L);
    luaC_objbarrier(S->L, &f, f.getProtos()[i]);
    loadFunction(S, *f.getProtos()[i]);
  }
}


/*
** Load the upvalues for a function. The names must be filled first,
** because the filling of the other fields can raise read errors and
** the creation of the error message can call an emergency collection;
** in that case all prototypes must be consistent for the GC.
*/
// Use span accessors
static void loadUpvalues (LoadState *S, Proto& f) {
  int n = loadInt(S);
  f.setUpvalues(luaM_newvectorchecked<Upvaldesc>(S->L, n));
  f.setUpvaluesSize(n);
  auto upvaluesSpan = f.getUpvaluesSpan();
  // make array valid for GC
  for (Upvaldesc& uv : upvaluesSpan) {
    uv.setName(nullptr);
  }
  for (Upvaldesc& uv : upvaluesSpan) {  // following calls can raise errors
    uv.setInStack(loadByte(S));
    uv.setIndex(loadByte(S));
    uv.setKind(loadByte(S));
  }
}


// Use span accessors
static void loadDebug (LoadState *S, Proto& f) {
  int n = loadInt(S);
  if (S->fixed) {
    f.setLineInfo(getaddr<ls_byte>(S, n));
    f.setLineInfoSize(n);
  }
  else {
    f.setLineInfo(luaM_newvectorchecked<ls_byte>(S->L, n));
    f.setLineInfoSize(n);
    auto lineInfoSpan = f.getDebugInfo().getLineInfoSpan();
    loadVector(S, lineInfoSpan.data(), lineInfoSpan.size());
  }
  n = loadInt(S);
  if (n > 0) {
    loadAlign(S, sizeof(int));
    if (S->fixed) {
      f.setAbsLineInfo(getaddr<AbsLineInfo>(S, n));
      f.setAbsLineInfoSize(n);
    }
    else {
      f.setAbsLineInfo(luaM_newvectorchecked<AbsLineInfo>(S->L, n));
      f.setAbsLineInfoSize(n);
      auto absLineInfoSpan = f.getDebugInfo().getAbsLineInfoSpan();
      loadVector(S, absLineInfoSpan.data(), absLineInfoSpan.size());
    }
  }
  n = loadInt(S);
  f.setLocVars(luaM_newvectorchecked<LocVar>(S->L, n));
  f.setLocVarsSize(n);
  auto locVarsSpan = f.getDebugInfo().getLocVarsSpan();
  for (LocVar& lv : locVarsSpan) {
    lv.setVarName(nullptr);
  }
  for (LocVar& lv : locVarsSpan) {
    loadString(S, f, lv.getVarNamePtr());
    lv.setStartPC(loadInt(S));
    lv.setEndPC(loadInt(S));
  }
  n = loadInt(S);
  if (n != 0) {  // does it have debug information?
    n = f.getUpvaluesSize();  // must be this many
    auto upvaluesSpan = f.getUpvaluesSpan();
    for (Upvaldesc& uv : upvaluesSpan)
      loadString(S, f, uv.getNamePtr());
  }
}


static void loadFunction (LoadState *S, Proto& f) {
  f.setLineDefined(loadInt(S));
  f.setLastLineDefined(loadInt(S));
  f.setNumParams(loadByte(S));
  f.setFlag(loadByte(S) & PF_ISVARARG);  // get only the meaningful flags
  if (S->fixed)
    f.setFlag(f.getFlag() | PF_FIXED);  // signal that code is fixed
  f.setMaxStackSize(loadByte(S));
  loadCode(S, f);
  loadConstants(S, f);
  loadUpvalues(S, f);
  loadProtos(S, f);
  loadString(S, f, f.getSourcePtr());
  loadDebug(S, f);
}


static void checkliteral (LoadState *S, const char *s, const char *msg) {
  char buff[sizeof(LUA_SIGNATURE) + sizeof(LUAC_DATA)];  // larger than both
  size_t len = strlen(s);
  loadVector(S, buff, len);
  if (memcmp(s, buff, len) != 0)
    error(S, msg);
}


static l_noret numerror (LoadState *S, const char *what, const char *tname) {
  const char *msg = luaO_pushfstring(S->L, "%s %s mismatch", tname, what);
  error(S, msg);
}


static void checknumsize (LoadState *S, int size, const char *tname) {
  if (size != loadByte(S))
    numerror(S, "size", tname);
}


static void checknumformat (LoadState *S, int eq, const char *tname) {
  if (!eq)
    numerror(S, "format", tname);
}


#define checknum(S,tvar,value,tname)  \
  { tvar i; checknumsize(S, sizeof(i), tname); \
    loadVar(S, i); \
    checknumformat(S, i == value, tname); }


static void checkHeader (LoadState *S) {
  // skip 1st char (already read and checked)
  checkliteral(S, &LUA_SIGNATURE[1], "not a binary chunk");
  if (loadByte(S) != LUAC_VERSION)
    error(S, "version mismatch");
  if (loadByte(S) != LUAC_FORMAT)
    error(S, "format mismatch");
  checkliteral(S, LUAC_DATA, "corrupted chunk");
  checknum(S, int, LUAC_INT, "int");
  checknum(S, Instruction, LUAC_INST, "instruction");
  checknum(S, lua_Integer, LUAC_INT, "Lua integer");
  checknum(S, lua_Number, LUAC_NUM, "Lua number");
}


/*
** Load precompiled chunk.
*/
LClosure *luaU_undump (lua_State *L, ZIO *Z, const char *name, int fixed) {
  LoadState S;
  LClosure *cl;
  if (*name == '@' || *name == '=')
    name = name + 1;
  else if (*name == LUA_SIGNATURE[0])
    name = "binary string";
  S.name = name;
  S.L = L;
  S.Z = Z;
  S.fixed = cast_byte(fixed);
  S.offset = 1;  // fist byte was already read
  checkHeader(&S);
  cl = LClosure::create(L, loadByte(&S));
  setclLvalue2s(L, L->getTop().p, cl);
  L->inctop();
  S.h = Table::create(L);  // create list of saved strings
  S.nstr = 0;
  sethvalue2s(L, L->getTop().p, S.h);  // anchor it
  L->inctop();
  cl->setProto(luaF_newproto(L));
  luaC_objbarrier(L, cl, cl->getProto());
  loadFunction(&S, *cl->getProto());
  if (cl->getNumUpvalues() != cl->getProto()->getUpvaluesSize())
    error(&S, "corrupted chunk");
  luai_verifycode(L, cl->getProto());
  L->getStackSubsystem().pop();  // pop table
  return cl;
}

