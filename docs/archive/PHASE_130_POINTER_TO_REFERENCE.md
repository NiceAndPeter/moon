# Phase 130: Comprehensive Pointer-to-Reference Conversions

**Status**: In Progress (Part 5 Complete)
**Date Started**: 2025-12-02
**Estimated Effort**: 2.5-3 weeks
**Performance Target**: Maintain ≤4.33s (currently ~2.12s)

---

## Overview

Convert ~210 function pointers to references across the Lua C++ codebase. This modernization improves type safety, code clarity, and aligns with C++23 best practices by making non-null requirements explicit.

---

## Motivation

### Benefits
1. **Type Safety**: References cannot be null, making intent explicit
2. **Code Clarity**: Signals required parameters vs optional ones
3. **Bug Prevention**: Eliminates accidental null dereferences
4. **Modern C++**: Aligns with C++23 best practices
5. **Compiler Optimization**: References may enable better aliasing analysis

### Scope
- **Total Functions**: ~210
- **Total Call Sites**: ~500-1000
- **Files Affected**: 15+ files across compiler, GC, and object modules

---

## Implementation Plan

### Part 1: expdesc* → expdesc& (Compiler Expressions)
**Status**: In Progress
**Scope**: ~80 functions, ~300-400 call sites
**Risk**: Low (zero null checks found)

**Files**:
- `src/compiler/lcode.cpp` (40+ functions)
- `src/compiler/parser.cpp` (25+ functions)
- `src/compiler/funcstate.cpp` (5+ functions)
- `src/compiler/lparser.h` (declarations)

**Functions to Convert** (FuncState):
- dischargevars, exp2anyreg, exp2anyregup, exp2nextreg, exp2val
- self, indexed, goiftrue, goiffalse
- storevar, setreturns, setoneret
- prefix, infix, posfix
- freeExpression, freeExpressions
- exp2const, const2val, str2K, exp2K
- discharge2reg, discharge2anyreg, exp2reg, exp2RK
- codeABRK, negatecondition, jumponcond, codenot
- isKstr, constfolding, codeunexpval
- finishbinexpval, codebinexpval, codebini, codebinK
- finishbinexpneg, codebinNoK, codearith
- codecommutative, codebitwise, codeorder, codeeq, codeconcat
- init_var, singlevaraux, newupvalue, searchvar, storevartop

**Functions to Convert** (Parser):
- codename, check_readonly
- buildglobal, buildvar, singlevar
- adjust_assign, expr
- fieldsel, yindex, constructor
- body, explist, funcargs
- primaryexp, suffixedexp, simpleexp, subexpr
- codeclosure, check_conflict, funcname

**Pattern**:
```cpp
// Before
void FuncState::storevar(expdesc *var, expdesc *ex) {
  expkind k = var->getKind();
  // ...
}
// Call site: fs->storevar(&var, &value);

// After
void FuncState::storevar(expdesc& var, expdesc& ex) {
  expkind k = var.getKind();
  // ...
}
// Call site: fs->storevar(var, value);
```

---

### Part 2: Table* & Proto* → References (Object Helpers)
**Status**: Pending
**Scope**: ~49 functions
**Risk**: Low (internal helpers, no null checks)

**Table* conversions** (~29 functions in `ltable.cpp`):
- keyinarray, sizehash, numusearray, numusehash
- hashkeyisempty, finishnodeset
- (all static helper functions in ltable.cpp)

**Proto* conversions** (~20 functions in serialization):
- `lundump.cpp`: loadCode, loadConstants, loadProtos, loadUpvalues, loadDebug
- `ldump.cpp`: dumpCode, dumpConstants, dumpProtos, dumpDebug

**Pattern**:
```cpp
// Before
static unsigned keyinarray(const Table *t, const TValue *key) {
  // ...
}

// After
static unsigned keyinarray(const Table& t, const TValue *key) {
  // ...
}
```

---

### Part 3: global_State* → global_State& (GC Internals)
**Status**: Pending
**Scope**: ~42 functions
**Risk**: Low-Medium (internal GC, one special init case)

**Files**:
- `src/memory/gc/gc_marking.cpp` (15+ functions)
- `src/memory/gc/gc_sweeping.cpp` (10+ functions)
- `src/memory/gc/gc_weak.cpp` (10+ functions)
- `src/memory/gc/gc_finalizer.cpp` (5+ functions)

**Functions to Convert**:
- GCMarking: reallymarkobject, propagatemark, propagateall
- GCMarking: traversetable, traverseproto, traverseCclosure, traverseLclosure
- GCSweeping: sweeplist, deletelist, sweepstep
- GCWeak: traverseephemeron, clearbykeys, clearbyvalues
- GCFinalizer: dothecall, GCTM, callallpendingfinalizers

**Note**: One null check exists in `lstate.cpp:361` for allocation failure at initialization - handle separately.

---

### Part 4: TString* → TString& (Compiler Strings)
**Status**: Pending
**Scope**: ~25 functions
**Risk**: Low (strings are always valid/interned)

**Files**:
- `src/compiler/parser.cpp`
- `src/compiler/lcode.cpp`
- `src/compiler/funcstate.cpp`

**Functions to Convert** (FuncState):
- registerlocalvar, searchupvalue, newupvalue, searchvar

**Functions to Convert** (Parser):
- new_localvar, new_varkind
- buildglobal, buildvar
- str_checkname usage sites

**Pattern**:
```cpp
// Before
short FuncState::registerlocalvar(TString *varname) {
  // ...
}

// After
short FuncState::registerlocalvar(TString& varname) {
  // ...
}
```

---

### Part 5: ConsControl* & BlockCnt* → References
**Status**: ✅ Complete (2025-12-03)
**Scope**: 8 functions, 10 call sites
**Risk**: Low (stack-allocated structures)

**Files**:
- `src/compiler/funcstate.cpp`
- `src/compiler/parser.cpp`
- `src/compiler/lparser.h`

**Functions Converted**:
- FuncState: closelistfield, lastlistfield, solvegotos, enterblock (4)
- Parser: recfield, listfield, field, open_func (4)

**Pattern**:
```cpp
// Before
void FuncState::closelistfield(ConsControl *cc) {
  // ...
}

// After
void FuncState::closelistfield(ConsControl& cc) {
  // ...
}
```

---

### Part 6: Member Variable Conversions
**Status**: ✅ Complete (2025-12-03)
**Scope**: 3 member variables in 2 classes
**Risk**: Medium (required constructor changes)

**FuncState members** (converted):
```cpp
// Before
class FuncState {
private:
  Proto *f;           // current function header
  FuncState *prev;    // enclosing function
  LexState *ls;       // lexical state
  BlockCnt *bl;       // chain of current blocks
};

// After
class FuncState {
private:
  Proto& f;           // ✅ Converted
  FuncState* prev;    // ❌ Kept pointer (can be null)
  LexState& ls;       // ✅ Converted
  BlockCnt* bl;       // ❌ Kept pointer (can be null, reassigned)
public:
  explicit FuncState(Proto& proto, LexState& lexState) noexcept;
};
```

**Parser members** (converted):
```cpp
// Before
class Parser {
private:
  LexState *ls;
  FuncState *fs;
};

// After
class Parser {
private:
  LexState& ls;  // ✅ Converted
  FuncState* fs; // ❌ Kept pointer (reassigned for nested functions)
public:
  explicit Parser(LexState& lexState, FuncState* funcState);
};
```

**Changes Made**:
- Added FuncState constructor with Proto& and LexState& parameters
- Updated Parser constructor to take LexState& by reference
- Removed setProto() and setLexState() setters
- Updated ~150 call sites (getProto()->  to getProto()., ls-> to ls.)
- Fixed luaC_objbarrier calls with &proto
- Updated FuncState initialization in 2 locations

---

## Testing Strategy

### After Each Part:
1. **Build**: Verify compilation with zero warnings
2. **Test Suite**: Run full test suite (`cd testes && ../build/lua all.lua`)
   - Expected: "final OK !!!"
3. **Benchmark**: Run 5 iterations, record times
   - Target: ≤4.33s per iteration
   - Current baseline: ~2.20s
4. **Git Commit**: Clean commit with descriptive message

### Commands:
```bash
# Build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Test
cd testes
../build/lua all.lua
# Expected: "final OK !!!"

# Benchmark (5 runs)
for i in 1 2 3 4 5; do \
    ../build/lua all.lua 2>&1 | grep "total time:"; \
done

# Commit
git add <files>
git commit -m "Phase 130 Part N: <description>"
```

---

## Risk Assessment

### Low Risk:
- ✅ expdesc* conversions (Part 1) - Zero null checks, clear usage
- ✅ Table* static helpers (Part 2) - Internal only
- ✅ Proto* serialization (Part 2) - Deterministic contexts
- ✅ ConsControl/BlockCnt (Part 5) - Stack-allocated

### Medium Risk:
- ⚠️ global_State* in GC (Part 3) - One special init case
- ⚠️ TString* in compiler (Part 4) - Verify string lifetime
- ⚠️ Member variables (Part 6) - Constructor changes

### Mitigation:
- Incremental approach (one part at a time)
- Comprehensive testing after each part
- Benchmark verification
- Easy rollback with git

---

## Success Criteria

1. ✅ All ~210 functions converted pointer → reference
2. ✅ All tests passing ("final OK !!!")
3. ✅ Performance maintained (≤4.33s, currently ~2.20s)
4. ✅ Zero compiler warnings
5. ✅ Code clarity improved
6. ✅ Type safety enhanced

---

## Progress Tracking

| Part | Scope | Status | Completion Date | Performance |
|------|-------|--------|-----------------|-------------|
| Part 1 | expdesc* (~80 funcs) | ✅ Complete | 2025-12-02 | ~2.20s |
| Part 2 | Table*/Proto* (~49 funcs) | ✅ Complete | 2025-12-02 | ~2.17s |
| Part 3 | global_State* (~42 funcs) | ✅ Complete | 2025-12-03 | ~2.17s |
| Part 4 | TString* (~25 funcs) | ✅ Complete | 2025-12-03 | ~2.17s |
| Part 5 | ConsControl*/BlockCnt* (8 funcs) | ✅ Complete | 2025-12-03 | ~2.12s |
| Part 6 | Member variables (3 vars) | ✅ Complete | 2025-12-03 | ~2.12s |

---

## Notes

### Patterns NOT Converted:
- ❌ **lua_State*** - C API compatibility requirement
- ❌ **StkId** (TValue*) - Stack pointers, can be reallocated
- ❌ **CallInfo*** - Can be null, list traversal patterns
- ❌ **FuncState::prev** - Can be null (checked in funcstate.cpp:305)
- ❌ **Pointers for optional parameters** - Nullability is semantic

### Key Insights:
1. expdesc is **never null** in compiler - always stack-allocated
2. global_State is **never null** during normal execution
3. Table/Proto helpers receive **validated pointers**
4. TString pointers are **interned** and managed by string table
5. Member conversions require **constructor changes**

---

**Last Updated**: 2025-12-03
**Current Part**: All 6 parts complete! ✅
**Overall Status**: 100% complete (All parts 1-6 done: 210+ functions + 3 member variables)
