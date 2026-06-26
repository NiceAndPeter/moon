/*
** save precompiled Lua chunks
** See Copyright Notice in lua.h
*/

#define LUA_CORE

#include "lprefix.h"


#include <climits>
#include <cstddef>
#include <span>

#include "lua.h"

#include "lapi.h"
#include "lgc.h"
#include "lobject.h"
#include "lstate.h"
#include "ltable.h"
#include "lundump.h"


typedef struct {
  lua_State *L;
  lua_Writer writer;
  void *data;
  size_t offset;  // current position relative to beginning of dump
  int strip;
  int status;
  Table *h;  // table to track saved strings
  lua_Unsigned nstr;  // counter for counting saved strings
} DumpState;


/*
** All high-level dumps go through dumpVector; you can change it to
** change the endianness of the result
*/
template<typename T>
inline void dumpVector(DumpState* D, const T* v, size_t n) {
	dumpBlock(D, v, n * sizeof(T));
}

#define dumpLiteral(D, s)	dumpBlock(D,s,sizeof(s) - sizeof(char))


/*
** Dump the block of memory pointed by 'b' with given 'size'.
** 'b' should not be nullptr, except for the last call signaling the end
** of the dump.
*/
static void dumpBlock (DumpState *D, const void *b, size_t size) {
  if (D->status == 0) {  // do not write anything after an error
    lua_unlock(D->L);
    D->status = (*D->writer)(D->L, b, size, D->data);
    lua_lock(D->L);
    D->offset += size;
  }
}


/*
** Dump enough zeros to ensure that current position is a multiple of
** 'align'.
*/
static void dumpAlign (DumpState *D, unsigned align) {
  unsigned padding = align - cast_uint(D->offset % align);
  if (padding < align) {  // padding == align means no padding
    static lua_Integer paddingContent = 0;
    lua_assert(align <= sizeof(lua_Integer));
    dumpBlock(D, &paddingContent, padding);
  }
  lua_assert(D->offset % align == 0);
}


template<typename T>
inline void dumpVar(DumpState* D, const T& x) {
	dumpVector(D, &x, 1);
}


static void dumpByte (DumpState *D, int y) {
  lu_byte x = (lu_byte)y;
  dumpVar(D, x);
}


/*
** size for 'dumpVarint' buffer: each byte can store up to 7 bits.
** (The "+6" rounds up the division.)
*/
inline constexpr int DIBS = (l_numbits<lua_Unsigned>() + 6) / 7;

/*
** Dumps an unsigned integer using the MSB Varint encoding
*/
static void dumpVarint (DumpState *D, lua_Unsigned x) {
  lu_byte buff[DIBS];
  unsigned n = 1;
  buff[DIBS - 1] = x & 0x7f;  // fill least-significant byte
  while ((x >>= 7) != 0)  // fill other bytes in reverse order
    buff[DIBS - (++n)] = cast_byte((x & 0x7f) | 0x80);
  dumpVector(D, buff + DIBS - n, n);
}


static void dumpSize (DumpState *D, size_t sz) {
  dumpVarint(D, static_cast<lua_Unsigned>(sz));
}


static void dumpInt (DumpState *D, int x) {
  lua_assert(x >= 0);
  dumpVarint(D, cast_uint(x));
}


static void dumpNumber (DumpState *D, lua_Number x) {
  dumpVar(D, x);
}


/*
** Signed integers are coded to keep small values small. (Coding -1 as
** 0xfff...fff would use too many bytes to save a quite common value.)
** A non-negative x is coded as 2x; a negative x is coded as -2x - 1.
** (0 => 0; -1 => 1; 1 => 2; -2 => 3; 2 => 4; ...)
*/
static void dumpInteger (DumpState *D, lua_Integer x) {
  lua_Unsigned cx = (x >= 0) ? 2u * l_castS2U(x)
                             : (2u * ~l_castS2U(x)) + 1;
  dumpVarint(D, cx);
}


/*
** Dump a String. First dump its "size": size==0 means nullptr;
** size==1 is followed by an index and means "reuse saved string with
** that index"; size>=2 is followed by the string contents with real
** size==size-2 and means that string, which will be saved with
** the next available index.
*/
static void dumpString (DumpState *D, TString *tstring) {
  if (tstring == nullptr)
    dumpSize(D, 0);
  else {
    TValue idx;
    LuaT tag = D->h->getStr(tstring, &idx);
    if (!tagisempty(tag)) {  // string already saved?
      dumpVarint(D, 1);  // reuse a saved string
      dumpVarint(D, l_castS2U(ivalue(&idx)));  // index of saved string
    }
    else {  // must write and save the string
      TValue key, value;  // to save the string in the hash
      size_t size;
      const char *s = getStringWithLength(tstring, size);
      dumpSize(D, size + 2);
      dumpVector(D, s, size + 1);  // include ending '\0'
      D->nstr++;  // one more saved string
      setsvalue(D->L, &key, tstring);  // the string is the key
      value.setInt(l_castU2S(D->nstr));  // its index is the value
      D->h->set(D->L, &key, &value);  // h[tstring] = nstr
      // integer value does not need barrier
    }
  }
}


static void dumpCode (DumpState *D, const Proto& f) {
  auto code = f.getCodeSpan();
  dumpInt(D, static_cast<int>(code.size()));
  dumpAlign(D, sizeof(code[0]));
  lua_assert(code.data() != nullptr);
  dumpVector(D, code.data(), cast_uint(code.size()));
}


static void dumpFunction (DumpState *D, const Proto& f);

static void dumpConstants (DumpState *D, const Proto& f) {
  auto constants = f.getConstantsSpan();
  dumpInt(D, static_cast<int>(constants.size()));
  for (const auto& constant : constants) {
    LuaT tt = ttypetag(&constant);
    dumpByte(D, static_cast<lu_byte>(tt));
    switch (tt) {
      case LuaT::NUMFLT:
        dumpNumber(D, fltvalue(&constant));
        break;
      case LuaT::NUMINT:
        dumpInteger(D, ivalue(&constant));
        break;
      case LuaT::SHRSTR:
      case LuaT::LNGSTR:
        dumpString(D, tsvalue(&constant));
        break;
      default:
        lua_assert(tt == LuaT::NIL || tt == LuaT::VFALSE || tt == LuaT::VTRUE);
    }
  }
}


static void dumpProtos (DumpState *D, const Proto& f) {
  auto protos = f.getProtosSpan();
  dumpInt(D, static_cast<int>(protos.size()));
  for (Proto* proto : protos) {
    dumpFunction(D, *proto);
  }
}


static void dumpUpvalues (DumpState *D, const Proto& f) {
  auto upvalues = f.getUpvaluesSpan();
  dumpInt(D, static_cast<int>(upvalues.size()));
  for (const auto& upvalue : upvalues) {
    dumpByte(D, upvalue.getInStackRaw());
    dumpByte(D, upvalue.getIndex());
    dumpByte(D, upvalue.getKind());
  }
}


static void dumpDebug (DumpState *D, const Proto& f) {
  auto lineinfo = f.getDebugInfo().getLineInfoSpan();
  int n = (D->strip) ? 0 : static_cast<int>(lineinfo.size());
  dumpInt(D, n);
  if (lineinfo.data() != nullptr)
    dumpVector(D, lineinfo.data(), cast_uint(n));
  auto abslineinfo = f.getDebugInfo().getAbsLineInfoSpan();
  n = (D->strip) ? 0 : static_cast<int>(abslineinfo.size());
  dumpInt(D, n);
  if (n > 0) {
    // 'abslineinfo' is an array of structures of int's
    dumpAlign(D, sizeof(int));
    dumpVector(D, abslineinfo.data(), cast_uint(n));
  }
  auto locvars = f.getDebugInfo().getLocVarsSpan();
  n = (D->strip) ? 0 : static_cast<int>(locvars.size());
  dumpInt(D, n);
  for (const auto& lv : locvars.subspan(0, static_cast<size_t>(n))) {
    dumpString(D, lv.getVarName());
    dumpInt(D, lv.getStartPC());
    dumpInt(D, lv.getEndPC());
  }
  auto upvalues = f.getUpvaluesSpan();
  n = (D->strip) ? 0 : static_cast<int>(upvalues.size());
  dumpInt(D, n);
  for (const auto& upvalue : upvalues.subspan(0, static_cast<size_t>(n))) {
    dumpString(D, upvalue.getName());
  }
}


static void dumpFunction (DumpState *D, const Proto& f) {
  dumpInt(D, f.getLineDefined());
  dumpInt(D, f.getLastLineDefined());
  dumpByte(D, f.getNumParams());
  dumpByte(D, f.getFlag());
  dumpByte(D, f.getMaxStackSize());
  dumpCode(D, f);
  dumpConstants(D, f);
  dumpUpvalues(D, f);
  dumpProtos(D, f);
  dumpString(D, D->strip ? nullptr : f.getSource());
  dumpDebug(D, f);
}


#define dumpNumInfo(D, tvar, value)  \
  { tvar i = value; dumpByte(D, sizeof(tvar)); dumpVar(D, i); }


static void dumpHeader (DumpState *D) {
  dumpLiteral(D, LUA_SIGNATURE);
  dumpByte(D, LUAC_VERSION);
  dumpByte(D, LUAC_FORMAT);
  dumpLiteral(D, LUAC_DATA);
  dumpNumInfo(D, int, LUAC_INT);
  dumpNumInfo(D, Instruction, LUAC_INST);
  dumpNumInfo(D, lua_Integer, LUAC_INT);
  dumpNumInfo(D, lua_Number, LUAC_NUM);
}


/*
** dump Lua function as precompiled chunk
*/
int luaU_dump (lua_State *L, const Proto *f, lua_Writer w, void *data,
               int strip) {
  DumpState D;
  D.h = Table::create(L);  // aux. table to keep strings already dumped
  sethvalue2s(L, L->getTop().p, D.h);  // anchor it
  L->getStackSubsystem().push();
  D.L = L;
  D.writer = w;
  D.offset = 0;
  D.data = data;
  D.strip = strip;
  D.status = 0;
  D.nstr = 0;
  dumpHeader(&D);
  dumpByte(&D, f->getUpvaluesSize());
  dumpFunction(&D, *f);
  dumpBlock(&D, nullptr, 0);  // signal end of dump
  return D.status;
}

