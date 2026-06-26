# Lua C++ Conversion Project - AI Assistant Guide

## Project Overview

Converting Lua 5.5 from C to modern C++23:
- **Zero performance regression** (strict requirement)
- **C API compatibility** preserved
- **CRTP** for static polymorphism
- **Full encapsulation** with private fields

**Performance**: ~2.33s avg ✅ (well under target ≤4.33s; GCC 15.2 toolchain)
**Status**: All in-flight work landed on `main`; GC correctness fix + cleanup waves + LTO workaround + layout guards merged.
**Completed**: Phases 1-127, 129-1, 130-ALL, 131, 133, 134 + GC fix + cleanup Waves 0-5 + declare-on-first-use + LTO fix + layout guards | **Quality**: 96.1% coverage, zero warnings

> **Authoritative status (2026-06-27, on `main`):** This file is the single
> status of record. Recent landed work, in merge order:
> - **GC tail-padding fix** (`gcHeaderReserved_` in `src/objects/lobject_core.h`,
>   **do NOT remove it**): derived GC types were reusing `GCObject`'s tail padding
>   and clobbering `tt`/`marked`. Now also **enforced by `static_assert`** that
>   `sizeof(GCObject) == 2*sizeof(void*)` (PR #81).
> - **Cleanup waves 0–5** (markers stripped, comment style normalized, identifier
>   abbreviations expanded, internal types → PascalCase: `StringTable`, `ExpDesc`,
>   `GlobalState`, `LuaLongJmp`; `lua_State` kept as the public typedef).
> - **declare-on-first-use** applied across libraries/core/objects/serialization/
>   auxiliary/interpreter (PR #79).
> - **GCC-15 LTO miscompile worked around** (PR #78): GC translation units built
>   without IPO on GCC; clang+LTO is clean. Guarded by the **`LTO Smoke` CI job**
>   on both gcc-13 and clang-15 (PR #80).
> - **Compile-time layout guards** for the type-punned GC structures (PR #81):
>   GCObject, UValue, `Limbox`/Node, and the TString short-string overlay.
>
> Two `-Wno-error=` relaxations remain in `CMakeLists.txt` for GCC-15 false
> positives (`maybe-uninitialized`, `format-truncation`), plus
> `-Wno-error=sign-conversion` for clang parity. The earlier ~2.11s figures were
> on the prior toolchain; ~2.33s is the GCC-15.2 baseline.

---

## Completed Milestones ✅

**Core Architecture** (100%):
- 19/19 structs → classes | CRTP inheritance | C++ exceptions | Modern CMake

**Code Modernization** (99.9%):
- ~520 macros → inline functions | Modern casts | enum class | nullptr
- std::array | [[nodiscard]] (55+ functions, found 5 bugs!) | bool returns
- Const correctness | Pointer-to-reference conversions

**Architecture**:
- VirtualMachine class (all wrappers eliminated) | Header modularization (-79% lobject.h)
- LuaStack centralization | GC modularization (6 modules) | SRP refactoring

---

## Recent Phase History

**Phases 115-124** (Earlier modernization):
- **115-116**: std::span adoption, UB fixes
- **117-118**: Bool predicates, [[nodiscard]] (15+ annotations, found 1 bug)
- **119**: std::array conversion (4 arrays, 5.5% perf improvement)
- **121**: Header modularization (lobject.h -79%, 6 focused headers)
- **122**: VirtualMachine class creation (21 methods, ~2.26s avg)
- **123**: Final macro conversions (20+ macros → functions/templates, 99.9% complete)

**Phase 125**: luaV_* Wrapper Elimination ✅
- Converted 90+ call sites from luaV_* wrappers to direct VirtualMachine methods
- Deleted 3 wrapper files, removed 277 lines
- **Result**: ~2.16s avg, cleaner architecture, -0.8% code size

**Phase 126**: Const Correctness ✅
- Added `const` to 5 getter methods (Table, Udata, CClosure, LClosure)
- Made `Table::powerOfTwo()` constexpr
- **Result**: ~2.15s avg (maintained)

**Phase 127**: [[nodiscard]] Expansion ✅
- Added to ~40 critical functions (stack ops, GC, factory methods, strings)
- **Found & fixed 4 real bugs** (closepaux, resetthread, OP_CLOSE, return handler)
- **Result**: ~2.15s avg (maintained)

**Phase 129**: Range-Based For Loops (Part 1) ✅
- Converted 4 traditional loops in lundump.cpp to C++23 range-based for
- Fixed scoping bug in upvalues string loading
- **Result**: ~2.20s avg (maintained)

**Phase 130**: Pointer-to-Reference Conversions ✅ **ALL 6 PARTS COMPLETE!**
- **Part 1**: expdesc* → expdesc& (~80 params, ~200+ call sites in parser/funcstate/lcode)
- **Part 2**: Table*/Proto* → References (~45 helpers in ltable/lundump/ldump, ~100+ call sites)
- **Part 3**: global_State* → global_State& (~42 GC functions in 4 modules: marking, sweeping, weak, finalizer)
- **Part 4**: TString* → TString& (~15 compiler functions: registerlocalvar, searchupvalue, newupvalue, searchvar, singlevaraux, stringK, new_localvar, buildglobal, buildvar, fornum, forlist, labelstat, checkrepeated, newgotoentry; updated 4x eqstr helpers)
- **Part 5**: ConsControl* & BlockCnt* → References (8 compiler infrastructure functions: solvegotos, enterblock, open_func, closelistfield, lastlistfield, recfield, listfield, field; 10 call sites)
- **Part 6**: Member Variables → References (FuncState: Proto& f, LexState& ls; Parser: LexState& ls; added constructors, updated ~150 call sites)
- **Benefits**: Type safety (no null), modern C++23 idiom, clearer semantics, explicit lifetimes
- **Total**: 210+ functions + 3 member variables converted
- **Result**: ~2.12s avg (50% faster than baseline!)
- See `docs/archive/PHASE_130_POINTER_TO_REFERENCE.md`

**Phase 131**: Identifier Modernization - Quick Wins Batch 1 ✅
- **Part 1A**: StringInterner members (3 variables: `envn` → `environmentName`, `brkn` → `breakLabelName`, `glbn` → `globalKeywordName`)
- **Part 1B**: expdesc final fields (3 fields: `t` → `trueJumpList`, `f` → `falseJumpList`, `ro` → `isReadOnly`)
- **Impact**: Completed last remaining cryptic single/two-letter identifiers in core compiler data structures
- **Files Changed**: 21 files across compiler, core, objects, VM
- **Result**: ~2.09s avg (tests pass, maintained performance)

**Phase 133**: Compiler Expression Variables ✅
- Modernized ~200+ local variables in compiler code generation (lcode.cpp, parser.cpp)
- Patterns: `e1`/`e2` → `leftExpr`/`rightExpr`, `r1`/`r2` → `leftRegister`/`rightRegister`
- Updated ~50 functions including codebinexpval, exp2anyreg, discharge2reg
- **Impact**: Complex code generation logic now self-documenting
- **Result**: ~2.11s avg (compiler-only, no benchmark needed)

**Phase 134**: VM Dispatch Lambda Names ✅
- Renamed 20 VM interpreter lambdas from cryptic abbreviations to descriptive names
- **Register Access** (9): `RA` → `getRegisterA`, `vRA` → `getValueA`, `KB` → `getConstantB`, etc.
- **State Management** (5): `updatetrap` → `updateTrap`, `savepc` → `saveProgramCounter`, etc.
- **Control Flow** (3): `dojump` → `performJump`, `donextjump` → `performNextJump`, etc.
- **Exception Handling** (3): `Protect` → `protectCall`, `ProtectNT` → `protectCallNoTop`, etc.
- **Impact**: ~105+ uses in VM hot path, dramatic clarity improvement (⭐⭐ → ⭐⭐⭐⭐⭐)
- **Files Changed**: src/vm/lvirtualmachine.cpp (1 file, 20 lambdas + ~105 uses)
- **Result**: ~2.11s avg ✅ (zero performance regression in VM hot path!)

**GC Fix + Cleanup Pass** (branch `fix/gc-tail-padding-and-cleanup`, 2026-06-26) ✅
- **GC correctness fix**: base-class tail-padding reuse corrupted `tt`/`marked` on
  derived GC types; fixed with `gcHeaderReserved_` in `lobject_core.h`. First green
  baseline on GCC 15.2 (added `-Wno-error=maybe-uninitialized`/`format-truncation`
  for GCC-15 false positives). See `docs/GC_PITFALLS_ANALYSIS.md`.
- **Wave 0**: stripped `$Id` headers, `#define lfoo_c` unit markers, ~204
  `Phase NNN:` provenance markers (84 internal files; public `lauxlib.h` untouched).
- **Wave 3**: genuine C-style pointer casts → `static_cast`; unsafe `strcpy` in
  `lstrlib addlenmod()` → `std::copy_n`. Scalar casts left (sanctioned `cast_*`).
- **Wave 4**: internal type renames to PascalCase (`StringTable`, `ExpDesc`,
  `GlobalState`, `LuaLongJmp`). `lua_State` kept (public opaque typedef).
- **Wave 1**: normalized single-line `/* */` comments to `//` across all 80
  internal files (~5087 lines; multi-line doc blocks and mid-expression inline
  comments left intact).
- **Wave 2** (convention abbreviations → full words, each verified + benchmarked):
  `fs`→`funcState`, `ls`→`lexState`, `ci`→`callInfo`, `tm`→`metamethod`,
  `uv`→`upvalue` (after disambiguating Udata user-values: `UValue::uv`→`value`,
  `Udata::uv[]`→`userValues[]`), `ts`→`tstring`. See
  `docs/IDENTIFIER_MODERNIZATION_PLAN.md`.
- **Wave 5** (targeted refactors — assessed, mostly already done): extracted
  `LexState::readName` from the lexer dispatch. Found the rest low-value or
  too risky for a zero-regression project: remaining macros are all *necessary*
  (see `NECESSARY_MACROS.md`); leftover loops are raw-array/hot-path; and the long
  functions (`str_format`/`str_pack` with gotos+fallthrough, recursive `match`)
  are stable, subtle code — deliberately left intact.
- **Deferred** (diminishing-returns / real risk): VM register operands `ra/rb/rc`,
  bulk single-letter loop/temp locals, decomposing stable subtle functions,
  Wave 6 (Doxygen).
- **Result**: ~2.33s avg (build + `testes/all.lua` green), zero perf regression.

---

## Performance

**Baseline**: 4.20s (Nov 2025) | **Target**: ≤4.33s (3% tolerance)
**Current**: ~2.33s avg ✅ (GCC 15.2; well under target. Earlier ~2.11s figures were on the prior toolchain.)

```bash
# Benchmark (5 runs)
cd /home/user/lua_cpp/testes
for i in 1 2 3 4 5; do ../build/lua all.lua 2>&1 | grep "total time:"; done
```

---

## Architecture

**CRTP Polymorphism**: `GCBase<Derived>` for zero-cost static dispatch (9 GC types)
**Encapsulation**: All fields private, comprehensive accessors with inline/constexpr
**Modern C++23**: enum class | [[nodiscard]] | constexpr | std::span | std::array | nullptr

**Structure** (81 files, ~35k lines):
```
src/
├── auxiliary/     - Auxiliary library
├── compiler/      - Parser, lexer, codegen
├── core/          - VM core (lapi, ldo, lstate, ltm)
├── memory/gc/     - 6 focused GC modules
├── objects/       - Table, TString, Proto, etc.
├── vm/            - Bytecode interpreter
└── [5 more dirs]  - libraries, serialization, testing, etc.
```

---

## Build & Test

```bash
# Build
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build

# Test
cd testes && ../build/lua all.lua  # Expected: "final OK !!!"

# Sanitizers
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DLUA_ENABLE_ASAN=ON -DLUA_ENABLE_UBSAN=ON
```

**Build Options**: LUA_BUILD_TESTS | LUA_ENABLE_ASSERTIONS | LUA_ENABLE_ASAN | LUA_ENABLE_UBSAN | LUA_ENABLE_LTO

---

## Code Style

**Naming**: Classes (PascalCase) | Methods (camelCase) | Members (snake_case) | Constants (UPPER_SNAKE)
**Inline**: Accessors (inline) | Simple compute (inline constexpr) | Complex logic (.cpp)

**Key Files**:
- `include/lua.h` - Public C API
- `src/core/lstate.h` - lua_State, global_State
- `src/objects/lobject.h` - Core types
- `src/memory/lgc.h` - GCBase<T> CRTP
- `src/vm/lvm.cpp` - Bytecode interpreter (HOT PATH)
- `src/compiler/lcode.cpp` - Code generation

---

## Macros: 99.9% Complete ✅

**Converted** (~520): Type checks | Field accessors | Instruction ops | Casts | Memory | GC

**Remaining** (~140 necessary - see `docs/NECESSARY_MACROS.md`):
- Public C API (87) - C compatibility required
- Platform abstraction (41) - POSIX/Windows/ISO C
- Preprocessor (5) - Token pasting, stringification
- Conditional compilation (7) - Debug/HARDMEMTESTS
- VM dispatch (3) - Performance-critical computed goto
- Forward declaration (1) - luaM_error (incomplete type)
- User-customizable (10) - Designed for override
- Test-only (13) - Low priority

---

## Git & Workflow

**Branch**: `claude/*` branches | **Commit**: `git commit -m "Phase N: Description"` | **Push**: `-u origin <branch>`

---

## Process Rules ⚠️

**NEVER**:
1. Skip testing/benchmarking after changes
2. Accept >3% performance regression
3. Break public C API (lua.h, lauxlib.h, lualib.h)

**Note**: Scripted bulk edits (sed/awk/perl/python) are fine for mechanical,
behavior-preserving transforms (comment style, identifier renames) — always
verify with a full build + `testes/all.lua` afterward.

**ALWAYS**:
1. Test after every phase
2. Benchmark significant changes
3. Revert if performance degrades >3%
4. Commit frequently (clean history)
5. Keep internal code pure C++ (no `#ifdef __cplusplus`)

---

## Success Metrics ✅

19/19 classes | ~520 macros converted (99.9%) | VirtualMachine complete | GC modularized
All casts modern | All enums type-safe | CRTP active (9 types) | CI/CD with sanitizers
Zero warnings | 96.1% coverage | 30+ tests passing | **50% faster than baseline!**
Phases 1-127, 129-1, 130-1/2/3/4/5/6 complete (Phase 130: 100% done!)

**Result**: Modern C++23 codebase with exceptional performance!

---

## Key Learnings

1. Inline functions = zero-cost (no overhead vs macros)
2. CRTP = zero-cost (static dispatch, no vtables)
3. Encapsulation doesn't hurt perf (same compiled code)
4. std::span has subtle costs (Phase 115: 11.9% regression)
5. std::array can improve perf (Phase 119: 5.5% improvement)
6. Exceptions are efficient (faster than setjmp/longjmp)
7. Incremental conversion works (small phases + frequent testing)
8. Reference accessors critical (avoid copies in hot paths)
9. [[nodiscard]] finds real bugs (caught 5 bugs in Phases 118, 127!)
10. Template functions + lambdas = zero overhead (Phase 123 GC macros)
11. Eliminate wrappers aggressively (Phase 125: better perf + cleaner code)

---

## Future Opportunities

**Potential Next Steps** (see `docs/archive/PHASE_SUGGESTIONS.md` for details):
- Phase 129 Part 2: Range-based for loops in ldebug.cpp (medium risk)
- Phase 128: std::span performance optimization (if needed)
- Additional pointer-to-reference conversions (Phase 130 continuation)
- More const correctness improvements

**Defer (Low Value/High Risk)**:
- Boolean conversions (8 remaining, diminishing returns)
- Loop counter conversion (400 instances, high risk)
- Size variable conversion (underflow risk)
- Register index strong types (very invasive)
- lua_State SRP refactoring (VM hot path risk)

See `docs/TYPE_MODERNIZATION_ANALYSIS.md` and `docs/archive/PHASE_SUGGESTIONS.md`

---

## Documentation

**Primary**: [CLAUDE.md](CLAUDE.md) (this file) | [README.md](README.md) | [CMAKE_BUILD.md](docs/CMAKE_BUILD.md)

**Architecture**: [SRP_ANALYSIS.md](docs/SRP_ANALYSIS.md) | [CPP_MODERNIZATION_ANALYSIS.md](docs/CPP_MODERNIZATION_ANALYSIS.md) | [TYPE_MODERNIZATION_ANALYSIS.md](docs/TYPE_MODERNIZATION_ANALYSIS.md)

**Specialized**: [NECESSARY_MACROS.md](docs/NECESSARY_MACROS.md) | [GC_SIMPLIFICATION_ANALYSIS.md](docs/GC_SIMPLIFICATION_ANALYSIS.md) | [GC_PITFALLS_ANALYSIS.md](docs/GC_PITFALLS_ANALYSIS.md) | [SPAN_MODERNIZATION_PLAN.md](docs/SPAN_MODERNIZATION_PLAN.md) | [COVERAGE_ANALYSIS.md](docs/COVERAGE_ANALYSIS.md) | [UNDEFINED_BEHAVIOR_ANALYSIS.md](docs/UNDEFINED_BEHAVIOR_ANALYSIS.md)

**Historical**: completed phase write-ups are archived under [docs/archive/](docs/archive/) (see its README).

**CI/CD**: [.github/workflows/ci.yml](.github/workflows/ci.yml) | [coverage.yml](.github/workflows/coverage.yml) | [static-analysis.yml](.github/workflows/static-analysis.yml)

---

## Quick Reference

```bash
# Build & Test
cd /home/user/lua_cpp
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
cd testes && ../build/lua all.lua  # Expected: "final OK !!!"

# Benchmark (5 runs, target ≤4.33s)
for i in 1 2 3 4 5; do ../build/lua all.lua 2>&1 | grep "total time:"; done

# Git
git status && git log --oneline -10
git add <files> && git commit -m "Phase N: Description" && git push -u origin <branch>
```

---

**Updated**: 2026-06-27 | **Phases**: 1-127, 129-1, 130-ALL, 131, 133, 134 + GC fix + cleanup Waves 0-5 + declare-on-first-use + LTO fix + layout guards ✅ | **Performance**: ~2.33s ✅ (GCC 15.2, well under target)
**Status**: Modern C++23, VirtualMachine complete, const-correct, [[nodiscard]] safety. All work merged to `main`: **GC tail-padding corruption fixed** (+ `static_assert` guard), cleanup waves, declare-on-first-use, **GCC-15 LTO miscompile worked around + `LTO Smoke` CI guard**, compile-time layout guards for type-punned GC structures. CI green on gcc-13 + clang-15 + ASan/UBSan.
