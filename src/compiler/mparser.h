/*
** Lua Parser
** See Copyright Notice in lua.h
*/

#ifndef lparser_h
#define lparser_h

#include <span>
#include "mlimits.h"
#include "mobject.h"
#include "mopcodes.h"
#include "mtm.h"
#include "mzio.h"
#include "mlex.h"
#include "../memory/MoonVector.h"

/*
** Expression and variable descriptor.
** Code generation for variables and expressions can be delayed to allow
** optimizations; An 'ExpDesc' structure describes a potentially-delayed
** variable/expression. It has a description of its "main" value plus a
** list of conditional jumps that can also produce its value (generated
** by short-circuit operators 'and'/'or').
*/

// kinds of variables/expressions
typedef enum {
  VVOID,  /* when 'ExpDesc' describes the last expression of a list,
             this kind means an empty list (so, no expression) */
  VNIL,  // constant nil
  VTRUE,  // constant true
  VFALSE,  // constant false
  VK,  // constant in 'k'; info = index of constant in 'k'
  VKFLT,  // floating constant; nval = numerical float value
  VKINT,  // integer constant; ival = numerical integer value
  VKSTR,  /* string constant; strval = TString address;
             (string is fixed by the scanner) */
  VNONRELOC,  /* expression has its value in a fixed register;
                 info = result register */
  VLOCAL,  /* local variable; var.ridx = register index;
              var.vidx = relative index in 'actvar.arr'  */
  VGLOBAL,  /* global variable;
               info = relative index in 'actvar.arr' (or -1 for
                      implicit declaration) */
  VUPVAL,  // upvalue variable; info = index of upvalue in 'upvalues'
  VCONST,  /* compile-time <const> variable;
              info = absolute index in 'actvar.arr'  */
  VINDEXED,  /* indexed variable;
                ind.t = table register;
                ind.idx = key's R index;
                ind.ro = true if it represents a read-only global;
                ind.keystr = if key is a string, index in 'k' of that string;
                             -1 if key is not a string */
  VINDEXUP,  /* indexed upvalue;
                ind.idx = key's K index;
                ind.* as in VINDEXED */
  VINDEXI, /* indexed variable with constant integer;
                ind.t = table register;
                ind.idx = key's value */
  VINDEXSTR, /* indexed variable with literal string;
                ind.idx = key's K index;
                ind.* as in VINDEXED */
  VJMP,  /* expression is a test/comparison;
            info = pc of corresponding jump instruction */
  VRELOC,  /* expression can put result in any register;
              info = instruction pc */
  VCALL,  // expression is a function call; info = instruction pc
  VVARARG  // vararg expression; info = instruction pc
} expkind;


class ExpDesc {
private:
  expkind k;
  union {
    moon_Integer integerValue;  // for VKINT
    moon_Number floatValue;  // for VKFLT
    TString *stringValue;  // for VKSTR
    int info;  // for generic use
    struct {  // for indexed variables
      short keyIndex;  // index (R or "long" K)
      lu_byte tableRegister;  // table (register or upvalue)
      lu_byte isReadOnly;  // true if variable is read-only
      int stringKeyIndex;  // index in 'k' of string key, or -1 if not a string
    } ind;
    struct {  // for local variables
      lu_byte registerIndex;  // register holding the variable
      short variableIndex;  // index in 'actvar.arr'
    } var;
  } u;
  int trueJumpList;  // patch list of 'exit when true'
  int falseJumpList;  // patch list of 'exit when false'

public:
  // Inline accessors
  expkind getKind() const noexcept { return k; }
  void setKind(expkind kind) noexcept { k = kind; }
  bool isConstant() const noexcept { return k == VNIL || k == VFALSE || k == VTRUE || k == VKINT || k == VKFLT || k == VKSTR; }

  // Union field accessors (generic/constant values)
  int getInfo() const noexcept { return u.info; }
  void setInfo(int i) noexcept { u.info = i; }
  moon_Integer getIntValue() const noexcept { return u.integerValue; }
  void setIntValue(moon_Integer i) noexcept { u.integerValue = i; }
  moon_Number getFloatValue() const noexcept { return u.floatValue; }
  void setFloatValue(moon_Number n) noexcept { u.floatValue = n; }
  TString* getStringValue() const noexcept { return u.stringValue; }
  void setStringValue(TString* s) noexcept { u.stringValue = s; }

  // Indexed variable accessors (u.ind)
  short getIndexedKeyIndex() const noexcept { return u.ind.keyIndex; }
  void setIndexedKeyIndex(short idx) noexcept { u.ind.keyIndex = idx; }
  lu_byte getIndexedTableReg() const noexcept { return u.ind.tableRegister; }
  void setIndexedTableReg(lu_byte treg) noexcept { u.ind.tableRegister = treg; }
  lu_byte isIndexedReadOnly() const noexcept { return u.ind.isReadOnly; }
  void setIndexedReadOnly(lu_byte ro) noexcept { u.ind.isReadOnly = ro; }
  int getIndexedStringKeyIndex() const noexcept { return u.ind.stringKeyIndex; }
  void setIndexedStringKeyIndex(int keystr) noexcept { u.ind.stringKeyIndex = keystr; }

  // Local variable accessors (u.var)
  lu_byte getLocalRegister() const noexcept { return u.var.registerIndex; }
  void setLocalRegister(lu_byte ridx) noexcept { u.var.registerIndex = ridx; }
  short getLocalVarIndex() const noexcept { return u.var.variableIndex; }
  void setLocalVarIndex(short vidx) noexcept { u.var.variableIndex = vidx; }

  // Patch lists
  int getTrueList() const noexcept { return trueJumpList; }
  void setTrueList(int list) noexcept { trueJumpList = list; }
  int* getTrueListRef() noexcept { return &trueJumpList; }
  int getFalseList() const noexcept { return falseJumpList; }
  void setFalseList(int list) noexcept { falseJumpList = list; }
  int* getFalseListRef() noexcept { return &falseJumpList; }

  // Expression kind helper methods

  // Check if expression kind is a variable
  static bool isVar(expkind kind) noexcept {
    return VLOCAL <= kind && kind <= VINDEXSTR;
  }

  // Check if expression kind is indexed
  static bool isIndexed(expkind kind) noexcept {
    return VINDEXED <= kind && kind <= VINDEXSTR;
  }

  // Expression initialization methods
  void init(expkind kind, int i);
  void initString(TString *s);
};


// kinds of variables
inline constexpr lu_byte VDKREG = 0;  // regular local
inline constexpr lu_byte RDKCONST = 1;  // local constant
inline constexpr lu_byte RDKTOCLOSE = 2;  // to-be-closed
inline constexpr lu_byte RDKCTC = 3;  // local compile-time constant
inline constexpr lu_byte GDKREG = 4;  // regular global
inline constexpr lu_byte GDKCONST = 5;  // global constant

// description of an active variable
class Vardesc {
public:
  union {
    struct {
      Value value_;  // value for compile-time constant
      lu_byte tt_;  // type tag for compile-time constant
      lu_byte kind;
      lu_byte registerIndex;  // register holding the variable
      short protoLocalVarIndex;  // index of the variable in the Proto's 'locvars' array
      TString *name;  // variable name
    } vd;
    TValue k;  // constant value (if any)
  };

  // Variable kind helper methods

  // Check if variable is in register
  bool isInReg() const noexcept {
    return vd.kind <= RDKTOCLOSE;
  }

  // Check if variable is global
  bool isGlobal() const noexcept {
    return vd.kind >= GDKREG;
  }
};



// description of pending goto statements and label statements
typedef struct Labeldesc {
  TString *name;  // label identifier
  int pc;  // position in code
  int line;  // line where it appeared
  short numberOfActiveVariables;  // number of active variables in that position
  lu_byte close;  // true for goto that escapes upvalues
} Labeldesc;


// list of labels or gotos
class Labellist {
private:
  MoonVector<Labeldesc> vec;

public:
  explicit Labellist(moon_State* L) : vec(L) {
    // Pre-reserve capacity to avoid early reallocations
    vec.reserve(16);
  }

  // Accessor methods matching old interface
  Labeldesc* getArr() noexcept { return vec.data(); }
  const Labeldesc* getArr() const noexcept { return vec.data(); }
  int getN() const noexcept { return static_cast<int>(vec.size()); }
  int getSize() const noexcept { return static_cast<int>(vec.capacity()); }

  // Modifying size
  void setN(int new_n) { vec.resize(static_cast<size_t>(new_n)); }

  // Direct vector access for modern operations
  void push_back(const Labeldesc& desc) { vec.push_back(desc); }
  void reserve(int capacity) { vec.reserve(static_cast<size_t>(capacity)); }
  Labeldesc& operator[](int index) { return vec[static_cast<size_t>(index)]; }
  const Labeldesc& operator[](int index) const { return vec[static_cast<size_t>(index)]; }

  // For moonM_growvector replacement
  void ensureCapacity(int needed) {
    if (needed > getSize()) {
      vec.reserve(static_cast<size_t>(needed));
    }
  }
  Labeldesc* allocateNew() {
    vec.resize(vec.size() + 1);
    return &vec.back();
  }
};


// dynamic structures used by the parser
class Dyndata {
private:
  MoonVector<Vardesc> actvar_vec;

public:
  Labellist gt;  // list of pending gotos
  Labellist label;  // list of active labels

  explicit Dyndata(moon_State* L)
    : actvar_vec(L), gt(L), label(L) {
    // Pre-reserve typical capacity to avoid early reallocations
    actvar_vec.reserve(32);
  }

  // Direct actvar accessor methods - avoid temporary object creation
  Vardesc* actvarGetArr() noexcept { return actvar_vec.data(); }
  const Vardesc* actvarGetArr() const noexcept { return actvar_vec.data(); }
  int actvarGetN() const noexcept { return static_cast<int>(actvar_vec.size()); }
  int actvarGetSize() const noexcept { return static_cast<int>(actvar_vec.capacity()); }

  void actvarSetN(int new_n) { actvar_vec.resize(static_cast<size_t>(new_n)); }
  Vardesc& actvarAt(int index) { return actvar_vec[static_cast<size_t>(index)]; }
  const Vardesc& actvarAt(int index) const { return actvar_vec[static_cast<size_t>(index)]; }

  Vardesc* actvarAllocateNew() {
    actvar_vec.resize(actvar_vec.size() + 1);
    return &actvar_vec.back();
  }

  // std::span accessors for actvar array
  std::span<Vardesc> actvarGetSpan() noexcept {
    return std::span(actvar_vec.data(), actvar_vec.size());
  }
  std::span<const Vardesc> actvarGetSpan() const noexcept {
    return std::span(actvar_vec.data(), actvar_vec.size());
  }

  // Legacy accessor interface for backward compatibility
  class ActvarAccessor {
  private:
    Dyndata* dyn;
  public:
    explicit ActvarAccessor(Dyndata* d) : dyn(d) {}
    int getN() const noexcept { return dyn->actvarGetN(); }
    void setN(int n) { dyn->actvarSetN(n); }
    Vardesc& operator[](int i) { return dyn->actvarAt(i); }
    Vardesc* allocateNew() { return dyn->actvarAllocateNew(); }
  };

  ActvarAccessor actvar() noexcept { return ActvarAccessor{this}; }
};


// control of blocks
struct BlockCnt;  // defined in lparser.c
struct ConsControl;  // defined in lparser.c
struct LHS_assign;  // defined in lparser.c


/*
** FuncState Subsystems - Single Responsibility Principle refactoring
** These classes separate FuncState's responsibilities into focused components
*/

// 1. Code Buffer - Bytecode generation and line info tracking
class CodeBuffer {
private:
  int pc;  // Program counter (next instruction)
  int lasttarget;  // Label of last 'jump label'
  int previousline;  // Last line saved in lineinfo
  int numberOfAbsoluteLineInfo;  // Number of absolute line info entries
  lu_byte instructionsSinceAbsoluteLineInfo;  // Instructions since last absolute line info

public:
  // Inline accessors for reading
  int getPC() const noexcept { return pc; }
  int getLastTarget() const noexcept { return lasttarget; }
  int getPreviousLine() const noexcept { return previousline; }
  int getNumberOfAbsoluteLineInfo() const noexcept { return numberOfAbsoluteLineInfo; }
  lu_byte getInstructionsSinceAbsoluteLineInfo() const noexcept { return instructionsSinceAbsoluteLineInfo; }

  // Setters
  void setPC(int pc_) noexcept { pc = pc_; }
  void setLastTarget(int lasttarget_) noexcept { lasttarget = lasttarget_; }
  void setPreviousLine(int previousline_) noexcept { previousline = previousline_; }
  void setNumberOfAbsoluteLineInfo(int numberOfAbsoluteLineInfo_) noexcept { numberOfAbsoluteLineInfo = numberOfAbsoluteLineInfo_; }
  void setInstructionsSinceAbsoluteLineInfo(lu_byte instructionsSinceAbsoluteLineInfo_) noexcept { instructionsSinceAbsoluteLineInfo = instructionsSinceAbsoluteLineInfo_; }

  // Increment/decrement methods
  void incrementPC() noexcept { pc++; }
  void decrementPC() noexcept { pc--; }
  int postIncrementPC() noexcept { return pc++; }
  void incrementNumberOfAbsoluteLineInfo() noexcept { numberOfAbsoluteLineInfo++; }
  void decrementNumberOfAbsoluteLineInfo() noexcept { numberOfAbsoluteLineInfo--; }
  int postIncrementNumberOfAbsoluteLineInfo() noexcept { return numberOfAbsoluteLineInfo++; }
  lu_byte postIncrementInstructionsSinceAbsoluteLineInfo() noexcept { return instructionsSinceAbsoluteLineInfo++; }
  void decrementInstructionsSinceAbsoluteLineInfo() noexcept { instructionsSinceAbsoluteLineInfo--; }

  // Reference accessors for compound assignments
  int& getPCRef() noexcept { return pc; }
  int& getLastTargetRef() noexcept { return lasttarget; }
  int& getPreviousLineRef() noexcept { return previousline; }
  int& getNumberOfAbsoluteLineInfoRef() noexcept { return numberOfAbsoluteLineInfo; }
  lu_byte& getInstructionsSinceAbsoluteLineInfoRef() noexcept { return instructionsSinceAbsoluteLineInfo; }
};


// 2. Constant Pool - Constant value management and deduplication
class ConstantPool {
private:
  Table *cache;  // Cache for constant deduplication
  int numberOfConstants;  // Number of constants in proto

public:
  // Inline accessors
  Table* getCache() const noexcept { return cache; }
  int getNumberOfConstants() const noexcept { return numberOfConstants; }

  void setCache(Table* cache_) noexcept { cache = cache_; }
  void setNumberOfConstants(int numberOfConstants_) noexcept { numberOfConstants = numberOfConstants_; }

  // Increment
  void incrementNumberOfConstants() noexcept { numberOfConstants++; }

  // Reference accessor
  int& getNumberOfConstantsRef() noexcept { return numberOfConstants; }
};


// 3. Variable Scope - Local variable and label tracking
class VariableScope {
private:
  int firstlocal;  // Index of first local in this function (Dyndata array)
  int firstlabel;  // Index of first label in this function
  short numberOfDebugVariables;  // Number of variables in f->locvars (debug info)
  short numberOfActiveVariables;  // Number of active variable declarations

public:
  // Inline accessors
  int getFirstLocal() const noexcept { return firstlocal; }
  int getFirstLabel() const noexcept { return firstlabel; }
  short getNumDebugVars() const noexcept { return numberOfDebugVariables; }
  short getNumActiveVars() const noexcept { return numberOfActiveVariables; }

  void setFirstLocal(int firstlocal_) noexcept { firstlocal = firstlocal_; }
  void setFirstLabel(int firstlabel_) noexcept { firstlabel = firstlabel_; }
  void setNumDebugVars(short ndebugvars_) noexcept { numberOfDebugVariables = ndebugvars_; }
  void setNumActiveVars(short nactvar_) noexcept { numberOfActiveVariables = nactvar_; }

  // Increment
  short postIncrementNumDebugVars() noexcept { return numberOfDebugVariables++; }

  // Reference accessors
  short& getNumDebugVarsRef() noexcept { return numberOfDebugVariables; }
  short& getNumActiveVarsRef() noexcept { return numberOfActiveVariables; }
};


// 4. Register Allocator - Register allocation tracking
class RegisterAllocator {
private:
  lu_byte firstFreeRegister;  // First free register

public:
  // Inline accessors
  lu_byte getFirstFreeRegister() const noexcept { return firstFreeRegister; }
  void setFirstFreeRegister(lu_byte firstFreeRegister_) noexcept { firstFreeRegister = firstFreeRegister_; }

  // Decrement
  void decrementFirstFreeRegister() noexcept { firstFreeRegister--; }

  // Reference accessor
  lu_byte& getFirstFreeRegisterRef() noexcept { return firstFreeRegister; }
};


// 5. Upvalue Tracker - Upvalue management
class UpvalueTracker {
private:
  lu_byte numberOfUpvalues;  // Number of upvalues
  lu_byte needsCloseUpvalues;  // Function needs to close upvalues when returning

public:
  // Inline accessors
  lu_byte getNumUpvalues() const noexcept { return numberOfUpvalues; }
  lu_byte getNeedClose() const noexcept { return needsCloseUpvalues; }

  void setNumUpvalues(lu_byte numberOfUpvalues_) noexcept { numberOfUpvalues = numberOfUpvalues_; }
  void setNeedClose(lu_byte needsCloseUpvalues_) noexcept { needsCloseUpvalues = needsCloseUpvalues_; }

  // Reference accessors
  lu_byte& getNumUpvaluesRef() noexcept { return numberOfUpvalues; }
  lu_byte& getNeedCloseRef() noexcept { return needsCloseUpvalues; }
};


// state needed to generate code for a given function
class FuncState {
private:
  // Core context (references for non-null, non-reassigned members)
  Proto& f;  // current function header
  class FuncState *prev;  // enclosing function (can be null)
  class LexState& lexState;  // lexical state
  struct BlockCnt *bl;  // chain of current blocks (can be null, reassigned)
  int numberOfNestedPrototypes;  // number of elements in 'p' (nested functions)

  // Subsystems (SRP refactoring)
  CodeBuffer codeBuffer;  // Bytecode generation & line info
  ConstantPool constantPool;  // Constant management
  VariableScope variableScope;  // Local variables & labels
  RegisterAllocator registerAlloc;  // Register allocation
  UpvalueTracker upvalueTrack;  // Upvalue tracking

public:
  // Constructor (required for reference members)
  explicit FuncState(Proto& proto, class LexState& lexStateRef) noexcept
    : f(proto), prev(nullptr), lexState(lexStateRef), bl(nullptr), numberOfNestedPrototypes(0),
      codeBuffer(), constantPool(), variableScope(), registerAlloc(), upvalueTrack() {}

  // Core context accessors (return references where appropriate)
  inline Proto& getProto() const noexcept { return f; }
  inline FuncState* getPrev() const noexcept { return prev; }
  inline class LexState& getLexState() const noexcept { return lexState; }
  inline struct BlockCnt* getBlock() const noexcept { return bl; }
  inline int getNumberOfNestedPrototypes() const noexcept { return numberOfNestedPrototypes; }

  // Setters (Proto& and LexState& are set via constructor, not settable)
  inline void setPrev(FuncState* prev_) noexcept { prev = prev_; }
  inline void setBlock(struct BlockCnt* bl_) noexcept { bl = bl_; }
  inline void setNumberOfNestedPrototypes(int numberOfNestedPrototypes_) noexcept { numberOfNestedPrototypes = numberOfNestedPrototypes_; }
  inline void incrementNumberOfNestedPrototypes() noexcept { numberOfNestedPrototypes++; }
  inline int& getNumberOfNestedPrototypesRef() noexcept { return numberOfNestedPrototypes; }

  // Subsystem access methods (for direct subsystem manipulation)
  inline CodeBuffer& getCodeBuffer() noexcept { return codeBuffer; }
  inline const CodeBuffer& getCodeBuffer() const noexcept { return codeBuffer; }
  inline ConstantPool& getConstantPool() noexcept { return constantPool; }
  inline const ConstantPool& getConstantPool() const noexcept { return constantPool; }
  inline VariableScope& getVariableScope() noexcept { return variableScope; }
  inline const VariableScope& getVariableScope() const noexcept { return variableScope; }
  inline RegisterAllocator& getRegisterAllocator() noexcept { return registerAlloc; }
  inline const RegisterAllocator& getRegisterAllocator() const noexcept { return registerAlloc; }
  inline UpvalueTracker& getUpvalueTracker() noexcept { return upvalueTrack; }
  inline const UpvalueTracker& getUpvalueTracker() const noexcept { return upvalueTrack; }

  // Delegating accessors for CodeBuffer
  inline int getPC() const noexcept { return codeBuffer.getPC(); }
  inline int getLastTarget() const noexcept { return codeBuffer.getLastTarget(); }
  inline int getPreviousLine() const noexcept { return codeBuffer.getPreviousLine(); }
  inline int getNumberOfAbsoluteLineInfo() const noexcept { return codeBuffer.getNumberOfAbsoluteLineInfo(); }
  inline lu_byte getInstructionsSinceAbsoluteLineInfo() const noexcept { return codeBuffer.getInstructionsSinceAbsoluteLineInfo(); }

  inline void setPC(int pc_) noexcept { codeBuffer.setPC(pc_); }
  inline void setLastTarget(int lasttarget_) noexcept { codeBuffer.setLastTarget(lasttarget_); }
  inline void setPreviousLine(int previousline_) noexcept { codeBuffer.setPreviousLine(previousline_); }
  inline void setNumberOfAbsoluteLineInfo(int numberOfAbsoluteLineInfo_) noexcept { codeBuffer.setNumberOfAbsoluteLineInfo(numberOfAbsoluteLineInfo_); }
  inline void setInstructionsSinceAbsoluteLineInfo(lu_byte instructionsSinceAbsoluteLineInfo_) noexcept { codeBuffer.setInstructionsSinceAbsoluteLineInfo(instructionsSinceAbsoluteLineInfo_); }

  inline void incrementPC() noexcept { codeBuffer.incrementPC(); }
  inline void decrementPC() noexcept { codeBuffer.decrementPC(); }
  inline int postIncrementPC() noexcept { return codeBuffer.postIncrementPC(); }
  inline void incrementNumberOfAbsoluteLineInfo() noexcept { codeBuffer.incrementNumberOfAbsoluteLineInfo(); }
  inline void decrementNumberOfAbsoluteLineInfo() noexcept { codeBuffer.decrementNumberOfAbsoluteLineInfo(); }
  inline int postIncrementNumberOfAbsoluteLineInfo() noexcept { return codeBuffer.postIncrementNumberOfAbsoluteLineInfo(); }
  inline lu_byte postIncrementInstructionsSinceAbsoluteLineInfo() noexcept { return codeBuffer.postIncrementInstructionsSinceAbsoluteLineInfo(); }
  inline void decrementInstructionsSinceAbsoluteLineInfo() noexcept { codeBuffer.decrementInstructionsSinceAbsoluteLineInfo(); }

  inline int& getPCRef() noexcept { return codeBuffer.getPCRef(); }
  inline int& getLastTargetRef() noexcept { return codeBuffer.getLastTargetRef(); }
  inline int& getPreviousLineRef() noexcept { return codeBuffer.getPreviousLineRef(); }
  inline int& getNumberOfAbsoluteLineInfoRef() noexcept { return codeBuffer.getNumberOfAbsoluteLineInfoRef(); }
  inline lu_byte& getInstructionsSinceAbsoluteLineInfoRef() noexcept { return codeBuffer.getInstructionsSinceAbsoluteLineInfoRef(); }

  // Delegating accessors for ConstantPool
  inline Table* getKCache() const noexcept { return constantPool.getCache(); }
  inline int getNumberOfConstants() const noexcept { return constantPool.getNumberOfConstants(); }

  inline void setKCache(Table* kcache_) noexcept { constantPool.setCache(kcache_); }
  inline void setNumberOfConstants(int numberOfConstants_) noexcept { constantPool.setNumberOfConstants(numberOfConstants_); }
  inline void incrementNumberOfConstants() noexcept { constantPool.incrementNumberOfConstants(); }
  inline int& getNumberOfConstantsRef() noexcept { return constantPool.getNumberOfConstantsRef(); }

  // Delegating accessors for VariableScope
  inline int getFirstLocal() const noexcept { return variableScope.getFirstLocal(); }
  inline int getFirstLabel() const noexcept { return variableScope.getFirstLabel(); }
  inline short getNumDebugVars() const noexcept { return variableScope.getNumDebugVars(); }
  inline short getNumActiveVars() const noexcept { return variableScope.getNumActiveVars(); }

  inline void setFirstLocal(int firstlocal_) noexcept { variableScope.setFirstLocal(firstlocal_); }
  inline void setFirstLabel(int firstlabel_) noexcept { variableScope.setFirstLabel(firstlabel_); }
  inline void setNumDebugVars(short ndebugvars_) noexcept { variableScope.setNumDebugVars(ndebugvars_); }
  inline void setNumActiveVars(short nactvar_) noexcept { variableScope.setNumActiveVars(nactvar_); }

  inline short postIncrementNumDebugVars() noexcept { return variableScope.postIncrementNumDebugVars(); }
  inline short& getNumDebugVarsRef() noexcept { return variableScope.getNumDebugVarsRef(); }
  inline short& getNumActiveVarsRef() noexcept { return variableScope.getNumActiveVarsRef(); }

  // Delegating accessors for RegisterAllocator
  inline lu_byte getFirstFreeRegister() const noexcept { return registerAlloc.getFirstFreeRegister(); }
  inline void setFirstFreeRegister(lu_byte firstFreeRegister_) noexcept { registerAlloc.setFirstFreeRegister(firstFreeRegister_); }
  inline void decrementFirstFreeRegister() noexcept { registerAlloc.decrementFirstFreeRegister(); }
  inline lu_byte& getFirstFreeRegisterRef() noexcept { return registerAlloc.getFirstFreeRegisterRef(); }

  // Delegating accessors for UpvalueTracker
  inline lu_byte getNumUpvalues() const noexcept { return upvalueTrack.getNumUpvalues(); }
  inline lu_byte getNeedClose() const noexcept { return upvalueTrack.getNeedClose(); }
  inline void setNumUpvalues(lu_byte nups_) noexcept { upvalueTrack.setNumUpvalues(nups_); }
  inline void setNeedClose(lu_byte needclose_) noexcept { upvalueTrack.setNeedClose(needclose_); }
  inline lu_byte& getNumUpvaluesRef() noexcept { return upvalueTrack.getNumUpvaluesRef(); }
  inline lu_byte& getNeedCloseRef() noexcept { return upvalueTrack.getNeedCloseRef(); }

  // Code generation methods (from lcode.h)
  // Note: OpCode is typedef'd in lopcodes.h, we use int to avoid circular deps
  int code(Instruction i);
  int codeABx(int o, int A, int Bx);
  int codeABCk(int o, int A, int B, int C, int k);
  int codeABC(int o, int A, int B, int C) { return codeABCk(o, A, B, C, 0); }
  int codevABCk(int o, int A, int B, int C, int k);
  int exp2const(const ExpDesc& e, TValue *v);
  void fixline(int line);
  void nil(int from, int n);
  void reserveregs(int n);
  void checkstack(int n);
  void intCode(int reg, moon_Integer n);
  void dischargevars(ExpDesc& e);
  int exp2anyreg(ExpDesc& e);
  void exp2anyregup(ExpDesc& e);
  void exp2nextreg(ExpDesc& e);
  void exp2val(ExpDesc& e);
  void self(ExpDesc& e, ExpDesc& key);
  void indexed(ExpDesc& t, ExpDesc& k);
  void goiftrue(ExpDesc& e);
  void storevar(ExpDesc& var, ExpDesc& e);
  void setreturns(ExpDesc& e, int nresults);
  void setoneret(ExpDesc& e);
  int jump();
  void ret(int first, int nret);
  void patchlist(int list, int target);
  void patchtohere(int list);
  void concat(int *l1, int l2);
  int getlabel();
  // Operator functions use strongly-typed enum classes for type safety
  void prefix(UnOpr op, ExpDesc& v, int line);
  void infix(BinOpr op, ExpDesc& v);
  void posfix(BinOpr op, ExpDesc& v1, ExpDesc& v2, int line);
  void settablesize(int pcpos, unsigned ra, unsigned asize, unsigned hsize);
  void setlist(int base, int nelems, int tostore);
  void finish();
  // Code generation primitives (public as they're used by other methods)
  int codeAsBx(OpCode o, int A, int Bc);
  int codek(int reg, int k);
  int getjump(int position);
  void fixjump(int position, int dest);
  Instruction *getjumpcontrol(int position);
  int patchtestreg(int node, int reg);
  void patchlistaux(int list, int vtarget, int reg, int dtarget);
  // More code generation methods (public for now as used by unconverted functions)
  int condjump(OpCode o, int A, int B, int C, int k);
  int removevalues(int list);
  void savelineinfo(Proto& proto, int line);
  void removelastlineinfo();
  void removelastinstruction();
  Instruction *previousinstruction();
  void freeRegister(int reg);
  void freeRegisters(int r1, int r2);
  void freeExpression(ExpDesc& e);
  void freeExpressions(ExpDesc& e1, ExpDesc& e2);
  TValue *const2val(const ExpDesc& e);
  int codeextraarg(int A);
  // Constant management (public for now as used by unconverted functions)
  int addk(Proto& proto, TValue *v);
  int k2proto(TValue *key, TValue *v);
  int stringK(TString& s);
  int intK(moon_Integer n);
  int numberK(moon_Number r);
  int boolF();
  int boolT();
  int nilK();
  void floatCode(int reg, moon_Number flt);
  int str2K(ExpDesc& e);
  int exp2K(ExpDesc& e);
  // Expression & code generation (public for now as used by unconverted functions)
  void discharge2reg(ExpDesc& e, int reg);
  void discharge2anyreg(ExpDesc& e);
  int code_loadbool(int A, OpCode op);
  int need_value(int list);
  void exp2reg(ExpDesc& e, int reg);
  int exp2RK(ExpDesc& e);
  void codeABRK(OpCode o, int A, int B, ExpDesc& ec);
  void negatecondition(ExpDesc& e);
  int jumponcond(ExpDesc& e, int cond);
  void codenot(ExpDesc& e);
  int isKstr(ExpDesc& e);
  int constfolding(int op, ExpDesc& e1, const ExpDesc& e2);
  void codeunexpval(OpCode op, ExpDesc& e, int line);
  void finishbinexpval(ExpDesc& e1, ExpDesc& e2, OpCode op, int v2, int flip, int line, OpCode mmop, TMS event);
  void codebinexpval(BinOpr opr, ExpDesc& e1, ExpDesc& e2, int line);
  void codebini(OpCode op, ExpDesc& e1, ExpDesc& e2, int flip, int line, TMS event);
  void codebinK(BinOpr opr, ExpDesc& e1, ExpDesc& e2, int flip, int line);
  int finishbinexpneg(ExpDesc& e1, ExpDesc& e2, OpCode op, int line, TMS event);
  void codebinNoK(BinOpr opr, ExpDesc& e1, ExpDesc& e2, int flip, int line);
  void codearith(BinOpr opr, ExpDesc& e1, ExpDesc& e2, int flip, int line);
  void codecommutative(BinOpr op, ExpDesc& e1, ExpDesc& e2, int line);
  void codebitwise(BinOpr opr, ExpDesc& e1, ExpDesc& e2, int line);
  void codeorder(BinOpr opr, ExpDesc& e1, ExpDesc& e2);
  void codeeq(BinOpr opr, ExpDesc& e1, ExpDesc& e2);
  void codeconcat(ExpDesc& e1, ExpDesc& e2, int line);
  // Limit checking
  l_noret errorlimit(int limit, const char *what);
  void checklimit(int v, int l, const char *what);
  // Variable utilities
  Vardesc *getlocalvardesc(int vidx);
  lu_byte reglevel(int nvar);
  lu_byte nvarstack();
  LocVar *localdebuginfo(int vidx);
  void init_var(ExpDesc& e, int vidx);
  short registerlocalvar(TString& varname);
  // Variable scope
  void removevars(int tolevel);
  // Upvalue and variable search
  int searchupvalue(TString& name);
  Upvaldesc *allocupvalue();
  int newupvalue(TString& name, ExpDesc& v);
  int searchvar(TString& n, ExpDesc& var);
  void markupval(int level);
  void marktobeclosed();
  // Variable lookup auxiliary
  void singlevaraux(TString& n, ExpDesc& var, int base);
  // Goto resolution
  void solvegotos(BlockCnt& blockCnt);
  // Block management (used by parser infrastructure)
  void enterblock(BlockCnt& blk, lu_byte isloop);
  void leaveblock();
  // Constructor helpers (used by parser infrastructure)
  void closelistfield(ConsControl& cc);
  void lastlistfield(ConsControl& cc);
  int maxtostore();
  // Variable handling (used by parser infrastructure)
  void setvararg(int nparams);
  void storevartop(ExpDesc& var);
  void checktoclose(int level);
  void fixforjump(int pcpos, int dest, int back);

private:
  // Internal helper methods (only used within lcode.cpp)
  int codesJ(int o, int sj, int k);
  int finaltarget(int i);
  void goiffalse(ExpDesc& e);
};


/*
** Parser class - Separates parsing logic from lexical analysis
** Extracted from LexState to achieve proper separation of concerns
*/
class Parser {
private:
  class LexState& lexState;  // lexical state (for tokens and shared data)
  class FuncState *funcState;  // current function state (reassigned for nested functions)

public:
  // Constructor (LexState& required)
  explicit Parser(class LexState& lexStateRef, class FuncState* funcStatePtr)
    : lexState(lexStateRef), funcState(funcStatePtr) {}

  // Accessors
  inline class LexState& getLexState() const noexcept { return lexState; }
  inline class FuncState* getFuncState() const noexcept { return funcState; }
  inline class Dyndata* getDyndata() const noexcept { return lexState.getDyndata(); }

  // Setters (LexState& is set via constructor, FuncState* can be reassigned)
  inline void setFuncState(class FuncState* newFuncState) noexcept { funcState = newFuncState; }

  // Parser utility methods (extracted from LexState public API)
  l_noret error_expected(int token);
  int testnext(int c);
  void check(int c);
  void checknext(int c);
  void check_match(int what, int who, int where);
  TString *str_checkname();

  // Variable utilities
  void codename(ExpDesc& e);
  int new_varkind(TString* name, lu_byte kind);
  int new_localvar(TString& name);

  template<size_t N>
  inline int new_localvarliteral(const char (&v)[N]) {
    return new_localvar(*lexState.newString(v, N - 1));
  }

  void check_readonly(ExpDesc& e);
  void adjustlocalvars(int nvars);

  // Variable building and assignment
  void buildglobal(TString& varname, ExpDesc& var);
  void buildvar(TString& varname, ExpDesc& var);
  void singlevar(ExpDesc& var);
  void adjust_assign(int nvars, int nexps, ExpDesc& e);

  // Label and goto management
  int newgotoentry(TString& name, int line);

  // Parser infrastructure
  Proto *addprototype();
  void mainfunc(FuncState *funcState);

private:
  // Parser implementation methods (extracted from LexState private methods)
  void statement();
  void expr(ExpDesc& v);
  int block_follow(int withuntil);
  void statlist();
  void fieldsel(ExpDesc& v);
  void yindex(ExpDesc& v);
  void recfield(ConsControl& cc);
  void listfield(ConsControl& cc);
  void field(ConsControl& cc);
  void constructor(ExpDesc& t);
  void parlist();
  void body(ExpDesc& e, int ismethod, int line);
  int explist(ExpDesc& v);
  void funcargs(ExpDesc& f);
  void primaryexp(ExpDesc& v);
  void suffixedexp(ExpDesc& v);
  void simpleexp(ExpDesc& v);
  BinOpr subexpr(ExpDesc& v, int limit);
  void block();
  void restassign(struct LHS_assign *lh, int nvars);
  int cond();
  void gotostat(int line);
  void breakstat(int line);
  void checkrepeated(TString& name);
  void labelstat(TString& name, int line);
  void whilestat(int line);
  void repeatstat(int line);
  void exp1();
  void forbody(int base, int line, int nvars, int isgen);
  void fornum(TString& varname, int line);
  void forlist(TString& indexname);
  void forstat(int line);
  void test_then_block(int *escapelist);
  void ifstat(int line);
  void localfunc();
  lu_byte getvarattribute(lu_byte df);
  void localstat();
  lu_byte getglobalattribute(lu_byte df);
  void globalnames(lu_byte defkind);
  void globalstat();
  void globalfunc(int line);
  void globalstatfunc(int line);
  int funcname(ExpDesc& v);
  void funcstat(int line);
  void exprstat();
  void retstat();
  void codeclosure(ExpDesc& v);
  void open_func(FuncState *funcState, BlockCnt& bl);
  void close_func();
  void check_conflict(struct LHS_assign *lh, ExpDesc& v);
};


MOONI_FUNC lu_byte moonY_nvarstack (FuncState *funcState);
MOONI_FUNC void moonY_checklimit (FuncState *funcState, int v, int l,
                                const char *what);
MOONI_FUNC LClosure *moonY_parser (moon_State *L, ZIO *z, Mbuffer *buff,
                                 Dyndata *dyd, const char *name, int firstchar);


/*
** Marks the end of a patch list. It is an invalid value both as an absolute
** address, and as a list link (would link an element to itself).
*/
inline constexpr int NO_JUMP = -1;


// true if operation is foldable (that is, it is arithmetic or bitwise)
inline constexpr bool foldbinop(BinOpr op) noexcept {
	return op <= BinOpr::OPR_SHR;
}


// get (pointer to) instruction of given 'ExpDesc'
inline Instruction& getinstruction(FuncState* funcState, ExpDesc& e) noexcept {
	return funcState->getProto().getCode()[e.getInfo()];
}


#endif
