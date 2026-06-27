# Garbage Collection Models for Lua: Tracing vs. Reference Counting vs. Ownership

**Status:** design / analysis note (no code changes). Explores whether a RAII +
move-semantics memory model could replace Lua's tracing garbage collector, what a
"Rust-like Lua" would look like, and what fits this performance-critical C++23 port.

---

## 1. Purpose

Lua reclaims memory with an incremental, generational **tracing** collector. Two
very different philosophies exist: CPython's **reference counting + cycle
collector**, and Rust's compile-time **ownership/borrowing** (no GC at all). This
note compares all three — plus the modern middle ground, **Perceus** reference
counting (Koka, Lean 4) — and works out what each would mean for the Lua *language*
and for this port, where **performance is paramount** (≤4.33s hard target,
currently ~2.33s) and the **C API ABI is fixed** (`lua.h` byte-stable; `TValue` is
a 16-byte tagged union holding a raw `GCObject*`).

The guiding question: *can RAII + move semantics replace the GC, and at what
semantic cost?*

## 2. The models in brief

**Lua 5.5 — incremental/generational tracing mark-and-sweep.**
No per-object reference count. Every collectable object carries a 2-word intrusive
header (`next`, `tt`, `marked`; see `src/objects/lobject_core.h`) and lives on one
of the global lists (`allgc`, `finobj`, generational `survival`/`old1`/`reallyold`,
the gray lists; see `src/core/lstate.h`). Reachability is computed from roots
(registry, main thread, stacks, `_ENV`) by tri-color marking; unreachable objects
are swept. Mutator and collector interleave via **write barriers**
(`luaC_barrier*` in `src/memory/lgc.h`). Cycles are reclaimed for free because
reachability, not counting, decides liveness.

**CPython — reference counting + generational cycle collector.**
Every `PyObject` has `ob_refcnt`. `Py_INCREF`/`Py_DECREF` adjust it on every
reference change; at zero the object is freed **immediately** (deterministic).
Reference counting alone leaks cycles, so a *separate* generational **cycle
collector** tracks container types (those that can form cycles), periodically does
**trial reference subtraction** via each type's `tp_traverse`, and reclaims groups
that are only reachable from within themselves. CPython is therefore *two*
collectors stacked.

**Rust — compile-time ownership, borrowing, move semantics (no runtime GC).**
Each value has exactly one **owner**; assignment **moves** ownership (the source is
invalidated) unless the type is `Copy`. Temporary shared access is granted by
**borrows** (`&`/`&mut`) whose validity the **borrow checker** proves at compile
time. When the owner leaves scope, `Drop` runs deterministically (RAII). Shared
ownership is opt-in (`Rc`/`Arc`); cycles must be broken manually with `Weak`. There
is **no tracing and no cycle reclamation** — cyclic `Rc` graphs leak, by design.

**Sidebar — Perceus (Koka, Lean 4): the faithful "RAII + move" for a dynamic
language.** The compiler inserts **non-atomic** `dup`/`drop` and does **reuse
analysis**: when it can prove a reference is the last one, it mutates the object
**in place** instead of free+alloc — precisely "move semantics" generalized beyond
lexical scope. This is the closest principled realization of the "RAII replaces GC"
idea. It still needs cycle handling (Koka/Lean stay mostly functional to avoid
cycles) and still pays per-operation counting cost.

## 3. Dimension-by-dimension comparison

| Dimension | Lua (tracing) | CPython (refcount + cycles) | Rust (ownership) | Perceus (Koka/Lean) |
|---|---|---|---|---|
| Per-object overhead | 2-word header (`marked`) | `ob_refcnt` (+ gc header on containers) | none (compile-time) | refcount field |
| Hot-path cost | barriers only on black→white writes; **assignment is free** | inc/dec on **every** reference change | none at runtime | non-atomic inc/dec, often elided by reuse |
| Reclamation timing | lazy, batched at GC steps | **immediate** at refcount 0 | **deterministic** at scope exit | deterministic at last drop |
| Cycles | reclaimed natively | needs separate cycle collector | **leak** unless `Weak` | leak unless broken / aux collector |
| Sharing / aliasing | unrestricted (reference semantics) | unrestricted | explicit (`Rc`/borrows) | unrestricted refs |
| Weak references | weak tables, ephemerons (k/v/kv) | `weakref` objects | `Weak<T>` | weak refs |
| Finalizers | `__gc`: lazy, ordered, resurrection | `__del__` at refcount 0; `tp_finalize` for cycles | `Drop`, deterministic | `drop`, deterministic |
| Pauses / latency | incremental + generational pacing | none from refcount; small cycle pauses | none | none |
| Throughput | high (no counting traffic) | lower (counting traffic) | highest (nothing at runtime) | high-ish (counting, mitigated by reuse) |
| Mutator coordination | write barriers | refcount discipline everywhere | borrow checker (compile time) | compiler-inserted dup/drop |
| Programmer burden | none | none (C-API authors mind refs) | high (ownership/lifetimes) | low (compiler does it) |
| Effect on Lua C API | matches today | would change `TValue`/ABI | would change the language | would change `TValue`/ABI |

## 4. How each handles Lua's hard cases

Idiomatic Lua routinely produces ownership graphs that are **shared and cyclic**:

```lua
local a = {}
local b = a                       -- aliasing: two refs to one object (no copy)
t.field = a                       -- escapes to a longer-lived container
local f = function() return a end -- captured by a closure (escapes its scope)
a.self = a                        -- a cycle
setmetatable(obj, Class)          -- obj <-> Class mutual references (OOP)
```

- **Aliasing** — Lua/CPython: fine (reference semantics). Rust: requires `Rc` or a
  borrow; plain assignment would *move* `a` and invalidate it.
- **Escape** (return / store / capture) — Lua/CPython: fine. Rust: the value must
  be moved into the longer-lived owner or shared via `Rc`; a closure capturing it
  needs `move` + shared ownership.
- **Cycles** — Lua: native. CPython: the cycle collector. Rust/Perceus: **leak**
  unless the cycle is broken with an explicit weak edge.
- **Weak tables / ephemerons** — exist *because* collection is reachability-based
  and lazy. Under refcounting or ownership they change meaning: "weak" becomes
  "does not contribute to the count/ownership," and entries clear
  **deterministically** at the last strong drop rather than "at the next GC."
- **`__gc` finalizers** — Lua runs them lazily, in dependency order, with
  resurrection allowed. Deterministic schemes run them synchronously at the drop
  point, mid-bytecode — changing ordering, resurrection, and re-entrancy/exception
  behavior.

## 5. A "Rust-like Lua"

What would Lua look like if it adopted Rust's ownership model to eliminate the GC?

### 5.1 Core change: values gain an ownership discipline

Today every table/closure/userdata is a **reference type** with shared, mutable
aliasing. A Rust-like Lua would make assignment a **move** by default:

```lua
-- Rust-like Lua (hypothetical)
local a = {}        -- 'a' owns the table
local b = a         -- MOVE: 'b' owns it now; 'a' is invalidated
print(a.x)          -- COMPILE ERROR: use after move

local b = &a        -- borrow: 'b' is a temporary shared view, 'a' still owns
local b = &mut a    -- exclusive borrow: may mutate, no other borrow allowed
```

Shared ownership becomes explicit:

```lua
local a = rc({})    -- reference-counted shared owner
local b = a:clone() -- second owner (count = 2); both valid
a.self = a:weak()   -- a cycle MUST use a weak edge, else it leaks / fails to build
```

### 5.2 What the runtime gains

- **No tracing GC, no `marked` header, no gray lists, no barriers.** The 2-word
  header disappears; objects are freed **deterministically** when their owner scope
  ends or their `rc` count hits zero.
- **Move = in-place reuse.** When ownership transfers and the source is dead, the
  VM can reuse the storage (the Perceus optimization) — excellent for
  throughput-sensitive loops that today churn the allocator.
- **Predictable latency.** No pauses; `collectgarbage()` and its pacing knobs
  become obsolete.

### 5.3 What breaks in existing Lua

This is the crux — it is **not backward compatible**:

- **Aliasing-by-default code stops compiling.** `local b = a; use(a)` is a
  use-after-move; essentially all existing Lua relies on aliasing.
- **OOP via metatables** (`obj <-> metatable`, parent/child links, observer
  patterns) is inherently cyclic → must be rewritten with explicit `weak` edges.
- **Closures capturing upvalues** become moves/borrows; shared mutable upvalues
  (the basis of Lua's closure and iterator idioms) need `rc` + interior mutability.
- **A borrow checker in a dynamic language.** Either you add **lifetimes** and
  static borrow checking (and Lua is no longer a small, embeddable dynamic
  language — most scripts need annotation), or you enforce borrows **dynamically**
  (a `RefCell`-style panic on aliased mutation), which reintroduces per-access cost
  and turns latent aliasing into runtime errors.
- **Tables are arrays *and* hashmaps *and* objects.** Lua's single mutable
  reference type does the work Rust splits across many types with different
  ownership rules; collapsing them under one discipline is awkward.

### 5.4 Semantic changes summary (Rust-like or any deterministic scheme)

1. **Assignment semantics change** from alias to move (or require explicit `rc`).
2. **`__gc`/finalizers become deterministic** RAII drops — synchronous, ordered by
   scope, able to run mid-instruction (re-entrancy / exception hazards).
3. **Weak tables / ephemerons** redefined around counts/ownership, not reachability.
4. **Cycles must be explicit** (`weak`) or they leak / fail to build.
5. **`collectgarbage()` API** becomes meaningless or redefined.
6. **Latency improves, throughput risks regressing** (counting traffic / borrow
   checks) — directly against "performance is paramount" for this interpreter.
7. **The C API breaks**: `TValue` can no longer be a raw-pointer tagged union;
   every embedder and library that touches `lua_State` internals is affected.

## 6. Why pure RAII + move cannot replace tracing

RAII expresses ownership that is **static, unique, and tree-shaped**; `unique_ptr`
+ move transfer a *single* owner along a chain. Lua's object graph is **dynamic,
shared, and cyclic**, decided at runtime by the script. Move semantics handle
ownership *transfer* but do nothing for *sharing* or *cycles* — and both are
idiomatic Lua. Therefore RAII + move can only ever manage the **subset** of values
that are provably unique, non-escaping, and acyclic. Reclaiming the rest requires
either shared ownership (which leaks cycles → needs a cycle collector, i.e.
Python's model) or reachability analysis (i.e. tracing — what Lua already does).

## 7. Recommendation for this project

Given the fixed C API and "performance is paramount," **do not replace the tracing
collector.** The RAII idea pays off only as a **hybrid optimization layered on top
of the existing tracer**, with **zero language-semantic change**:

- **Escape analysis** in the compiler to identify temporaries that provably do not
  escape, alias, or form cycles, and give *those* deterministic, scope-based frees
  (stack/arena allocation, scalar replacement) — the JVM HotSpot approach.
- **Last-use / in-place reuse** (the sound part of Perceus) for short-lived
  table/string temporaries in hot loops, reusing storage instead of alloc+free.
- Keep tracing as the correctness backstop for everything shared, escaping, or
  cyclic.

This captures the *spirit* of RAII + move (deterministic reclamation and in-place
reuse where ownership is statically knowable) while preserving Lua's semantics, the
C API, and throughput. A full ownership model ("Rust-like Lua") is a **different
language**, and a refcount + cycle-collector ("Python-like Lua") trades throughput
for latency — neither fits this project's constraints, but both are worth recording
as the design space.

Where RAII *does* apply cleanly today, without touching the collector, is the
**non-GC owning allocations**: parser/lexer scratch buffers, `Mbuffer`, the dynamic
arrays already wrapped by `src/memory/LuaVector.h`, and the custom allocator in
`src/memory/luaallocator.h`. Those are unique, acyclic, scope-bound resources and
are the natural place for `unique_ptr`/RAII cleanup independent of this analysis.

## 8. References / further reading

- Lua 5.4/5.5 GC: `src/memory/lgc.h`, `src/memory/gc/*`, and the other `docs/GC_*`
  notes in this repo.
- CPython: `Modules/gcmodule.c`, `Include/object.h` (`ob_refcnt`, `tp_traverse`).
- Rust ownership / borrowing; `Rc` / `Arc` / `Weak`.
- Perceus reference counting: Reinking, Xie, de Moura, Leijen — *"Perceus: Garbage
  Free Reference Counting with Reuse"* (Koka, Lean 4).
- Region inference (MLKit); JVM escape analysis / scalar replacement.
