# Phase 122: Naming Improvements - Remaining Work

**Status**: Phases 122.1-122.3 Complete (3 of 12, ~67 sites renamed)
**Remaining**: Phases 122.4-122.11 (~438 sites estimated)

---

## ✅ Completed Phases

### Phase 122.1: Table Fields ✓
- `lsizenode` → `logSizeOfNodeArray` (7 sites)
- Files: ltable.h, ltable.cpp
- Commit: 5bf923b7

### Phase 122.2: VariableScope/BlockCnt/Labeldesc ✓
- `nactvar` → `numberOfActiveVariables` (20 sites)
- `ndebugvars` → `numberOfDebugVariables` (10 sites)
- Files: lparser.h, funcstate.cpp, parselabels.cpp, parser.cpp, parseutils.cpp
- Commit: 4d5fc094

### Phase 122.3: FuncState/CodeBuffer/ConstantPool ✓
- `nabslineinfo` → `numberOfAbsoluteLineInfo` (10 sites)
- `iwthabs` → `instructionsSinceAbsoluteLineInfo` (12 sites)
- `count` (ConstantPool) → `numberOfConstants` (8 sites)
- `np` → `numberOfNestedPrototypes` (2 sites)
- Files: lparser.h, lcode.cpp, parser.cpp
- Commit: 76bc6aaf

---

## 📋 Remaining Phases (Detailed Guide)

### Phase 122.4: lua_State Count Fields (~15 sites)

**Files to modify**: `lstate.h`, possibly `ldo.cpp`, `lgc.cpp`

**Renames**:
```
nci → numberOfCallInfos
nCcalls → numberOfCCalls
```

**Search patterns**:
```bash
grep -rn "\bnci\b" src/core/
grep -rn "\bnCcalls\b" src/core/
grep -rn "getNci\|setNci\|getNciRef" src/
grep -rn "getNCcalls\|setNCcalls\|getNCcallsRef" src/
```

**Process**:
1. Read `src/core/lstate.h` - locate `nci` and `nCcalls` fields
2. Rename private fields
3. Update accessor methods: `getNci()` → `getNumberOfCallInfos()`, etc.
4. Update delegating accessors in lua_State class
5. Search and replace all usage sites in implementation files
6. Build, test, benchmark, commit

---

### Phase 122.5: CallInfo Union Fields (~25 sites)

**Files to modify**: `lstate.h`, `ldo.cpp`, `lvm.cpp`

**Renames**:
```
CallInfo union fields:
  nextraargs → numberOfExtraArgs
  nres → numberOfResults
  nyield → numberOfYielded
```

**Search patterns**:
```bash
grep -rn "nextraargs" src/
grep -rn "\.nres\b" src/
grep -rn "nyield" src/
```

**Process**:
1. Read `src/core/lstate.h` - locate CallInfo union definition
2. Rename union fields
3. Update all accessor patterns (e.g., `ci->nres` becomes `ci->numberOfResults`)
4. Build, test, benchmark, commit

**Estimated scope**: 8-10 nextraargs, 12-15 nres, 3-5 nyield sites

---

### Phase 122.6: Index Fields (~150 sites) **LARGEST PHASE**

**Files to modify**: `lparser.h`, `lcode.cpp`, `parser.cpp`, `parseutils.cpp`, many others

**Renames**:
```
Vardesc struct:
  ridx → registerIndex (~25 sites)
  pidx → protoLocalVarIndex (~10 sites)
  vidx → activeVarIndex (~15 sites)

expdesc union fields:
  ind.idx → keyIndex (~40 sites)
  ind.t → tableRegister (~25 sites)
  ind.keystr → stringKeyIndex (~15 sites)
  var.ridx → registerIndex (~10 sites)
  var.vidx → variableIndex (~10 sites)
```

**Search patterns**:
```bash
# Vardesc
grep -rn "\.ridx\b" src/compiler/
grep -rn "\.pidx\b" src/compiler/
grep -rn "\.vidx\b" src/compiler/

# expdesc
grep -rn "\.ind\.idx\b" src/compiler/
grep -rn "\.ind\.t\b" src/compiler/
grep -rn "\.ind\.keystr\b" src/compiler/
grep -rn "\.var\.ridx\b" src/compiler/
grep -rn "\.var\.vidx\b" src/compiler/
```

**Process**:
1. Read `src/compiler/lparser.h` - Vardesc and expdesc class definitions
2. Rename Vardesc fields (ridx, pidx, vidx)
3. Build and fix compilation errors
4. Test and commit "Phase 122.6a: Vardesc index fields"
5. Rename expdesc.ind fields (idx, t, keystr)
6. Build and fix compilation errors
7. Test and commit "Phase 122.6b: expdesc indexed variable fields"
8. Rename expdesc.var fields (ridx, vidx)
9. Build, test, commit "Phase 122.6c: expdesc local variable fields"

**Warning**: This is the largest phase. Consider breaking into 3 sub-phases (a, b, c) as outlined above.

---

### Phase 122.7: Register Tracking Fields (~40 sites)

**Files to modify**: `lparser.h`, `lcode.cpp`, `funcstate.cpp`, `parser.cpp`

**Renames**:
```
RegisterAllocator class:
  freereg → firstFreeRegister
```

**Search patterns**:
```bash
grep -rn "\bfreereg\b" src/compiler/
grep -rn "getFreeReg\|setFreeReg\|decrementFreeReg\|getFreeRegRef" src/
```

**Process**:
1. Read `src/compiler/lparser.h` - RegisterAllocator class
2. Rename `freereg` → `firstFreeRegister`
3. Update accessor methods: `getFreeReg()` → `getFirstFreeRegister()`, etc.
4. Update FuncState delegating accessors
5. Search and replace all ~40 usage sites
6. Build, test, benchmark, commit

**Estimated scope**: ~35-40 sites

---

### Phase 122.8: String Fields (~50 sites)

**Files to modify**: `lstring.h`, `lstring.cpp`, `lstate.h`, `lgc.cpp`

**Renames**:
```
TString class:
  shrlen → shortLength (~15 sites)
  lnglen → longLength (~10 sites)
  hnext → hashNext (~12 sites)

stringtable struct:
  nuse → numberOfElements (~8 sites)
  size → tableSize (~5 sites)
```

**Search patterns**:
```bash
# TString
grep -rn "\.shrlen\b" src/objects/
grep -rn "\.lnglen\b" src/objects/
grep -rn "\.hnext\b" src/objects/
grep -rn "getShrlen\|setShrlen" src/
grep -rn "getLnglen\|setLnglen" src/
grep -rn "getNext\|setNext" src/objects/lstring

# stringtable
grep -rn "stringtable" src/core/lstate.h
grep -rn "strt\.nuse" src/
grep -rn "strt\.size" src/
```

**Process**:
1. Read `src/objects/lstring.h` - TString class
2. Rename TString private fields
3. Update accessor methods
4. Read `src/core/lstate.h` - stringtable struct
5. Rename stringtable fields
6. Update all usage sites
7. Build, test, benchmark, commit

**Note**: `hnext` is used ONLY in TString hash table chains, not the general `next` GC field.

---

### Phase 122.9: Upvalue Fields (~45 sites)

**Files to modify**: `lparser.h`, `lfunc.h`, `lcode.cpp`, `funcstate.cpp`

**Renames**:
```
UpvalueTracker class:
  nups → numberOfUpvalues (~20 sites)
  needclose → needsCloseUpvalues (~8 sites)

CClosure/LClosure classes:
  nupvalues → numberOfUpvalues (already good accessor names, just rename field)
```

**Search patterns**:
```bash
# UpvalueTracker
grep -rn "\bnups\b" src/compiler/
grep -rn "getNumUpvalues\|setNumUpvalues" src/compiler/lparser.h
grep -rn "needclose" src/compiler/

# Closure classes
grep -rn "nupvalues" src/objects/lfunc
```

**Process**:
1. Read `src/compiler/lparser.h` - UpvalueTracker class
2. Rename `nups` → `numberOfUpvalues`, `needclose` → `needsCloseUpvalues`
3. Update accessor methods (keep names like `getNumUpvalues()` - they delegate)
4. Read `src/objects/lfunc.h` - CClosure, LClosure
5. Rename `nupvalues` → `numberOfUpvalues` in both classes
6. Update all ~45 usage sites
7. Build, test, benchmark, commit

---

### Phase 122.10: Expression Value Fields (~60 sites)

**Files to modify**: `lparser.h`, `lcode.cpp`, `parser.cpp`, `parseutils.cpp`

**Renames**:
```
expdesc union fields:
  nval → floatValue (~25 sites)
  ival → integerValue (~30 sites)
  strval → stringValue (~15 sites)
```

**Search patterns**:
```bash
grep -rn "\.nval\b" src/compiler/
grep -rn "\.ival\b" src/compiler/
grep -rn "\.strval\b" src/compiler/
grep -rn "getFloatValue\|setFloatValue" src/compiler/lparser.h
grep -rn "getIntValue\|setIntValue" src/compiler/lparser.h
grep -rn "getStringValue\|setStringValue" src/compiler/lparser.h
```

**Process**:
1. Read `src/compiler/lparser.h` - expdesc class
2. Rename union value fields
3. Update accessor methods: `getFloatValue()`, `getIntValue()` (some already exist), `getStringValue()`
4. Search and replace ~70 direct field access sites
5. Build, test, benchmark, commit

**Note**: Accessors already use good names - just need to align private fields.

---

### Phase 122.11: CodeBuffer Line Info Fields (~20 sites) **ALREADY DONE IN 122.3**

**Status**: COMPLETE in Phase 122.3 ✓

Fields already renamed:
- ~~`previousline` → `previousLineNumber`~~ (kept as `previousline` - acceptable)
- ~~`lasttarget` → `lastJumpTarget`~~ (kept as `lasttarget` - acceptable)
- ✓ `iwthabs` → `instructionsSinceAbsoluteLineInfo` (DONE)
- ✓ `nabslineinfo` → `numberOfAbsoluteLineInfo` (DONE)

**Remaining optional work**:
```
CodeBuffer class (optional renames):
  previousline → previousLineNumber (~10 sites)
  lasttarget → lastJumpTarget (~12 sites)
```

These are less critical since they're already somewhat clear. Consider skipping or doing as Phase 122.12.

---

## 🎯 Implementation Strategy

### Recommended Order:
1. **Phase 122.4**: lua_State (~15 sites, straightforward)
2. **Phase 122.5**: CallInfo (~25 sites, straightforward)
3. **Phase 122.7**: Register tracking (~40 sites, medium)
4. **Phase 122.9**: Upvalue fields (~45 sites, medium)
5. **Phase 122.8**: String fields (~50 sites, medium)
6. **Phase 122.10**: Expression values (~60 sites, medium)
7. **Phase 122.6**: Index fields (~150 sites, LARGE - break into 3 sub-phases)

### Per-Phase Checklist:
- [ ] Read header file(s) to understand structure
- [ ] Rename private field(s)
- [ ] Update accessor method names for consistency
- [ ] Search for all usage sites with grep
- [ ] Use Edit tool individually for each change (NO batch sed/awk)
- [ ] Build: `cmake --build build`
- [ ] Test: `cd testes && ../build/lua all.lua` (expect "final OK !!!")
- [ ] Benchmark (5 runs): `for i in 1 2 3 4 5; do ../build/lua all.lua 2>&1 | grep "total time:"; done`
- [ ] Verify performance ≤4.33s (≤3% regression from 4.20s baseline)
- [ ] Commit with descriptive message following existing pattern
- [ ] Update TodoWrite to mark phase complete

---

## 📊 Progress Tracking

**Total Scope**: ~500 rename sites across 12 phases

| Phase | Description | Sites | Status |
|-------|-------------|-------|--------|
| 122.1 | Table::lsizenode | 7 | ✅ Complete |
| 122.2 | VariableScope counts | 30 | ✅ Complete |
| 122.3 | FuncState/CodeBuffer | 32 | ✅ Complete |
| 122.4 | lua_State counts | ~15 | ⏳ Pending |
| 122.5 | CallInfo unions | ~25 | ⏳ Pending |
| 122.6 | Index fields | ~150 | ⏳ Pending |
| 122.7 | Register tracking | ~40 | ⏳ Pending |
| 122.8 | String fields | ~50 | ⏳ Pending |
| 122.9 | Upvalue fields | ~45 | ⏳ Pending |
| 122.10 | Expression values | ~60 | ⏳ Pending |
| 122.11 | Line info (optional) | ~20 | ⚠️ Mostly done in 122.3 |
| **Total** | | **~474** | **13.8% complete (67/474)** |

---

## ⚠️ Important Reminders

### Critical Rules (NEVER VIOLATE):
1. **NO batch processing** - Use Edit tool for EACH change individually
2. **NEVER use sed/awk/perl** for bulk edits
3. **Test after every phase** - Benchmark significant changes
4. **Revert if >3% regression** - Performance target is strict (≤4.33s)
5. **Commit frequently** - One commit per phase for easy rollback

### Architecture Constraints:
1. **C compatibility ONLY for public API** (lua.h, lauxlib.h, lualib.h)
2. **Internal code is pure C++** - No `#ifdef __cplusplus`
3. **Performance target**: ≤4.33s (3% tolerance from 4.20s baseline)
4. **Zero C API breakage** - Public interface unchanged

### Naming Style:
- **Verbose camelCase**: `numberOfActiveVariables` (NOT `numActiveVars`)
- **Descriptive names**: `logSizeOfNodeArray` (NOT `logNodeSize`)
- **Consistency**: If field is `numberOfX`, accessor is `getNumberOfX()`

---

## 🔍 Useful Grep Patterns

### Find all abbreviated names (exploratory):
```bash
# Two-letter field access patterns
grep -rn "\.[a-z][a-z]\b" src/ | grep -v "getc\|pc\|bl\|ls\|fs" | head -50

# Three-letter abbreviated fields
grep -rn "\.\b[a-z]{3}\b" src/ | grep -v "get\|set" | head -50

# Common abbreviation patterns
grep -rn "\bn[A-Z][a-z]" src/  # nXxx patterns (nci, nres, etc.)
grep -rn "size\|count\|num\|idx" src/  # Size/count/index patterns
```

### Verify renames are complete:
```bash
# After renaming, verify old names are gone
grep -rn "\blsizenode\b" src/  # Should return 0 results
grep -rn "\bnactvar\b" src/    # Should return 0 results
grep -rn "\bnabslineinfo\b" src/  # Should return 0 results
```

---

## 📈 Expected Benefits

### Readability Improvements:
- **Before**: `t->lsizenode` (What does 'l' mean? Log? Length? Last?)
- **After**: `t->logSizeOfNodeArray` (Crystal clear: log₂ of node array size)

- **Before**: `fs->nactvar` (Number of... what? Actions? Active?)
- **After**: `fs->numberOfActiveVariables` (Unambiguous)

- **Before**: `e->ind.idx` (Index into what?)
- **After**: `e->ind.keyIndex` (Index of the key - clear!)

### Maintainability:
- New contributors can understand code without extensive comments
- Reduced cognitive load when reading unfamiliar code
- Fewer bugs from misunderstanding abbreviated names
- Better IDE autocomplete and search

### Performance:
- **Zero runtime impact** - All renames are compile-time only
- **Same generated assembly** - Compiler optimizes field names away
- **Proven so far**: Phases 122.1-122.3 show 0% regression (2.14-2.19s avg)

---

## 🚀 Getting Started (Next Session)

```bash
# Navigate to project
cd /home/peter/claude/lua/build

# Verify current status
git log --oneline -5
git status

# Check current performance baseline
cd ../testes
for i in 1 2 3 4 5; do ../build/lua all.lua 2>&1 | grep "total time:"; done
cd ../build

# Start with Phase 122.4 (smallest remaining phase)
# Read this file for exact patterns and process
# Follow per-phase checklist meticulously
```

**Current branch**: main
**Latest commit**: Phase 122.3 (76bc6aaf)
**Performance baseline**: ~2.14-2.19s (excellent)
**Next phase**: 122.4 (lua_State count fields, ~15 sites)

---

## 📝 Commit Message Template

```
Phase 122.X: Rename [category] fields for clarity

Replaced abbreviated field names with verbose camelCase:
- [OldName1] → [NewName1] (~X sites)
- [OldName2] → [NewName2] (~Y sites)

Updated accessor methods for consistency.

Changes across:
- [file1.h] ([brief description])
- [file2.cpp] ([brief description])

Performance: ~X.XXs avg (5 runs) - excellent, no regression
Tests: All passing ✓

🤖 Generated with [Claude Code](https://claude.com/claude-code)

Co-Authored-By: Claude <noreply@anthropic.com>
```

---

**Document Version**: 1.0
**Last Updated**: 2025-11-22
**Author**: Claude Code
**Status**: Ready for Phase 122.4+
