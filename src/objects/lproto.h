/*
** Function prototypes and debug information
** See Copyright Notice in lua.h
*/


#ifndef lproto_h
#define lproto_h


#include <span>

#include "llimits.h"
#include "lua.h"
#include "lobject_core.h"  /* GCBase, GCObject */

/* Forward declarations */
class TString;
class TValue;
typedef l_uint32 Instruction;


/*
** {==================================================================
** Prototypes
** ===================================================================
** Note: LUA_VPROTO now defined in ltvalue.h
*/


/*
** Description of an upvalue for function prototypes
*/
class Upvaldesc {
private:
  TString *name;  /* upvalue name (for debug information) */
  lu_byte instack;  /* whether it is in stack (register) */
  lu_byte idx;  /* index of upvalue (in stack or in outer function's list) */
  lu_byte kind;  /* kind of corresponding variable */

public:
  // Inline accessors
  TString* getName() const noexcept { return name; }
  TString** getNamePtr() noexcept { return &name; }  // For serialization
  bool isInStack() const noexcept { return instack != 0; }
  lu_byte getInStackRaw() const noexcept { return instack; }
  lu_byte getIndex() const noexcept { return idx; }
  lu_byte getKind() const noexcept { return kind; }

  // Inline setters
  void setName(TString* n) noexcept { name = n; }
  void setInStack(lu_byte val) noexcept { instack = val; }
  void setIndex(lu_byte i) noexcept { idx = i; }
  void setKind(lu_byte k) noexcept { kind = k; }
};


/*
** Description of a local variable for function prototypes
** (used for debug information)
*/
class LocVar {
private:
  TString *varname;
  int startpc;  /* first point where variable is active */
  int endpc;    /* first point where variable is dead */

public:
  // Inline accessors
  TString* getVarName() const noexcept { return varname; }
  TString** getVarNamePtr() noexcept { return &varname; }  // For serialization
  int getStartPC() const noexcept { return startpc; }
  int getEndPC() const noexcept { return endpc; }
  bool isActive(int pc) const noexcept { return startpc <= pc && pc < endpc; }

  // Inline setters
  void setVarName(TString* name) noexcept { varname = name; }
  void setStartPC(int pc) noexcept { startpc = pc; }
  void setEndPC(int pc) noexcept { endpc = pc; }
};


/*
** Associates the absolute line source for a given instruction ('pc').
** The array 'lineinfo' gives, for each instruction, the difference in
** lines from the previous instruction. When that difference does not
** fit into a byte, Lua saves the absolute line for that instruction.
** (Lua also saves the absolute line periodically, to speed up the
** computation of a line number: we can use binary search in the
** absolute-line array, but we must traverse the 'lineinfo' array
** linearly to compute a line.)
*/
class AbsLineInfo {
private:
  int pc;
  int line;

public:
  // Inline accessors
  int getPC() const noexcept { return pc; }
  int getLine() const noexcept { return line; }

  // Inline setters
  void setPC(int p) noexcept { pc = p; }
  void setLine(int l) noexcept { line = l; }
};


/*
** Flags in Prototypes
*/
inline constexpr lu_byte PF_ISVARARG = 1;
inline constexpr lu_byte PF_FIXED = 2;  /* prototype has parts in fixed memory */


/*
** Proto Subsystem - Debug information management
** Separates debug data from runtime execution data for better organization
*/
class ProtoDebugInfo {
private:
  /* Line information */
  ls_byte *lineinfo;            /* Map from opcodes to source lines */
  int sizelineinfo;
  AbsLineInfo *abslineinfo;     /* Absolute line info for faster lookup */
  int sizeabslineinfo;

  /* Local variable information */
  LocVar *locvars;              /* Local variable descriptors */
  int sizelocvars;

  /* Source location */
  int linedefined;              /* First line of function definition */
  int lastlinedefined;          /* Last line of function definition */
  TString *source;              /* Source file name */

public:
  /* Inline accessors */
  ls_byte* getLineInfo() const noexcept { return lineinfo; }
  int getLineInfoSize() const noexcept { return sizelineinfo; }
  AbsLineInfo* getAbsLineInfo() const noexcept { return abslineinfo; }
  int getAbsLineInfoSize() const noexcept { return sizeabslineinfo; }
  LocVar* getLocVars() const noexcept { return locvars; }
  int getLocVarsSize() const noexcept { return sizelocvars; }
  int getLineDefined() const noexcept { return linedefined; }
  int getLastLineDefined() const noexcept { return lastlinedefined; }
  TString* getSource() const noexcept { return source; }

  /* Inline setters */
  void setLineInfo(ls_byte* li) noexcept { lineinfo = li; }
  void setLineInfoSize(int s) noexcept { sizelineinfo = s; }
  void setAbsLineInfo(AbsLineInfo* ali) noexcept { abslineinfo = ali; }
  void setAbsLineInfoSize(int s) noexcept { sizeabslineinfo = s; }
  void setLocVars(LocVar* lv) noexcept { locvars = lv; }
  void setLocVarsSize(int s) noexcept { sizelocvars = s; }
  void setLineDefined(int l) noexcept { linedefined = l; }
  void setLastLineDefined(int l) noexcept { lastlinedefined = l; }
  void setSource(TString* s) noexcept { source = s; }

  /* Reference accessors for luaM_growvector */
  int& getLineInfoSizeRef() noexcept { return sizelineinfo; }
  int& getAbsLineInfoSizeRef() noexcept { return sizeabslineinfo; }
  int& getLocVarsSizeRef() noexcept { return sizelocvars; }
  ls_byte*& getLineInfoRef() noexcept { return lineinfo; }
  AbsLineInfo*& getAbsLineInfoRef() noexcept { return abslineinfo; }
  LocVar*& getLocVarsRef() noexcept { return locvars; }

  /* Pointer accessors */
  TString** getSourcePtr() noexcept { return &source; }

  /* Phase 112: std::span accessors for debug info arrays */
  std::span<ls_byte> getLineInfoSpan() noexcept {
    return std::span(lineinfo, static_cast<size_t>(sizelineinfo));
  }
  std::span<const ls_byte> getLineInfoSpan() const noexcept {
    return std::span(lineinfo, static_cast<size_t>(sizelineinfo));
  }

  std::span<AbsLineInfo> getAbsLineInfoSpan() noexcept {
    return std::span(abslineinfo, static_cast<size_t>(sizeabslineinfo));
  }
  std::span<const AbsLineInfo> getAbsLineInfoSpan() const noexcept {
    return std::span(abslineinfo, static_cast<size_t>(sizeabslineinfo));
  }

  std::span<LocVar> getLocVarsSpan() noexcept {
    return std::span(locvars, static_cast<size_t>(sizelocvars));
  }
  std::span<const LocVar> getLocVarsSpan() const noexcept {
    return std::span(locvars, static_cast<size_t>(sizelocvars));
  }
};


/*
** Function Prototypes
*/
// Proto inherits from GCBase (CRTP)
class Proto : public GCBase<Proto> {
private:
  /* Runtime data (always needed for execution) */
  lu_byte numparams;  /* number of fixed (named) parameters */
  lu_byte flag;
  lu_byte maxstacksize;  /* number of registers needed by this function */
  int sizeupvalues;  /* size of 'upvalues' */
  int sizek;  /* size of 'k' */
  int sizecode;
  int sizep;  /* size of 'p' */
  TValue *k;  /* constants used by the function */
  Instruction *code;  /* opcodes */
  Proto **p;  /* functions defined inside the function */
  Upvaldesc *upvalues;  /* upvalue information */
  GCObject *gclist;

  /* Debug subsystem (debug information) */
  ProtoDebugInfo debugInfo;

public:
  // Phase 50: Constructor - initializes all fields to safe defaults
  Proto() noexcept
    : numparams(0), flag(0), maxstacksize(0), sizeupvalues(0),
      sizek(0), sizecode(0), sizep(0), k(nullptr), code(nullptr),
      p(nullptr), upvalues(nullptr), gclist(nullptr), debugInfo() {
    // Initialize debug info subsystem
    debugInfo.setLineInfoSize(0);
    debugInfo.setAbsLineInfoSize(0);
    debugInfo.setLocVarsSize(0);
    debugInfo.setLineDefined(0);
    debugInfo.setLastLineDefined(0);
    debugInfo.setLineInfo(nullptr);
    debugInfo.setAbsLineInfo(nullptr);
    debugInfo.setLocVars(nullptr);
    debugInfo.setSource(nullptr);
  }

  // Phase 50: Destructor - trivial (GC calls free() method explicitly)
  ~Proto() noexcept = default;

  // Phase 50: Placement new operator - integrates with Lua's GC (implemented in lgc.h)
  static void* operator new(size_t size, lua_State* L, LuaT tt);

  // Disable regular new/delete (must use placement new with GC)
  static void* operator new(size_t) = delete;
  static void operator delete(void*) = delete;

  /* Subsystem access (for direct debug info manipulation) */
  ProtoDebugInfo& getDebugInfo() noexcept { return debugInfo; }
  const ProtoDebugInfo& getDebugInfo() const noexcept { return debugInfo; }

  /* Runtime data accessors */
  lu_byte getNumParams() const noexcept { return numparams; }
  lu_byte getFlag() const noexcept { return flag; }
  lu_byte getMaxStackSize() const noexcept { return maxstacksize; }
  int getCodeSize() const noexcept { return sizecode; }
  int getConstantsSize() const noexcept { return sizek; }
  int getUpvaluesSize() const noexcept { return sizeupvalues; }
  int getProtosSize() const noexcept { return sizep; }
  bool isVarArg() const noexcept { return flag != 0; }
  Instruction* getCode() const noexcept { return code; }
  TValue* getConstants() const noexcept { return k; }

  /* Phase 112: std::span accessors for arrays */
  std::span<Instruction> getCodeSpan() noexcept {
    return std::span(code, static_cast<size_t>(sizecode));
  }
  std::span<const Instruction> getCodeSpan() const noexcept {
    return std::span(code, static_cast<size_t>(sizecode));
  }

  std::span<TValue> getConstantsSpan() noexcept {
    return std::span(k, static_cast<size_t>(sizek));
  }
  std::span<const TValue> getConstantsSpan() const noexcept {
    return std::span(k, static_cast<size_t>(sizek));
  }

  std::span<Proto*> getProtosSpan() noexcept {
    return std::span(p, static_cast<size_t>(sizep));
  }
  std::span<Proto* const> getProtosSpan() const noexcept {
    return std::span(p, static_cast<size_t>(sizep));
  }

  std::span<Upvaldesc> getUpvaluesSpan() noexcept {
    return std::span(upvalues, static_cast<size_t>(sizeupvalues));
  }
  std::span<const Upvaldesc> getUpvaluesSpan() const noexcept {
    return std::span(upvalues, static_cast<size_t>(sizeupvalues));
  }

  Proto** getProtos() const noexcept { return p; }
  Upvaldesc* getUpvalues() const noexcept { return upvalues; }
  GCObject* getGclist() const noexcept { return gclist; }

  /* Delegating accessors for ProtoDebugInfo */
  int getLineInfoSize() const noexcept { return debugInfo.getLineInfoSize(); }
  int getLocVarsSize() const noexcept { return debugInfo.getLocVarsSize(); }
  int getAbsLineInfoSize() const noexcept { return debugInfo.getAbsLineInfoSize(); }
  int getLineDefined() const noexcept { return debugInfo.getLineDefined(); }
  int getLastLineDefined() const noexcept { return debugInfo.getLastLineDefined(); }
  TString* getSource() const noexcept { return debugInfo.getSource(); }
  ls_byte* getLineInfo() const noexcept { return debugInfo.getLineInfo(); }
  AbsLineInfo* getAbsLineInfo() const noexcept { return debugInfo.getAbsLineInfo(); }
  LocVar* getLocVars() const noexcept { return debugInfo.getLocVars(); }

  /* Runtime data setters */
  void setNumParams(lu_byte n) noexcept { numparams = n; }
  void setFlag(lu_byte f) noexcept { flag = f; }
  void setMaxStackSize(lu_byte s) noexcept { maxstacksize = s; }
  void setCodeSize(int s) noexcept { sizecode = s; }
  void setConstantsSize(int s) noexcept { sizek = s; }
  void setUpvaluesSize(int s) noexcept { sizeupvalues = s; }
  void setProtosSize(int s) noexcept { sizep = s; }
  void setCode(Instruction* c) noexcept { code = c; }
  void setConstants(TValue* constants) noexcept { k = constants; }
  void setProtos(Proto** protos) noexcept { p = protos; }
  void setUpvalues(Upvaldesc* uv) noexcept { upvalues = uv; }
  void setGclist(GCObject* gc) noexcept { gclist = gc; }

  /* Delegating setters for ProtoDebugInfo */
  void setLineInfoSize(int s) noexcept { debugInfo.setLineInfoSize(s); }
  void setLocVarsSize(int s) noexcept { debugInfo.setLocVarsSize(s); }
  void setAbsLineInfoSize(int s) noexcept { debugInfo.setAbsLineInfoSize(s); }
  void setLineDefined(int l) noexcept { debugInfo.setLineDefined(l); }
  void setLastLineDefined(int l) noexcept { debugInfo.setLastLineDefined(l); }
  void setSource(TString* s) noexcept { debugInfo.setSource(s); }
  void setLineInfo(ls_byte* li) noexcept { debugInfo.setLineInfo(li); }
  void setAbsLineInfo(AbsLineInfo* ali) noexcept { debugInfo.setAbsLineInfo(ali); }
  void setLocVars(LocVar* lv) noexcept { debugInfo.setLocVars(lv); }

  /* Pointer accessors for serialization and GC */
  TString** getSourcePtr() noexcept { return debugInfo.getSourcePtr(); }
  GCObject** getGclistPtr() noexcept { return &gclist; }

  /* Runtime data reference accessors for luaM_growvector */
  int& getCodeSizeRef() noexcept { return sizecode; }
  int& getConstantsSizeRef() noexcept { return sizek; }
  int& getUpvaluesSizeRef() noexcept { return sizeupvalues; }
  int& getProtosSizeRef() noexcept { return sizep; }

  Instruction*& getCodeRef() noexcept { return code; }
  TValue*& getConstantsRef() noexcept { return k; }
  Proto**& getProtosRef() noexcept { return p; }
  Upvaldesc*& getUpvaluesRef() noexcept { return upvalues; }

  /* Delegating reference accessors for ProtoDebugInfo */
  int& getLineInfoSizeRef() noexcept { return debugInfo.getLineInfoSizeRef(); }
  int& getLocVarsSizeRef() noexcept { return debugInfo.getLocVarsSizeRef(); }
  int& getAbsLineInfoSizeRef() noexcept { return debugInfo.getAbsLineInfoSizeRef(); }
  ls_byte*& getLineInfoRef() noexcept { return debugInfo.getLineInfoRef(); }
  AbsLineInfo*& getAbsLineInfoRef() noexcept { return debugInfo.getAbsLineInfoRef(); }
  LocVar*& getLocVarsRef() noexcept { return debugInfo.getLocVarsRef(); }

  // Phase 44.5: Additional Proto helper methods

  // Get relative PC for debug info
  int getPCRelative(const Instruction* pc) const noexcept {
    return cast_int(pc - code) - 1;
  }

  // Methods (implemented in lfunc.cpp)
  lu_mem memorySize() const;
  void free(lua_State* L);
  const char* getLocalName(int local_number, int pc) const;
};

/* }================================================================== */


#endif
