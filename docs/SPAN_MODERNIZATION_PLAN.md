# std::span Modernization Plan

**Date**: 2025-11-20 (Updated: 2026-06-26)
**Status**: ⚠️ **Partially Implemented & PAUSED** - Phases 115-116 done; further span work deferred
**Target**: Zero-cost type safety improvements using C++20 std::span

> **STATUS (2026-06-26):** Phases 115-116 integrated `std::span` for Proto/Dyndata
> accessors. Further span adoption is **deferred** — `std::span` showed subtle perf
> costs in hot paths (Phase 115 saw an 11.9% regression before mitigation), so the
> remaining proposals here are not worth the risk while perf is already ~2.33s
> (well under the ≤4.33s target). Retained for historical context; no active work.

> **Update (2025-11-21)**: Phases 115-116 completed std::span integration for Proto and Dyndata accessors.
> Performance regression identified (4.70s avg in Phase 115 Part 3), requiring optimization work.
> See CLAUDE.md for current status and Phase 116 completion details.

---

## Executive Summary

This plan introduces `std::span<T>` throughout the Lua C++ codebase to replace raw pointer + size pairs, improving type safety, API clarity, and maintainability with **zero performance overhead**.

### Goals

- ✅ Replace pointer + size pairs with type-safe `std::span<T>`
- ✅ Improve API expressiveness and safety
- ✅ Enable automatic bounds checking in debug builds
- ✅ Maintain **≤3% performance regression** requirement (target ≤4.33s)
- ✅ Preserve C API compatibility (internal changes only)
- ✅ Zero-cost abstraction (inline, no runtime overhead)

### Benefits

1. **Type Safety**: Prevents pointer/size mismatches at compile time
2. **Const-Correctness**: `span<const T>` vs `span<T>` enforces mutability
3. **Expressiveness**: Intent is clearer (view over array, not ownership)
4. **Debug Safety**: Automatic bounds checking in debug builds
5. **Modern C++**: Aligns with C++20/23 best practices
6. **Zero Cost**: Optimizes to same assembly as raw pointers

### Risks & Mitigations

| Risk | Mitigation |
|------|------------|
| Performance regression | Benchmark after every phase; revert if >3% |
| Dangling spans after reallocation | Clear guidelines on span lifetime |
| C API compatibility | Only internal changes, C API unchanged |
| Learning curve | Comprehensive examples and documentation |

---

## Background: What is std::span?

`std::span<T>` is a C++20 feature providing a **non-owning view** over a contiguous sequence of elements:

```cpp
// Before: Raw pointer + size
void process(const Instruction* code, int size);

// After: std::span (clearer intent, type-safe)
void process(std::span<const Instruction> code);
```

**Properties**:
- **Non-owning**: Does not manage memory (like a reference)
- **Zero-cost**: Compiles to same code as raw pointer + size
- **Bounds-checked**: In debug builds, catches out-of-bounds access
- **Const-correct**: `span<const T>` vs `span<T>` enforces mutability

---

## Phase 1: Foundation & Infrastructure

**Goal**: Set up span usage patterns and add helper utilities
**Files**: New header, documentation, examples
**Risk**: LOW
**Estimated Impact**: ~50 insertions

### 1.1: Create Span Utilities Header

Create `src/core/lspan.h`:

```cpp
/*
** $Id: lspan.h $
** std::span utilities for Lua
** See Copyright Notice in lua.h
*/

#ifndef lspan_h
#define lspan_h

#include <span>
#include "llimits.h"

/*
** Type aliases for common Lua spans
** These improve readability and provide a central place for span types
*/

// Forward declarations
class TValue;
struct Instruction;
class Proto;
struct Upvaldesc;
struct LocVar;
struct AbsLineInfo;

// Common span types
using InstructionSpan = std::span<Instruction>;
using ConstInstructionSpan = std::span<const Instruction>;

using TValueSpan = std::span<TValue>;
using ConstTValueSpan = std::span<const TValue>;

using ProtoSpan = std::span<Proto*>;
using ConstProtoSpan = std::span<Proto* const>;

using UpvaldescSpan = std::span<Upvaldesc>;
using ConstUpvaldescSpan = std::span<const Upvaldesc>;

using ByteSpan = std::span<lu_byte>;
using ConstByteSpan = std::span<const lu_byte>;

using CharSpan = std::span<char>;
using ConstCharSpan = std::span<const char>;

/*
** Helper functions for span creation
** These make it easier to create spans from pointer + size pairs
*/

// Create span from pointer + size (runtime size)
template<typename T>
inline constexpr std::span<T> makeSpan(T* ptr, size_t size) noexcept {
    return std::span<T>(ptr, size);
}

// Create span from pointer + size (compile-time size)
template<typename T, size_t N>
inline constexpr std::span<T, N> makeSpan(T* ptr) noexcept {
    return std::span<T, N>(ptr, N);
}

// Create empty span
template<typename T>
inline constexpr std::span<T> emptySpan() noexcept {
    return std::span<T>();
}

#endif
```

**Validation**:
```bash
cmake --build build
cd testes && ../build/lua all.lua
```

### 1.2: Documentation & Guidelines

Create span usage guidelines document:

```markdown
# std::span Usage Guidelines

## When to Use std::span

✅ **DO use span for**:
- Function parameters viewing arrays/buffers
- Read-only access to sequences (`span<const T>`)
- Mutable access to sequences (`span<T>`)
- Subset/slice operations (`.subspan()`)

❌ **DON'T use span for**:
- Owning pointers (use unique_ptr/manual management)
- Members that outlive reallocation (use pointer + size)
- C API functions (keep raw pointers for C compatibility)

## Lifetime Rules

⚠️ **CRITICAL**: Spans are non-owning views. They become invalid when:
1. The underlying buffer is reallocated
2. The underlying buffer is freed
3. The function that created the span returns (if span outlives scope)

### Safe Pattern (Function Parameter):
```cpp
void process(std::span<const Instruction> code) {
    // Use 'code' span within function - SAFE
    for (const auto& inst : code) {
        // ...
    }
} // Span goes out of scope - SAFE
```

### Unsafe Pattern (Dangling Span):
```cpp
std::span<Instruction> getCode(Proto* p) {
    return std::span(p->getCode(), p->getCodeSize());
    // DANGER: Span outlives function, but buffer could be reallocated
}

void caller() {
    auto code = getCode(proto);
    // ... later ...
    luaM_realloc(...);  // Reallocation invalidates 'code' span!
    code[0];  // UNDEFINED BEHAVIOR - dangling span
}
```

### Safe Pattern (Immediate Use):
```cpp
void caller(Proto* p) {
    auto code = std::span(p->getCode(), p->getCodeSize());
    process(code);  // Use immediately - SAFE (if no reallocation in process)
}
```

## Const-Correctness

Always use `span<const T>` for read-only access:

```cpp
// Good: Clear read-only intent
void print(std::span<const TValue> values);

// Bad: Suggests mutation (even if function doesn't mutate)
void print(std::span<TValue> values);
```

## Migration Patterns

### Pattern 1: Function Parameter (pointer + size → span)

**Before**:
```cpp
void process(const Instruction* code, int size) {
    for (int i = 0; i < size; i++) {
        // use code[i]
    }
}

// Caller
process(proto->getCode(), proto->getCodeSize());
```

**After**:
```cpp
void process(std::span<const Instruction> code) {
    for (const auto& inst : code) {  // Range-based for loop!
        // use inst
    }
}

// Caller (option 1: explicit span)
process(std::span(proto->getCode(), proto->getCodeSize()));

// Caller (option 2: implicit conversion if parameter accepts span)
process({proto->getCode(), proto->getCodeSize()});
```

### Pattern 2: Class Method Returning Span (Safe)

**Before**:
```cpp
class Proto {
    Instruction* getCode() const { return code; }
    int getCodeSize() const { return sizecode; }
};

// Caller must remember both
auto* code = p->getCode();
int size = p->getCodeSize();
```

**After**:
```cpp
class Proto {
    std::span<Instruction> getCodeSpan() {
        return std::span(code, sizecode);
    }
    std::span<const Instruction> getCodeSpan() const {
        return std::span(code, sizecode);
    }

    // Keep old accessors for C compatibility
    Instruction* getCode() const { return code; }
    int getCodeSize() const { return sizecode; }
};

// Caller gets both in one call
auto code = p->getCodeSpan();
for (const auto& inst : code) { /* ... */ }
```

⚠️ **IMPORTANT**: Only use span-returning methods when the span will be used immediately and the underlying buffer won't be reallocated during use.

### Pattern 3: Array Member with Size

For class members, keep pointer + size (not span member):

```cpp
class Proto {
private:
    Instruction* code;  // Keep pointer (owns/references memory)
    int sizecode;       // Keep size

public:
    // Provide span accessor for convenient access
    std::span<Instruction> getCodeSpan() {
        return std::span(code, sizecode);
    }

    // Keep raw accessors for C API and GC
    Instruction* getCode() const { return code; }
    int getCodeSize() const { return sizecode; }
};
```

**Why not `std::span<Instruction> code;` as member?**
- Spans are views, not storage
- Reallocation requires updating pointer + size separately
- GC and C API need raw pointers
```

**Validation**: Documentation review

---

## Phase 2: Proto Code Array (Low Risk Pilot)

**Goal**: Convert Proto's `code` array to use span accessors
**Files**: `src/objects/lobject.h`, `src/objects/lfunc.cpp`, `src/vm/lvm.cpp`, `src/compiler/lcode.cpp`
**Risk**: LOW (frequently accessed, but changes are localized)
**Estimated Impact**: ~80 changes

### 2.1: Add Span Accessors to Proto

**File**: `src/objects/lobject.h` (Proto class)

```cpp
class Proto : public GCBase<Proto> {
private:
    Instruction* code;
    int sizecode;
    // ... other fields ...

public:
    /* Existing accessors - KEEP for C API compatibility */
    inline Instruction* getCode() const noexcept { return code; }
    inline int getCodeSize() const noexcept { return sizecode; }

    /* NEW: Span accessors for modern C++ code */
    inline std::span<Instruction> getCodeSpan() noexcept {
        return std::span(code, sizecode);
    }
    inline std::span<const Instruction> getCodeSpan() const noexcept {
        return std::span(code, sizecode);
    }

    /* NEW: Get instruction at index (bounds-checked in debug) */
    inline Instruction& getInstructionAt(int pc) noexcept {
        lua_assert(pc >= 0 && pc < sizecode);
        return code[pc];
    }
    inline const Instruction& getInstructionAt(int pc) const noexcept {
        lua_assert(pc >= 0 && pc < sizecode);
        return code[pc];
    }
};
```

### 2.2: Convert VM Interpreter (lvm.cpp)

Update bytecode interpreter to use span-based access where beneficial:

**Before**:
```cpp
// In luaV_execute
const Instruction* code = cl->p->getCode();
int size = cl->p->getCodeSize();

for (;;) {
    Instruction i = code[pc++];
    // ...
}
```

**After**:
```cpp
// In luaV_execute
auto code = cl->p->getCodeSpan();

for (;;) {
    Instruction i = code[pc++];  // Same access pattern, bounds-checked in debug
    // ...
}
```

**Note**: VM hot path - verify zero performance regression!

### 2.3: Convert Compiler (lcode.cpp)

Update code generation functions:

**Before**:
```cpp
void FuncState::patchlist(int list, int target) {
    Instruction* code = f->getCode();
    // ...
    code[pc] = instruction;
}
```

**After**:
```cpp
void FuncState::patchlist(int list, int target) {
    auto code = f->getCodeSpan();
    // ...
    code[pc] = instruction;  // Same access, safer
}
```

### 2.4: Validation

```bash
cmake --build build
cd testes && ../build/lua all.lua

# Benchmark (5 runs)
for i in 1 2 3 4 5; do \
    ../build/lua all.lua 2>&1 | grep "total time:"; \
done

# Target: ≤4.33s (≤3% regression from 4.20s baseline)
```

**Commit**: `Phase 112: Add span accessors to Proto code array`

---

## Phase 3: Proto Constants Array

**Goal**: Convert Proto's constants array (`k`, `sizek`)
**Files**: `src/objects/lobject.h`, compiler files, VM files
**Risk**: MEDIUM (TValue array, widely used)
**Estimated Impact**: ~60 changes

### 3.1: Add Span Accessors

```cpp
class Proto : public GCBase<Proto> {
public:
    /* Existing */
    inline TValue* getConstants() const noexcept { return k; }
    inline int getConstantsSize() const noexcept { return sizek; }

    /* NEW */
    inline std::span<TValue> getConstantsSpan() noexcept {
        return std::span(k, sizek);
    }
    inline std::span<const TValue> getConstantsSpan() const noexcept {
        return std::span(k, sizek);
    }

    inline TValue& getConstantAt(int idx) noexcept {
        lua_assert(idx >= 0 && idx < sizek);
        return k[idx];
    }
    inline const TValue& getConstantAt(int idx) const noexcept {
        lua_assert(idx >= 0 && idx < sizek);
        return k[idx];
    }
};
```

### 3.2: Update Callsites

Convert functions that iterate over constants:

```cpp
// Before
TValue* k = p->getConstants();
int nk = p->getConstantsSize();
for (int i = 0; i < nk; i++) {
    // process k[i]
}

// After
auto k = p->getConstantsSpan();
for (const auto& constant : k) {  // Range-based for!
    // process constant
}
```

### 3.3: Validation

Same as Phase 2: Build, test, benchmark.

**Commit**: `Phase 113: Add span accessors to Proto constants array`

---

## Phase 4: Proto Nested Protos Array

**Goal**: Convert Proto's nested prototypes array (`p`, `sizep`)
**Files**: `src/objects/lobject.h`, GC files, compiler files
**Risk**: LOW (Proto** array, less frequently accessed)
**Estimated Impact**: ~40 changes

### 4.1: Add Span Accessors

```cpp
class Proto : public GCBase<Proto> {
public:
    /* Existing */
    inline Proto** getProtos() const noexcept { return p; }
    inline int getProtosSize() const noexcept { return sizep; }

    /* NEW */
    inline std::span<Proto*> getProtosSpan() noexcept {
        return std::span(p, sizep);
    }
    inline std::span<Proto* const> getProtosSpan() const noexcept {
        return std::span(p, sizep);
    }

    inline Proto*& getProtoAt(int idx) noexcept {
        lua_assert(idx >= 0 && idx < sizep);
        return p[idx];
    }
    inline Proto* const& getProtoAt(int idx) const noexcept {
        lua_assert(idx >= 0 && idx < sizep);
        return p[idx];
    }
};
```

### 4.2: Update GC Traversal

```cpp
// Before
Proto** p = proto->getProtos();
int np = proto->getProtosSize();
for (int i = 0; i < np; i++) {
    markobject(g, p[i]);
}

// After
auto protos = proto->getProtosSpan();
for (Proto* nested : protos) {
    markobject(g, nested);
}
```

### 4.3: Validation

Build, test, benchmark.

**Commit**: `Phase 114: Add span accessors to Proto nested protos array`

---

## Phase 5: Proto Upvalues Array

**Goal**: Convert Proto's upvalues array (`upvalues`, `sizeupvalues`)
**Files**: `src/objects/lobject.h`, compiler files
**Risk**: LOW
**Estimated Impact**: ~50 changes

### 5.1: Add Span Accessors

```cpp
class Proto : public GCBase<Proto> {
public:
    /* Existing */
    inline Upvaldesc* getUpvalues() const noexcept { return upvalues; }
    inline int getUpvaluesSize() const noexcept { return sizeupvalues; }

    /* NEW */
    inline std::span<Upvaldesc> getUpvaluesSpan() noexcept {
        return std::span(upvalues, sizeupvalues);
    }
    inline std::span<const Upvaldesc> getUpvaluesSpan() const noexcept {
        return std::span(upvalues, sizeupvalues);
    }

    inline Upvaldesc& getUpvalueAt(int idx) noexcept {
        lua_assert(idx >= 0 && idx < sizeupvalues);
        return upvalues[idx];
    }
    inline const Upvaldesc& getUpvalueAt(int idx) const noexcept {
        lua_assert(idx >= 0 && idx < sizeupvalues);
        return upvalues[idx];
    }
};
```

### 5.2: Update Compiler

Convert upvalue handling in compiler:

```cpp
// Before
Upvaldesc* upvalues = fs->f->getUpvalues();
int nups = fs->f->getUpvaluesSize();
for (int i = 0; i < nups; i++) {
    if (upvalues[i].instack == vidx) {
        // ...
    }
}

// After
auto upvalues = fs->f->getUpvaluesSpan();
for (const auto& uv : upvalues) {
    if (uv.instack == vidx) {
        // ...
    }
}
```

### 5.3: Validation

Build, test, benchmark.

**Commit**: `Phase 115: Add span accessors to Proto upvalues array`

---

## Phase 6: ProtoDebugInfo Arrays

**Goal**: Convert debug info arrays (lineinfo, abslineinfo, locvars)
**Files**: `src/objects/lobject.h`, `src/core/ldebug.cpp`
**Risk**: LOW (debug info, not performance-critical)
**Estimated Impact**: ~60 changes

### 6.1: Add Span Accessors to ProtoDebugInfo

```cpp
class ProtoDebugInfo {
private:
    ls_byte* lineinfo;
    int sizelineinfo;
    AbsLineInfo* abslineinfo;
    int sizeabslineinfo;
    LocVar* locvars;
    int sizelocvars;
    // ...

public:
    /* Existing accessors - KEEP */
    inline ls_byte* getLineInfo() const noexcept { return lineinfo; }
    inline int getLineInfoSize() const noexcept { return sizelineinfo; }
    // ... etc ...

    /* NEW: Span accessors */
    inline std::span<ls_byte> getLineInfoSpan() noexcept {
        return std::span(lineinfo, sizelineinfo);
    }
    inline std::span<const ls_byte> getLineInfoSpan() const noexcept {
        return std::span(lineinfo, sizelineinfo);
    }

    inline std::span<AbsLineInfo> getAbsLineInfoSpan() noexcept {
        return std::span(abslineinfo, sizeabslineinfo);
    }
    inline std::span<const AbsLineInfo> getAbsLineInfoSpan() const noexcept {
        return std::span(abslineinfo, sizeabslineinfo);
    }

    inline std::span<LocVar> getLocVarsSpan() noexcept {
        return std::span(locvars, sizelocvars);
    }
    inline std::span<const LocVar> getLocVarsSpan() const noexcept {
        return std::span(locvars, sizelocvars);
    }
};
```

### 6.2: Update Debug Functions

Convert `luaG_*` functions in `ldebug.cpp`:

```cpp
// Before
LocVar* locvars = p->getDebugInfo().getLocVars();
int nlocvars = p->getDebugInfo().getLocVarsSize();
for (int i = 0; i < nlocvars; i++) {
    if (locvars[i].startpc <= pc && pc < locvars[i].endpc) {
        // ...
    }
}

// After
auto locvars = p->getDebugInfo().getLocVarsSpan();
for (const auto& var : locvars) {
    if (var.startpc <= pc && pc < var.endpc) {
        // ...
    }
}
```

### 6.3: Validation

Build, test, benchmark (minimal performance impact expected).

**Commit**: `Phase 116: Add span accessors to ProtoDebugInfo arrays`

---

## Phase 7: Table Array

**Goal**: Convert Table's array portion (`array`, `asize`)
**Files**: `src/objects/ltable.h`, `src/objects/ltable.cpp`
**Risk**: MEDIUM (hot path for table access)
**Estimated Impact**: ~70 changes

### 7.1: Add Span Accessors to Table

```cpp
class Table : public GCBase<Table> {
private:
    Value* array;
    unsigned int asize;
    // ...

public:
    /* Existing accessors */
    inline Value* getArray() noexcept { return array; }
    inline unsigned int arraySize() const noexcept { return asize; }

    /* NEW: Span accessors */
    inline std::span<Value> getArraySpan() noexcept {
        return std::span(array, asize);
    }
    inline std::span<const Value> getArraySpan() const noexcept {
        return std::span(array, asize);
    }

    inline Value& getArrayAt(unsigned int idx) noexcept {
        lua_assert(idx < asize);
        return array[idx];
    }
    inline const Value& getArrayAt(unsigned int idx) const noexcept {
        lua_assert(idx < asize);
        return array[idx];
    }
};
```

### 7.2: Update Table Operations

**CAREFUL**: Table operations are hot paths. Only convert where there's clear benefit.

```cpp
// Good candidate: Iteration over array
void luaH_printArray(const Table* t) {
    auto arr = t->getArraySpan();
    for (const auto& val : arr) {
        luaO_print(&val);
    }
}

// Keep as-is: Hot path indexed access (already fast)
TValue* luaH_getint(Table* t, lua_Integer key) {
    if (cast_uint(key - 1) < t->arraySize()) {
        return &t->getArray()[key - 1];  // Direct pointer access - keep
    }
    // ...
}
```

### 7.3: Validation

**CRITICAL**: Benchmark thoroughly - tables are performance-sensitive!

```bash
cmake --build build
cd testes && ../build/lua all.lua

# Benchmark 10 runs (high confidence)
for i in {1..10}; do \
    ../build/lua all.lua 2>&1 | grep "total time:"; \
done

# Verify ≤4.33s
```

**Commit**: `Phase 117: Add span accessors to Table array portion`

---

## Phase 8: Buffer Operations (Mbuffer, Zio)

**Goal**: Convert buffer classes to use span accessors
**Files**: `src/serialization/lzio.h`, `src/compiler/llex.cpp`
**Risk**: LOW (mostly lexer buffers)
**Estimated Impact**: ~50 changes

### 8.1: Add Span Accessors to Mbuffer

```cpp
class Mbuffer {
public:
    char* buffer;
    size_t n;         // Current length
    size_t buffsize;  // Capacity

    /* Constructor */
    Mbuffer() noexcept : buffer(nullptr), n(0), buffsize(0) {}

    /* NEW: Span accessors */
    // Get span of used portion (n bytes)
    std::span<char> getUsedSpan() noexcept {
        return std::span(buffer, n);
    }
    std::span<const char> getUsedSpan() const noexcept {
        return std::span(buffer, n);
    }

    // Get span of full buffer (buffsize bytes)
    std::span<char> getBufferSpan() noexcept {
        return std::span(buffer, buffsize);
    }
    std::span<const char> getBufferSpan() const noexcept {
        return std::span(buffer, buffsize);
    }
};
```

### 8.2: Add Span Accessors to Zio

```cpp
class Zio {
public:
    size_t n;            // bytes still unread
    const char* p;       // current position in buffer
    lua_Reader reader;
    void* data;
    lua_State* L;

    /* Constructor */
    Zio(lua_State* L_arg, lua_Reader reader_arg, void* data_arg) noexcept
        : n(0), p(nullptr), reader(reader_arg), data(data_arg), L(L_arg) {}

    /* NEW: Span accessor */
    std::span<const char> getUnreadSpan() const noexcept {
        return std::span(p, n);
    }
};
```

### 8.3: Update Lexer Buffer Operations

Convert lexer functions that manipulate buffers:

```cpp
// Before
char* buff = luaZ_buffer(&ls->buff);
size_t len = luaZ_bufflen(&ls->buff);
// process buff[0..len-1]

// After
auto buff = ls->buff.getUsedSpan();
// process buff (cleaner!)
```

### 8.4: Validation

Build, test, benchmark.

**Commit**: `Phase 118: Add span accessors to Mbuffer and Zio`

---

## Phase 9: Function Parameters (Selective Conversion)

**Goal**: Convert function parameters from `(ptr, size)` to `span<T>` where beneficial
**Files**: Multiple (ldebug, lobject, lstring, etc.)
**Risk**: MEDIUM (API changes, many callsites)
**Estimated Impact**: ~150 changes

### 9.1: Identify Candidate Functions

**Good candidates** (read-only, no ownership transfer):
- `luaO_chunkid(char* out, const char* source, size_t srclen)`
  → `luaO_chunkid(char* out, std::span<const char> source)`
- `luaG_printlocals(const Proto* f, int pc, const TValue* locals, int nlocals)`
  → `luaG_printlocals(const Proto* f, int pc, std::span<const TValue> locals)`

**Keep as-is** (C API, ownership, reallocation):
- `luaM_realloc_(lua_State* L, void* block, size_t osize, size_t nsize)`
- Any function in public C API (`lua.h`, `lauxlib.h`)

### 9.2: Convert luaO_chunkid (Example)

**Before**:
```cpp
// lobject.h
LUAI_FUNC void luaO_chunkid(char* out, const char* source, size_t srclen);

// lobject.cpp
void luaO_chunkid(char* out, const char* source, size_t srclen) {
    for (size_t i = 0; i < srclen; i++) {
        // process source[i]
    }
}

// Caller
luaO_chunkid(out, str, strlen(str));
```

**After**:
```cpp
// lobject.h
LUAI_FUNC void luaO_chunkid(char* out, std::span<const char> source);

// lobject.cpp
void luaO_chunkid(char* out, std::span<const char> source) {
    for (char c : source) {  // Range-based for!
        // process c
    }
}

// Caller (implicit conversion from pointer + size)
luaO_chunkid(out, {str, strlen(str)});
```

### 9.3: Convert Debug Functions

Convert `luaG_*` functions that take array parameters:

```cpp
// Before
void luaG_printlocals(lua_State* L, const TValue* stack, int nstack) {
    for (int i = 0; i < nstack; i++) {
        // process stack[i]
    }
}

// After
void luaG_printlocals(lua_State* L, std::span<const TValue> stack) {
    for (const auto& val : stack) {
        // process val
    }
}
```

### 9.4: Update Callsites

Update all callsites to pass spans:

```cpp
// Before
luaG_printlocals(L, L->stack.getStack().p, L->stack.stackSize());

// After
auto stack_view = std::span(L->stack.getStack().p, L->stack.stackSize());
luaG_printlocals(L, stack_view);
```

### 9.5: Validation

Build, test, benchmark after each function conversion.

**Commit**: `Phase 119: Convert function parameters to std::span (batch 1)`

---

## Phase 10: String View Operations

**Goal**: Add span-based string accessors for TString
**Files**: `src/objects/lobject.h`, `src/objects/lstring.cpp`
**Risk**: LOW (additive, existing accessors unchanged)
**Estimated Impact**: ~40 changes

### 10.1: Add Span Accessors to TString

```cpp
class TString : public GCBase<TString> {
public:
    /* Existing accessors - KEEP */
    const char* c_str() const noexcept;
    size_t length() const noexcept;

    /* NEW: Span accessor */
    std::span<const char> getStringSpan() const noexcept {
        return std::span(c_str(), length());
    }

    // For mutable strings (rare)
    std::span<char> getMutableStringSpan() noexcept {
        return std::span(getContentsPtr(), length());
    }
};
```

### 10.2: Use String Spans in String Operations

```cpp
// Before
const char* str = ts->c_str();
size_t len = ts->length();
for (size_t i = 0; i < len; i++) {
    process(str[i]);
}

// After
auto str = ts->getStringSpan();
for (char c : str) {
    process(c);
}
```

### 10.3: Consider std::string_view

**Alternative**: Use `std::string_view` for strings:

```cpp
class TString : public GCBase<TString> {
public:
    std::string_view getStringView() const noexcept {
        return std::string_view(c_str(), length());
    }
};
```

**Pros**:
- More string-specific API (`starts_with`, `ends_with`, `find`, etc.)
- Better for string operations

**Cons**:
- Different type than `span<const char>` (requires conversion)
- Less consistent with other span usage

**Decision**: Use `span<const char>` for consistency, but consider `string_view` for string-heavy operations.

### 10.4: Validation

Build, test, benchmark.

**Commit**: `Phase 120: Add span accessors to TString`

---

## Phase 11: Comprehensive Testing & Validation

**Goal**: Ensure all span conversions are correct and performant
**Files**: Tests, benchmarks
**Risk**: LOW (validation phase)
**Estimated Impact**: Documentation

### 11.1: Run Full Test Suite

```bash
cd /home/peter/claude/lua
cmake --build build
cd testes
../build/lua all.lua
```

**Expected**: `final OK !!!`

### 11.2: Performance Validation

```bash
# 10-run benchmark for high confidence
for i in {1..10}; do \
    ../build/lua all.lua 2>&1 | grep "total time:"; \
done

# Calculate average, ensure ≤4.33s
```

### 11.3: Sanitizer Testing

```bash
# Rebuild with sanitizers
cd /home/peter/claude/lua
cmake -B build -DCMAKE_BUILD_TYPE=Debug \
    -DLUA_ENABLE_ASAN=ON \
    -DLUA_ENABLE_UBSAN=ON
cmake --build build

# Run tests
cd testes && ../build/lua all.lua
```

**Expected**: No sanitizer errors

### 11.4: Bounds Checking Validation

Create test to verify bounds checking works:

```cpp
// In ltests.cpp (test mode)
#ifdef LUA_USE_APICHECK
static void test_span_bounds() {
    Proto* p = /* ... */;
    auto code = p->getCodeSpan();

    // This should trigger assertion in debug builds:
    // code[code.size() + 100];  // Out of bounds!

    // This should be safe:
    if (!code.empty()) {
        Instruction i = code[0];  // OK
    }
}
#endif
```

### 11.5: Documentation Update

Update CLAUDE.md:

```markdown
## Modern C++ Features

### std::span Usage

- **std::span<T>**: Used throughout for array views (pointer + size)
- **Benefits**: Type safety, bounds checking (debug), zero-cost abstraction
- **Examples**:
  - `Proto::getCodeSpan()` - bytecode array view
  - `Proto::getConstantsSpan()` - constants array view
  - `Table::getArraySpan()` - table array portion view

### Span Guidelines

- ✅ Use for function parameters viewing arrays
- ✅ Use for read-only access (`span<const T>`)
- ❌ Don't store spans across reallocation
- ❌ Don't use in C API functions
```

**Commit**: `Phase 121: Comprehensive span testing and validation`

---

## Performance Expectations

### Optimized Builds (Release, -O3)

**Expected**: Zero performance difference

`std::span` compiles to identical assembly as raw pointer + size:

```cpp
// Span version
void process(std::span<const Instruction> code) {
    for (const auto& inst : code) { /* ... */ }
}

// Pointer + size version
void process(const Instruction* code, int size) {
    for (int i = 0; i < size; i++) { /* ... */ }
}

// Assembly: IDENTICAL in optimized builds
```

### Debug Builds (-O0, assertions enabled)

**Expected**: Minimal overhead (<5%), better safety

- Bounds checking enabled (catches bugs early)
- Assertion checks on span access
- More verbose error messages

### Target Performance

| Configuration | Target | Notes |
|--------------|--------|-------|
| Release (-O3) | ≤4.33s | ≤3% regression from 4.20s baseline |
| Debug (-O0) | N/A | Performance not critical for debug builds |

---

## Success Criteria

### Phase Completion Checklist

For each phase:

- [ ] Code changes implemented
- [ ] Build succeeds with zero warnings
- [ ] All tests pass (`final OK !!!`)
- [ ] Benchmark ≤4.33s (release build)
- [ ] Sanitizers pass (debug build)
- [ ] Code reviewed for span lifetime issues
- [ ] Committed with descriptive message

### Overall Success Metrics

- [ ] **20+ arrays converted** to use span accessors
- [ ] **100+ function parameters** converted to spans
- [ ] **Zero performance regression** (≤3% tolerance)
- [ ] **All tests passing** (30+ test files)
- [ ] **Sanitizers clean** (ASAN, UBSAN)
- [ ] **Documentation complete** (CLAUDE.md updated)
- [ ] **Type safety improved** (compile-time checking)
- [ ] **Code clarity improved** (intent clearer)

---

## Rollback Plan

If any phase causes unacceptable regression (>3%):

1. **Identify**: Benchmark shows >4.33s
2. **Revert**: `git revert HEAD` (or `git reset --hard HEAD~1` if not pushed)
3. **Analyze**: Profile to find performance issue
4. **Adjust**:
   - Keep hot paths as raw pointers
   - Use span only for non-critical paths
5. **Retry**: Implement revised approach

---

## Open Questions

1. **Should we use `std::string_view` for TString?**
   - Pro: More string-specific API
   - Con: Different type than `span<const char>`
   - **Decision**: Start with `span<const char>` for consistency, evaluate later

2. **Should LuaStack use spans for stack slices?**
   - Pro: Safer stack access
   - Con: Stack reallocates frequently (span lifetime issues)
   - **Decision**: Evaluate in later phase (not Phase 1-10)

3. **Should we add compile-time sized spans (`span<T, N>`)?**
   - Pro: Even better performance (size known at compile time)
   - Con: Limited applicability (most arrays are runtime-sized)
   - **Decision**: Use runtime-sized spans for now

4. **Should function return types use span?**
   - Pro: Convenient (returns pointer + size in one call)
   - Con: Lifetime issues if span outlives buffer
   - **Decision**: Only for immediate use; document carefully

---

## Timeline Estimate

Assuming careful, incremental development:

| Phase | Description | Estimated Time |
|-------|-------------|----------------|
| 1 | Foundation & Infrastructure | 2-3 hours |
| 2 | Proto code array | 3-4 hours |
| 3 | Proto constants array | 2-3 hours |
| 4 | Proto nested protos | 2-3 hours |
| 5 | Proto upvalues | 2-3 hours |
| 6 | ProtoDebugInfo arrays | 3-4 hours |
| 7 | Table array | 4-5 hours |
| 8 | Buffer operations | 3-4 hours |
| 9 | Function parameters | 8-10 hours |
| 10 | String operations | 2-3 hours |
| 11 | Testing & validation | 4-5 hours |
| **Total** | | **35-47 hours** |

**Note**: Times include implementation, testing, benchmarking, and debugging.

---

## References

- **C++20 std::span**: https://en.cppreference.com/w/cpp/container/span
- **Lua C++ Conversion**: CLAUDE.md
- **Performance Requirements**: ≤4.33s (≤3% from 4.20s baseline)
- **Similar Work**: Phase 94 (LuaStack centralization), Phase 101 (GC modularization)

---

**Status**: Ready for implementation
**Next Step**: Phase 1 - Foundation & Infrastructure
**Owner**: AI Assistant / Developer
**Last Updated**: 2025-11-20
