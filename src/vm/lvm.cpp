/*
** Lua virtual machine
** See Copyright Notice in lua.h
*/

#define LUA_CORE

#include "lprefix.h"

#include <algorithm>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "lua.h"

#include "lapi.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lvm.h"
#include "lvirtualmachine.h"


/*
** By default, use jump tables in the main interpreter loop on gcc
** and compatible compilers.
**
** PERFORMANCE NOTE: Jump tables (computed goto) provide faster dispatch
** in the VM's main interpreter loop compared to a switch statement. GCC
** and Clang can generate a single indirect jump instead of cascading
** comparisons, improving instruction cache utilization and branch prediction.
*/
#if !defined(LUA_USE_JUMPTABLE)
#if defined(__GNUC__)
#define LUA_USE_JUMPTABLE	1
#else
#define LUA_USE_JUMPTABLE	0
#endif
#endif



/*
** Limit for table tag-method (metamethod) chains to prevent infinite loops.
** When __index or __newindex metamethods redirect to other tables/objects,
** this limit ensures we don't loop forever if there's a cycle in the chain.
*/
inline constexpr int MAXTAGLOOP = 2000;


/*
** ===========================================================================
** Type conversion functions
** ===========================================================================
** Moved to lvm_conversion.cpp:
**   - l_strton()              - String to number conversion
**   - luaV_tonumber_()        - Value to float conversion
**   - luaV_flttointeger()     - Float to integer with rounding
**   - luaV_tointegerns()      - Value to integer (no string coercion)
**   - luaV_tointeger()        - Value to integer (with string coercion)
**   - TValue::toNumber()      - TValue conversion methods
**   - TValue::toInteger()
**   - TValue::toIntegerNoString()
** ===========================================================================
*/


/*
** ===========================================================================
** For-loop operations (lua_State methods)
** ===========================================================================
** Moved to lvm_loops.cpp:
**   - lua_State::forLimit()      - Convert for-loop limit to integer
**   - lua_State::forPrep()       - Prepare numerical for loop (OP_FORPREP)
**   - lua_State::floatForLoop()  - Execute float for-loop step
** ===========================================================================
*/


/*
** ===========================================================================
** Table access operations
** ===========================================================================
** Moved to lvm_table.cpp:
**   - luaV_finishget()  - Finish table access with __index metamethod
**   - luaV_finishset()  - Finish table assignment with __newindex metamethod
** ===========================================================================
*/


/*
** ===========================================================================
** Comparison operations
** ===========================================================================
** Moved to lvm_comparison.cpp:
**   - l_strcmp()                 - String comparison with locale support
**   - LTintfloat(), LEintfloat() - Integer vs float comparisons
**   - LTfloatint(), LEfloatint() - Float vs integer comparisons
**   - lua_State::lessThanOthers(), lua_State::lessEqualOthers()
**   - luaV_lessthan(), luaV_lessequal() - Main comparison operations
**   - luaV_equalobj()            - Equality comparison with metamethods
** ===========================================================================
*/


/*
** ===========================================================================
** String concatenation and length operations
** ===========================================================================
** Moved to lvm_string.cpp:
**   - tostring()     - Ensure value is a string (with coercion)
**   - isemptystr()   - Check if string is empty
**   - copy2buff()    - Copy strings from stack to buffer
**   - luaV_concat()  - Main concatenation operation
**   - luaV_objlen()  - Length operator (#) implementation
** ===========================================================================
*/



/*
** create a new Lua closure, push it in the stack, and initialize
** its upvalues.
*/
void lua_State::pushClosure(Proto *p, UpVal **encup, StkId base, StkId ra) {
  auto upvaluesSpan = p->getUpvaluesSpan();
  int nup = static_cast<int>(upvaluesSpan.size());
  LClosure *ncl = LClosure::create(this, nup);
  ncl->setProto(p);
  setclLvalue2s(this, ra, ncl);  // anchor new closure in stack
  int i = 0;
  for (const auto& uv : upvaluesSpan) {  // fill in its upvalues
    if (uv.isInStack())  // upvalue refers to local variable?
      ncl->setUpval(i, luaF_findupval(this, base + uv.getIndex()));
    else  // get upvalue from enclosing function
      ncl->setUpval(i, encup[uv.getIndex()]);
    luaC_objbarrier(this, ncl, ncl->getUpval(i));
    i++;
  }
}


/*
** finish execution of an opcode interrupted by a yield
*/
// luaV_finishOp removed - use VirtualMachine::finishOp() directly




/*
** {==================================================================
** Macros for arithmetic/bitwise/comparison opcodes in 'luaV_execute'
**
** All these macros are to be used exclusively inside the main
** iterpreter loop (function luaV_execute) and may access directly
** the local variables of that function (L, i, pc, ci, etc.).
** ===================================================================
*/

inline constexpr lua_Integer l_addi(lua_State*, lua_Integer a, lua_Integer b) noexcept {
	return intop(+, a, b);
}

inline constexpr lua_Integer l_subi(lua_State*, lua_Integer a, lua_Integer b) noexcept {
	return intop(-, a, b);
}

inline constexpr lua_Integer l_muli(lua_State*, lua_Integer a, lua_Integer b) noexcept {
	return intop(*, a, b);
}

inline constexpr lua_Integer l_band(lua_Integer a, lua_Integer b) noexcept {
	return intop(&, a, b);
}

inline constexpr lua_Integer l_bor(lua_Integer a, lua_Integer b) noexcept {
	return intop(|, a, b);
}

inline constexpr lua_Integer l_bxor(lua_Integer a, lua_Integer b) noexcept {
	return intop(^, a, b);
}

inline constexpr bool l_lti(lua_Integer a, lua_Integer b) noexcept {
	return a < b;
}

inline constexpr bool l_lei(lua_Integer a, lua_Integer b) noexcept {
	return a <= b;
}

inline constexpr bool l_gti(lua_Integer a, lua_Integer b) noexcept {
	return a > b;
}

inline constexpr bool l_gei(lua_Integer a, lua_Integer b) noexcept {
	return a >= b;
}

/*
** NOTE: The VM operation macros (op_arithI, op_arith, op_arithK, op_bitwise, etc.)
** have been converted to lambdas defined inside luaV_execute() for better type safety
** and debuggability. See lines 1378-1514 for the lambda implementations.
*/

// }==================================================================


/*
** {==================================================================
** Function 'luaV_execute': main interpreter loop
** ===================================================================
**
** ARCHITECTURE OVERVIEW:
** This is the heart of the Lua VM - a register-based bytecode interpreter.
** Unlike stack-based VMs (like the JVM or Python's CPython), Lua uses
** registers for local variables and intermediate values, reducing stack
** manipulation overhead.
**
** KEY DESIGN DECISIONS:
** 1. Register-based: Instructions reference register indices (A, B, C fields)
**    rather than implicitly using a stack. This reduces instruction count
**    and improves cache locality.
**
** 2. Inline dispatch: The main loop uses either computed goto (jump tables)
**    or a large switch statement to dispatch instructions. Computed goto is
**    ~10-30% faster on modern CPUs due to better branch prediction.
**
** 3. Hot-path optimization: Common operations (table access, arithmetic on
**    integers) have fast paths inlined directly in the VM loop to avoid
**    function call overhead.
**
** 4. Protect macros: Operations that can raise errors or trigger GC use
**    Protect() macros to save VM state (pc, top) before the operation.
**    This enables proper stack unwinding via C++ exceptions.
**
** 5. Trap mechanism: The 'trap' variable tracks whether hooks are enabled
**    or stack reallocation is needed. Checked before each instruction fetch
**    to handle debugger breakpoints and step-through.
**
** PERFORMANCE CRITICAL: This function processes billions of instructions
** per second. Every cycle counts. Changes here should be benchmarked.
*/

/*
** some macros for common tasks in 'luaV_execute'
*/


/*
** Register and constant access functions (converted from macros to lambdas)
**
** RA, RB, RC: Convert instruction field to stack index (StkId pointer)
** vRA, vRB, vRC: Get TValue* from stack index (s2v = stack-to-value)
** KB, KC: Get constant from constant table using instruction field
** RKC: Get either register or constant depending on 'k' bit in instruction
**
** Example instruction format (iABC):
**   OP_ADD A B C  means: R(A) := R(B) + R(C)
**   OP_ADDK A B C means: R(A) := R(B) + K(C)  [if k bit set]
**
** NOTE: These have been converted to lambdas defined inside luaV_execute()
** for better type safety and debuggability. See lines 1274-1301 for implementations.
*/



/*
** State management functions (converted from macros to lambdas)
**
** updatetrap(ci): Update local trap variable from CallInfo
** updatebase(ci): Update local base pointer from CallInfo
** updatestack(ra,ci,i): Conditionally update base and ra if trap is set
**
** NOTE: These have been converted to lambdas defined inside luaV_execute()
** for better type safety. See lines ~1304-1323 for implementations.
*/


/*
** Control flow functions (converted from macros to lambdas)
**
** dojump(ci,i,e): Execute a jump instruction. The 'updatetrap' allows signals
**                 to stop tight loops. (Without it, the local copy of 'trap'
**                 could never change.)
** donextjump(ci): For test instructions, execute the jump instruction that follows it
** docondjump(cond,ci,i): Conditional jump - skip next instruction if 'cond' is not
**                        what was expected (parameter 'k'), else do next instruction,
**                        which must be a jump.
**
** NOTE: These have been converted to lambdas defined inside luaV_execute()
** for better type safety. See lines ~1331-1345 for implementations.
*/


/*
** Correct global 'pc' (program counter).
** The local 'pc' variable is kept in a register for performance. Before any
** operation that might throw an exception, we must save it to the CallInfo
** so stack unwinding can report the correct error location.
**
** savepc(ci): Save local pc to CallInfo
** savestate(L,ci): Save both pc and top to CallInfo and lua_State
**
** NOTE: These have been converted to lambdas defined inside luaV_execute()
** for better type safety. See lines ~1317-1323 for implementations.
**
** EXCEPTION HANDLING: This implementation uses C++ exceptions instead of
** setjmp/longjmp. When an error is thrown, the exception handler needs
** accurate pc and top values to:
** 1. Report the correct line number in error messages
** 2. Properly unwind the stack to the correct depth
** 3. Close any to-be-closed variables at the right stack level
*/


/*
** function executed during Lua functions at points where the
** function can yield.
*/
#if !defined(luai_threadyield)
inline void luai_threadyield(lua_State* L) noexcept {
  lua_unlock(L);
  lua_lock(L);
}
#endif

/*
** Check if garbage collection is needed and yield thread if necessary.
**
** 'c' is the limit of live values in the stack (typically L->top or ci->top)
**
** PERFORMANCE vs CORRECTNESS: GC is expensive, so we only check conditionally
** (luaC_condGC) rather than forcing collection. The GC uses a debt-based system
** to determine when collection is needed.
**
** The macro saves state before GC (because GC can trigger __gc metamethods that
** might throw errors), then updates trap after (because GC might have changed hooks).
**
** luai_threadyield allows the OS to schedule other threads. Without it, tight
** loops could starve other threads on single-core systems.
*/
#define checkGC(L,c)  \
	{ luaC_condGC(L, (savepc(ci), L->getStackSubsystem().setTopPtr(c)), \
                         updatetrap(ci)); \
           luai_threadyield(L); }


#define vmdispatch(o)	switch(o)
#define vmcase(l)	case l:
#define vmbreak		break


/*
** Execute a Lua function (LClosure) starting at the given CallInfo.
**
** PARAMETERS:
** - L: Lua state (contains stack, current CI, and global state)
** - ci: CallInfo for the function being executed
**
** LOCAL VARIABLES (kept in registers for performance):
** - cl: Current LClosure (Lua function) being executed
** - k: Constant table for current function (cl->proto->k)
** - base: Base of current stack frame (points to function's first register)
** - pc: Program counter (points to next instruction to execute)
** - trap: Cached copy of hook mask (0 if no hooks, non-zero if hooks enabled)
**
** EXECUTION FLOW:
** startfunc: Initialize for a new function call
** returning: Return from a nested call, continue in current function
** ret: Common return point for all return opcodes
**
** The function continues executing until:
** 1. A return instruction is executed and ci has CIST_FRESH flag (new C frame)
** 2. An error is thrown (C++ exception)
** 3. The function yields (coroutine suspend)
*/
// luaV_execute removed - use VirtualMachine::execute() directly

// }==================================================================


/*
** lua_State VM operation methods (delegate to the VirtualMachine class)
*/

void lua_State::execute(CallInfo *callinfo) {
  vm_->execute(callinfo);
}

void lua_State::finishOp() {
  vm_->finishOp();
}

void lua_State::concat(int total) {
  vm_->concat(total);
}

void lua_State::objlen(StkId ra, const TValue *rb) {
  vm_->objlen(ra, rb);
}

LuaT lua_State::finishGet(const TValue *t, TValue *key, StkId val, LuaT tag) {
  return vm_->finishGet(t, key, val, tag);
}

void lua_State::finishSet(const TValue *t, TValue *key, TValue *val, int aux) {
  vm_->finishSet(t, key, val, aux);
}

/*
** lua_State arithmetic operation methods (now delegate to VirtualMachine class)
*/

lua_Integer lua_State::idiv(lua_Integer m, lua_Integer n) {
  return vm_->idiv(m, n);
}

lua_Integer lua_State::mod(lua_Integer m, lua_Integer n) {
  return vm_->mod(m, n);
}

lua_Number lua_State::modf(lua_Number m, lua_Number n) {
  return vm_->modf(m, n);
}
