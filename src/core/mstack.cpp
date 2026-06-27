/*
** Lua Stack Management
** See Copyright Notice in lua.h
*/

#define MOON_CORE

#include "mprefix.h"

#include <algorithm>
#include <climits>
#include <cstring>

#include "moon.h"

#include "mstack.h"
#include "mapi.h"
#include "mdebug.h"
#include "mdo.h"
#include "mfunc.h"
#include "mgc.h"
#include "mmem.h"
#include "mobject.h"
#include "mstate.h"


/*
** Constants for stack management
*/

// Some stack space for error handling
inline constexpr int STACKERRSPACE = 200;

/*
** MOONI_MAXSTACK limits the size of the Lua stack.
** It must fit into INT_MAX/2.
*/
#if !defined(MOONI_MAXSTACK)
#if 1000000 < (std::numeric_limits<int>::max() / 2)
#define MOONI_MAXSTACK           1000000
#else
#define MOONI_MAXSTACK           (std::numeric_limits<int>::max() / 2u)
#endif
#endif

// Maximum stack size that respects size_t
#define MAXSTACK_BYSIZET  ((MAX_SIZET / sizeof(StackValue)) - STACKERRSPACE)

// Minimum between MOONI_MAXSTACK and MAXSTACK_BYSIZET
#define MAXSTACK	cast_int(MOONI_MAXSTACK < MAXSTACK_BYSIZET  \
			        ? MOONI_MAXSTACK : MAXSTACK_BYSIZET)

// Stack size with extra space for error handling
#define ERRORSTACKSIZE	(MAXSTACK + STACKERRSPACE)


/*
** In ISO C, any pointer use after the pointer has been deallocated is
** undefined behavior. So, before a stack reallocation, all pointers
** should be changed to offsets, and after the reallocation they should
** be changed back to pointers. As during the reallocation the pointers
** are invalid, the reallocation cannot run emergency collections.
** Alternatively, we can use the old address after the deallocation.
** That is not strict ISO C, but seems to work fine everywhere.
** The following macro chooses how strict is the code.
*/
#if !defined(MOONI_STRICT_ADDRESS)
#define MOONI_STRICT_ADDRESS	1
#endif


// Conditional stack movement for debugging
#if !defined(HARDSTACKTESTS)
template<typename Pre, typename Pos>
inline void condmovestack(moon_State* L, Pre&& pre, Pos&& pos) noexcept {
	cast_void(L); cast_void(pre); cast_void(pos);
}
#else
// realloc stack keeping its size
template<typename Pre, typename Pos>
inline void condmovestack(moon_State* L, Pre&& pre, Pos&& pos) {
	int sz_ = L->getStackSubsystem().getSize();
	pre();
	L->getStackSubsystem().realloc(L, sz_, 0);
	pos();
}
#endif


/*
** ==================================================================
** Stack Initialization and Cleanup
** ==================================================================
*/

/*
** Initialize a new stack (called from lstate.cpp)
** L is used for memory allocation (may be different from owning thread)
*/
void MoonStack::init(moon_State* L) {
  // allocate stack array
  stack.p = moonM_newvector<StackValue>(L, BASIC_STACK_SIZE + EXTRA_STACK);
  tbclist.p = stack.p;

  // erase new stack
  std::for_each_n(stack.p, BASIC_STACK_SIZE + EXTRA_STACK, [](StackValue& sv) {
    setnilvalue(s2v(&sv));
  });

  stack_last.p = stack.p + BASIC_STACK_SIZE;
  top.p = stack.p + 1;  // will be set properly by caller
}


/*
** Free stack memory (called from lstate.cpp)
*/
void MoonStack::free(moon_State* L) {
  if (stack.p == nullptr)
    return;  // stack not completely built yet
  // free stack
  moonM_freearray(L, stack.p, cast_sizet(getSize() + EXTRA_STACK));
}


/*
** ==================================================================
** Stack Usage Calculation
** ==================================================================
*/

/*
** Compute how much of the stack is being used, by computing the
** maximum top of all call frames in the stack and the current top.
*/
int MoonStack::inUse(const moon_State* L) const {
  const CallInfo* ci_iter;
  int res;
  StkId lim = top.p;

  for (ci_iter = L->getCI(); ci_iter != nullptr; ci_iter = ci_iter->getPrevious()) {
    if (lim < ci_iter->topRef().p)
      lim = ci_iter->topRef().p;
  }

  moon_assert(lim <= stack_last.p + EXTRA_STACK);
  res = cast_int(lim - stack.p) + 1;  // part of stack in use

  if (res < MOON_MINSTACK)
    res = MOON_MINSTACK;  // ensure a minimum size

  return res;
}


/*
** ==================================================================
** Pointer Adjustment for Reallocation
** ==================================================================
*/

#if MOONI_STRICT_ADDRESS

/*
** Change all pointers to the stack into offsets (before reallocation).
*/
void MoonStack::relPointers(moon_State* L) {
  CallInfo* callInfo;
  UpVal* up;

  top.offset = save(top.p);
  tbclist.offset = save(tbclist.p);

  for (up = L->getOpenUpval(); up != nullptr; up = up->getOpenNext())
    up->setOffset(save(up->getLevel()));

  for (callInfo = L->getCI(); callInfo != nullptr; callInfo = callInfo->getPrevious()) {
    callInfo->topRef().offset = save(callInfo->topRef().p);
    callInfo->funcRef().offset = save(callInfo->funcRef().p);
  }
}


/*
** Change back all offsets into pointers (after reallocation).
*/
void MoonStack::correctPointers(moon_State* L, StkId oldstack) {
  CallInfo* callInfo;
  UpVal* up;
  UNUSED(oldstack);

  top.p = restore(top.offset);
  tbclist.p = restore(tbclist.offset);

  for (up = L->getOpenUpval(); up != nullptr; up = up->getOpenNext())
    up->setVP(s2v(restore(up->getOffset())));

  for (callInfo = L->getCI(); callInfo != nullptr; callInfo = callInfo->getPrevious()) {
    callInfo->topRef().p = restore(callInfo->topRef().offset);
    callInfo->funcRef().p = restore(callInfo->funcRef().offset);
    if (callInfo->isLua())
      callInfo->getTrap() = 1;  // signal to update 'trap' in 'moonV_execute'
  }
}

#else  // !MOONI_STRICT_ADDRESS

/*
** Assume that it is fine to use an address after its deallocation,
** as long as we do not dereference it.
*/
void MoonStack::relPointers(moon_State* L) {
  UNUSED(L);  // do nothing
}


/*
** Correct pointers into 'oldstack' to point into new stack.
*/
void MoonStack::correctPointers(moon_State* L, StkId oldstack) {
  CallInfo* callInfo;
  UpVal* up;
  StkId newstack = stack.p;

  if (oldstack == newstack)
    return;

  top.p = top.p - oldstack + newstack;
  tbclist.p = tbclist.p - oldstack + newstack;

  for (up = L->getOpenUpval(); up != nullptr; up = up->getOpenNext())
    up->setVP(s2v(up->getLevel() - oldstack + newstack));

  for (callInfo = L->getCI(); callInfo != nullptr; callInfo = callInfo->getPrevious()) {
    callInfo->topRef().p = callInfo->topRef().p - oldstack + newstack;
    callInfo->funcRef().p = callInfo->funcRef().p - oldstack + newstack;
    if (callInfo->isLua())
      callInfo->getTrap() = 1;  // signal to update 'trap' in 'moonV_execute'
  }
}

#endif  // MOONI_STRICT_ADDRESS


/*
** ==================================================================
** Stack Reallocation
** ==================================================================
*/

/*
** Reallocate stack to exact size 'newsize'.
** Returns 1 on success, 0 on failure.
*/
int MoonStack::realloc(moon_State* L, int newsize, int raiseerror) {
  int oldsize = getSize();
  int i;
  StkId newstack;
  StkId oldstack = stack.p;
  lu_byte oldgcstop = G(L)->getGCStopEm();

  moon_assert(newsize <= MAXSTACK || newsize == ERRORSTACKSIZE);

  relPointers(L);  // change pointers to offsets
  G(L)->setGCStopEm(1);  // stop emergency collection

  newstack = moonM_reallocvector<StackValue>(L, oldstack, oldsize + EXTRA_STACK,
                                   static_cast<size_t>(newsize + EXTRA_STACK));

  G(L)->setGCStopEm(oldgcstop);  // restore emergency collection

  if (l_unlikely(newstack == nullptr)) {  // reallocation failed?
    correctPointers(L, oldstack);  // change offsets back to pointers
    if (raiseerror)
      moonM_error(L);
    else
      return 0;  // do not raise an error
  }

  stack.p = newstack;
  correctPointers(L, oldstack);  // change offsets back to pointers
  stack_last.p = stack.p + newsize;

  // erase new segment
  for (i = oldsize + EXTRA_STACK; i < newsize + EXTRA_STACK; i++)
    setnilvalue(s2v(newstack + i));

  return 1;
}


/*
** Grow stack by at least 'n' elements.
** Returns 1 on success, 0 on failure.
*/
int MoonStack::grow(moon_State* L, int n, int raiseerror) {
  int size = getSize();

  if (l_unlikely(size > MAXSTACK)) {
    /* if stack is larger than maximum, thread is already using the
       extra space reserved for errors, that is, thread is handling
       a stack error; cannot grow further than that. */
    moon_assert(getSize() == ERRORSTACKSIZE);
    if (raiseerror)
      L->errorError();  // stack error inside message handler
    return 0;  // if not 'raiseerror', just signal it
  }
  else if (n < MAXSTACK) {  // avoids arithmetic overflows
    int newsize;
    // Check for overflow in size * 1.5 calculation
    if (size > INT_MAX / 3 * 2) {
      // size + (size >> 1) could overflow, use MAXSTACK
      newsize = MAXSTACK;
    } else {
      newsize = size + (size >> 1);  // tentative new size (size * 1.5)
    }

    // Safe calculation of needed space
    ptrdiff_t stack_used = top.p - stack.p;
    moon_assert(stack_used >= 0 && stack_used <= INT_MAX);
    int needed;
    if (stack_used > INT_MAX - n) {
      // needed calculation would overflow, use MAXSTACK
      needed = MAXSTACK;
    } else {
      needed = cast_int(stack_used) + n;
    }

    if (newsize > MAXSTACK)  // cannot cross the limit
      newsize = MAXSTACK;
    if (newsize < needed)  // but must respect what was asked for
      newsize = needed;
    if (l_likely(newsize <= MAXSTACK))
      return realloc(L, newsize, raiseerror);
  }

  // else stack overflow
  // add extra size to be able to handle the error message
  realloc(L, ERRORSTACKSIZE, raiseerror);
  if (raiseerror)
    moonG_runerror(L, "stack overflow");
  return 0;
}


/*
** Shrink stack to reasonable size.
** Called after function returns to free unused stack space.
*/
void MoonStack::shrink(moon_State* L) {
  int inuse = inUse(L);
  int max = (inuse > MAXSTACK / 3) ? MAXSTACK : inuse * 3;

  /* if thread is currently not handling a stack overflow and its
     size is larger than maximum "reasonable" size, shrink it */
  if (inuse <= MAXSTACK && getSize() > max) {
    int nsize = (inuse > MAXSTACK / 2) ? MAXSTACK : inuse * 2;
    realloc(L, nsize, 0);  // ok if that fails
  }
  else  // don't change stack
    condmovestack(L, [](){}, [](){});  // (change only for debugging)

  moonE_shrinkCI(L);  // shrink CI list
}


/*
** Increment top with stack overflow check.
** Used when pushing a single value.
*/
void MoonStack::incTop(moon_State* L) {
  top.p++;
  moonD_checkstack(L, 1);
}


/*
** ==================================================================
** INDEX CONVERSION OPERATIONS
** ==================================================================
** Convert Lua API indices to internal stack pointers.
** Moved from index2value() and index2stack() in lapi.cpp.
*/

/*
** Convert an acceptable index to a pointer to its respective value.
** Non-valid indices return the special nil value 'G(L)->getNilValue()'.
**
** Replaces index2value() from lapi.cpp.
*/
TValue* MoonStack::indexToValue(moon_State* L, int idx) {
  CallInfo *callInfo = L->getCI();
  if (idx > 0) {
    StkId o = callInfo->funcRef().p + idx;
    api_check(L, idx <= callInfo->topRef().p - (callInfo->funcRef().p + 1), "unacceptable index");
    if (o >= top.p) return G(L)->getNilValue();
    else return s2v(o);
  }
  else if (!ispseudo(idx)) {  // negative index
    api_check(L, idx != 0 && -idx <= top.p - (callInfo->funcRef().p + 1),
                 "invalid index");
    return s2v(top.p + idx);
  }
  else if (idx == MOON_REGISTRYINDEX)
    return G(L)->getRegistry();
  else {  // upvalues
    idx = MOON_REGISTRYINDEX - idx;
    api_check(L, idx <= MAXUPVAL + 1, "upvalue index too large");
    if (ttisCclosure(s2v(callInfo->funcRef().p))) {  // C closure?
      CClosure *func = clCvalue(s2v(callInfo->funcRef().p));
      return (idx <= func->getNumUpvalues()) ? func->getUpvalue(idx-1)
                                      : G(L)->getNilValue();
    }
    else {  // light C function or Lua function (through a hook)?)
      api_check(L, ttislcf(s2v(callInfo->funcRef().p)), "caller not a C function");
      return G(L)->getNilValue();  // no upvalues
    }
  }
}


/*
** Convert a valid actual index (not a pseudo-index) to its address.
**
** Replaces index2stack() from lapi.cpp.
*/
StkId MoonStack::indexToStack(moon_State* L, int idx) {
  CallInfo *callInfo = L->getCI();
  if (idx > 0) {
    StkId o = callInfo->funcRef().p + idx;
    api_check(L, o < top.p, "invalid index");
    return o;
  }
  else {  // non-positive index
    api_check(L, idx != 0 && -idx <= top.p - (callInfo->funcRef().p + 1),
                 "invalid index");
    api_check(L, !ispseudo(idx), "invalid index");
    return top.p + idx;
  }
}


/*
** ==================================================================
** API OPERATION HELPERS
** ==================================================================
** Helper methods for Lua C API validation.
*/

/*
** Check if stack has at least n elements (replaces api_checknelems).
*/
bool MoonStack::checkHasElements(CallInfo* callInfo, int n) const noexcept {
  return (n) < (top.p - callInfo->funcRef().p);
}


/*
** Check if n elements can be popped (replaces api_checkpop).
** Also verifies no to-be-closed variables would be affected.
*/
bool MoonStack::checkCanPop(CallInfo* callInfo, int n) const noexcept {
  return (n) < top.p - callInfo->funcRef().p &&
         tbclist.p < top.p - n;
}


/*
** ==================================================================
** STACK QUERY HELPERS
** ==================================================================
*/

/*
** Get depth relative to function base (current function's local variables).
*/
int MoonStack::getDepthFromFunc(CallInfo* callInfo) const noexcept {
  return cast_int(top.p - (callInfo->funcRef().p + 1));
}


/*
** ==================================================================
** ASSIGNMENT OPERATIONS
** ==================================================================
** Assign values to stack slots with GC awareness.
*/

/*
** Assign to stack slot from TValue.
*/
void MoonStack::setSlot(StackValue* dest, const TValue* src) noexcept {
  *s2v(dest) = *src;
}


/*
** Copy between stack slots.
*/
void MoonStack::copySlot(StackValue* dest, StackValue* src) noexcept {
  *s2v(dest) = *s2v(src);
}


/*
** Set slot to nil.
*/
void MoonStack::setNil(StackValue* slot) noexcept {
  setnilvalue(s2v(slot));
}
