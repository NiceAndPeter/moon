# Massive & Aggressive Identifier Modernization Plan
**Lua C++23 Conversion Project**
**Plan Date**: 2025-12-03
**Last Updated**: 2026-06-26

> **STATUS (2026-06-26): LARGELY SUPERSEDED.** Phases 131/133/134 (StringInterner
> members, expdesc fields, compiler expression vars, VM dispatch lambdas) are
> COMPLETE. The remaining identifier work in this doc (Phases 132, 135-143) was
> re-scoped into the codebase-wide **cleanup pass** on branch
> `fix/gc-tail-padding-and-cleanup` (see `~/.claude/plans/analyse-the-codebase-an-vivid-badger.md`).
> That pass concluded — after a focused increment — that the **bulk single-letter
> rename work here is diminishing-returns**: most such locals are idiomatic and
> readable in their small scopes, cannot be batch-replaced safely, and the
> hot-path renames (Phases 138/142/143) carry real perf risk for marginal gain.
> **Recommendation: do NOT pursue Part 2 (Phases 137-143) wholesale.** Treat any
> future identifier work as a *specific, bounded target* (e.g. one file's cryptic
> locals), not a sweep. Part 1 quick wins are done; the rest is deferred by choice,
> not by oversight.

---

## Executive Summary

**Goal**: Systematically improve identifiers at ALL levels of the codebase - from cryptic single-letter variables to abbreviated function names to unclear member variables.

**Scope**: ~315+ high-impact identifiers across ~15 critical files, with ~5000+ total opportunities identified

**Strategy**: Multi-tiered phased approach, prioritizing high-impact/low-risk changes first, with aggressive pursuit of clarity improvements while respecting performance constraints.

**Current State Analysis**:
- ✅ **90% member variables modernized** (Phase 122, 130) - Excellent foundation
- ❌ **50-60% functions still cryptic** - Major opportunity
- ❌ **70% local variables single-letter** - Significant clarity issue
- ✅ **C API preserved** - Public interfaces untouched (correct)
- ⚠️ **~50 abbreviation patterns** with ~5000+ total occurrences

---

## Guiding Principles

1. **Clarity Over Brevity**: Modern IDEs eliminate typing concerns - optimize for reading
2. **Performance First**: MUST maintain ≤4.33s target (currently ~2.12s - 50% faster!)
3. **Benchmark Critical Paths**: Any hot-path change requires 5-run benchmark validation
4. **Preserve Domain Standards**: Keep `pc`, `gc`, `ci` where established (30+ year conventions)
5. **Aggressive But Pragmatic**: Push boundaries, but respect hard constraints
6. **Incremental Validation**: Small batches, frequent testing, quick revert if needed

---

## Identified Opportunities by Tier

### TIER 1: Quick Wins (High Impact, Low Risk)
**Total**: ~220 identifiers | **Estimated Effort**: 2-3 days | **Risk**: LOW

| Category | Count | Example | Priority |
|----------|-------|---------|----------|
| StringInterner members | 3 | `envn` → `environmentName` | ⭐⭐⭐⭐⭐ |
| expdesc final fields | 3 | `t`/`f` → `trueJumpList`/`falseJumpList` | ⭐⭐⭐⭐⭐ |
| String function names | 7 | `tsslen()` → `getStringLength()` | ⭐⭐⭐⭐⭐ |
| Compiler expression vars | ~200 | `e1`/`e2` → `leftExpr`/`rightExpr` | ⭐⭐⭐⭐⭐ |

### TIER 2: Medium Impact (Moderate Effort, Some Risk)
**Total**: ~95 identifiers | **Estimated Effort**: 2-3 days | **Risk**: MEDIUM

| Category | Count | Example | Priority |
|----------|-------|---------|----------|
| VM dispatch lambdas | 15 | `RA(i)` → `getRegisterA(i)` | ⭐⭐⭐⭐ |
| Table rehashing vars | ~80 | `n`/`np`/`mp` → `node`/`newNode`/`mainPosNode` | ⭐⭐⭐ |

### TIER 3: High-Risk/High-Effort (Defer or Conditional)
**Total**: ~60 identifiers | **Estimated Effort**: 5-7 days | **Risk**: HIGH

| Category | Count | Example | Priority |
|----------|-------|---------|----------|
| VM loop main variables | 10 | `cl`/`k`/`base` → `currentClosure`/`constants`/`stackFrameBase` | ⭐⭐ |
| Type member variables | ~50 | `tt_` → `typeTag_` | ⭐ |

### OUT OF SCOPE (Deliberately Excluded)
**Total**: ~3000+ identifiers | **Reason**: Too invasive, low ROI, or necessary conventions

| Category | Reason |
|----------|--------|
| Loop counters (`i`, `j`, `k`) | Acceptable convention (~400 instances, massive risk) |
| Core abbreviations (`tm`, `uv`, `ci`, `fs`, `ls`) | 30-year Lua conventions (~1500+ instances) |
| VM registers (`ra`, `rb`, `rc`, `vra`, `vrb`, `vrc`) | Well-documented, matches bytecode spec (~300 instances) |
| Size variables | HIGH underflow risk with size_t (~30 instances) |
| Public C API | ABI compatibility requirement |

---

## Phased Implementation Plan

### **Phase 131: Quick Wins Batch 1 - Member Variables & expdesc** ✅ **COMPLETE**
**Effort**: 3-4 hours | **Risk**: LOW | **Priority**: ⭐⭐⭐⭐⭐
**Completed**: 2025-12-03 | **Result**: ~2.09s avg ✅ Tests pass!

#### Part 1A: StringInterner Member Names (LOWEST-HANGING FRUIT!)
**Files**: `src/compiler/llex.h`, `src/compiler/llex.cpp`, `src/compiler/lparser.cpp`

**Changes**:
```cpp
// llex.h (lines 140-142)
- TString *envn;  /* environment variable name */
- TString *brkn;  /* "break" name (used as a label) */
- TString *glbn;  /* "global" name (when not a reserved word) */
+ TString *environmentName;  /* environment variable name */
+ TString *breakLabelName;  /* "break" name (used as a label) */
+ TString *globalKeywordName;  /* "global" name (when not a reserved word) */
```

**Call Sites**: ~30 total
- Update accessor implementations in llex.cpp
- Update ~20-25 uses in parser and lexer

**Impact**: ⭐⭐⭐⭐⭐ VERY HIGH
- Currently: `envn` (⭐ Very cryptic - "environment n"???)
- After: `environmentName` (⭐⭐⭐⭐⭐ Crystal clear)

**Validation**:
- Run full test suite: `cd testes && ../build/lua all.lua`
- Expected: "final OK !!!" - No benchmark needed (encapsulated class members)

---

#### Part 1B: expdesc Final Fields (COMPLETE PHASE 130 WORK)
**Files**: `src/compiler/lparser.h`, `src/compiler/lcode.cpp`, ~10 compiler files

**Changes**:
```cpp
// lparser.h (lines 98-100)
class expdesc {
  // ... existing fields ...
- int t;  /* patch list of 'exit when true' */
- int f;  /* patch list of 'exit when false' */
+ int trueJumpList;   /* patch list of 'exit when true' */
+ int falseJumpList;  /* patch list of 'exit when false' */

  // Also in u.ind:
- lu_byte ro;  /* true if variable is read-only */
+ lu_byte isReadOnly;  /* true if variable is read-only */
```

**Call Sites**: ~150-200 total
- Update all `e.t` → `e.trueJumpList`
- Update all `e.f` → `e.falseJumpList`
- Update all `e.u.ind.ro` → `e.u.ind.isReadOnly`

**Files Affected**: `lcode.cpp`, `lparser.cpp`, `parser.cpp`

**Impact**: ⭐⭐⭐⭐⭐ VERY HIGH
- Core compiler data structure
- Currently: `e.t`, `e.f` (⭐ Extremely cryptic in isolation)
- After: `e.trueJumpList`, `e.falseJumpList` (⭐⭐⭐⭐⭐ Self-documenting)

**Note**: Phase 130 improved most expdesc fields - these are THE LAST remaining cryptic ones!

**Validation**:
- Run full test suite
- No benchmark needed (compiler-only, not VM hot path)

---

### **Phase 132: Quick Wins Batch 2 - String Function Names**
**Effort**: 4-6 hours | **Risk**: LOW-MEDIUM | **Priority**: ⭐⭐⭐⭐⭐

**Files**: `src/objects/lstring.h`, `src/objects/lstring.cpp`, ~18 call-site files

**Changes** (7 functions, ~190 call sites):
```cpp
// lstring.h
- inline size_t tsslen(const TString* ts) noexcept;
+ inline size_t getStringLength(const TString* ts) noexcept;

- inline char* getshrstr(TString* ts) noexcept;
+ inline char* getShortStringContents(TString* ts) noexcept;

- inline const char* getlngstr(const TString* ts) noexcept;
+ inline const char* getLongStringContents(const TString* ts) noexcept;

- inline const char* getstr(const TString* ts) noexcept;
+ inline const char* getStringContents(const TString* ts) noexcept;

- inline const char* rawgetshrstr(const TString* ts) noexcept;
+ inline const char* rawGetShortStringContents(const TString* ts) noexcept;

- inline char* getlstr(size_t len, const TString* ts) noexcept;
+ inline char* getStringWithLength(size_t len, const TString* ts) noexcept;

- inline bool eqshrstr(TString* a, TString* b) noexcept;
+ inline bool shortStringsEqual(TString* a, TString* b) noexcept;
```

**Call Sites Distribution**:
- `tsslen()`: ~50 call sites (very frequent!)
- `getstr()`: ~40 call sites
- `getshrstr()`: ~30 call sites
- `getlngstr()`: ~25 call sites
- Others: ~45 call sites combined

**Impact**: ⭐⭐⭐⭐⭐ VERY HIGH
- Public internal API used throughout codebase
- Currently: `tsslen()` (⭐ "ts s len"??? Extremely cryptic!)
- After: `getStringLength()` (⭐⭐⭐⭐⭐ Obvious purpose)

**Strategy**:
- Use Edit tool for EACH call site (no sed/awk bulk edits)
- Update ~20 files in compiler, GC, VM, libraries
- Process in logical groups: GC files, compiler files, VM files, etc.

**Validation**:
- Run full test suite after each file or small group
- Final benchmark (5 runs) - strings are accessed in hot paths
- Target: ≤4.33s (currently ~2.12s, have massive headroom)

---

### **Phase 133: Compiler Expression Variables** ✅ **COMPLETE**
**Effort**: 8-12 hours | **Risk**: LOW | **Priority**: ⭐⭐⭐⭐⭐
**Completed**: 2025-12-03 | **Result**: ~2.11s avg ✅ Compiler clarity dramatically improved!

**Scope**: ~200 local variable occurrences in compiler code generation

**Files**: `src/compiler/lcode.cpp` (primary), `src/compiler/parser.cpp`

**Common Patterns to Modernize**:

```cpp
// Pattern 1: Expression pairs
// Before
void codebinexpval(BinOpr opr, expdesc& e1, expdesc& e2, int line)

// After
void codebinexpval(BinOpr opr, expdesc& leftExpr, expdesc& rightExpr, int line)

// Pattern 2: Register indices
// Before
int r1 = exp2anyreg(e1);
int r2 = exp2anyreg(e2);

// After
int leftRegister = exp2anyreg(leftExpr);
int rightRegister = exp2anyreg(rightExpr);

// Pattern 3: Constant indices
// Before
int k = addk(proto, v);

// After
int constantIndex = addk(proto, value);

// Pattern 4: Generic expression temporaries
// Before
expdesc e;
int r = exp2anyreg(&e);

// After
expdesc expr;
int targetRegister = exp2anyreg(&expr);
```

**Specific Targets**:
- `e`, `e1`, `e2`, `ex` → `expr`, `leftExpr`, `rightExpr`
- `r`, `r1`, `r2` → `targetRegister`, `leftRegister`, `rightRegister`
- `k` (when index) → `constantIndex`
- `k` (when boolean) → `isConstant`
- `v`, `v1`, `v2` → `value`, `leftValue`, `rightValue`
- `op`, `opr` → `operation`, `binaryOp`, `unaryOp`

**Functions to Improve** (~50 functions):
- `codebinexpval()`, `codeunexpval()`, `codearith()`
- `exp2anyreg()`, `exp2reg()`, `exp2RK()`, `exp2K()`
- `discharge2reg()`, `discharge2anyreg()`
- All arithmetic/comparison generation functions

**Strategy**:
- Start with high-level functions (codebinexpval, codearith)
- Work down to helper functions
- Use Edit tool for each function body
- Group related functions together for consistency

**Impact**: ⭐⭐⭐⭐⭐ CRITICAL
- Code generation is complex and currently very hard to follow
- Single-letter variables require constant mental translation
- This affects ~200+ variable uses in 50+ functions

**Validation**:
- Run test suite after each ~10 functions
- No benchmark needed (compiler-only code)

---

### **Phase 134: VM Dispatch Lambda Names** ✅ **COMPLETE**
**Effort**: 3-4 hours | **Risk**: MEDIUM (performance) | **Priority**: ⭐⭐⭐⭐
**Completed**: 2025-12-04 | **Result**: ~2.11s avg ✅ Zero performance regression!

**Scope**: 15 lambda definitions in VM interpreter, ~200+ uses

**Files**: `src/vm/lvm.cpp` (lines 1274-1345, main execution loop)

**Current Lambdas** (cryptic 2-3 letter names):
```cpp
// Lines 1274-1345 in lvm.cpp
auto RA = [&](Instruction i) -> StkId { return ra(ci, i); };
auto RB = [&](Instruction i) -> int { return GETARG_B(i); };
auto RC = [&](Instruction i) -> int { return GETARG_C(i); };
auto RKC = [&](Instruction i) -> const TValue* { return KC(ci, i); };
auto RKB = [&](Instruction i) -> const TValue* { return KB(ci, i); };
auto KB = [&](Instruction i) -> const TValue* { return k + GETARG_B(i); };
auto KC = [&](Instruction i) -> const TValue* { return k + GETARG_C(i); };
auto vRA = [&](Instruction i) { return s2v(RA(i)); };
auto vRB = [&](Instruction i) { return s2v(ra(ci, i) + GETARG_B(i)); };
auto vRC = [&](Instruction i) { return s2v(ra(ci, i) + GETARG_C(i)); };
auto updatetrap = [&](CallInfo *ci) { trap = ci->getCallStatus() & CIST_HOOKMASK; };
auto savepc = [&](CallInfo *ci) { ci->setSavedPC(pc); };
```

**Proposed Renames**:
```cpp
auto getRegisterA = [&](Instruction i) -> StkId { return ra(ci, i); };
auto getArgB = [&](Instruction i) -> int { return GETARG_B(i); };
auto getArgC = [&](Instruction i) -> int { return GETARG_C(i); };
auto getConstantFromArgC = [&](Instruction i) -> const TValue* { return KC(ci, i); };
auto getConstantFromArgB = [&](Instruction i) -> const TValue* { return KB(ci, i); };
auto getConstantB = [&](Instruction i) -> const TValue* { return k + GETARG_B(i); };
auto getConstantC = [&](Instruction i) -> const TValue* { return k + GETARG_C(i); };
auto getValueA = [&](Instruction i) { return s2v(getRegisterA(i)); };
auto getValueB = [&](Instruction i) { return s2v(ra(ci, i) + GETARG_B(i)); };
auto getValueC = [&](Instruction i) { return s2v(ra(ci, i) + GETARG_C(i)); };
auto updateHookMask = [&](CallInfo *ci) { trap = ci->getCallStatus() & CIST_HOOKMASK; };
auto saveProgramCounter = [&](CallInfo *ci) { ci->setSavedPC(pc); };
```

**Call Sites**: ~200+ uses throughout 3000-line luaV_execute() function

**Impact**: ⭐⭐⭐⭐ CRITICAL (most-read code)
- VM execution core is most frequently read code
- Currently requires mental translation (RA = "register A")
- Self-documenting names eliminate cognitive load

**CRITICAL CONSTRAINT**: ⚠️ **PERFORMANCE-SENSITIVE HOT PATH**

**Validation Process** (MANDATORY):
1. Make changes to lambda definitions only
2. Update ~200 call sites throughout luaV_execute()
3. **RUN 5-ITERATION BENCHMARK**:
   ```bash
   cd testes
   for i in 1 2 3 4 5; do ../build/lua all.lua 2>&1 | grep "total time:"; done
   ```
4. **Calculate average**: MUST be ≤4.33s (currently ~2.12s)
5. **If >4.33s OR >10% regression from 2.12s**: REVERT IMMEDIATELY
6. **If 2.12-2.50s**: Acceptable (still 40% faster than baseline)
7. **If >2.50s but <4.33s**: Discuss with user before keeping

**Why Performance Risk Exists**:
- Lambda names affect debugging symbols
- Compiler might handle longer names differently in inlining decisions
- This is the TIGHTEST hot loop (billions of executions per second)

**Expected Outcome**: Zero performance impact (names are compile-time, should inline identically)

---

### **Phase 135: Table Rehashing Variables** ⚠️ **OPTIONAL/CONDITIONAL**
**Effort**: 6-8 hours | **Risk**: MEDIUM | **Priority**: ⭐⭐⭐

**Scope**: ~80 local variables in table rehashing logic

**Files**: `src/objects/ltable.cpp` (focus on lines 290-600, rehashing functions)

**Common Patterns**:
```cpp
// Pattern 1: Node pointers
// Before
Node *n = mainpositionTV(t, key);
Node *othern = mainpositionfromnode(t, mp);
Node *f = getfreepos(t);

// After
Node *node = mainpositionTV(t, key);
Node *collidingNode = mainpositionfromnode(t, mainPositionNode);
Node *freeNode = getfreepos(t);

// Pattern 2: Size calculations
// Before
unsigned int na = numusehash(t, &asize, &nsize);
unsigned int oldasize = t->arraySize();
unsigned int oldhsize = allocsizenode(t);

// After
unsigned int arrayIndicesCount = numusehash(t, &arraySize, &hashSize);
unsigned int oldArraySize = t->arraySize();
unsigned int oldHashSize = allocsizenode(t);

// Pattern 3: Rehashing counters
// Before
unsigned int ct[MAXABITS + 1];
unsigned int na, optimal;

// After
unsigned int counters[MAXABITS + 1];
unsigned int arrayCount, optimalSize;
```

**Key Variables to Improve**:
- `n`, `np`, `mp` → `node`, `newNode`, `mainPositionNode`
- `f` → `freeNode`
- `othern` → `collidingNode`
- `ct` → `counters`
- `na` → `arrayIndicesCount` or `arrayCount`
- `asize`, `nsize` → `arraySize`, `hashSize`
- `oldasize`, `oldhsize` → `oldArraySize`, `oldHashSize`

**Impact**: ⭐⭐⭐ MEDIUM-HIGH
- Table rehashing is complex dual-representation optimization
- Currently very difficult to follow
- Better names would significantly improve comprehension

**Risk**: Performance-sensitive (table operations in hot paths)

**Validation**:
1. Run full test suite
2. **BENCHMARK (5 runs)** - MANDATORY
3. Target: ≤4.33s, prefer ≤2.50s
4. If >10% regression, revert or investigate

---

### **Phase 136: VM Loop Main Variables** ⚠️ **HIGH RISK - OPTIONAL**
**Effort**: 4-6 hours | **Risk**: HIGH | **Priority**: ⭐⭐ (DEFER UNLESS JUSTIFIED)

**Scope**: 10 core variables in luaV_execute() used 100+ times each

**Files**: `src/vm/lvm.cpp` (main loop variables, lines 1250-1270)

**Current Variables**:
```cpp
// Line ~1250-1270
LClosure *cl = clLvalue(s2v(ci->func.p));
StkId base = ci->func.p + 1;
TValue *k = cl->getConstants();
const Instruction *pc = ci->getSavedPC();
int trap = L->hookmask;
```

**Proposed**:
```cpp
LClosure *currentClosure = clLvalue(s2v(ci->func.p));
StkId stackFrameBase = ci->func.p + 1;
TValue *constants = currentClosure->getConstants();
const Instruction *programCounter = ci->getSavedPC();
int hooksEnabled = L->hookmask;
```

**Impact**: ⭐⭐⭐⭐ HIGH (readability)
- These are THE most frequently used variables in Lua
- Currently: `cl`, `k`, `base`, `pc` (require domain knowledge)
- After: Self-documenting

**CRITICAL RISK**: ⚠️ **PERFORMANCE**
- Used ~100+ times EACH in tight loop
- Register allocation might change
- Debug symbols might affect code generation
- Billions of executions per second

**Recommendation**: **DEFER UNLESS STRONGLY JUSTIFIED**
- Consider detailed inline comments instead
- Risk/reward ratio unfavorable
- Only attempt if Phases 131-135 succeed flawlessly

**IF ATTEMPTED** (not recommended):
1. Make changes
2. **INTENSIVE BENCHMARKING** (10 runs, not 5)
3. Accept ONLY if <5% regression
4. Monitor carefully, be ready to revert

---

## Naming Convention Standards

### Established Conventions (to be enforced consistently)

**General Rules**:
- **Classes**: PascalCase (`expdesc`, `FuncState`, `VirtualMachine`)
- **Methods**: camelCase (`exp2anyreg`, `discharge2reg`)
- **Members**: snake_case (`true_jump_list`, `environment_name`)
- **Constants**: UPPER_SNAKE_CASE (`MAX_FSTACK`, `LUA_MAXSTACK`)
- **Local variables**: camelCase or descriptive (`leftExpr`, `targetRegister`)

**Specific Patterns**:

1. **Expressions**: Use `expr` or `expression`, never just `e`
   - Single: `expr`, `expression`
   - Pairs: `leftExpr`/`rightExpr`, `leftExpression`/`rightExpression`

2. **Registers**: Always use "Register" word/suffix
   - `targetRegister`, `sourceRegister`, `leftRegister`, `rightRegister`
   - NEVER: `r`, `r1`, `r2`, `reg`

3. **Constants**: Disambiguate index vs boolean vs pointer
   - Index: `constantIndex`
   - Boolean: `isConstant`
   - Pointer: `constants` (plural)

4. **Indices**: Always explicit about "index" or "Index"
   - `constantIndex`, `registerIndex`, `variableIndex`
   - NEVER: `k`, `i`, `idx` (except loop counter `i` is OK)

5. **Counts/Sizes**: Spell out completely
   - `numberOfConstants`, `upvalueCount`, `arraySize`, `hashSize`
   - NEVER: `nk`, `nups`, `asize`, `nsize`

6. **Booleans**: Use `is`/`has`/`can` prefix
   - `isReadOnly`, `isConstant`, `hasJumps`, `canFold`
   - NEVER: `ro`, `k` (as boolean)

7. **Lists**: Use descriptive suffix
   - `trueJumpList`, `falseJumpList`, `patchList`
   - NEVER: `t`, `f`, `list` (too generic)

### Preserve These (Established Conventions)

**Domain-Standard Abbreviations** (DO NOT CHANGE):
- `pc` = program counter (universally understood)
- `gc` = garbage collector (universally understood)
- `ci` = CallInfo (Lua convention, 30+ years)
- `tm` = tag method/metamethod (Lua convention)
- `uv` = upvalue (Lua convention)

**VM Bytecode Terms** (DO NOT CHANGE):
- `ra`, `rb`, `rc` = VM register operands A, B, C (matches Lua bytecode spec)
- `vra`, `vrb`, `vrc` = values in registers (established, actually quite clear)

**Loop Counters** (ACCEPTABLE AS-IS):
- `i`, `j`, `k` in simple loops (<10 lines)
- Use descriptive names for complex loops

---

## Risk Mitigation Strategy

### Performance Protection

**Benchmark Protocol**:
1. **When**: After ANY change to:
   - VM execution loop (lvm.cpp)
   - Table operations (ltable.cpp)
   - String operations (lstring.cpp/h)
   - GC hot paths (gc_marking.cpp)

2. **How**:
   ```bash
   cd testes
   for i in 1 2 3 4 5; do
     ../build/lua all.lua 2>&1 | grep "total time:"
   done
   ```

3. **Acceptance Criteria**:
   - ✅ **≤4.33s**: Within target, ACCEPT
   - ✅ **≤2.50s**: Within 20% of current 2.12s, ACCEPT
   - ⚠️ **2.50-4.33s**: Discuss with user
   - ❌ **>4.33s**: REJECT, revert immediately

4. **Frequency**:
   - Phase 131: No benchmark (encapsulated members/compiler)
   - Phase 132: Benchmark at end (string functions)
   - Phase 133: No benchmark (compiler only)
   - Phase 134: **MANDATORY benchmark** (VM hot path)
   - Phase 135: **MANDATORY benchmark** (table operations)
   - Phase 136: **INTENSIVE benchmark** (VM core variables)

### Testing Protocol

**After Every Phase**:
```bash
cd testes
../build/lua all.lua
# Expected: "final OK !!!"
```

**After Every ~5-10 Function Changes** (during phase):
```bash
# Quick validation
cd testes
../build/lua all.lua
```

### Rollback Plan

**If Performance Regresses**:
1. Identify specific change causing regression
2. Revert that specific change
3. Re-test
4. Document as "attempted but reverted due to performance"

**If Tests Fail**:
1. Fix immediately if obvious
2. If not obvious, revert last change
3. Debug in isolation
4. Re-apply with fix

---

## Success Metrics

### Quantitative

**Target Improvements**:
- ✅ Phase 131: 220+ identifiers improved (members + expdesc + compiler vars)
- ✅ Phase 132: 7 functions + ~190 call sites (string API)
- ✅ Phase 134: 15 lambdas + ~200 uses (VM dispatch)
- ✅ Phase 135: ~80 variables (table operations)
- **Total**: ~315+ high-impact identifiers

**Performance**:
- ✅ Maintain ≤4.33s target (currently ~2.12s)
- ✅ No single phase causes >10% regression

**Quality**:
- ✅ Maintain 96.1% test coverage
- ✅ Maintain zero warnings
- ✅ All tests pass after every phase

### Qualitative

**Clarity Improvements**:
- ⭐⭐⭐⭐⭐ → ⭐: `envn` → `environmentName` (dramatic)
- ⭐ → ⭐⭐⭐⭐⭐: `tsslen()` → `getStringLength()` (dramatic)
- ⭐⭐ → ⭐⭐⭐⭐⭐: `t`/`f` → `trueJumpList`/`falseJumpList` (dramatic)
- ⭐⭐ → ⭐⭐⭐⭐: `e1`/`e2` → `leftExpr`/`rightExpr` (major)
- ⭐⭐⭐ → ⭐⭐⭐⭐⭐: `RA` → `getRegisterA` (major)

**Maintainability**:
- New contributors can understand code without constant reference to docs
- Code is increasingly self-documenting
- Complex algorithms (table rehashing, code generation) become followable

---

## Timeline Estimate

**Aggressive Schedule** (Full-time equivalent):
- Phase 131: 3-4 hours (Day 1)
- Phase 132: 4-6 hours (Day 1-2)
- Phase 133: 8-12 hours (Day 2-3)
- Phase 134: 3-4 hours + benchmarking (Day 3)
- Phase 135: 6-8 hours + benchmarking (Day 4, optional)
- **Total**: 3-4 days for Phases 131-134 (core improvements)

**Conservative Schedule** (with testing, validation, potential reverts):
- Add 50% buffer for unexpected issues
- **Total**: 5-6 days for Phases 131-134

**Phase 136**: Not included in timeline (DEFER unless justified)

---

## Open Questions for User

Before proceeding, I'd like to clarify:

1. **Aggressiveness Level**:
   - Should I proceed with ALL of Phases 131-135?
   - Or start with just Phase 131 (quick wins) and assess?

2. **Performance Tolerance**:
   - Confirm acceptance criteria: ≤2.50s preferred, ≤4.33s required?
   - How should we handle 2.50-4.33s gray zone?

3. **Phase 136 (VM main variables)**:
   - Should this be attempted, or is inline documentation better?
   - High risk for moderate gain - your preference?

4. **Naming Preferences**:
   - Any specific naming conventions you prefer?
   - Happy with proposed patterns (camelCase locals, snake_case members)?

5. **Function Renames** (potential future phase):
   - Should we also tackle function names like `exp2anyreg()` → `ensureExpressionInRegister()`?
   - Or keep function names as-is and focus on variables?

---

## Appendices

### A. Catalog of Abbreviations

**Identified ~50 distinct patterns** across codebase:

| Abbrev | Meaning | Frequency | Clarity | Action |
|--------|---------|-----------|---------|--------|
| `envn` | environment name | 3 | ⭐ | ✅ Phase 131 |
| `brkn` | break label name | 3 | ⭐ | ✅ Phase 131 |
| `glbn` | global keyword name | 3 | ⭐ | ✅ Phase 131 |
| `tsslen` | TString length | 50 | ⭐ | ✅ Phase 132 |
| `getshrstr` | get short string | 30 | ⭐⭐ | ✅ Phase 132 |
| `getlngstr` | get long string | 25 | ⭐⭐ | ✅ Phase 132 |
| `t`, `f` | true/false lists | 200 | ⭐ | ✅ Phase 131 |
| `e1`, `e2` | expressions | 200 | ⭐⭐ | ✅ Phase 133 |
| `r1`, `r2` | registers | 100 | ⭐⭐ | ✅ Phase 133 |
| `RA`, `RB`, `RC` | VM registers | 200 | ⭐⭐⭐ | ✅ Phase 134 |
| `n`, `np`, `mp` | nodes | 80 | ⭐⭐ | ⚠️ Phase 135 |
| `cl` | closure | 100 | ⭐⭐⭐ | ⛔ Phase 136 |
| `tm` | tag method | 87 | ⭐⭐⭐ | ❌ Keep (Lua convention) |
| `uv` | upvalue | 130 | ⭐⭐⭐ | ❌ Keep (Lua convention) |
| `ci` | CallInfo | 337 | ⭐⭐ | ❌ Keep (Lua convention) |
| `pc` | program counter | 305 | ⭐⭐⭐⭐ | ❌ Keep (universal) |

### B. Files Affected Summary

**Phase 131**: 3 files (llex.h/cpp, lparser.h/cpp, lcode.cpp, parser.cpp)
**Phase 132**: ~20 files (string users across codebase)
**Phase 133**: 2 files (lcode.cpp, parser.cpp)
**Phase 134**: 1 file (lvm.cpp)
**Phase 135**: 1 file (ltable.cpp)

**Total Core Files**: ~25 unique files across all phases

### C. Estimated Identifier Counts

- **Phase 131**: 220 identifiers (3 members + 3 fields + ~200 compiler vars + ~14 string functions)
- **Phase 132**: 7 functions + ~190 call sites
- **Phase 133**: Covered in 131
- **Phase 134**: 15 lambdas + ~200 uses
- **Phase 135**: ~80 variables

**Total High-Impact**: ~315+ identifiers across phases 131-135

---

## Conclusion

This plan represents a **massive and aggressive** identifier modernization effort targeting ~315+ high-impact identifiers across the codebase, with potential for ~5000+ total improvements if extended.

**Core Philosophy**: Push aggressively for clarity improvements while maintaining absolute respect for performance and stability. Start with quick wins (Phase 131), build confidence, then tackle increasingly complex areas (Phases 132-135).

**Expected Outcome**:
- Dramatically improved code readability
- Self-documenting complex algorithms
- Maintained exceptional performance (~2.12s)
- Continued zero warnings and 96.1% coverage
- Foundation for future maintainability

**Next Step**: Await user approval and clarification on open questions, then execute Phase 131 as first aggressive push toward modern, readable C++23 codebase.

---

**Plan Version**: 1.0
**Status**: AWAITING APPROVAL
**Confidence**: HIGH for Phases 131-134, MEDIUM for Phase 135, LOW for Phase 136

---
---

# PART 2: AGGRESSIVE DEFERRED IDENTIFIERS
## (No Respect for Legacy Conventions)

**Philosophy**: Maximum clarity, zero compromise for legacy conventions. This section targets ALL the "sacred cow" abbreviations that were deferred in Part 1 due to Lua's 30-year history. Modern codebase = modern names, period.

**Target**: ~3500+ identifiers previously considered "untouchable"

**Risk Level**: VERY HIGH - These are pervasive conventions, massive scope
**Reward**: Complete modernization, zero remaining cryptic abbreviations

---

## Phase 137: Core Lua Abbreviations Elimination

### Part A: Metamethod "tm" → Complete Forms
**Scope**: 87 occurrences across 11 files

**Current Convention**: `tm` = "tag method" (Lua's historical term for metamethods)

**Changes**:
```cpp
// Before
const TValue *tm = luaT_gettm(events, TM_INDEX, ename);
if (tm == NULL) return NULL;

// After
const TValue *metamethod = luaT_gettm(events, TM_INDEX, ename);
if (metamethod == NULL) return NULL;
```

**Rationale**: "Tag method" is obsolete terminology from Lua 4.0 (released 2000). Modern Lua documentation calls them "metamethods". The abbreviation `tm` is:
- ⭐ Cryptic to new developers
- Historical baggage from 25-year-old naming
- Inconsistent with modern Lua terminology

**Files Affected**: ltm.h, ltm.cpp, lvm.cpp, ltable.cpp, lobject.cpp, lapi.cpp, and 5 more

**Variable Names**:
- `tm` → `metamethod`
- `tm1`, `tm2` → `leftMetamethod`, `rightMetamethod`
- `tm` (in loops) → `currentMetamethod`

**Function Parameters**:
```cpp
// Before
const TValue *luaT_gettm(const Table *events, TMS event, TString *ename);

// After
const TValue *luaT_getMetamethod(const Table *events, TMS event, TString *ename);
```

**Impact**: ⭐⭐⭐⭐ HIGH - Central OOP mechanism in Lua, used throughout

---

### Part B: Upvalue "uv" → "upvalue"
**Scope**: 130 occurrences across 21 files

**Current Convention**: `uv` = upvalue (Lua closure concept)

**Changes**:
```cpp
// Before
UpVal *uv = cl->upvals[i];
if (uv->isClosed()) {
    // ...
}

// After
UpVal *upvalue = cl->upvals[i];
if (upvalue->isClosed()) {
    // ...
}
```

**Rationale**: While "upvalue" is a legitimate Lua term, abbreviating to `uv` saves only 5 characters while losing significant clarity. The full word is:
- ⭐⭐ Moderately clear vs ⭐⭐⭐⭐⭐ completely clear
- Only 7 characters total (not long!)
- Self-documenting

**Variable Names**:
- `uv` → `upvalue`
- `uv1`, `uv2` → `firstUpvalue`, `secondUpvalue` OR `leftUpvalue`, `rightUpvalue`
- `nuv` → `upvalueCount` (already some progress here)

**Files Affected**: lfunc.h, lfunc.cpp, lvm.cpp, ldo.cpp, lgc.cpp, and 16 more

**Impact**: ⭐⭐⭐⭐ HIGH - Core closure mechanism

---

### Part C: CallInfo "ci" → "callInfo"
**Scope**: 337 occurrences across 22 files (LARGEST SINGLE CHANGE)

**Current Convention**: `ci` = CallInfo structure (call stack frame)

**Changes**:
```cpp
// Before
CallInfo *ci = L->ci;
StkId func = ci->func.p;
ci->callstatus |= CIST_TAIL;

// After
CallInfo *callInfo = L->ci;
StkId func = callInfo->func.p;
callInfo->callstatus |= CIST_TAIL;
```

**Rationale**: This is the MOST pervasive abbreviation. `ci` appears 337 times! Yet:
- ⭐ Very cryptic (2-letter abbreviation)
- `callInfo` is only 8 characters (manageable)
- Appears in critical code paths (VM, API, debug)

**Variable Names**:
- `ci` → `callInfo`
- `nci` → `newCallInfo`
- `oci` → `oldCallInfo`

**Member Access**:
- `L->ci` → stays as-is (field name in lua_State)
- But ALL local variables change

**Files Affected**: lvm.cpp (98 occurrences!), ldo.cpp (64), lapi.cpp (41), ldebug.cpp (38), and 18 more

**Impact**: ⭐⭐⭐⭐⭐ CRITICAL
- Most pervasive abbreviation
- Appears in hottest code paths
- Highest risk but highest reward

**Special Consideration**: This change affects VM interpreter heavily. MANDATORY intensive benchmarking.

---

### Part D: FuncState "fs" → "funcState"
**Scope**: 116 occurrences across 13 files (mostly compiler)

**Current Convention**: `fs` = FuncState (compiler function state)

**Changes**:
```cpp
// Before
int exp2anyreg(FuncState *fs, expdesc *e);
int reg = fs->freereg;

// After
int exp2anyreg(FuncState *funcState, expdesc *e);
int reg = funcState->freereg;
```

**Rationale**: Compiler code, not VM hot path. Lower performance risk. `funcState` is clear and only 9 characters.

**Variable Names**:
- `fs` → `funcState`
- Parameters in ~50 functions

**Files Affected**: lcode.h, lcode.cpp, lparser.cpp, parser.cpp, funcstate.cpp, and 8 more

**Impact**: ⭐⭐⭐⭐ HIGH - Compiler core data structure

---

### Part E: LexState "ls" → "lexState"
**Scope**: 181 occurrences across 15 files (lexer/parser)

**Current Convention**: `ls` = LexState (lexer state)

**Changes**:
```cpp
// Before
void Parser::next(LexState *ls) {
    ls->lastline = ls->linenumber;
}

// After
void Parser::next(LexState *lexState) {
    lexState->lastline = lexState->linenumber;
}

// Also member variables (Phase 130 made them references):
class FuncState {
    LexState& ls;  // Currently abbreviated
    // →
    LexState& lexState;
};
```

**Rationale**: Similar to `fs`, this is compiler code. Low performance risk, high clarity gain.

**Variable Names**:
- `ls` → `lexState`
- Member variables: `ls` → `lexState` (in FuncState, Parser classes)

**Files Affected**: llex.h, llex.cpp, lparser.h, lparser.cpp, parser.cpp, and 10 more

**Impact**: ⭐⭐⭐⭐ HIGH - Lexer/parser core

---

### Part F: TString Pointer "ts" → "tstring" or "stringPtr"
**Scope**: 190 occurrences across 18 files

**Current Convention**: `ts` = TString* (pointer to string object)

**Changes**:
```cpp
// Before
TString *ts = tsvalue(o);
size_t len = tsslen(ts);  // Note: tsslen already being renamed in Phase 132

// After
TString *tstring = tsvalue(o);
size_t len = getStringLength(tstring);

// OR (more descriptive in context):
TString *stringPtr = tsvalue(o);
TString *keyString = luaS_newliteral(L, "key");
```

**Rationale**: `ts` is type-based naming. Better to use purpose-based:
- Generic contexts: `tstring` or `stringPtr`
- Specific contexts: `keyString`, `nameString`, `valueString`

**Variable Names**:
- `ts` → `tstring` (generic) OR contextual (preferred)
- `ts1`, `ts2` → `leftString`, `rightString`

**Files Affected**: lstring.cpp, lgc.cpp, lvm.cpp, ltable.cpp, and 14 more

**Impact**: ⭐⭐⭐ MEDIUM-HIGH - String operations throughout

---

## Phase 138: VM Register Abbreviations (MOST CONTROVERSIAL)

### Scope: ~300 occurrences in VM interpreter

**Current Convention**: `ra`, `rb`, `rc` = VM register operands A, B, C (matches Lua bytecode spec)

**Controversy**: These match published Lua VM documentation and bytecode format. BUT they're still cryptic when reading implementation.

**Proposed Changes**:
```cpp
// Current (lvm.cpp ~line 1400-3000)
vmcase(OP_ADD) {
  StkId ra = RA(i);
  TValue *rb = vRB(i);
  TValue *rc = vRC(i);
  lua_Number nb, nc;
  if (ttisinteger(rb) && ttisinteger(rc)) {
    // ...
  }
}

// After - Option 1 (Explicit)
vmcase(OP_ADD) {
  StkId registerA = getRegisterA(i);
  TValue *valueB = getValueB(i);
  TValue *valueC = getValueC(i);
  lua_Number numberB, numberC;
  if (ttisinteger(valueB) && ttisinteger(valueC)) {
    // ...
  }
}

// After - Option 2 (Compromise)
vmcase(OP_ADD) {
  StkId destReg = getRegisterA(i);  // Destination register
  TValue *leftVal = getValueB(i);    // Left operand
  TValue *rightVal = getValueC(i);   // Right operand
  lua_Number leftNum, rightNum;
  if (ttisinteger(leftVal) && ttisinteger(rightVal)) {
    // ...
  }
}
```

**Rationale**:
- Yes, `ra`/`rb`/`rc` match bytecode spec
- BUT: Implementation should be readable WITHOUT constantly referencing spec
- Lua VM documentation is external - internal code should be self-documenting
- Modern practice: clarity > matching external documentation

**Pattern Guidelines**:
- `ra` → `destRegister` or `registerA` (depending on semantic role)
- `rb`, `rc` → `leftOperand`, `rightOperand` (when binary op)
- `rb` → `sourceRegister` (when unary op or move)
- `vra`, `vrb`, `vrc` → `destValue`, `leftValue`, `rightValue`

**Files Affected**: lvm.cpp (primary), lvirtualmachine.cpp

**Impact**: ⭐⭐⭐⭐⭐ MAXIMUM
- Most controversial change (breaks external doc alignment)
- Highest readability gain in most-read code
- Performance risk: VERY HIGH (tightest loop)

**Validation**: INTENSIVE benchmarking required (10+ runs)

---

## Phase 139: Loop Counter Modernization

### Scope: ~400 occurrences across 40+ files

**Current Convention**: `int i`, `int j`, `int k` for loop counters

**Proposed Changes**:

**Simple Loops** (acceptable to keep):
```cpp
// Before & After - KEEP AS-IS (acceptable)
for (int i = 0; i < 10; i++) {
    arr[i] = 0;
}
```

**Complex Loops** (improve):
```cpp
// Before
for (int i = 0; i < proto->sizeupvalues; i++) {
    UpVal *uv = cl->upvals[i];
    // ... 50 lines of logic ...
}

// After
for (int upvalueIndex = 0; upvalueIndex < proto->sizeupvalues; upvalueIndex++) {
    UpVal *upvalue = cl->upvals[upvalueIndex];
    // ... 50 lines of logic ...
}
```

**Nested Loops** (critical):
```cpp
// Before
for (int i = 0; i < h->sizearray; i++) {
    for (int j = 0; j < h->lsizenode; j++) {
        Node *n = &h->node[j];
        // ... which index is which?
    }
}

// After
for (int arrayIndex = 0; arrayIndex < h->sizearray; arrayIndex++) {
    for (int nodeIndex = 0; nodeIndex < h->lsizenode; nodeIndex++) {
        Node *n = &h->node[nodeIndex];
        // ... crystal clear!
    }
}
```

**Strategy**:
- Simple loops (<5 lines): Keep `i`, `j`, `k`
- Complex loops (>10 lines): Use descriptive names
- Nested loops: ALWAYS use descriptive names
- Reverse loops: Use descriptive (avoid `i--` confusion)

**Common Patterns**:
- `i` over constants → `constantIndex`
- `i` over upvalues → `upvalueIndex`
- `i` over locals → `localIndex`
- `i` over instructions → `instructionIndex` or `pc` (program counter)
- `i` over nodes → `nodeIndex`
- `j` in nested → contextual second name

**Files Affected**: Literally everywhere (40+ files)

**Impact**: ⭐⭐⭐⭐ HIGH
- Massive scope (400+ loops)
- Medium risk (off-by-one errors, sign issues)
- High reward (complex loops become much clearer)

**Validation**: Intensive testing required, incremental changes

---

## Phase 140: Size Variables to size_t

### Scope: ~30 size-related variables

**Current Convention**: `int` for sizes (risk of negative values, but no overflow)

**Proposed Changes**:
```cpp
// Before
int oldsize = proto->getLocVarsSize();
int newsize = oldsize * 2;
if (newsize < 0) { /* overflow check */ }

// After
size_t oldsize = proto->getLocVarsSize();
size_t newsize = oldsize * 2;
// Overflow naturally handled by size_t

// CRITICAL: Handle subtraction carefully
// Before
int freeregs = MAX_FSTACK - freereg;  // Could be negative (error case)

// After
if (freereg > MAX_FSTACK) {
    luaG_runerror(L, "register overflow");
}
size_t freeregs = MAX_FSTACK - freereg;  // Safe after check
```

**Critical Pattern - Subtraction**:
```cpp
// DANGEROUS:
size_t diff = a - b;  // If b > a, wraps to huge number!

// SAFE Pattern 1: Check first
if (b > a) {
    // Handle error
}
size_t diff = a - b;

// SAFE Pattern 2: Use signed type for differences
ptrdiff_t diff = static_cast<ptrdiff_t>(a) - static_cast<ptrdiff_t>(b);
if (diff < 0) {
    // Handle error
}
```

**Variables to Change**:
- All `*size`, `*Size` variables → `size_t`
- All count variables that can't be negative → `size_t`
- Keep signed when representing differences or errors

**Files Affected**: lstack.cpp, funcstate.cpp, ltable.cpp, lfunc.cpp, and 10+ more

**Impact**: ⭐⭐⭐ MEDIUM
- Type correctness improvement
- Requires careful audit of ALL subtraction operations
- Risk: HIGH (underflow bugs)

**Validation**: Exhaustive review of every subtraction, intensive testing

---

## Phase 141: Register Index Strong Types

### Scope: ~50 function signatures, ~200+ call sites

**Current Convention**: `int` for register indices (can accidentally pass wrong index type)

**Proposed Changes**:
```cpp
// Define strong types
enum class RegisterIndex : lu_byte {};
enum class ConstantIndex : int {};
enum class StackIndex : int {};
enum class LocalIndex : short {};

// Before
int exp2anyreg(expdesc *e);
void discharge2reg(expdesc *e, int reg);
int getlocvar(FuncState *fs, int idx);

// After
RegisterIndex exp2anyreg(expdesc *e);
void discharge2reg(expdesc *e, RegisterIndex reg);
int getlocvar(FuncState *funcState, LocalIndex idx);

// Benefits - Compiler catches errors:
RegisterIndex reg = getFreeReg();
ConstantIndex constIdx = getConstant(proto, value);
discharge2reg(e, constIdx);  // ❌ Compile error! Can't pass ConstantIndex where RegisterIndex expected
```

**Arithmetic Support**:
```cpp
// Need to support arithmetic
inline RegisterIndex operator+(RegisterIndex lhs, int rhs) {
    return static_cast<RegisterIndex>(static_cast<int>(lhs) + rhs);
}

inline RegisterIndex& operator++(RegisterIndex& idx) {
    idx = static_cast<RegisterIndex>(static_cast<int>(idx) + 1);
    return idx;
}
```

**Files Affected**: lcode.h, lcode.cpp, lparser.h, funcstate.cpp, and 15+ more

**Impact**: ⭐⭐⭐⭐ HIGH
- Prevents entire class of bugs (wrong index type)
- Very invasive change (touches ~200+ call sites)
- High effort, high reward

**Validation**: Compiler will catch many errors, but logic must be reviewed

---

## Phase 142: Generic Single-Letter Variables in Hot Paths

### Scope: ~500+ single-letter variables in ltable.cpp, lvm.cpp, lobject.cpp

**Current Convention**: Performance-critical code uses terse names (`p`, `q`, `n`, `h`, `m`)

**Controversial Stance**: Even in hot paths, clarity > brevity in modern C++

**Example - Table Operations** (ltable.cpp):
```cpp
// Before (lines 290-350)
static Node *mainpositionTV(const Table& t, const TValue *key) {
    lua_Integer i = ivalue(key);
    lua_Number n = fltvalue(key);
    void *p = pvalue(key);
    // ... 'i', 'n', 'p' used throughout
}

// After
static Node *mainpositionTV(const Table& t, const TValue *key) {
    lua_Integer intValue = ivalue(key);
    lua_Number floatValue = fltvalue(key);
    void *ptrValue = pvalue(key);
    // ... self-documenting
}

// Before (rehashing)
static void reinsert(const Table& t, Node *n) {
    Node *mp = mainpositionfromnode(&t, n);
    if (mp != n) {
        Node *othern;
        while ((othern = gnext(mp)) != n) {
            mp = othern;
        }
        // ... which node is which???
    }
}

// After
static void reinsert(const Table& t, Node *nodeToReinsert) {
    Node *mainPosNode = mainpositionfromnode(&t, nodeToReinsert);
    if (mainPosNode != nodeToReinsert) {
        Node *currentNode;
        while ((currentNode = gnext(mainPosNode)) != nodeToReinsert) {
            mainPosNode = currentNode;
        }
        // ... crystal clear!
    }
}
```

**Pattern Guidelines**:
- `p`, `q` → `ptr`, `pointer`, OR contextual (`node`, `entry`)
- `n` → NEVER use (ambiguous: node? number? count?)
  - `n` as Node* → `node`, `currentNode`, `targetNode`
  - `n` as number → `number`, `value`, `count`
  - `n` as size → `size`, `length`, `count`
- `h` → `hash` or `hashValue` (never just `h`)
- `m` → contextual (`mid`, `middle`, `multiplier`)
- `i` → only acceptable in simple loops, else `index` or contextual

**Files Affected**: ltable.cpp (80+ vars), lvm.cpp (50+ vars), lobject.cpp (30+ vars)

**Impact**: ⭐⭐⭐⭐⭐ MAXIMUM
- Hardest to read code becomes readable
- Performance risk: VERY HIGH
- Requires INTENSIVE benchmarking

---

## Phase 143: VM Core Variables (From Part 1 Phase 136 - NOW APPROVED)

Since we're no longer respecting conventions, Phase 136 is now greenlit:

**Scope**: 10 variables in luaV_execute(), used 100+ times each

```cpp
// Before
LClosure *cl = clLvalue(s2v(ci->func.p));
StkId base = ci->func.p + 1;
TValue *k = cl->getConstants();
const Instruction *pc = ci->getSavedPC();
int trap = L->hookmask;

// After
LClosure *currentClosure = clLvalue(s2v(callInfo->func.p));
StkId stackFrameBase = callInfo->func.p + 1;
TValue *constants = currentClosure->getConstants();
const Instruction *programCounter = callInfo->getSavedPC();
int hooksEnabled = L->hookmask;
```

**Exception**: Keep `pc` as `programCounter` or `pc` - `pc` is universally understood in VM contexts.

**Impact**: ⭐⭐⭐⭐⭐ CRITICAL - most frequently accessed variables in Lua

---

## Risk Assessment & Validation Strategy

### Performance Benchmarking

**Phases Requiring Benchmarking**:
- Phase 137-C (ci → callInfo): MANDATORY (VM hot path, 337 occurrences)
- Phase 138 (ra/rb/rc): MANDATORY INTENSIVE (tightest loop)
- Phase 142 (hot path variables): MANDATORY INTENSIVE (table/VM operations)
- Phase 143 (VM core): MANDATORY INTENSIVE (most critical)

**Benchmark Protocol**:
```bash
# Standard (5 runs)
for i in 1 2 3 4 5; do
    ../build/lua all.lua 2>&1 | grep "total time:"
done

# Intensive (10 runs for high-risk changes)
for i in 1 2 3 4 5 6 7 8 9 10; do
    ../build/lua all.lua 2>&1 | grep "total time:"
done
```

**Acceptance Criteria** (REVISED - MORE AGGRESSIVE):
- ✅ **≤2.50s**: Excellent, within 20% of current 2.12s
- ⚠️ **2.50-3.50s**: Acceptable, discuss tradeoffs
- ⚠️⚠️ **3.50-4.33s**: Marginal, strong justification needed
- ❌ **>4.33s**: REJECT, revert

**Key Difference from Part 1**: Willing to accept MORE performance cost for clarity (up to 3.50s still gives 20% margin)

### Testing Strategy

**After EVERY Phase**:
1. Run full test suite: `cd testes && ../build/lua all.lua`
2. Expected: "final OK !!!"
3. If any failures: STOP, debug, fix before continuing

**Incremental Changes**:
- Phases 137-138: Change 20-30 occurrences, test, repeat
- Phases 139-142: Change 10-15 occurrences, test, repeat
- Never batch more than 50 changes without testing

### Rollback Strategy

**Performance Regression**:
1. Identify which specific phase caused regression
2. Revert that phase entirely
3. Document as "attempted but caused X% regression"
4. Continue with other phases

**Correctness Issues**:
1. Fix if obvious (typo, wrong variable)
2. If not obvious: REVERT phase, debug in isolation
3. Re-apply once fix identified

---

## Expected Outcomes

### Quantitative Targets

**Identifiers Improved**:
- Phase 137: ~900 occurrences (tm=87, uv=130, ci=337, fs=116, ls=181, ts=190)
- Phase 138: ~300 occurrences (ra/rb/rc in VM)
- Phase 139: ~400 loop counters (estimate 200 need improvement)
- Phase 140: ~30 size variables
- Phase 141: ~50 functions, ~200 call sites
- Phase 142: ~500 hot-path variables
- Phase 143: ~10 core vars, ~1000 uses

**Total**: ~2400+ identifiers improved in Part 2

**Combined with Part 1**: ~315 + ~2400 = **~2715 high-impact identifiers**

**Remaining Low-Impact**: ~2000+ (acceptable abbreviations, necessary conventions)

### Qualitative Goals

**Complete Modernization**:
- ✅ Zero cryptic 2-letter abbreviations in implementation
- ✅ Self-documenting code throughout
- ✅ No reliance on external documentation to read code
- ✅ Clarity prioritized over historical conventions

**Readability**:
- New contributors can understand VM without deep Lua knowledge
- Complex algorithms (table rehashing) followable line-by-line
- No mental translation required (ci → CallInfo, tm → metamethod)

**Maintainability**:
- Code changes easier to review (clear variable purposes)
- Refactoring safer (strong types catch errors)
- Future development faster (less cognitive load)

---

## Timeline Estimate

**Aggressive Schedule** (Full-time, experienced with codebase):
- Phase 137 (Core abbreviations): 2-3 days (A-F parts, 900 occurrences)
- Phase 138 (VM registers): 1-2 days + intensive benchmarking
- Phase 139 (Loop counters): 3-4 days (400 loops, careful work)
- Phase 140 (Size variables): 1 day + intensive review
- Phase 141 (Strong types): 2-3 days (invasive changes)
- Phase 142 (Hot path variables): 2-3 days + intensive benchmarking
- Phase 143 (VM core): 1 day + intensive benchmarking

**Total**: 12-18 days for Part 2

**Combined**: 15-22 days for complete identifier modernization (Parts 1 + 2)

---

## Conclusion: No Compromise Approach

This Part 2 plan represents **zero compromise** modernization:
- ❌ Reject "but it's a Lua convention" arguments
- ❌ Reject "but it matches the bytecode spec" arguments
- ❌ Reject "but it's always been that way" arguments
- ✅ Accept "modern C++ prioritizes clarity" philosophy
- ✅ Accept some performance cost for dramatic readability gains
- ✅ Accept massive effort for complete modernization

**Core Belief**: A modern C++23 codebase should be readable WITHOUT:
- Knowing Lua's 30-year history
- Memorizing abbreviation tables
- Consulting external VM documentation
- Mental translation of cryptic names

**Result**: A codebase that reads like modern C++, not like 1990s C with C++ syntax.

**Trade-offs Accepted**:
- Performance: Up to 3.50s acceptable (vs 2.12s current, 4.33s target)
- Effort: 12-18 days of intensive renaming work
- Risk: High - pervasive changes to core VM and compiler
- Reward: Complete, uncompromising modernization

---

**Part 2 Version**: 1.0
**Philosophy**: Maximum Clarity, Zero Compromise
**Status**: AWAITING APPROVAL (aggressive approach)
**Confidence**: MEDIUM (high risk, high reward)
