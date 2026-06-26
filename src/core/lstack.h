/*
** Lua Stack Management
** See Copyright Notice in lua.h
*/

#ifndef lstack_h
#define lstack_h

#include "lobject.h"
#include "llimits.h"

/*
** Forward declarations
*/
struct lua_State;
struct CallInfo;
class UpVal;

/*
** LuaStack - Stack management subsystem for lua_State
**
** RESPONSIBILITY:
** This class encapsulates all stack-related operations for a Lua thread.
** It manages the dynamic stack that holds Lua values during execution.
**
** DESIGN:
** - Single Responsibility: Handles ONLY stack management
** - Zero-cost abstraction: All accessors are inline
** - Private fields: Full encapsulation with accessor methods
** - Owned by lua_State: lua_State delegates stack operations to this subsystem
**
** STACK STRUCTURE:
** The Lua stack is a dynamically-sized array of StackValue slots:
**
**   stack.p ───────┬─────────────┐
**                  │ slot 0      │ (function being called)
**                  ├─────────────┤
**                  │ slot 1      │ (first argument/local)
**                  ├─────────────┤
**                  │ ...         │
**                  ├─────────────┤
**   top.p ─────────┤             │ (first free slot)
**                  ├─────────────┤
**                  │ ...         │ (available space)
**                  ├─────────────┤
**   stack_last.p ──┤             │ (end of usable stack)
**                  ├─────────────┤
**                  │ EXTRA_STACK │ (reserved for error handling)
**                  └─────────────┘
**
** DYNAMIC REALLOCATION:
** The stack grows automatically when more space is needed. During reallocation,
** ALL pointers into the stack become invalid and must be adjusted.
**
** POINTER PRESERVATION:
** Use save()/restore() to convert pointers to offsets before reallocation,
** then convert back to pointers after reallocation.
**
** TO-BE-CLOSED VARIABLES:
** The tbclist field tracks variables that need cleanup (__close metamethod)
** when they go out of scope.
*/
class LuaStack {
private:
  StkIdRel top;         /* first free slot in the stack */
  StkIdRel stack_last;  /* end of stack (last element + 1) */
  StkIdRel stack;       /* stack base */
  StkIdRel tbclist;     /* list of to-be-closed variables */

public:
  /*
  ** Field accessors - return references to allow .p and .offset access
  */

  /* Top pointer accessors */
  StkIdRel& getTop() noexcept { return top; }
  const StkIdRel& getTop() const noexcept { return top; }
  void setTop(StkIdRel t) noexcept { top = t; }

  /* Stack base pointer accessors */
  StkIdRel& getStack() noexcept { return stack; }
  const StkIdRel& getStack() const noexcept { return stack; }
  void setStack(StkIdRel s) noexcept { stack = s; }

  /* Stack limit pointer accessors */
  StkIdRel& getStackLast() noexcept { return stack_last; }
  const StkIdRel& getStackLast() const noexcept { return stack_last; }
  void setStackLast(StkIdRel sl) noexcept { stack_last = sl; }

  /* To-be-closed list pointer accessors */
  StkIdRel& getTbclist() noexcept { return tbclist; }
  const StkIdRel& getTbclist() const noexcept { return tbclist; }
  void setTbclist(StkIdRel tbc) noexcept { tbclist = tbc; }

  /*
  ** Computed properties
  */

  /* Get current stack size (number of usable slots) */
  int getSize() const noexcept {
    return cast_int(stack_last.p - stack.p);
  }

  /* Check if there is space for n more elements */
  bool hasSpace(int n) const noexcept {
    return stack_last.p - top.p > n;
  }

  /*
  ** Pointer preservation methods
  **
  ** These methods convert stack pointers to/from offsets, allowing them
  ** to survive stack reallocation. Always use these before/after reallocating.
  */

  /* Convert stack pointer to offset from base */
  ptrdiff_t save(StkId pt) const noexcept {
    return pt - stack.p;  /* direct pointer arithmetic, no char* round-trip */
  }

  /* Convert offset to stack pointer */
  StkId restore(ptrdiff_t n) const noexcept {
    return stack.p + n;  /* direct pointer arithmetic, safe with LTO */
  }

  /*
  ** ============================================================
  ** BASIC STACK MANIPULATION
  ** ============================================================
  ** Simple operations on the top pointer. These assume space
  ** has already been checked via ensureSpace().
  */

  /* Push one slot (increment top) */
  void push() noexcept {
    top.p++;
  }

  /* Pop one slot (decrement top) */
  void pop() noexcept {
    top.p--;
  }

  /* Pop n slots from stack */
  void popN(int n) noexcept {
    top.p -= n;
  }

  /* Adjust top by n (positive or negative) */
  void adjust(int n) noexcept {
    top.p += n;
  }

  /* Set top to specific pointer value */
  void setTopPtr(StkId ptr) noexcept {
    top.p = ptr;
  }

  /* Set top to offset from stack base */
  void setTopOffset(int offset) noexcept {
    top.p = stack.p + offset;
  }

  /*
  ** ============================================================
  ** API OPERATIONS (with bounds checking)
  ** ============================================================
  ** Operations used by the Lua C API with runtime assertions.
  */

  /* Push with bounds check (replaces api_incr_top macro) */
  void pushChecked(StkId limit) noexcept {
    top.p++;
    lua_assert(top.p <= limit);
  }

  /* Check if stack has at least n elements (replaces api_checknelems) */
  bool checkHasElements(CallInfo* ci, int n) const noexcept;

  /* Check if n elements can be popped (replaces api_checkpop) */
  bool checkCanPop(CallInfo* ci, int n) const noexcept;

  /*
  ** ============================================================
  ** INDEX CONVERSION
  ** ============================================================
  ** Convert Lua API indices to internal stack pointers.
  ** Handles positive indices, negative indices, and pseudo-indices.
  ** Replaces index2value() and index2stack() from lapi.cpp.
  */

  /* Convert API index to TValue* (replaces index2value) */
  TValue* indexToValue(lua_State* L, int idx);

  /* Convert API index to StkId (replaces index2stack) */
  StkId indexToStack(lua_State* L, int idx);

  /*
  ** ============================================================
  ** SPACE CHECKING
  ** ============================================================
  ** Ensure the stack has enough space, growing if necessary.
  ** Replaces luaD_checkstack() and checkstackp from ldo.h.
  */

  /* Ensure space for n elements (replaces luaD_checkstack) */
  int ensureSpace(lua_State* L, int n) {
    if (l_unlikely(stack_last.p - top.p <= n)) {
      return grow(L, n, 1);
    }
#if defined(HARDSTACKTESTS)
    else {
      int sz = getSize();
      realloc(L, sz, 0);
    }
#endif
    return 1;
  }

  /* Ensure space preserving pointer (replaces checkstackp) */
  template<typename T>
  T* ensureSpaceP(lua_State* L, int n, T* ptr) {
    if (l_unlikely(stack_last.p - top.p <= n)) {
      ptrdiff_t offset = save(reinterpret_cast<StkId>(ptr));
      grow(L, n, 1);
      return reinterpret_cast<T*>(restore(offset));
    }
#if defined(HARDSTACKTESTS)
    else {
      ptrdiff_t offset = save(reinterpret_cast<StkId>(ptr));
      int sz = getSize();
      realloc(L, sz, 0);
      return reinterpret_cast<T*>(restore(offset));
    }
#endif
    return ptr;
  }

  /*
  ** ============================================================
  ** ASSIGNMENT OPERATIONS
  ** ============================================================
  ** Assign values to stack slots with GC-aware write barriers.
  */

  /* Assign to stack slot from TValue */
  void setSlot(StackValue* dest, const TValue* src) noexcept;

  /* Copy between stack slots */
  void copySlot(StackValue* dest, StackValue* src) noexcept;

  /* Set slot to nil */
  void setNil(StackValue* slot) noexcept;

  /*
  ** ============================================================
  ** STACK QUERIES
  ** ============================================================
  ** Query stack state and dimensions.
  */

  /* Available space before stack_last */
  int getAvailable() const noexcept {
    return cast_int(stack_last.p - top.p);
  }

  /* Current depth (elements from base to top) */
  int getDepth() const noexcept {
    return cast_int(top.p - stack.p);
  }

  /* Depth relative to function base */
  int getDepthFromFunc(CallInfo* ci) const noexcept;

  /* Check if can fit n elements (alias for hasSpace) */
  bool canFit(int n) const noexcept {
    return stack_last.p - top.p > n;
  }

  /*
  ** ============================================================
  ** ELEMENT ACCESS
  ** ============================================================
  ** Direct access to stack elements by index.
  */

  /* Get TValue at absolute offset from stack base (0-indexed) */
  TValue* at(int offset) noexcept {
    lua_assert(offset >= 0 && stack.p + offset < top.p);
    return s2v(stack.p + offset);
  }

  /* Get TValue at offset from top (-1 = top element) */
  TValue* fromTop(int offset) noexcept {
    lua_assert(offset <= 0 && top.p + offset >= stack.p);
    return s2v(top.p + offset);
  }

  /* Get top-most TValue (top - 1) */
  TValue* topValue() noexcept {
    lua_assert(top.p > stack.p);
    return s2v(top.p - 1);
  }

  /*
  ** ============================================================
  ** LEGACY STACK OPERATIONS
  ** ============================================================
  ** These methods predate the aggressive centralization.
  ** Implemented in lstack.cpp.
  */

  /* Increment top with stack check */
  void incTop(lua_State* L);

  /* Shrink stack to reasonable size */
  void shrink(lua_State* L);

  /* Grow stack by at least n elements */
  int grow(lua_State* L, int n, int raiseerror);

  /* Reallocate stack to exact size */
  int realloc(lua_State* L, int newsize, int raiseerror);

  /* Calculate how much of the stack is currently in use */
  int inUse(const lua_State* L) const;

  /*
  ** Stack initialization and cleanup
  */

  /* Initialize a new stack (allocates memory) */
  void init(lua_State* L);

  /* Free stack memory */
  void free(lua_State* L);

  /*
  ** Pointer adjustment for reallocation
  ** These are called internally by realloc()
  */

  /* Convert all stack pointers to offsets (before realloc) */
  void relPointers(lua_State* L);

  /* Convert all offsets back to pointers (after realloc) */
  void correctPointers(lua_State* L, StkId oldstack);
};


#endif
