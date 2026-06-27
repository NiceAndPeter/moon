/*
** Stack and Call structure of Lua
** See Copyright Notice in lua.h
*/

#define MOON_CORE

#include "mprefix.h"


#include <cstdlib>
#include <cstring>

#include "moon.h"

#include "mapi.h"
#include "mdebug.h"
#include "mdo.h"
#include "mfunc.h"
#include "mgc.h"
#include "mmem.h"
#include "mobject.h"
#include "mopcodes.h"
#include "mparser.h"
#include "mstate.h"
#include "mstring.h"
#include "mtable.h"
#include "mtm.h"
#include "mundump.h"
#include "mvirtualmachine.h"
#include "mvm.h"
#include "mzio.h"



inline constexpr bool errorstatus(int s) noexcept {
	return (s) > MOON_YIELD;
}


/*
** these macros allow user-specific actions when a thread is
** resumed/yielded.
*/
#if !defined(mooni_userstateresume)
#define mooni_userstateresume(L,n)	((void)L)
#endif

#if !defined(mooni_userstateyield)
#define mooni_userstateyield(L,n)	((void)L)
#endif


/*
** {======================================================
** Error-recovery functions
** =======================================================
*/

/*
** Pure C++ exception handling
**
** MODERNIZATION: Replaced C-style setjmp/longjmp with proper C++ exceptions:
** - Removed jmp_buf from MoonLongJmp struct
** - Removed MOONI_THROW/MOONI_TRY macros
** - Removed conditional compilation for POSIX/ISO C variants
** - Removed #include <setjmp.h>
** - Created MoonException class to carry error status
** - Updated doThrow() to use C++ throw statements
** - Updated rawRunProtected() to use C++ try-catch blocks
**
** Benefits: Cleaner code, no C legacy, idiomatic C++
** Performance: ~2.23s (same as before)
**
** DESIGN RATIONALE:
** C++ exceptions provide several advantages over setjmp/longjmp:
** 1. Automatic destructors: Stack unwinding calls destructors for C++ objects
** 2. Type safety: Can catch different exception types differently
** 3. Better compiler optimizations: Compilers can optimize normal (non-exception) paths
** 4. No need to save/restore register state manually
**
** The MoonException class encapsulates the error status and handler chain pointer.
** This maintains compatibility with Lua's error handling model while using modern C++.
*/
class MoonException {
private:
  TStatus status_;
  struct MoonLongJmp *handler_;  // for chain compatibility

public:
  explicit MoonException(TStatus status, struct MoonLongJmp *handler = nullptr) noexcept
    : status_(status), handler_(handler) {}

  TStatus status() const noexcept { return status_; }
  struct MoonLongJmp* handler() const noexcept { return handler_; }
};

// Error handler chain node (simplified from old longjmp version)
struct MoonLongJmp {
  struct MoonLongJmp *previous;
  TStatus status;  // error code
};


// Convert to moon_State method
void moon_State::setErrorObj(TStatus errcode, StkId oldtop) {
  if (errcode == MOON_ERRMEM) {  // memory error?
    setsvalue2s(this, oldtop, G(this)->getMemErrMsg());  // reuse preregistered msg.
  }
  else {
    moon_assert(errorstatus(errcode));  // must be a real error
    moon_assert(!ttisnil(s2v(getTop().p - 1)));  // with a non-nil object
    *s2v(oldtop) = *s2v(getTop().p - 1);  /* move it to 'oldtop' - use operator= */
  }
  getStackSubsystem().setTopPtr(oldtop + 1);  // top goes back to old top plus error object
}


/*
** Throw a Lua error with the given error code.
**
** EXCEPTION PROPAGATION STRATEGY:
** 1. If current thread has an error handler (errorJmp), throw MoonException
** 2. If no handler in current thread, try to propagate to main thread
** 3. If main thread has no handler, call panic function and abort
**
** ERROR CODES:
** - MOON_ERRRUN: Runtime error (e.g., type error, nil indexing)
** - MOON_ERRMEM: Memory allocation failure
** - MOON_ERRERR: Error in error handler (recursive error)
** - MOON_ERRSYNTAX: Syntax error during compilation
**
** COROUTINE HANDLING:
** When a coroutine errors without a protected call in its own stack,
** we propagate the error to the main thread. This prevents coroutine
** errors from silently terminating the coroutine without notification.
**
** PANIC FUNCTION:
** The panic function is the last chance for the application to handle
** the error before abort(). It can longjmp out or exit gracefully.
** This is set via moon_atpanic() in the C API.
*/
l_noret moon_State::doThrow(TStatus errcode) {
  if (getErrorJmp()) {  // thread has an error handler?
    getErrorJmp()->status = errcode;  // set status
    throw MoonException(errcode, getErrorJmp());  // throw C++ exception
  }
  else {  // thread has no error handler
    GlobalState *g = G(this);
    moon_State *mainth = mainthread(g);
    errcode = moonE_resetthread(this, errcode);  // close all upvalues
    setStatus(errcode);
    if (mainth->getErrorJmp()) {  // main thread has a handler?
      *s2v(mainth->getTop().p) = *s2v(getTop().p - 1);  /* copy error obj. - use operator= */
      mainth->getStackSubsystem().push();
      mainth->doThrow(errcode);  // re-throw in main thread
    }
    else {  // no handler at all; abort
      if (g->getPanic()) {  // panic function?
        moon_unlock(this);
        g->getPanic()(this);  // call panic function (last chance to jump out)
      }
      abort();
    }
  }
}


// Convert to moon_State method
l_noret moon_State::throwBaseLevel(TStatus errcode) {
  if (errorJmp) {
    // unroll error entries up to the first level
    while (errorJmp->previous != nullptr)
      errorJmp = errorJmp->previous;
  }
  doThrow(errcode);
}


/*
** Execute a function in protected mode using C++ exception handling.
**
** PARAMETERS:
** - f: Function pointer to execute (Pfunc = void (*)(moon_State*, void*))
** - ud: User data to pass to the function
**
** RETURN VALUE:
** - MOON_OK (0) if function executed successfully
** - Error code (MOON_ERRRUN, MOON_ERRMEM, etc.) if an error was thrown
**
** MECHANISM:
** 1. Set up a new error handler (MoonLongJmp) in a chain
** 2. Execute function f() inside a try block
** 3. Catch MoonException and extract error code
** 4. Restore previous error handler from chain
** 5. Return status code to caller
**
** ERROR HANDLER CHAIN:
** The chain allows nested protected calls. Each pcall/xpcall creates
** a new handler. When an error is thrown, it propagates up the chain
** until caught by the appropriate handler.
**
** Example call stack:
**   moon_pcall() -> rawRunProtected() -> f() -> error() -> doThrow()
**   doThrow() throws MoonException -> caught here -> returns error status
**
** ENCAPSULATION NOTE:
** This is now a moon_State method rather than a free function, following
** the C++ modernization. All state manipulation uses accessor
** methods (getNumberOfCCalls, setErrorJmp, etc.) rather than direct field access.
*/
TStatus moon_State::rawRunProtected(Pfunc f, void *ud) {
  l_uint32 oldnCcalls = getNumberOfCCalls();
  MoonLongJmp lj;
  lj.status = MOON_OK;
  lj.previous = getErrorJmp();  // chain new error handler
  setErrorJmp(&lj);

  try {
    f(this, ud);  // call function protected
  }
  catch (const MoonException& ex) {  // Lua error
    if (ex.handler() != &lj && ex.handler() != nullptr)  // not the correct level?
      throw;  // rethrow to upper level
    lj.status = ex.status();
  }
  catch (...) {  // non-Lua exception
    lj.status = static_cast<TStatus>(-1);  // create some error code
  }

  setErrorJmp(lj.previous);  // restore old error handler
  setNumberOfCCalls(oldnCcalls);
  return lj.status;
}

// }======================================================


/*
** {==================================================================
** Stack reallocation
** ===================================================================
**
** DYNAMIC STACK:
** Lua's stack grows dynamically as needed. Unlike fixed-size stacks,
** this allows deep recursion when necessary while conserving memory
** for simple scripts.
**
** POINTER INVALIDATION:
** When the stack is reallocated, ALL pointers into the stack become
** invalid. We must either:
** 1. Convert all pointers to offsets before reallocation (MOONI_STRICT_ADDRESS=1)
** 2. Adjust all pointers by the difference between old and new stack (MOONI_STRICT_ADDRESS=0)
**
** The strict mode (option 1) is slower but more correct according to ISO C.
** The non-strict mode (option 2) works on all real platforms but is technically UB.
**
** TRAP SIGNAL:
** After stack reallocation, we set the 'trap' flag in all Lua call frames.
** This signals the VM to update its cached 'base' pointer on the next instruction.
*/

// some stack space for error handling
#define STACKERRSPACE	200


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


// maximum stack size that respects size_t
#define MAXSTACK_BYSIZET  ((MAX_SIZET / sizeof(StackValue)) - STACKERRSPACE)

/*
** Minimum between MOONI_MAXSTACK and MAXSTACK_BYSIZET
** (Maximum size for the stack must respect size_t.)
*/
#define MAXSTACK	cast_int(MOONI_MAXSTACK < MAXSTACK_BYSIZET  \
			        ? MOONI_MAXSTACK : MAXSTACK_BYSIZET)


// stack size with extra space for error handling
#define ERRORSTACKSIZE	(MAXSTACK + STACKERRSPACE)


// raise a stack error while running the message handler
// Convert to moon_State method
l_noret moon_State::errorError() {
  TString *msg = TString::create(this, "error in error handling", 23);
  setsvalue2s(this, getTop().p, msg);
  getStackSubsystem().push();  // assume EXTRA_STACK
  doThrow(MOON_ERRERR);
}


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

/*
** relstack() and correctstack() moved to MoonStack class (lstack.cpp)
** as relPointers() and correctPointers() methods
*/


// }==================================================================


/*
** Call a hook for the given event. Make sure there is a hook to be
** called. (Both 'L->hook' and 'L->hookmask', which trigger this
** function, can be changed asynchronously by signals.)
*/
// Convert to moon_State method
void moon_State::callHook(int event, int line,
                              int ftransfer, int ntransfer) {
  moon_Hook hook_func = getHook();
  if (hook_func && getAllowHook()) {  // make sure there is a hook
    CallInfo *ci_local = callInfo;
    ptrdiff_t top_saved = this->saveStack(getTop().p);  // preserve original 'top'
    ptrdiff_t ci_top = this->saveStack(ci_local->topRef().p);  // idem for 'callInfo->getTop()'
    moon_Debug ar;
    ar.event = event;
    ar.currentline = line;
    ar.i_ci = ci_local;
    transferinfo.ftransfer = ftransfer;
    transferinfo.ntransfer = ntransfer;
    if (ci_local->isLua() && getTop().p < ci_local->topRef().p)
      getStackSubsystem().setTopPtr(ci_local->topRef().p);  // protect entire activation register
    moonD_checkstack(this, MOON_MINSTACK);  // ensure minimum stack size
    if (ci_local->topRef().p < getTop().p + MOON_MINSTACK)
      ci_local->topRef().p = getTop().p + MOON_MINSTACK;
    setAllowHook(0);  // cannot call hooks inside a hook
    ci_local->callStatusRef() |= CIST_HOOKED;
    moon_unlock(this);
    (*hook_func)(this, &ar);
    moon_lock(this);
    moon_assert(!getAllowHook());
    setAllowHook(1);
    ci_local->topRef().p = this->restoreStack(ci_top);
    getStackSubsystem().setTopPtr(this->restoreStack(top_saved));
    ci_local->callStatusRef() &= ~CIST_HOOKED;
  }
}


/*
** Executes a call hook for Lua functions. This function is called
** whenever 'hookmask' is not zero, so it checks whether call hooks are
** active.
*/
// Convert to moon_State method
void moon_State::hookCall(CallInfo *ci_arg) {
  setOldPC(0);  // set 'oldpc' for new function
  if (getHookMask() & MOON_MASKCALL) {  // is call hook on?
    int event = (ci_arg->callStatusRef() & CIST_TAIL) ? MOON_HOOKTAILCALL
                                             : MOON_HOOKCALL;
    Proto *p = ci_arg->getFunc()->getProto();
    (*ci_arg->getSavedPCPtr())++;  // hooks assume 'pc' is already incremented
    callHook(event, -1, 1, p->getNumParams());
    (*ci_arg->getSavedPCPtr())--;  // correct 'pc'
  }
}


/*
** Executes a return hook for Lua and C functions and sets/corrects
** 'oldpc'. (Note that this correction is needed by the line hook, so it
** is done even when return hooks are off.)
*/
// Convert to private moon_State method
void moon_State::retHook(CallInfo *ci_arg, int nres) {
  if (getHookMask() & MOON_MASKRET) {  // is return hook on?
    moon_assert(getTop().p >= getStack().p + nres);  // ensure nres is in bounds
    auto firstres = getTop().p - nres;  // index of first result
    auto delta = 0;  // correction for vararg functions
    if (ci_arg->isLua()) {
      Proto *p = ci_arg->getFunc()->getProto();
      if (p->getFlag() & PF_ISVARARG)
        delta = ci_arg->getExtraArgs() + p->getNumParams() + 1;
    }
    ci_arg->funcRef().p += delta;  // if vararg, back to virtual 'func'
    auto ftransfer = cast_int(firstres - ci_arg->funcRef().p);
    callHook(MOON_HOOKRET, -1, ftransfer, nres);  // call it
    ci_arg->funcRef().p -= delta;
  }
  if ((ci_arg = ci_arg->getPrevious())->isLua())
    setOldPC(ci_arg->getFunc()->getProto()->getPCRelative(ci_arg->getSavedPC()));  // set 'oldpc'
}


/*
** Check whether 'func' has a '__call' metafield. If so, put it in the
** stack, below original 'func', so that 'moonD_precall' can call it.
** Raise an error if there is no '__call' metafield.
** Bits CIST_CCMT in status count how many _call metamethods were
** invoked and how many corresponding extra arguments were pushed.
** (This count will be saved in the 'callstatus' of the call).
**  Raise an error if this counter overflows.
*/
// Convert to private moon_State method
unsigned moon_State::tryFuncTM(StkId func, unsigned status_val) {
  StkId p;
  const TValue *metamethod = moonT_gettmbyobj(this, s2v(func), TMS::TM_CALL);
  if (l_unlikely(ttisnil(metamethod)))  // no metamethod?
    moonG_callerror(this, s2v(func));
  moon_assert(func >= getStack().p && getTop().p > func);  // ensure valid bounds
  for (p = getTop().p; p > func; p--)  // open space for metamethod
    *s2v(p) = *s2v(p-1);  /* shift stack - use operator= */
  getStackSubsystem().push();  // stack space pre-allocated by the caller
  getStackSubsystem().setSlot(func, metamethod);  // metamethod is the new function to be called
  if ((status_val & MAX_CCMT) == MAX_CCMT)  // is counter full?
    moonG_runerror(this, "'__call' chain too long");
  return status_val + (1u << CIST_CCMT);  // increment counter
}


// Generic case for 'moveresult'
// Convert to private moon_State method
void moon_State::genMoveResults(StkId res, int nres,
                                             int wanted) {
  moon_assert(nres >= 0 && getTop().p >= getStack().p + nres);  // ensure nres valid
  StkId firstresult = getTop().p - nres;  // index of first result
  int i;
  if (nres > wanted)  // extra results?
    nres = wanted;  // don't need them
  moon_assert(firstresult >= getStack().p && res >= getStack().p);  // ensure valid pointers
  for (i = 0; i < nres; i++)  // move all results to correct place
    *s2v(res + i) = *s2v(firstresult + i);  /* use operator= */
  for (; i < wanted; i++)  // complete wanted number of results
    setnilvalue(s2v(res + i));
  getStackSubsystem().setTopPtr(res + wanted);  // top points after the last result
}


/*
** Given 'nres' results at 'firstResult', move 'fwanted-1' of them
** to 'res'.  Handle most typical cases (zero results for commands,
** one result for expressions, multiple results for tail calls/single
** parameters) separated. The flag CIST_TBC in 'fwanted', if set,
** forces the switch to go to the default case.
*/
// Convert to private moon_State method
void moon_State::moveResults(StkId res, int nres,
                                          l_uint32 fwanted) {
  switch (fwanted) {  // handle typical cases separately
    case 0 + 1:  // no values needed
      getStackSubsystem().setTopPtr(res);
      return;
    case 1 + 1:  // one value needed
      if (nres == 0)  // no results?
        setnilvalue(s2v(res));  // adjust with nil
      else  // at least one result
        *s2v(res) = *s2v(getTop().p - nres);  /* move it to proper place - use operator= */
      getStackSubsystem().setTopPtr(res + 1);
      return;
    case MOON_MULTRET + 1:
      genMoveResults( res, nres, nres);  // we want all results
      break;
    default: {  // two/more results and/or to-be-closed variables
      int wanted = CallInfo::getNResults(fwanted);
      if (fwanted & CIST_TBC) {  // to-be-closed variables?
        callInfo->setNRes(nres);
        callInfo->callStatusRef() |= CIST_CLSRET;  // in case of yields
        res = moonF_close(this, res, CLOSEKTOP, 1);
        callInfo->callStatusRef() &= ~CIST_CLSRET;
        if (hookmask) {  // if needed, call hook after '__close's
          ptrdiff_t savedres = this->saveStack(res);
          retHook(callInfo, nres);
          res = this->restoreStack(savedres);  // hook can move stack
        }
        if (wanted == MOON_MULTRET)
          wanted = nres;  // we want all results
      }
      genMoveResults( res, nres, wanted);
      break;
    }
  }
}


/*
** Finishes a function call: calls hook if necessary, moves current
** number of results to proper place, and returns to previous call
** info. If function has to close variables, hook must be called after
** that.
*/
// Convert to moon_State method
void moon_State::postCall(CallInfo *ci_arg, int nres) {
  l_uint32 fwanted = ci_arg->callStatusRef() & (CIST_TBC | CIST_NRESULTS);
  if (l_unlikely(getHookMask()) && !(fwanted & CIST_TBC))
    retHook(ci_arg, nres);
  // move results to proper place
  moveResults(ci_arg->funcRef().p, nres, fwanted);
  // function cannot be in any of these cases when returning
  moon_assert(!(ci_arg->callStatusRef() &
        (CIST_HOOKED | CIST_YPCALL | CIST_FIN | CIST_CLSRET)));
  setCI(ci_arg->getPrevious());  // back to caller (after closing variables)
}



inline CallInfo* next_ci(moon_State* L) {
	return L->getCI()->getNext() ? L->getCI()->getNext() : moonE_extendCI(L);
}


/*
** Allocate and initialize CallInfo structure. At this point, the
** only valid fields in the call status are number of results,
** CIST_C (if it's a C function), and number of extra arguments.
** (All these bit-fields fit in 16-bit values.)
*/
// Convert to private moon_State method
CallInfo* moon_State::prepareCallInfo(StkId func, unsigned status_val,
                                                StkId top_arg) {
  CallInfo *ci_new = setCI(next_ci(this));  // new frame
  ci_new->funcRef().p = func;
  moon_assert((status_val & ~(CIST_NRESULTS | CIST_C | MAX_CCMT)) == 0);
  ci_new->callStatusRef() = status_val;
  ci_new->topRef().p = top_arg;
  return ci_new;
}


/*
** precall for C functions
*/
// Convert to private moon_State method
int moon_State::preCallC(StkId func, unsigned status_val,
                                            moon_CFunction f) {
  int n;  // number of returns
  CallInfo *ci_new;
  checkstackp(this, MOON_MINSTACK, func);  // ensure minimum stack size
  ci_new = setCI(prepareCallInfo(func, status_val | CIST_C,
                               getTop().p + MOON_MINSTACK));
  moon_assert(ci_new->topRef().p <= getStackLast().p);
  if (l_unlikely(hookmask & MOON_MASKCALL)) {
    int narg = cast_int(getTop().p - func) - 1;
    callHook(MOON_HOOKCALL, -1, 1, narg);
  }
  moon_unlock(this);
  n = (*f)(this);  // do the actual call
  moon_lock(this);
  api_checknelems(this, n);
  postCall(ci_new, n);
  return n;
}


/*
** Prepare a function for a tail call, building its call info on top
** of the current call info. 'narg1' is the number of arguments plus 1
** (so that it includes the function itself). Return the number of
** results, if it was a C function, or -1 for a Lua function.
*/
// Convert to moon_State method
int moon_State::preTailCall(CallInfo *ci_arg, StkId func,
                                    int narg1, int delta) {
  unsigned status_val = MOON_MULTRET + 1;
 retry:
  switch (ttypetag(s2v(func))) {
    case MoonT::CCL:  // C closure
      return preCallC(func, status_val, clCvalue(s2v(func))->getFunction());
    case MoonT::LCF:  // light C function
      return preCallC(func, status_val, fvalue(s2v(func)));
    case MoonT::LCL: {  // Lua function
      Proto *p = clLvalue(s2v(func))->getProto();
      auto fsize = p->getMaxStackSize();  // frame size
      auto nfixparams = p->getNumParams();
      checkstackp(this, fsize - delta, func);
      ci_arg->funcRef().p -= delta;  // restore 'func' (if vararg)
      for (int i = 0; i < narg1; i++)  // move down function and arguments
        *s2v(ci_arg->funcRef().p + i) = *s2v(func + i);  /* use operator= */
      func = ci_arg->funcRef().p;  // moved-down function
      for (; narg1 <= nfixparams; narg1++)
        setnilvalue(s2v(func + narg1));  // complete missing arguments
      ci_arg->topRef().p = func + 1 + fsize;  // top for new function
      moon_assert(ci_arg->topRef().p <= getStackLast().p);
      ci_arg->setSavedPC(p->getCode());  // starting point
      ci_arg->callStatusRef() |= CIST_TAIL;
      getStackSubsystem().setTopPtr(func + narg1);  // set top
      return -1;
    }
    default: {  // not a function
      checkstackp(this, 1, func);  // space for metamethod
      status_val = tryFuncTM(func, status_val);  // try '__call' metamethod
      narg1++;
      goto retry;  // try again
    }
  }
}


/*
** Prepares the call to a function (C or Lua). For C functions, also do
** the call. The function to be called is at '*func'.  The arguments
** are on the stack, right after the function.  Returns the CallInfo
** to be executed, if it was a Lua function. Otherwise (a C function)
** returns nullptr, with all the results on the stack, starting at the
** original function position.
*/
// Convert to moon_State method
CallInfo* moon_State::preCall(StkId func, int nresults) {
  unsigned status_val = cast_uint(nresults + 1);
  moon_assert(status_val <= MAXRESULTS + 1);
 retry:
  switch (ttypetag(s2v(func))) {
    case MoonT::CCL:  // C closure
      preCallC(func, status_val, clCvalue(s2v(func))->getFunction());
      return nullptr;
    case MoonT::LCF:  // light C function
      preCallC(func, status_val, fvalue(s2v(func)));
      return nullptr;
    case MoonT::LCL: {  // Lua function
      CallInfo *ci_new;
      Proto *p = clLvalue(s2v(func))->getProto();
      auto narg = cast_int(getTop().p - func) - 1;  // number of real arguments
      auto nfixparams = p->getNumParams();
      auto fsize = p->getMaxStackSize();  // frame size
      checkstackp(this, fsize, func);
      ci_new = setCI(prepareCallInfo(func, status_val, func + 1 + fsize));
      ci_new->setSavedPC(p->getCode());  // starting point
      for (; narg < nfixparams; narg++) {
        setnilvalue(s2v(getTop().p));  // complete missing arguments
        getStackSubsystem().push();
      }
      moon_assert(ci_new->topRef().p <= getStackLast().p);
      return ci_new;
    }
    default: {  // not a function
      checkstackp(this, 1, func);  // space for metamethod
      status_val = tryFuncTM(func, status_val);  // try '__call' metamethod
      goto retry;  // try again with metamethod
    }
  }
}


/*
** Call a function (C or Lua) through C. 'inc' can be 1 (increment
** number of recursive invocations in the C stack) or nyci (the same
** plus increment number of non-yieldable calls).
** This function can be called with some use of EXTRA_STACK, so it should
** check the stack before doing anything else. 'moonD_precall' already
** does that.
*/
// Convert to private moon_State method
void moon_State::cCall(StkId func, int nResults, l_uint32 inc) {
  CallInfo *ci_result;
  getNumberOfCCallsRef() += inc;
  if (l_unlikely(getCcalls(this) >= MOONI_MAXCCALLS)) {
    checkstackp(this, 0, func);  // free any use of EXTRA_STACK
    moonE_checkcstack(this);
  }
  if ((ci_result = preCall(func, nResults)) != nullptr) {  // Lua function?
    ci_result->callStatusRef() |= CIST_FRESH;  // mark that it is a "fresh" execute
    getVM().execute(ci_result);  // call it
  }
  getNumberOfCCallsRef() -= inc;
}


/*
** External interface for 'ccall'
*/
// Convert to moon_State method
void moon_State::call(StkId func, int nResults) {
  cCall( func, nResults, 1);
}


/*
** Similar to 'call', but does not allow yields during the call.
*/
// Convert to moon_State method
void moon_State::callNoYield(StkId func, int nResults) {
  cCall( func, nResults, nyci);
}


/*
** Finish the job of 'moon_pcallk' after it was interrupted by an yield.
** (The caller, 'finishCcall', does the final call to 'adjustresults'.)
** The main job is to complete the 'moonD_pcall' called by 'moon_pcallk'.
** If a '__close' method yields here, eventually control will be back
** to 'finishCcall' (when that '__close' method finally returns) and
** 'finishpcallk' will run again and close any still pending '__close'
** methods. Similarly, if a '__close' method errs, 'precover' calls
** 'unroll' which calls ''finishCcall' and we are back here again, to
** close any pending '__close' methods.
** Note that, up to the call to 'moonF_close', the corresponding
** 'CallInfo' is not modified, so that this repeated run works like the
** first one (except that it has at least one less '__close' to do). In
** particular, field CIST_RECST preserves the error status across these
** multiple runs, changing only if there is a new error.
*/
// Convert to private moon_State method
TStatus moon_State::finishPCallK(CallInfo *ci_arg) {
  TStatus status_val = static_cast<TStatus>(ci_arg->getRecoverStatus());  // get original status
  if (l_likely(status_val == MOON_OK))  // no error?
    status_val = MOON_YIELD;  // was interrupted by an yield
  else {  // error
    StkId func = this->restoreStack(ci_arg->getFuncIdx());
    setAllowHook(static_cast<lu_byte>(ci_arg->getOAH()));  // restore 'allowhook'
    func = moonF_close(this, func, status_val, 1);  // can yield or raise an error
    setErrorObj(status_val, func);
    shrinkStack();  // restore stack size in case of overflow
    ci_arg->setRecoverStatus(MOON_OK);  // clear original status
  }
  ci_arg->callStatusRef() &= ~CIST_YPCALL;
  setErrFunc(ci_arg->getOldErrFunc());
  /* if it is here, there were errors or yields; unlike 'moon_pcallk',
     do not change status */
  return status_val;
}


/*
** Completes the execution of a C function interrupted by an yield.
** The interruption must have happened while the function was either
** closing its tbc variables in 'moveresults' or executing
** 'moon_callk'/'moon_pcallk'. In the first case, it just redoes
** 'moonD_poscall'. In the second case, the call to 'finishpcallk'
** finishes the interrupted execution of 'moon_pcallk'.  After that, it
** calls the continuation of the interrupted function and finally it
** completes the job of the 'moonD_call' that called the function.  In
** the call to 'adjustresults', we do not know the number of results
** of the function called by 'moon_callk'/'moon_pcallk', so we are
** conservative and use MOON_MULTRET (always adjust).
*/
// Convert to private moon_State method
void moon_State::finishCCall(CallInfo *ci_arg) {
  int n;  // actual number of results from C function
  if (ci_arg->callStatusRef() & CIST_CLSRET) {  // was closing TBC variable?
    moon_assert(ci_arg->callStatusRef() & CIST_TBC);
    n = ci_arg->getNRes();  // just redo 'moonD_poscall'
    // don't need to reset CIST_CLSRET, as it will be set again anyway
  }
  else {
    TStatus status_val = MOON_YIELD;  // default if there were no errors
    moon_KFunction kf = ci_arg->getK();  // continuation function
    // must have a continuation and must be able to call it
    moon_assert(kf != nullptr && yieldable(this));
    if (ci_arg->callStatusRef() & CIST_YPCALL)  // was inside a 'moon_pcallk'?
      status_val = finishPCallK(ci_arg);  // finish it
    adjustresults(this, MOON_MULTRET);  // finish 'moon_callk'
    moon_unlock(this);
    n = (*kf)(this, APIstatus(status_val), ci_arg->getCtx());  // call continuation
    moon_lock(this);
    api_checknelems(this, n);
  }
  postCall(ci_arg, n);  // finish 'moonD_call'
}


/*
** Executes "full continuation" (everything in the stack) of a
** previously interrupted coroutine until the stack is empty (or another
** interruption long-jumps out of the loop).
*/
// Convert to private moon_State method
void moon_State::unrollContinuation(void *ud) {
  CallInfo *ci_current;
  UNUSED(ud);
  while ((ci_current = getCI()) != getBaseCI()) {  // something in the stack
    if (!ci_current->isLua())  // C function?
      finishCCall( ci_current);  // complete its execution
    else {  // Lua function
      getVM().finishOp();  // finish interrupted instruction
      getVM().execute(ci_current);  // execute down to higher C 'boundary'
    }
  }
}


/*
** Static wrapper for unrollContinuation to be used as Pfunc callback
*/
static void unroll (moon_State *L, void *ud) {
  L->unrollContinuation(ud);
}


/*
** Try to find a suspended protected call (a "recover point") for the
** given thread.
*/
// Convert to private moon_State method
CallInfo* moon_State::findPCall() {
  for (CallInfo *ci_iter = getCI(); ci_iter != nullptr; ci_iter = ci_iter->getPrevious()) {  // search for a pcall
    if (ci_iter->callStatusRef() & CIST_YPCALL)
      return ci_iter;
  }
  return nullptr;  // no pending pcall
}


/*
** Signal an error in the call to 'moon_resume', not in the execution
** of the coroutine itself. (Such errors should not be handled by any
** coroutine error handler and should not kill the coroutine.)
*/
static int resume_error (moon_State *L, const char *msg, int narg) {
  api_checkpop(L, narg);
  L->getStackSubsystem().popN(narg);  // remove args from the stack
  setsvalue2s(L, L->getTop().p, TString::create(L, msg));  // push error message
  api_incr_top(L);
  moon_unlock(L);
  return MOON_ERRRUN;
}


/*
** Do the work for 'moon_resume' in protected mode. Most of the work
** depends on the status of the coroutine: initial state, suspended
** inside a hook, or regularly suspended (optionally with a continuation
** function), plus erroneous cases: non-suspended coroutine or dead
** coroutine.
*/
static void resume (moon_State *L, void *ud) {
  int n = *static_cast<int*>(ud);  // number of arguments
  StkId firstArg = L->getTop().p - n;  // first argument
  CallInfo *callInfo = L->getCI();
  if (L->getStatus() == MOON_OK)  // starting a coroutine?
    L->cCall( firstArg - 1, MOON_MULTRET, 0);  // just call its body
  else {  // resuming from previous yield
    moon_assert(L->getStatus() == MOON_YIELD);
    L->setStatus(MOON_OK);  // mark that it is running (again)
    if (callInfo->isLua()) {  // yielded inside a hook?
      /* undo increment made by 'moonG_traceexec': instruction was not
         executed yet */
      moon_assert(callInfo->getCallStatus() & CIST_HOOKYIELD);
      (*callInfo->getSavedPCPtr())--;
      L->getStackSubsystem().setTopPtr(firstArg);  // discard arguments
      L->getVM().execute(callInfo);  // just continue running Lua code
    }
    else {  // 'common' yield
      if (callInfo->getK() != nullptr) {  // does it have a continuation function?
        moon_unlock(L);
        n = (*callInfo->getK())(L, MOON_YIELD, callInfo->getCtx());  // call continuation
        moon_lock(L);
        api_checknelems(L, n);
      }
      L->postCall( callInfo, n);  // finish 'moonD_call'
    }
    L->unrollContinuation( nullptr);  // run continuation
  }
}


/*
** Unrolls a coroutine in protected mode while there are recoverable
** errors, that is, errors inside a protected call. (Any error
** interrupts 'unroll', and this loop protects it again so it can
** continue.) Stops with a normal end (status == MOON_OK), an yield
** (status == MOON_YIELD), or an unprotected error ('findpcall' doesn't
** find a recover point).
*/
static TStatus precover (moon_State *L, TStatus status) {
  CallInfo *callInfo;
  while (errorstatus(status) && (callInfo = L->findPCall()) != nullptr) {
    L->setCI(callInfo);  // go down to recovery functions
    callInfo->setRecoverStatus(status);  // status to finish 'pcall'
    status = L->rawRunProtected( unroll, nullptr);
  }
  return status;
}


MOON_API int moon_resume (moon_State *L, moon_State *from, int nargs,
                                      int *nresults) {
  TStatus status;
  moon_lock(L);
  if (L->getStatus() == MOON_OK) {  // may be starting a coroutine
    if (L->getCI() != L->getBaseCI())  // not in base level?
      return resume_error(L, "cannot resume non-suspended coroutine", nargs);
    else if (L->getTop().p - (L->getCI()->funcRef().p + 1) == nargs)  // no function?
      return resume_error(L, "cannot resume dead coroutine", nargs);
  }
  else if (L->getStatus() != MOON_YIELD)  // ended with errors?
    return resume_error(L, "cannot resume dead coroutine", nargs);
  L->setNumberOfCCalls((from) ? getCcalls(from) : 0);
  if (getCcalls(L) >= MOONI_MAXCCALLS)
    return resume_error(L, "C stack overflow", nargs);
  L->getNumberOfCCallsRef()++;
  mooni_userstateresume(L, nargs);
  api_checkpop(L, (L->getStatus() == MOON_OK) ? nargs + 1 : nargs);
  status = L->rawRunProtected( resume, &nargs);
   // continue running after recoverable errors
  status = precover(L, status);
  if (l_likely(!errorstatus(status)))
    moon_assert(status == L->getStatus());  // normal end or yield
  else {  // unrecoverable error
    L->setStatus(status);  // mark thread as 'dead'
    L->setErrorObj( status, L->getTop().p);  // push error message
    L->getCI()->topRef().p = L->getTop().p;
  }
  *nresults = (status == MOON_YIELD) ? L->getCI()->getNYield()
                                    : cast_int(L->getTop().p - (L->getCI()->funcRef().p + 1));
  moon_unlock(L);
  return APIstatus(status);
}


MOON_API int moon_isyieldable (moon_State *L) {
  return yieldable(L);
}


MOON_API int moon_yieldk (moon_State *L, int nresults, moon_KContext ctx,
                        moon_KFunction k) {
  CallInfo *callInfo;
  mooni_userstateyield(L, nresults);
  moon_lock(L);
  callInfo = L->getCI();
  api_checkpop(L, nresults);
  if (l_unlikely(!yieldable(L))) {
    if (L != mainthread(G(L)))
      moonG_runerror(L, "attempt to yield across a C-call boundary");
    else
      moonG_runerror(L, "attempt to yield from outside a coroutine");
  }
  L->setStatus(MOON_YIELD);
  callInfo->setNYield(nresults);  // save number of results
  if (callInfo->isLua()) {  // inside a hook?
    moon_assert(!callInfo->isLuaCode());
    api_check(L, nresults == 0, "hooks cannot yield values");
    api_check(L, k == nullptr, "hooks cannot continue after yielding");
  }
  else {
    if ((callInfo->setK(k), k) != nullptr)  // is there a continuation?
      callInfo->setCtx(ctx);  // save context
    L->doThrow( MOON_YIELD);
  }
  moon_assert(callInfo->getCallStatus() & CIST_HOOKED);  // must be inside a hook
  moon_unlock(L);
  return 0;  // return to 'moonD_hook'
}


/*
** Auxiliary structure to call 'moonF_close' in protected mode.
*/
struct CloseP {
  StkId level;
  TStatus status;
};


/*
** Auxiliary function to call 'moonF_close' in protected mode.
*/
static void closepaux (moon_State *L, void *ud) {
  CloseP *pcl = static_cast<CloseP*>(ud);
  pcl->level = moonF_close(L, pcl->level, pcl->status, 0);
}


/*
** Calls 'moonF_close' in protected mode. Return the original status
** or, in case of errors, the new status.
*/
// Convert to moon_State method
TStatus moon_State::closeProtected(ptrdiff_t level, TStatus status_arg) {
  CallInfo *old_ci = getCI();
  lu_byte old_allowhooks = getAllowHook();
  for (;;) {  // keep closing upvalues until no more errors
    struct CloseP pcl;
    pcl.level = this->restoreStack(level); pcl.status = status_arg;
    status_arg = rawRunProtected(&closepaux, &pcl);
    if (l_likely(status_arg == MOON_OK))  // no more errors?
      return pcl.status;
    else {  // an error occurred; restore saved state and repeat
      setCI(old_ci);
      setAllowHook(old_allowhooks);
    }
  }
}


/*
** Call the C function 'func' in protected mode, restoring basic
** thread information ('allowhook', etc.) and in particular
** its stack level in case of errors.
*/
// Convert to moon_State method
TStatus moon_State::pCall(Pfunc func, void *u, ptrdiff_t old_top,
                                  ptrdiff_t ef) {
  TStatus status_result;
  CallInfo *old_ci = getCI();
  lu_byte old_allowhooks = getAllowHook();
  ptrdiff_t old_errfunc = getErrFunc();
  setErrFunc(ef);
  status_result = rawRunProtected(func, u);
  if (l_unlikely(status_result != MOON_OK)) {  // an error occurred?
    setCI(old_ci);
    setAllowHook(old_allowhooks);
    status_result = closeProtected(old_top, status_result);
    setErrorObj(status_result, this->restoreStack(old_top));
    shrinkStack();  // restore stack size in case of overflow
  }
  setErrFunc(old_errfunc);
  return status_result;
}



/*
** Execute a protected parser.
*/
struct SParser {  // data to 'f_parser'
  ZIO *z;
  Mbuffer buff;  // dynamic structure used by the scanner
  Dyndata dyd;  // dynamic structures used by the parser
  const char *mode;
  const char *name;

  // Constructor to properly initialize Dyndata
  explicit SParser(moon_State* L)
    : z(nullptr), buff(), dyd(L), mode(nullptr), name(nullptr) {}
};


static void checkmode (moon_State *L, const char *mode, const char *x) {
  if (strchr(mode, x[0]) == nullptr) {
    moonO_pushfstring(L,
       "attempt to load a %s chunk (mode is '%s')", x, mode);
    L->doThrow( MOON_ERRSYNTAX);
  }
}


static void f_parser (moon_State *L, void *ud) {
  LClosure *cl;
  SParser *p = static_cast<SParser*>(ud);
  const char *mode = p->mode ? p->mode : "bt";
  int c = zgetc(p->z);  // read first character
  if (c == MOON_SIGNATURE[0]) {
    int fixed = 0;
    if (strchr(mode, 'B') != nullptr)
      fixed = 1;
    else
      checkmode(L, mode, "binary");
    cl = moonU_undump(L, p->z, p->name, fixed);
  }
  else {
    checkmode(L, mode, "text");
    cl = moonY_parser(L, p->z, &p->buff, &p->dyd, p->name, c);
  }
  moon_assert(cl->getNumUpvalues() == cl->getProto()->getUpvaluesSize());
  cl->initUpvals(L);
}


// Convert to moon_State method
TStatus moon_State::protectedParser(ZIO *z, const char *name,
                                            const char *mode) {
  SParser p(this);  // Initialize with moon_State - Dyndata uses MoonVector now
  TStatus status_result;
  incnny(this);  // cannot yield during parsing
  p.z = z; p.name = name; p.mode = mode;
  // Dyndata (gt, label, actvar) auto-initialized via MoonVector - no manual setup
  moonZ_initbuffer(this, &p.buff);
  status_result = pCall(f_parser, &p, this->saveStack(getTop().p), getErrFunc());
  moonZ_freebuffer(this, &p.buff);
  // Dyndata auto-cleaned via RAII - no manual free needed
  decnny(this);
  return status_result;
}


/*
** Stack operation methods moved to MoonStack class (lstack.cpp)
** moon_State now delegates to stack_ subsystem via inline methods
*/


