# Phase 122: VirtualMachine Class Migration

**Date**: November 27, 2025
**Status**: In Progress
**Performance Target**: ≤4.33s

## Overview

Migrate all 21 luaV_* functions to a new `VirtualMachine` class, improving architecture through Single Responsibility Principle while maintaining zero performance regression.

## Motivation

The current architecture has 21 VM operations scattered as C-style functions (luaV_*) across 6 files. While well-organized, this approach:
1. **Lacks encapsulation** - Functions operate on lua_State* passed as parameter
2. **Mixed patterns** - 9 functions already wrapped in lua_State methods, 12 are not
3. **Unclear ownership** - VM operations conceptually belong together but aren't grouped
4. **C-style API** - Not taking full advantage of C++ class organization

Creating a dedicated `VirtualMachine` class addresses all these issues while following the project's modernization goals.

## Current State Analysis

### Existing luaV_* Functions (21 total)

#### Group 1: Execution (2 functions)
- `luaV_execute(lua_State *L, CallInfo *ci)` - Main bytecode interpreter loop
- `luaV_finishOp(lua_State *L)` - Finish interrupted operation after yield

#### Group 2: Type Conversions (4 functions)
- `luaV_tonumber_(const TValue *obj, lua_Number *n)` - Convert to float with string coercion
- `luaV_tointeger(const TValue *obj, lua_Integer *p, F2Imod mode)` - Convert to int with coercion
- `luaV_tointegerns(const TValue *obj, lua_Integer *p, F2Imod mode)` - Convert to int, no coercion
- `luaV_flttointeger(lua_Number n, lua_Integer *p, F2Imod mode)` - Float to int conversion

#### Group 3: Arithmetic (5 functions)
- `luaV_idiv(lua_State *L, lua_Integer m, lua_Integer n)` - Integer division
- `luaV_mod(lua_State *L, lua_Integer m, lua_Integer n)` - Integer modulus
- `luaV_modf(lua_State *L, lua_Number m, lua_Number n)` - Float modulus
- `luaV_shiftl(lua_Integer x, lua_Integer y)` - Bitwise left shift
- `luaV_shiftr(lua_Integer x, lua_Integer y)` - Bitwise right shift (inline)

#### Group 4: Comparisons (4 functions)
- `luaV_lessthan(lua_State *L, const TValue *l, const TValue *r)` - Less-than with metamethods
- `luaV_lessequal(lua_State *L, const TValue *l, const TValue *r)` - Less-equal with metamethods
- `luaV_equalobj(lua_State *L, const TValue *t1, const TValue *t2)` - Equality with metamethods
- `luaV_rawequalobj(const TValue *t1, const TValue *t2)` - Raw equality (inline)

#### Group 5: Table Operations (4 core + 4 inline helpers = 8 total)
Core:
- `luaV_finishget(...)` - Complete table access with __index metamethod
- `luaV_finishset(...)` - Complete table assignment with __newindex metamethod

Inline helpers (in lvm.h):
- `luaV_fastget(...)` - Fast path table read (2 template overloads)
- `luaV_fastgeti(...)` - Fast path integer-indexed read
- `luaV_fastset(...)` - Fast path table write (2 template overloads)
- `luaV_fastseti(...)` - Fast path integer-indexed write
- `luaV_finishfastset(...)` - Complete fast path write with GC barrier

#### Group 6: String/Object Operations (2 functions)
- `luaV_concat(lua_State *L, int total)` - Concatenate stack values
- `luaV_objlen(lua_State *L, StkId ra, const TValue *rb)` - Length operator (#)

### Call Site Statistics

**Total**: 166 luaV_* function calls across 15 files

**Primary callers**:
- `lvm.cpp`: 54 calls (main VM loop - HOT PATH)
- `lapi.cpp`: 21 calls (public C API)
- `ldo.cpp`: 4 calls (execution control)
- `ldebug.cpp`: 1 call (debugging)
- Others: ~86 calls spread across remaining files

### Existing lua_State Wrappers

Currently, **9 of 21** functions have lua_State method wrappers (lines 1494-1533 in lvm.cpp):
```cpp
void lua_State::execute(CallInfo *ci) { luaV_execute(this, ci); }
void lua_State::finishOp() { luaV_finishOp(this); }
void lua_State::concat(int total) { luaV_concat(this, total); }
void lua_State::objlen(StkId ra, const TValue *rb) { luaV_objlen(this, ra, rb); }
LuaT lua_State::finishGet(...) { return luaV_finishget(this, ...); }
void lua_State::finishSet(...) { luaV_finishset(this, ...); }
lua_Integer lua_State::idiv(lua_Integer m, lua_Integer n) { return luaV_idiv(this, m, n); }
lua_Integer lua_State::mod(lua_Integer m, lua_Integer n) { return luaV_mod(this, m, n); }
lua_Number lua_State::modf(lua_Number m, lua_Number n) { return luaV_modf(this, m, n); }
```

**Missing lua_State wrappers** (12 functions):
- All 4 type conversion functions
- All 4 comparison functions
- 2 bitwise shift functions
- 2 inline helpers (rawequalobj, shiftr)

## Design

### VirtualMachine Class Structure

```cpp
// src/vm/lvirtualmachine.h
class VirtualMachine {
private:
    lua_State* L;

public:
    explicit VirtualMachine(lua_State* state) : L(state) {}

    // Prevent copying (VM is tied to specific lua_State)
    VirtualMachine(const VirtualMachine&) = delete;
    VirtualMachine& operator=(const VirtualMachine&) = delete;

    // Allow moving
    VirtualMachine(VirtualMachine&&) noexcept = default;
    VirtualMachine& operator=(VirtualMachine&&) noexcept = default;

    // === EXECUTION === (lvm.cpp)
    void execute(CallInfo *ci);
    void finishOp();

    // === TYPE CONVERSIONS === (lvm_conversion.cpp)
    int tonumber(const TValue *obj, lua_Number *n);
    int tointeger(const TValue *obj, lua_Integer *p, F2Imod mode);
    int tointegerns(const TValue *obj, lua_Integer *p, F2Imod mode);
    static int flttointeger(lua_Number n, lua_Integer *p, F2Imod mode);

    // === ARITHMETIC === (lvm_arithmetic.cpp)
    [[nodiscard]] lua_Integer idiv(lua_Integer m, lua_Integer n);
    [[nodiscard]] lua_Integer mod(lua_Integer m, lua_Integer n);
    [[nodiscard]] lua_Number modf(lua_Number m, lua_Number n);
    [[nodiscard]] static lua_Integer shiftl(lua_Integer x, lua_Integer y);
    [[nodiscard]] static inline lua_Integer shiftr(lua_Integer x, lua_Integer y);

    // === COMPARISONS === (lvm_comparison.cpp)
    [[nodiscard]] int lessThan(const TValue *l, const TValue *r);
    [[nodiscard]] int lessEqual(const TValue *l, const TValue *r);
    [[nodiscard]] int equalObj(const TValue *t1, const TValue *t2);
    [[nodiscard]] static inline int rawequalObj(const TValue *t1, const TValue *t2);

    // === TABLE OPERATIONS === (lvm_table.cpp)
    LuaT finishGet(const TValue *t, TValue *key, StkId val, LuaT tag);
    void finishSet(const TValue *t, TValue *key, TValue *val, int aux);

    // Fast-path inline helpers (templates defined in header)
    template<typename F>
    inline LuaT fastget(Table *t, const TValue *key, TValue *res, F getfunc);

    template<typename F>
    inline int fastset(lua_State *L, Table *t, const TValue *key, const TValue *val, F setfunc);

    inline void fastgeti(Table *t, lua_Integer key, TValue *res, lu_byte &tag);
    inline void fastseti(lua_State *L, Table *t, lua_Integer key, TValue *val, int &hres);
    inline void finishfastset(lua_State *L, Table *t, const TValue *val);

    // === STRING/OBJECT OPERATIONS === (lvm_string.cpp)
    void concat(int total);
    void objlen(StkId ra, const TValue *rb);
};
```

### Integration with lua_State

Add VirtualMachine as a member of lua_State:

```cpp
// In lstate.h
class lua_State : public GCBase<lua_State> {
private:
    // ... existing fields ...
    VirtualMachine vm;  // NEW: VM operations

public:
    lua_State(global_State *g);

    // Access VM
    VirtualMachine& getVM() noexcept { return vm; }
    const VirtualMachine& getVM() const noexcept { return vm; }

    // ... other methods ...
};
```

### Usage Pattern

**Before**:
```cpp
luaV_execute(L, L->ci);
luaV_concat(L, total);
int result = luaV_lessthan(L, v1, v2);
```

**After**:
```cpp
L->getVM().execute(L->ci);
L->getVM().concat(total);
int result = L->getVM().lessThan(v1, v2);
```

Or with a local reference for multiple calls:
```cpp
VirtualMachine& vm = L->getVM();
vm.execute(L->ci);
vm.concat(total);
int result = vm.lessThan(v1, v2);
```

## Implementation Plan

### Part 1: Create VirtualMachine Class Infrastructure (2-3 hours)

1. **Create header file** `src/vm/lvirtualmachine.h`:
   - Class declaration with all 21 method signatures
   - Inline methods (rawequalObj, shiftr, fast* table operations)
   - Template definitions for fastget/fastset
   - Include guards, forward declarations

2. **Create source file** `src/vm/lvirtualmachine.cpp`:
   - Empty implementations (will fill in Part 2)
   - Include necessary headers

3. **Update CMakeLists.txt**:
   - Add lvirtualmachine.cpp to source list

4. **Add VM member to lua_State**:
   - Declare in lstate.h
   - Initialize in lua_State constructor

5. **Build and test**:
   - Verify compilation
   - Run test suite
   - Benchmark baseline

**Success Criteria**: Clean build, all tests pass, baseline performance recorded

---

### Part 2: Implement VM Methods (3-4 hours)

Migrate implementations from luaV_* functions to VirtualMachine methods. Strategy: **Keep luaV_* as thin wrappers temporarily** to avoid breaking existing call sites.

#### 2.1: Move Execution Methods
- `execute()` - Move luaV_execute implementation
- `finishOp()` - Move luaV_finishOp implementation
- Keep luaV_execute/luaV_finishOp as wrappers: `void luaV_execute(L, ci) { L->getVM().execute(ci); }`

#### 2.2: Move Type Conversion Methods
- `tonumber()` - Move luaV_tonumber_ implementation
- `tointeger()` - Move luaV_tointeger implementation
- `tointegerns()` - Move luaV_tointegerns implementation
- `flttointeger()` - Move luaV_flttointeger implementation (static, no lua_State)
- Keep luaV_* wrappers

#### 2.3: Move Arithmetic Methods
- `idiv()`, `mod()`, `modf()`, `shiftl()`, `shiftr()`
- Move implementations
- Keep luaV_* wrappers

#### 2.4: Move Comparison Methods
- `lessThan()`, `lessEqual()`, `equalObj()`, `rawequalObj()`
- Move implementations
- Keep luaV_* wrappers

#### 2.5: Move Table Methods
- `finishGet()`, `finishSet()`
- Template methods: `fastget()`, `fastset()`, `fastgeti()`, `fastseti()`, `finishfastset()`
- Keep luaV_* wrappers

#### 2.6: Move String/Object Methods
- `concat()`, `objlen()`
- Move implementations
- Keep luaV_* wrappers

#### 2.7: Update lua_State Wrappers
Change existing lua_State wrapper methods (9 functions) to call VirtualMachine instead of luaV_*:
```cpp
// Before
void lua_State::execute(CallInfo *ci) { luaV_execute(this, ci); }

// After
void lua_State::execute(CallInfo *ci) { vm.execute(ci); }
```

**Testing Strategy**:
- Compile after each group
- Run full test suite after each group
- Benchmark after completing all moves

**Success Criteria**: All tests pass, performance ≤4.33s, both APIs work (luaV_* and vm.*)

---

### Part 3: Update Call Sites - Core VM (4-6 hours)

**WARNING**: lvm.cpp is the HOT PATH - contains luaV_execute main loop accounting for ~35% of runtime. Must benchmark after changes!

#### 3.1: Update lvm.cpp (54 call sites)
Update in groups by function type:
1. **Execution calls**: `luaV_execute` → `vm.execute`
2. **Type conversions**: `luaV_tonumber_`, etc. → `vm.tonumber()`
3. **Arithmetic**: `luaV_idiv`, etc. → `vm.idiv()`
4. **Comparisons**: `luaV_lessthan`, etc. → `vm.lessThan()`
5. **Table ops**: `luaV_finishget`, `luaV_fastget` → `vm.finishGet()`, `vm.fastget()`
6. **String/Object**: `luaV_concat`, `luaV_objlen` → `vm.concat()`, `vm.objlen()`

**Strategy**:
- Add `VirtualMachine& vm = L->getVM();` at function start for multiple calls
- For single calls, use `L->getVM().method()`
- Update one function type at a time
- Benchmark after each type

#### 3.2: Remove lua_State Wrapper Methods
Once lvm.cpp is updated, the 9 lua_State wrapper methods are redundant. Remove from:
- lstate.h (declarations)
- lvm.cpp (implementations at lines 1494-1533)

**Success Criteria**: lvm.cpp fully migrated, performance ≤4.33s, wrapper methods removed

---

### Part 4: Update Call Sites - API & Support (3-4 hours)

Update remaining files in order of call site count:

#### 4.1: Update lapi.cpp (21 call sites)
- Main public C API file
- Update all luaV_* calls to vm.* calls
- Benchmark after completion

#### 4.2: Update ldo.cpp (4 call sites)
- Execution control functions
- Update calls
- Test coroutine functionality

#### 4.3: Update Remaining Files (~86 call sites total)
Files to update:
- ltable.cpp
- lcode.cpp
- ltm.cpp
- lparser.cpp
- lfunc.cpp
- lstrlib.cpp
- lmathlib.cpp
- lbaselib.cpp
- ldebug.cpp
- Any other files with luaV_* calls

**Strategy**:
- Update 2-3 files at a time
- Run tests after each batch
- Benchmark periodically

**Success Criteria**: All 166 call sites updated, all tests pass, performance ≤4.33s

---

### Part 5: Cleanup & Remove luaV_* Functions (2 hours)

#### 5.1: Remove luaV_* Function Wrappers
Delete wrapper implementations from:
- lvm.cpp (execution, finish)
- lvm_conversion.cpp (type conversions)
- lvm_arithmetic.cpp (arithmetic)
- lvm_comparison.cpp (comparisons)
- lvm_table.cpp (table ops)
- lvm_string.cpp (string/object)

#### 5.2: Remove luaV_* Declarations
Delete from lvm.h:
- All 21 luaV_* function declarations
- Any related macros or helpers

#### 5.3: Update Includes
Ensure all files that use VM operations include:
- `lvirtualmachine.h` (for VirtualMachine class)
- Remove direct lvm.h includes where no longer needed

#### 5.4: Final Verification
1. **Compilation**: No warnings with -Werror
2. **Tests**: Full test suite passes
3. **Performance**: Final 5-run benchmark ≤4.33s
4. **Coverage**: Verify no regression in code coverage
5. **Git**: Clean `git status`, no untracked files

**Success Criteria**: Clean codebase, zero luaV_* functions, all tests pass, performance target met

---

## Performance Considerations

### Critical Hot Paths (MUST remain inline)

These functions are called billions of times and MUST stay inline in header:

1. **VirtualMachine::rawequalObj()** - Raw equality checks in VM loop
2. **VirtualMachine::shiftr()** - Bitwise operations
3. **VirtualMachine::fastget()** - Fast-path table reads (template)
4. **VirtualMachine::fastgeti()** - Fast-path integer table reads
5. **VirtualMachine::fastset()** - Fast-path table writes (template)
6. **VirtualMachine::fastseti()** - Fast-path integer table writes
7. **VirtualMachine::finishfastset()** - GC barrier for fast path

### Warm Paths (inline beneficial but not critical)

- Type conversion helpers (tonumber wrapper, etc.)
- Static arithmetic functions that don't need lua_State

### Cold Paths (non-inline okay)

- `execute()` - Massive function, already non-inline
- `finishGet()` / `finishSet()` - Metamethod fallback
- `finishOp()` - Coroutine yields
- `concat()` - String concatenation
- All implementations in .cpp files

### Optimization Opportunities

1. **Method inlining**: Compiler can inline VirtualMachine methods when L is in a register
2. **Reduced indirection**: `vm.method()` vs `luaV_method(L)` - similar cost
3. **Cache locality**: VirtualMachine member in lua_State improves locality
4. **devirtualization**: No virtual functions, all static dispatch

### Benchmark Checkpoints

Run 5-iteration benchmark at these points:
1. After Part 1 (baseline with empty VM class)
2. After Part 2 (implementations moved)
3. After Part 3.1 (lvm.cpp updated)
4. After Part 4 (all call sites updated)
5. After Part 5 (final cleanup)

**Target**: Each checkpoint ≤4.33s (≤3% regression from 4.20s baseline)

---

## Risk Assessment

### Low Risk
- Creating VirtualMachine class structure
- Moving implementations with wrappers
- Updating non-VM files (lapi.cpp, ldo.cpp)

### Medium Risk
- Updating lvm.cpp call sites (hot path)
- Removing lua_State wrapper methods
- Large number of call sites (166 total)

### High Risk (Mitigated)
- Performance regression in VM loop
  - **Mitigation**: Keep inline functions inline
  - **Mitigation**: Benchmark after every major change
  - **Mitigation**: Incremental approach allows easy rollback
- Breaking existing functionality
  - **Mitigation**: Run full test suite after each part
  - **Mitigation**: Keep luaV_* wrappers during transition
  - **Mitigation**: Update call sites incrementally

### Rollback Strategy

Each part is independently revertible:
- Part 1: Delete new files, revert lua_State changes
- Part 2: Revert implementations, keep wrappers
- Part 3: Git revert specific files
- Part 4: Git revert specific files
- Part 5: Git revert cleanup

---

## Testing Strategy

### Unit Testing
- Run `testes/all.lua` after each part
- Expected: "final OK !!!"
- Covers all language features, VM operations, edge cases

### Performance Testing
```bash
cd /home/peter/claude/lua/testes
for i in 1 2 3 4 5; do \
    ../build/lua all.lua 2>&1 | grep "total time:"; \
done
```
- Target: ≤4.33s average
- Current baseline: ~4.26s

### Regression Testing
- Test coroutines (finishOp coverage)
- Test metamethods (finishGet/finishSet, comparisons)
- Test table operations (fast/slow paths)
- Test arithmetic edge cases (division by zero, overflows)
- Test type conversions (string coercion, float→int)

---

## Success Metrics

### Functional Requirements
- ✅ All 21 luaV_* functions → VirtualMachine methods
- ✅ All 166 call sites updated
- ✅ Zero luaV_* functions remain in codebase
- ✅ lua_State wrapper methods removed
- ✅ All tests pass ("final OK !!!")

### Performance Requirements
- ✅ Average runtime ≤4.33s (≤3% regression)
- ✅ No measurable slowdown in VM loop
- ✅ Hot-path functions remain inline

### Code Quality Requirements
- ✅ Zero compiler warnings (-Werror)
- ✅ Clean architecture (SRP compliance)
- ✅ Consistent naming (camelCase methods)
- ✅ Modern C++ (explicit constructors, delete copy, [[nodiscard]])
- ✅ Full encapsulation (private lua_State* member)

### Documentation Requirements
- ✅ VirtualMachine class fully documented
- ✅ Phase 122 document created (this file)
- ✅ CLAUDE.md updated with Phase 122 status
- ✅ Git commits with clear messages

---

## Estimated Timeline

- **Part 1**: 2-3 hours (infrastructure)
- **Part 2**: 3-4 hours (implementations)
- **Part 3**: 4-6 hours (core VM update)
- **Part 4**: 3-4 hours (API update)
- **Part 5**: 2 hours (cleanup)

**Total**: 14-19 hours

---

## Git Workflow

### Commit Strategy

One commit per part (5 commits total):
```bash
git add <files>
git commit -m "Phase 122 Part 1: Create VirtualMachine class infrastructure"
git commit -m "Phase 122 Part 2: Implement VirtualMachine methods"
git commit -m "Phase 122 Part 3: Update Core VM call sites"
git commit -m "Phase 122 Part 4: Update API and support call sites"
git commit -m "Phase 122 Part 5: Remove luaV_* functions and cleanup"
```

### Branch
- Continue on current branch: `claude/continue-previous-work-01LAsXFhAo9gZozmctQhBpf3`
- Or create new branch: `claude/phase-122-virtualmachine-class`

---

## Related Documentation

- **[CLAUDE.md](../CLAUDE.md)** - Main project guide
- **[SRP_ANALYSIS.md](SRP_ANALYSIS.md)** - Single Responsibility Principle analysis
- **[REFACTORING_SUMMARY.md](REFACTORING_SUMMARY.md)** - Prior refactoring phases
- **[CPP_MODERNIZATION_ANALYSIS.md](CPP_MODERNIZATION_ANALYSIS.md)** - C++ modernization opportunities

---

## Notes

### Design Rationale

**Why VirtualMachine class?**
1. **Single Responsibility**: VM operations grouped together
2. **Encapsulation**: Private lua_State* access
3. **Testability**: Can mock VirtualMachine for testing
4. **Maintainability**: Clear API boundary
5. **Consistency**: Follows project's class-based architecture

**Why not keep as free functions?**
- Free functions don't provide encapsulation
- Harder to maintain clear ownership
- Inconsistent with rest of codebase (19/19 structs → classes)

**Why not make lua_State methods?**
- lua_State already has 30+ responsibilities
- VM operations are conceptually separate
- Allows future optimization (e.g., separate VM from state)

### Future Opportunities

After Phase 122, consider:
1. **Split VirtualMachine**: Separate Executor, Converter, Operations classes
2. **VM State**: Move VM-specific state from lua_State to VirtualMachine
3. **Optimization**: Profile and optimize method dispatch
4. **Testing**: Add unit tests for individual VM operations

---

**Last Updated**: 2025-11-27
**Status**: Part 1 In Progress
**Performance**: Baseline 4.26s (target ≤4.33s)
