/*
** Lua Virtual Machine - Implementation
** See Copyright Notice in lua.h
*/

#define LUA_CORE

#include "lprefix.h"

#include <algorithm>

#include "lvirtualmachine.h"
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

/*
** Full implementations moved from the former luaV_* functions.
*/

// Helper functions for execute() - copied from lvm.cpp since they're needed here now

#if !defined(luai_threadyield)
inline void luai_threadyield(lua_State* L) noexcept {
  lua_unlock(L);
  lua_lock(L);
}
#endif

/*
** Constants from lvm.cpp needed for VM operations
*/

// Limit for table tag-method (metamethod) chains to prevent infinite loops
inline constexpr int MAXTAGLOOP = 2000;

/*
** Arithmetic and bitwise helper functions for execute()
** (copied from lvm.cpp since they're needed here now)
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


// === EXECUTION ===

void VirtualMachine::execute(CallInfo *callInfo) {
  LClosure *currentClosure;
  TValue *constants;
  StkId stackFrameBase;
  const Instruction *programCounter;
  int hooksEnabled;
#if LUA_USE_JUMPTABLE
#include "ljumptab.h"
#endif

  // Use free function helpers from lvm.h instead of class methods
  using ::tointegerns;
  using ::tonumberns;

  /* Convert operation macros to lambdas for better type safety and debuggability.
   * These lambdas capture local variables (L, pc, base, k, etc.) automatically.
   * Note: User has explicitly allowed performance regression for this conversion.
   */

  // Undefine operation macros to avoid naming conflicts
  #undef op_arithI
  #undef op_arithf_aux
  #undef op_arithf
  #undef op_arithfK
  #undef op_arith_aux
  #undef op_arith
  #undef op_arithK
  #undef op_bitwiseK
  #undef op_bitwise
  #undef op_order
  #undef op_orderI

  // Undefine register access macros to avoid naming conflicts
  #undef RA
  #undef vRA
  #undef RB
  #undef vRB
  #undef KB
  #undef RC
  #undef vRC
  #undef KC
  #undef RKC

  // Undefine state management macros to avoid naming conflicts
  #undef updatetrap
  #undef updatebase
  #undef updatestack
  #undef savepc
  #undef savestate

  // Undefine control flow macros to avoid naming conflicts
  #undef dojump
  #undef donextjump
  #undef docondjump

  // Undefine exception handling macros to avoid naming conflicts
  #undef Protect
  #undef ProtectNT
  #undef halfProtect

  // Undefine VM dispatch macro to avoid naming conflict
  #undef vmfetch

  // Register access lambdas (defined before operation lambdas that use them)
  auto getRegisterA = [&](Instruction i) -> StkId {
    return stackFrameBase + InstructionView(i).a();
  };
  auto getValueA = [&](Instruction i) -> TValue* {
    return s2v(stackFrameBase + InstructionView(i).a());
  };
  [[maybe_unused]] auto getRegisterB = [&](Instruction i) -> StkId {
    return stackFrameBase + InstructionView(i).b();
  };
  auto getValueB = [&](Instruction i) -> TValue* {
    return s2v(stackFrameBase + InstructionView(i).b());
  };
  auto getConstantB = [&](Instruction i) -> TValue* {
    return constants + InstructionView(i).b();
  };
  [[maybe_unused]] auto getRegisterC = [&](Instruction i) -> StkId {
    return stackFrameBase + InstructionView(i).c();
  };
  auto getValueC = [&](Instruction i) -> TValue* {
    return s2v(stackFrameBase + InstructionView(i).c());
  };
  auto getConstantC = [&](Instruction i) -> TValue* {
    return constants + InstructionView(i).c();
  };
  auto getRegisterOrConstantC = [&](Instruction i) -> TValue* {
    return InstructionView(i).testk() ? (constants + InstructionView(i).c()) : s2v(stackFrameBase + InstructionView(i).c());
  };

  // State management lambdas
  auto updateTrap = [&](CallInfo* ci_arg) {
    hooksEnabled = ci_arg->getTrap();
  };
  auto updateStackBase = [&](CallInfo* ci_arg) {
    stackFrameBase = ci_arg->funcRef().p + 1;
  };
  auto updateStackAfterRealloc = [&](StkId& ra_arg, CallInfo* ci_arg, Instruction inst) {
    if (l_unlikely(hooksEnabled)) {
      updateStackBase(ci_arg);
      ra_arg = getRegisterA(inst);
    }
  };
  auto saveProgramCounter = [&](CallInfo* ci_arg) {
    ci_arg->setSavedPC(programCounter);
  };
  auto saveInterpreterState = [&](lua_State* L_arg, CallInfo* ci_arg) {
    saveProgramCounter(ci_arg);
    L_arg->getStackSubsystem().setTopPtr(ci_arg->topRef().p);
  };

  // Control flow lambdas
  auto performJump = [&](CallInfo* ci_arg, Instruction inst, int e) {
    programCounter += InstructionView(inst).sj() + e;
    updateTrap(ci_arg);
  };
  auto performNextJump = [&](CallInfo* ci_arg) {
    Instruction ni = *programCounter;
    performJump(ci_arg, ni, 1);
  };
  auto performConditionalJump = [&](int cond, CallInfo* ci_arg, Instruction inst) {
    if (cond != InstructionView(inst).k())
      programCounter++;
    else
      performNextJump(ci_arg);
  };

  // Exception handling lambdas
  auto protectCall = [&](auto&& expr) {
    saveInterpreterState(L, callInfo);
    expr();
    updateTrap(callInfo);
  };
  auto protectCallNoTop = [&](auto&& expr) {
    saveProgramCounter(callInfo);
    expr();
    updateTrap(callInfo);
  };
  auto halfProtectedCall = [&](auto&& expr) {
    saveInterpreterState(L, callInfo);
    expr();
  };

  /*
  ** Check if garbage collection is needed and yield thread if necessary.
  ** 'c_val' is the limit of live values in the stack (typically L->top or callInfo->top)
  **
  ** PERFORMANCE: GC is expensive, so we only check conditionally (luaC_condGC).
  ** The GC uses a debt-based system to determine when collection is needed.
  **
  ** Saves state before GC (because GC can trigger __gc metamethods that might
  ** throw errors), then updates trap after (because GC might have changed hooks).
  **
  ** luai_threadyield allows the OS to schedule other threads, preventing tight
  ** loops from starving other threads on single-core systems.
  */
  auto checkGC = [&](lua_State* L_arg, StkId c_val) {
    luaC_condGC(L_arg,
                [&](){ saveProgramCounter(callInfo); L_arg->getStackSubsystem().setTopPtr(c_val); },
                [&](){ updateTrap(callInfo); });
    luai_threadyield(L_arg);
  };

  // Lambda: Arithmetic with immediate operand
  auto op_arithI = [&](auto iop, auto fop, Instruction i) {
    TValue *ra = getValueA(i);
    TValue *v1 = getValueB(i);
    int imm = InstructionView(i).sc();
    if (ttisinteger(v1)) {
      lua_Integer iv1 = ivalue(v1);
      programCounter++; ra->setInt(iop(L, iv1, imm));
    }
    else if (ttisfloat(v1)) {
      lua_Number nb = fltvalue(v1);
      lua_Number fimm = cast_num(imm);
      programCounter++; ra->setFloat(fop(L, nb, fimm));
    }
  };

  // Lambda: Auxiliary function for arithmetic operations over floats
  auto op_arithf_aux = [&](const TValue *v1, const TValue *v2, auto fop, Instruction i) {
    lua_Number n1, n2;
    if (tonumberns(v1, n1) && tonumberns(v2, n2)) {
      auto ra = getRegisterA(i);
      programCounter++; s2v(ra)->setFloat(fop(L, n1, n2));
    }
  };

  // Lambda: Arithmetic operations over floats with register operands
  auto op_arithf = [&](auto fop, Instruction i) {
    TValue *v1 = getValueB(i);
    TValue *v2 = getValueC(i);
    op_arithf_aux(v1, v2, fop, i);
  };

  // Lambda: Arithmetic operations with K operands for floats
  auto op_arithfK = [&](auto fop, Instruction i) {
    TValue *v1 = getValueB(i);
    TValue *v2 = getConstantC(i);
    lua_assert(ttisnumber(v2));
    op_arithf_aux(v1, v2, fop, i);
  };

  // Lambda: Auxiliary for arithmetic operations over integers and floats
  auto op_arith_aux = [&](const TValue *v1, const TValue *v2, auto iop, auto fop, Instruction i) {
    if (ttisinteger(v1) && ttisinteger(v2)) {
      auto ra = getRegisterA(i);
      auto i1 = ivalue(v1);
      auto i2 = ivalue(v2);
      programCounter++; s2v(ra)->setInt(iop(L, i1, i2));
    }
    else {
      op_arithf_aux(v1, v2, fop, i);
    }
  };

  // Lambda: Arithmetic operations with register operands
  auto op_arith = [&](auto iop, auto fop, Instruction i) {
    TValue *v1 = getValueB(i);
    TValue *v2 = getValueC(i);
    op_arith_aux(v1, v2, iop, fop, i);
  };

  // Lambda: Arithmetic operations with K operands
  auto op_arithK = [&](auto iop, auto fop, Instruction i) {
    TValue *v1 = getValueB(i);
    TValue *v2 = getConstantC(i);
    lua_assert(ttisnumber(v2));
    op_arith_aux(v1, v2, iop, fop, i);
  };

  // Lambda: Bitwise operations with constant operand
  auto op_bitwiseK = [&](auto op, Instruction i) {
    auto *v1 = getValueB(i);
    auto *v2 = getConstantC(i);
    lua_Integer i1;
    auto i2 = ivalue(v2);
    if (tointegerns(v1, &i1)) {
      auto ra = getRegisterA(i);
      programCounter++; s2v(ra)->setInt(op(i1, i2));
    }
  };

  // Lambda: Bitwise operations with register operands
  auto op_bitwise = [&](auto op, Instruction i) {
    auto *v1 = getValueB(i);
    auto *v2 = getValueC(i);
    lua_Integer i1, i2;
    if (tointegerns(v1, &i1) && tointegerns(v2, &i2)) {
      auto ra = getRegisterA(i);
      programCounter++; s2v(ra)->setInt(op(i1, i2));
    }
  };

  // Lambda: Order operations with register operands
  // Note: Cannot use operators as template parameters, so we pass comparator function objects
  auto op_order = [&](auto cmp, auto other, Instruction i) {
    TValue *ra = getValueA(i);
    int cond;
    TValue *rb = getValueB(i);
    if (ttisnumber(ra) && ttisnumber(rb))
      cond = cmp(ra, rb);  // Use comparator function object
    else
      protectCall([&]() { cond = other(L, ra, rb); });
    performConditionalJump(cond, callInfo, i);
  };

  // Lambda: Order operations with immediate operand
  auto op_orderI = [&](auto opi, auto opf, int inv, TMS metamethodEvent, Instruction i) {
    TValue *ra = getValueA(i);
    int cond;
    int im = InstructionView(i).sb();
    if (ttisinteger(ra))
      cond = opi(ivalue(ra), im);
    else if (ttisfloat(ra)) {
      lua_Number fa = fltvalue(ra);
      lua_Number fim = cast_num(im);
      cond = opf(fa, fim);
    }
    else {
      int isf = InstructionView(i).c();
      protectCall([&]() { cond = luaT_callorderiTM(L, ra, im, inv, isf, metamethodEvent); });
    }
    performConditionalJump(cond, callInfo, i);
  };

  // Comparator function objects for op_order (operators cannot be passed as template params)
  auto cmp_lt = [](const TValue* a, const TValue* b) { return *a < *b; };
  auto cmp_le = [](const TValue* a, const TValue* b) { return *a <= *b; };

  // "Other" comparison lambdas for op_order (non-numeric comparisons)
  auto other_lt = [&](lua_State* L_arg, const TValue* l, const TValue* r) {
    return L_arg->lessThanOthers(l, r);
  };
  auto other_le = [&](lua_State* L_arg, const TValue* l, const TValue* r) {
    return L_arg->lessEqualOthers(l, r);
  };

 startfunc:
  hooksEnabled = L->getHookMask();
 returning:  // hooksEnabled already set
  currentClosure = callInfo->getFunc();
  constants = currentClosure->getProto()->getConstants();
  programCounter = callInfo->getSavedPC();
  if (l_unlikely(hooksEnabled))
    hooksEnabled = luaG_tracecall(L);
  stackFrameBase = callInfo->funcRef().p + 1;

  Instruction i;  // instruction being executed (moved outside loop for lambda capture)

  // VM instruction fetch lambda
  auto vmfetch = [&]() {
    if (l_unlikely(hooksEnabled)) {  // stack reallocation or hooks?
      hooksEnabled = luaG_traceexec(L, programCounter);  // handle hooks
      updateStackBase(callInfo);  // correct stack
    }
    i = *(programCounter++);
  };

  // main loop of interpreter
  for (;;) {
    vmfetch();
    lua_assert(stackFrameBase == callInfo->funcRef().p + 1);
    lua_assert(stackFrameBase <= L->getTop().p && L->getTop().p <= L->getStackLast().p);
    // for tests, invalidate top for instructions not expecting it
    lua_assert(luaP_isIT(i) || (cast_void(L->getStackSubsystem().setTopPtr(stackFrameBase)), 1));
    switch (InstructionView(i).opcode()) {
      case OP_MOVE: {
        auto ra = getRegisterA(i);
        *s2v(ra) = *s2v(getRegisterB(i));  /* Use operator= for move */
        break;
      }
      case OP_LOADI: {
        auto ra = getRegisterA(i);
        auto b = InstructionView(i).sbx();
        s2v(ra)->setInt(b);
        break;
      }
      case OP_LOADF: {
        auto ra = getRegisterA(i);
        auto b = InstructionView(i).sbx();
        s2v(ra)->setFloat(cast_num(b));
        break;
      }
      case OP_LOADK: {
        auto ra = getRegisterA(i);
        auto *rb = constants + InstructionView(i).bx();
        L->getStackSubsystem().setSlot(ra, rb);
        break;
      }
      case OP_LOADKX: {
        auto ra = getRegisterA(i);
        auto *rb = constants + InstructionView(*programCounter).ax(); programCounter++;
        L->getStackSubsystem().setSlot(ra, rb);
        break;
      }
      case OP_LOADFALSE: {
        auto ra = getRegisterA(i);
        setbfvalue(s2v(ra));
        break;
      }
      case OP_LFALSESKIP: {
        auto ra = getRegisterA(i);
        setbfvalue(s2v(ra));
        programCounter++;  // skip next instruction
        break;
      }
      case OP_LOADTRUE: {
        auto ra = getRegisterA(i);
        setbtvalue(s2v(ra));
        break;
      }
      case OP_LOADNIL: {
        auto ra = getRegisterA(i);
        auto b = InstructionView(i).b();
        do {
          setnilvalue(s2v(ra++));
        } while (b--);
        break;
      }
      case OP_GETUPVAL: {
        auto ra = getRegisterA(i);
        auto b = InstructionView(i).b();
        L->getStackSubsystem().setSlot(ra, currentClosure->getUpval(b)->getVP());
        break;
      }
      case OP_SETUPVAL: {
        auto ra = getRegisterA(i);
        auto *upvalue = currentClosure->getUpval(InstructionView(i).b());
        *upvalue->getVP() = *s2v(ra);
        luaC_barrier(L, upvalue, s2v(ra));
        break;
      }
      case OP_GETTABUP: {
        auto ra = getRegisterA(i);
        auto *upval = currentClosure->getUpval(InstructionView(i).b())->getVP();
        auto *rc = getConstantC(i);
        auto *key = tsvalue(rc);  // key must be a short string
        LuaT tag;
        tag = fastget(upval, key, s2v(ra), [](Table* tbl, TString* strkey, TValue* res) { return tbl->getShortStr(strkey, res); });
        if (tagisempty(tag))
          protectCall([&]() { tag = finishGet(upval, rc, ra, tag); });
        break;
      }
      case OP_GETTABLE: {
        auto ra = getRegisterA(i);
        auto *rb = getValueB(i);
        auto *rc = getValueC(i);
        LuaT tag;
        if (ttisinteger(rc)) {  // fast track for integers?
          fastgeti(rb, ivalue(rc), s2v(ra), tag);
        }
        else
          tag = fastget(rb, rc, s2v(ra), [](Table* tbl, const TValue* key, TValue* res) { return tbl->get(key, res); });
        if (tagisempty(tag))
          protectCall([&]() { tag = finishGet(rb, rc, ra, tag); });
        break;
      }
      case OP_GETI: {
        auto ra = getRegisterA(i);
        auto *rb = getValueB(i);
        auto c = InstructionView(i).c();
        LuaT tag;
        fastgeti(rb, c, s2v(ra), tag);
        if (tagisempty(tag)) {
          TValue key;
          key.setInt(c);
          protectCall([&]() { tag = finishGet(rb, &key, ra, tag); });
        }
        break;
      }
      case OP_GETFIELD: {
        auto ra = getRegisterA(i);
        auto *rb = getValueB(i);
        auto *rc = getConstantC(i);
        auto *key = tsvalue(rc);  // key must be a short string
        LuaT tag;
        tag = fastget(rb, key, s2v(ra), [](Table* tbl, TString* strkey, TValue* res) { return tbl->getShortStr(strkey, res); });
        if (tagisempty(tag))
          protectCall([&]() { tag = finishGet(rb, rc, ra, tag); });
        break;
      }
      case OP_SETTABUP: {
        auto *upval = currentClosure->getUpval(InstructionView(i).a())->getVP();
        auto *rb = getConstantB(i);
        auto *rc = getRegisterOrConstantC(i);
        auto *key = tsvalue(rb);  // key must be a short string
        auto hres = fastset(upval, key, rc, [](Table* tbl, TString* strkey, TValue* val) { return tbl->psetShortStr(strkey, val); });
        if (hres == HOK)
          finishfastset(upval, rc);
        else
          protectCall([&]() { finishSet(upval, rb, rc, hres); });
        break;
      }
      case OP_SETTABLE: {
        auto ra = getRegisterA(i);
        auto *rb = getValueB(i);  // key (table is in 'ra')
        auto *rc = getRegisterOrConstantC(i);  // value
        int hres;
        if (ttisinteger(rb)) {  // fast track for integers?
          fastseti(s2v(ra), ivalue(rb), rc, hres);
        }
        else {
          hres = fastset(s2v(ra), rb, rc, [](Table* tbl, const TValue* key, TValue* val) { return tbl->pset(key, val); });
        }
        if (hres == HOK)
          finishfastset(s2v(ra), rc);
        else
          protectCall([&]() { finishSet(s2v(ra), rb, rc, hres); });
        break;
      }
      case OP_SETI: {
        auto ra = getRegisterA(i);
        auto b = InstructionView(i).b();
        auto *rc = getRegisterOrConstantC(i);
        int hres;
        fastseti(s2v(ra), b, rc, hres);
        if (hres == HOK)
          finishfastset(s2v(ra), rc);
        else {
          TValue key;
          key.setInt(b);
          protectCall([&]() { finishSet(s2v(ra), &key, rc, hres); });
        }
        break;
      }
      case OP_SETFIELD: {
        auto ra = getRegisterA(i);
        auto *rb = getConstantB(i);
        auto *rc = getRegisterOrConstantC(i);
        auto *key = tsvalue(rb);  // key must be a short string
        auto hres = fastset(s2v(ra), key, rc, [](Table* tbl, TString* strkey, TValue* val) { return tbl->psetShortStr(strkey, val); });
        if (hres == HOK)
          finishfastset(s2v(ra), rc);
        else
          protectCall([&]() { finishSet(s2v(ra), rb, rc, hres); });
        break;
      }
      case OP_NEWTABLE: {
        auto ra = getRegisterA(i);
        auto b = cast_uint(InstructionView(i).vb());  // log2(hash size) + 1
        auto c = cast_uint(InstructionView(i).vc());  // array size
        if (b > 0)
          b = 1u << (b - 1);  // hash size is 2^(b - 1)
        if (InstructionView(i).testk()) {  // non-zero extra argument?
          lua_assert(InstructionView(*programCounter).ax() != 0);
          // add it to array size
          c += cast_uint(InstructionView(*programCounter).ax()) * (MAXARG_vC + 1);
        }
        programCounter++;  // skip extra argument
        L->getStackSubsystem().setTopPtr(ra + 1);  // correct top in case of emergency GC
        auto *t = Table::create(L);  // memory allocation
        sethvalue2s(L, ra, t);
        if (b != 0 || c != 0)
          t->resize(L, c, b);  // idem
        checkGC(L, ra + 1);
        break;
      }
      case OP_SELF: {
        auto ra = getRegisterA(i);
        auto *rb = getValueB(i);
        auto *rc = getConstantC(i);
        auto *key = tsvalue(rc);  // key must be a short string
        L->getStackSubsystem().setSlot(ra + 1, rb);
        LuaT tag = fastget(rb, key, s2v(ra), [](Table* tbl, TString* strkey, TValue* res) { return tbl->getShortStr(strkey, res); });
        if (tagisempty(tag))
          protectCall([&]() { tag = finishGet(rb, rc, ra, tag); });
        break;
      }
      case OP_ADDI: {
        op_arithI(l_addi, luai_numadd, i);
        break;
      }
      case OP_ADDK: {
        op_arithK(l_addi, luai_numadd, i);
        break;
      }
      case OP_SUBK: {
        op_arithK(l_subi, luai_numsub, i);
        break;
      }
      case OP_MULK: {
        op_arithK(l_muli, luai_nummul, i);
        break;
      }
      case OP_MODK: {
        saveInterpreterState(L, callInfo);  // in case of division by 0
        op_arithK([this](lua_State*, lua_Integer a, lua_Integer b) { return mod(a, b); },
                  [this](lua_State*, lua_Number a, lua_Number b) { return modf(a, b); }, i);
        break;
      }
      case OP_POWK: {
        op_arithfK(luai_numpow, i);
        break;
      }
      case OP_DIVK: {
        op_arithfK(luai_numdiv, i);
        break;
      }
      case OP_IDIVK: {
        saveInterpreterState(L, callInfo);  // in case of division by 0
        op_arithK([this](lua_State*, lua_Integer a, lua_Integer b) { return idiv(a, b); }, luai_numidiv, i);
        break;
      }
      case OP_BANDK: {
        op_bitwiseK(l_band, i);
        break;
      }
      case OP_BORK: {
        op_bitwiseK(l_bor, i);
        break;
      }
      case OP_BXORK: {
        op_bitwiseK(l_bxor, i);
        break;
      }
      case OP_SHLI: {
        auto ra = getRegisterA(i);
        auto *rb = getValueB(i);
        auto ic = InstructionView(i).sc();
        lua_Integer ib;
        if (tointegerns(rb, &ib)) {
          programCounter++; s2v(ra)->setInt(VirtualMachine::shiftl(ic, ib));
        }
        break;
      }
      case OP_SHRI: {
        auto ra = getRegisterA(i);
        auto *rb = getValueB(i);
        auto ic = InstructionView(i).sc();
        lua_Integer ib;
        if (tointegerns(rb, &ib)) {
          programCounter++; s2v(ra)->setInt(VirtualMachine::shiftl(ib, -ic));
        }
        break;
      }
      case OP_ADD: {
        op_arith(l_addi, luai_numadd, i);
        break;
      }
      case OP_SUB: {
        op_arith(l_subi, luai_numsub, i);
        break;
      }
      case OP_MUL: {
        op_arith(l_muli, luai_nummul, i);
        break;
      }
      case OP_MOD: {
        saveInterpreterState(L, callInfo);  // in case of division by 0
        op_arith([this](lua_State*, lua_Integer a, lua_Integer b) { return mod(a, b); },
                 [this](lua_State*, lua_Number a, lua_Number b) { return modf(a, b); }, i);
        break;
      }
      case OP_POW: {
        op_arithf(luai_numpow, i);
        break;
      }
      case OP_DIV: {  // float division (always with floats)
        op_arithf(luai_numdiv, i);
        break;
      }
      case OP_IDIV: {  // floor division
        saveInterpreterState(L, callInfo);  // in case of division by 0
        op_arith([this](lua_State*, lua_Integer a, lua_Integer b) { return idiv(a, b); }, luai_numidiv, i);
        break;
      }
      case OP_BAND: {
        op_bitwise(l_band, i);
        break;
      }
      case OP_BOR: {
        op_bitwise(l_bor, i);
        break;
      }
      case OP_BXOR: {
        op_bitwise(l_bxor, i);
        break;
      }
      case OP_SHL: {
        op_bitwise(VirtualMachine::shiftl, i);
        break;
      }
      case OP_SHR: {
        op_bitwise(VirtualMachine::shiftr, i);
        break;
      }
      case OP_MMBIN: {
        auto ra = getRegisterA(i);
        auto pi = *(programCounter - 2);  // original arith. expression
        auto *rb = getValueB(i);
        auto metamethodEvent = static_cast<TMS>(InstructionView(i).c());
        auto result = getRegisterA(pi);
        lua_assert(OP_ADD <= InstructionView(pi).opcode() && InstructionView(pi).opcode() <= OP_SHR);
        protectCall([&]() { luaT_trybinTM(L, s2v(ra), rb, result, metamethodEvent); });
        break;
      }
      case OP_MMBINI: {
        auto ra = getRegisterA(i);
        auto pi = *(programCounter - 2);  // original arith. expression
        auto imm = InstructionView(i).sb();
        auto metamethodEvent = static_cast<TMS>(InstructionView(i).c());
        auto flip = InstructionView(i).k();
        auto result = getRegisterA(pi);
        protectCall([&]() { luaT_trybiniTM(L, s2v(ra), imm, flip, result, metamethodEvent); });
        break;
      }
      case OP_MMBINK: {
        auto ra = getRegisterA(i);
        auto pi = *(programCounter - 2);  // original arith. expression
        auto *imm = getConstantB(i);
        auto metamethodEvent = static_cast<TMS>(InstructionView(i).c());
        auto flip = InstructionView(i).k();
        auto result = getRegisterA(pi);
        protectCall([&]() { luaT_trybinassocTM(L, s2v(ra), imm, flip, result, metamethodEvent); });
        break;
      }
      case OP_UNM: {
        auto ra = getRegisterA(i);
        auto *rb = getValueB(i);
        lua_Number nb;
        if (ttisinteger(rb)) {
          auto ib = ivalue(rb);
          s2v(ra)->setInt(intop(-, 0, ib));
        }
        else if (tonumberns(rb, nb)) {
          s2v(ra)->setFloat(luai_numunm(L, nb));
        }
        else
          protectCall([&]() { luaT_trybinTM(L, rb, rb, ra, TMS::TM_UNM); });
        break;
      }
      case OP_BNOT: {
        auto ra = getRegisterA(i);
        auto *rb = getValueB(i);
        lua_Integer ib;
        if (tointegerns(rb, &ib)) {
          s2v(ra)->setInt(intop(^, ~l_castS2U(0), ib));
        }
        else
          protectCall([&]() { luaT_trybinTM(L, rb, rb, ra, TMS::TM_BNOT); });
        break;
      }
      case OP_NOT: {
        auto ra = getRegisterA(i);
        auto *rb = getValueB(i);
        if (l_isfalse(rb))
          setbtvalue(s2v(ra));
        else
          setbfvalue(s2v(ra));
        break;
      }
      case OP_LEN: {
        auto ra = getRegisterA(i);
        protectCall([&]() { objlen(ra, getValueB(i)); });
        break;
      }
      case OP_CONCAT: {
        auto ra = getRegisterA(i);
        auto n = InstructionView(i).b();  // number of elements to concatenate
        L->getStackSubsystem().setTopPtr(ra + n);  // mark the end of concat operands
        protectCallNoTop([&]() { concat(n); });
        checkGC(L, L->getTop().p);  // 'luaV_concat' ensures correct top
        break;
      }
      case OP_CLOSE: {
        auto ra = getRegisterA(i);
        lua_assert(!InstructionView(i).b());  // 'close must be alive
        protectCall([&]() { ra = luaF_close(L, ra, LUA_OK, 1); });
        break;
      }
      case OP_TBC: {
        auto ra = getRegisterA(i);
        // create new to-be-closed upvalue
        halfProtectedCall([&]() { luaF_newtbcupval(L, ra); });
        break;
      }
      case OP_JMP: {
        performJump(callInfo, i, 0);
        break;
      }
      case OP_EQ: {
        auto ra = getRegisterA(i);
        auto *rb = getValueB(i);
        int cond;
        protectCall([&]() { cond = equalObj(s2v(ra), rb); });
        performConditionalJump(cond, callInfo, i);
        break;
      }
      case OP_LT: {
        op_order(cmp_lt, other_lt, i);
        break;
      }
      case OP_LE: {
        op_order(cmp_le, other_le, i);
        break;
      }
      case OP_EQK: {
        auto ra = getRegisterA(i);
        auto *rb = getConstantB(i);
        // basic types do not use '__eq'; we can use raw equality
        auto cond = (*s2v(ra) == *rb);  // Use operator== for cleaner syntax
        performConditionalJump(cond, callInfo, i);
        break;
      }
      case OP_EQI: {
        auto ra = getRegisterA(i);
        auto im = InstructionView(i).sb();
        int cond;
        if (ttisinteger(s2v(ra)))
          cond = (ivalue(s2v(ra)) == im);
        else if (ttisfloat(s2v(ra)))
          cond = luai_numeq(fltvalue(s2v(ra)), cast_num(im));
        else
          cond = 0;  // other types cannot be equal to a number
        performConditionalJump(cond, callInfo, i);
        break;
      }
      case OP_LTI: {
        op_orderI(l_lti, luai_numlt, 0, TMS::TM_LT, i);
        break;
      }
      case OP_LEI: {
        op_orderI(l_lei, luai_numle, 0, TMS::TM_LE, i);
        break;
      }
      case OP_GTI: {
        op_orderI(l_gti, luai_numgt, 1, TMS::TM_LT, i);
        break;
      }
      case OP_GEI: {
        op_orderI(l_gei, luai_numge, 1, TMS::TM_LE, i);
        break;
      }
      case OP_TEST: {
        auto ra = getRegisterA(i);
        auto cond = !l_isfalse(s2v(ra));
        performConditionalJump(cond, callInfo, i);
        break;
      }
      case OP_TESTSET: {
        auto ra = getRegisterA(i);
        auto *rb = getValueB(i);
        if (l_isfalse(rb) == InstructionView(i).k())
          programCounter++;
        else {
          L->getStackSubsystem().setSlot(ra, rb);
          performNextJump(callInfo);
        }
        break;
      }
      case OP_CALL: {
        auto ra = getRegisterA(i);
        auto b = InstructionView(i).b();
        auto nresults = InstructionView(i).c() - 1;
        if (b != 0)  // fixed number of arguments?
          L->getStackSubsystem().setTopPtr(ra + b);  // top signals number of arguments
        // else previous instruction set top
        saveProgramCounter(callInfo);  // in case of errors
        CallInfo *newci;
        if ((newci = L->preCall( ra, nresults)) == nullptr)
          updateTrap(callInfo);  // C call; nothing else to be done
        else {  // Lua call: run function in this same C frame
          callInfo = newci;
          goto startfunc;
        }
        break;
      }
      case OP_TAILCALL: {
        auto ra = getRegisterA(i);
        auto b = InstructionView(i).b();  // number of arguments + 1 (function)
        auto nparams1 = InstructionView(i).c();
        // delta is virtual 'func' - real 'func' (vararg functions)
        auto delta = (nparams1) ? callInfo->getExtraArgs() + nparams1 : 0;
        if (b != 0)
          L->getStackSubsystem().setTopPtr(ra + b);
        else  // previous instruction set top
          b = cast_int(L->getTop().p - ra);
        saveProgramCounter(callInfo);  // several calls here can raise errors
        if (InstructionView(i).testk()) {
          luaF_closeupval(L, stackFrameBase);  // close upvalues from current call
          lua_assert(L->getTbclist().p < stackFrameBase);  // no pending tbc variables
          lua_assert(stackFrameBase == callInfo->funcRef().p + 1);
        }
        int n;  // number of results when calling a C function
        if ((n = L->preTailCall( callInfo, ra, b, delta)) < 0)  // Lua function?
          goto startfunc;  // execute the callee
        else {  // C function?
          callInfo->funcRef().p -= delta;  // restore 'func' (if vararg)
          L->postCall( callInfo, n);  // finish caller
          updateTrap(callInfo);  // 'luaD_poscall' can change hooks
          goto ret;  // caller returns after the tail call
        }
      }
      case OP_RETURN: {
        auto ra = getRegisterA(i);
        auto n = InstructionView(i).b() - 1;  // number of results
        auto nparams1 = InstructionView(i).c();
        if (n < 0)  // not fixed?
          n = cast_int(L->getTop().p - ra);  // get what is available
        saveProgramCounter(callInfo);
        if (InstructionView(i).testk()) {  // may there be open upvalues?
          callInfo->setNRes(n);  // save number of returns
          if (L->getTop().p < callInfo->topRef().p)
            L->getStackSubsystem().setTopPtr(callInfo->topRef().p);
          stackFrameBase = luaF_close(L, stackFrameBase, CLOSEKTOP, 1);
          updateTrap(callInfo);
          updateStackAfterRealloc(ra, callInfo, i);
        }
        if (nparams1)  // vararg function?
          callInfo->funcRef().p -= callInfo->getExtraArgs() + nparams1;
        L->getStackSubsystem().setTopPtr(ra + n);  // set call for 'luaD_poscall'
        L->postCall( callInfo, n);
        updateTrap(callInfo);  // 'luaD_poscall' can change hooks
        goto ret;
      }
      case OP_RETURN0: {
        if (l_unlikely(L->getHookMask())) {
          auto ra = getRegisterA(i);
          L->getStackSubsystem().setTopPtr(ra);
          saveProgramCounter(callInfo);
          L->postCall( callInfo, 0);  // no hurry...
          hooksEnabled = 1;
        }
        else {  // do the 'poscall' here
          auto nres = CallInfo::getNResults(callInfo->getCallStatus());
          L->setCI(callInfo->getPrevious());  // back to caller
          L->getStackSubsystem().setTopPtr(stackFrameBase - 1);
          for (; l_unlikely(nres > 0); nres--) {
            setnilvalue(s2v(L->getTop().p));
            L->getStackSubsystem().push();  // all results are nil
          }
        }
        goto ret;
      }
      case OP_RETURN1: {
        if (l_unlikely(L->getHookMask())) {
          auto ra = getRegisterA(i);
          L->getStackSubsystem().setTopPtr(ra + 1);
          saveProgramCounter(callInfo);
          L->postCall( callInfo, 1);  // no hurry...
          hooksEnabled = 1;
        }
        else {  // do the 'poscall' here
          auto nres = CallInfo::getNResults(callInfo->getCallStatus());
          L->setCI(callInfo->getPrevious());  // back to caller
          if (nres == 0)
            L->getStackSubsystem().setTopPtr(stackFrameBase - 1);  // asked for no results
          else {
            auto ra = getRegisterA(i);
            *s2v(stackFrameBase - 1) = *s2v(ra);  /* at least this result (operator=) */
            L->getStackSubsystem().setTopPtr(stackFrameBase);
            for (; l_unlikely(nres > 1); nres--) {
              setnilvalue(s2v(L->getTop().p));
              L->getStackSubsystem().push();  // complete missing results
            }
          }
        }
       ret:  // return from a Lua function
        if (callInfo->getCallStatus() & CIST_FRESH)
          return;  // end this frame
        else {
          callInfo = callInfo->getPrevious();
          goto returning;  // continue running caller in this frame
        }
      }
      case OP_FORLOOP: {
        auto ra = getRegisterA(i);
        if (ttisinteger(s2v(ra + 1))) {  // integer loop?
          auto count = l_castS2U(ivalue(s2v(ra)));
          if (count > 0) {  // still more iterations?
            auto step = ivalue(s2v(ra + 1));
            auto idx = ivalue(s2v(ra + 2));  // control variable
            s2v(ra)->changeInt(l_castU2S(count - 1));  // update counter
            idx = intop(+, idx, step);  // add step to index
            s2v(ra + 2)->changeInt(idx);  // update control variable
            programCounter -= InstructionView(i).bx();  // jump back
          }
        }
        else if (L->floatForLoop(ra))  // float loop
          programCounter -= InstructionView(i).bx();  // jump back
        updateTrap(callInfo);  // allows a signal to break the loop
        break;
      }
      case OP_FORPREP: {
        auto ra = getRegisterA(i);
        saveInterpreterState(L, callInfo);  // in case of errors
        if (L->forPrep(ra))
          programCounter += InstructionView(i).bx() + 1;  // skip the loop
        break;
      }
      case OP_TFORPREP: {
       /* before: 'ra' has the iterator function, 'ra + 1' has the state,
          'ra + 2' has the initial value for the control variable, and
          'ra + 3' has the closing variable. This opcode then swaps the
          control and the closing variables and marks the closing variable
          as to-be-closed.
       */
       auto ra = getRegisterA(i);
       TValue temp;  // to swap control and closing variables
       temp = *s2v(ra + 3);  // Use operator= for temp assignment
       *s2v(ra + 3) = *s2v(ra + 2);  /* Use operator= */
       *s2v(ra + 2) = temp;  /* Use operator= */
        // create to-be-closed upvalue (if closing var. is not nil)
        halfProtectedCall([&]() { luaF_newtbcupval(L, ra + 2); });
        programCounter += InstructionView(i).bx();  // go to end of the loop
        i = *(programCounter++);  // fetch next instruction
        lua_assert(InstructionView(i).opcode() == OP_TFORCALL && ra == getRegisterA(i));
        goto l_tforcall;
      }
      case OP_TFORCALL: {
       l_tforcall: {
        /* 'ra' has the iterator function, 'ra + 1' has the state,
           'ra + 2' has the closing variable, and 'ra + 3' has the control
           variable. The call will use the stack starting at 'ra + 3',
           so that it preserves the first three values, and the first
           return will be the new value for the control variable.
        */
        auto ra = getRegisterA(i);
        *s2v(ra + 5) = *s2v(ra + 3);  /* copy the control variable (operator=) */
        *s2v(ra + 4) = *s2v(ra + 1);  /* copy state (operator=) */
        *s2v(ra + 3) = *s2v(ra);  /* copy function (operator=) */
        L->getStackSubsystem().setTopPtr(ra + 3 + 3);
        protectCallNoTop([&]() { L->call( ra + 3, InstructionView(i).c()); });  // do the call
        updateStackAfterRealloc(ra, callInfo, i);  // stack may have changed
        i = *(programCounter++);  // go to next instruction
        lua_assert(InstructionView(i).opcode() == OP_TFORLOOP && ra == getRegisterA(i));
        goto l_tforloop;
      }}
      case OP_TFORLOOP: {
       l_tforloop: {
        auto ra = getRegisterA(i);
        if (!ttisnil(s2v(ra + 3)))  // continue loop?
          programCounter -= InstructionView(i).bx();  // jump back
        break;
      }}
      case OP_SETLIST: {
        auto ra = getRegisterA(i);
        auto n = cast_uint(InstructionView(i).vb());
        auto last = cast_uint(InstructionView(i).vc());
        auto *h = hvalue(s2v(ra));
        if (n == 0)
          n = cast_uint(L->getTop().p - ra) - 1;  // get up to the top
        else
          L->getStackSubsystem().setTopPtr(callInfo->topRef().p);  // correct top in case of emergency GC
        last += n;
        if (InstructionView(i).testk()) {
          last += cast_uint(InstructionView(*programCounter).ax()) * (MAXARG_vC + 1);
          programCounter++;
        }
        // when 'n' is known, table should have proper size
        if (last > h->arraySize()) {  // needs more space?
          // fixed-size sets should have space preallocated
          lua_assert(InstructionView(i).vb() == 0);
          h->resizeArray(L, last);  // preallocate it at once
        }
        for (; n > 0; n--) {
          auto *val = s2v(ra + n);
          obj2arr(h, last - 1, val);
          last--;
          luaC_barrierback(L, obj2gco(h), val);
        }
        break;
      }
      case OP_CLOSURE: {
        auto ra = getRegisterA(i);
        auto *p = currentClosure->getProto()->getProtos()[InstructionView(i).bx()];
        halfProtectedCall([&]() { L->pushClosure(p, currentClosure->getUpvalPtr(0), stackFrameBase, ra); });
        checkGC(L, ra + 1);
        break;
      }
      case OP_VARARG: {
        auto ra = getRegisterA(i);
        auto n = InstructionView(i).c() - 1;  // required results
        protectCall([&]() { luaT_getvarargs(L, callInfo, ra, n); });
        break;
      }
      case OP_VARARGPREP: {
        protectCallNoTop([&]() { luaT_adjustvarargs(L, InstructionView(i).a(), callInfo, currentClosure->getProto()); });
        if (l_unlikely(hooksEnabled)) {  // previous "Protect" updated hooksEnabled
          L->hookCall( callInfo);
          L->setOldPC(1);  // next opcode will be seen as a "new" line
        }
        updateStackBase(callInfo);  // function has new base after adjustment
        break;
      }
      case OP_EXTRAARG: {
        lua_assert(0);
        break;
      }
    }
  }
}

void VirtualMachine::finishOp() {
  CallInfo *callInfo = L->getCI();
  StkId base = callInfo->funcRef().p + 1;
  Instruction inst = *(callInfo->getSavedPC() - 1);  // interrupted instruction
  OpCode op = static_cast<OpCode>(InstructionView(inst).opcode());
  switch (op) {  // finish its execution
    case OP_MMBIN: case OP_MMBINI: case OP_MMBINK: {
      *s2v(base + InstructionView(*(callInfo->getSavedPC() - 2)).a()) = *s2v(--L->getTop().p);
      break;
    }
    case OP_UNM: case OP_BNOT: case OP_LEN:
    case OP_GETTABUP: case OP_GETTABLE: case OP_GETI:
    case OP_GETFIELD: case OP_SELF: {
      *s2v(base + InstructionView(inst).a()) = *s2v(--L->getTop().p);
      break;
    }
    case OP_LT: case OP_LE:
    case OP_LTI: case OP_LEI:
    case OP_GTI: case OP_GEI:
    case OP_EQ: {  // note that 'OP_EQI'/'OP_EQK' cannot yield
      lua_assert(L->getTop().p > L->getStack().p);  // ensure stack not empty
      int res = !l_isfalse(s2v(L->getTop().p - 1));
      L->getStackSubsystem().pop();
      lua_assert(InstructionView(*callInfo->getSavedPC()).opcode() == OP_JMP);
      if (res != InstructionView(inst).k())  // condition failed?
        callInfo->setSavedPC(callInfo->getSavedPC() + 1);  // skip jump instruction
      break;
    }
    case OP_CONCAT: {
      StkId top = L->getTop().p - 1;  // top when 'luaT_tryconcatTM' was called
      int a = InstructionView(inst).a();  // first element to concatenate
      lua_assert(top >= base + a + 1);  // ensure valid range for subtraction
      lua_assert(top >= L->getStack().p + 2);  // ensure top-2 is valid
      int total = cast_int(top - 1 - (base + a));  // yet to concatenate
      *s2v(top - 2) = *s2v(top);  /* put TM result in proper position (operator=) */
      L->getStackSubsystem().setTopPtr(top - 1);  // top is one after last element (at top-2)
      concat(total);  // concat them (may yield again)
      break;
    }
    case OP_CLOSE: {  // yielded closing variables
      callInfo->setSavedPC(callInfo->getSavedPC() - 1);  // repeat instruction to close other vars.
      break;
    }
    case OP_RETURN: {  // yielded closing variables
      StkId ra = base + InstructionView(inst).a();
      /* adjust top to signal correct number of returns, in case the
         return is "up to top" ('isIT') */
      L->getStackSubsystem().setTopPtr(ra + callInfo->getNRes());
      // repeat instruction to close other vars. and complete the return
      callInfo->setSavedPC(callInfo->getSavedPC() - 1);
      break;
    }
    default: {
      // only these other opcodes can yield
      lua_assert(op == OP_TFORCALL || op == OP_CALL ||
           op == OP_TAILCALL || op == OP_SETTABUP || op == OP_SETTABLE ||
           op == OP_SETI || op == OP_SETFIELD);
      break;
    }
  }
}

// === TYPE CONVERSIONS ===

// Helper function for string to number conversion
static int l_strton (const TValue *obj, TValue *result) {
  lua_assert(obj != result);
  if (!cvt2num(obj))  // is object not a string?
    return 0;
  else {
    TString *st = tsvalue(obj);
    size_t stlen;
    const char *s = getStringWithLength(st, stlen);
    return (luaO_str2num(s, result) == stlen + 1);
  }
}

int VirtualMachine::tonumber(const TValue *obj, lua_Number *n) const {
  TValue v;
  if (ttisinteger(obj)) {
    *n = cast_num(ivalue(obj));
    return 1;
  }
  else if (l_strton(obj, &v)) {  // string coercible to number?
    *n = nvalue(&v);  /* convert result of 'luaO_str2num' to a float */
    return 1;
  }
  else
    return 0;  // conversion failed
}

// Wrapper for lobject.h inline functions to call
int VirtualMachine_flttointeger(lua_Number n, lua_Integer *p, F2Imod mode) {
  return VirtualMachine::flttointeger(n, p, mode);
}

int VirtualMachine::flttointeger(lua_Number n, lua_Integer *p, F2Imod mode) {
  lua_Number f = l_floor(n);
  if (n != f) {  // not an integral value?
    if (mode == F2Imod::F2Ieq) return 0;  // fails if mode demands integral value
    else if (mode == F2Imod::F2Iceil)  // needs ceiling?
      f += 1;  // convert floor to ceiling (remember: n != f)
  }
  return lua_numbertointeger(f, p);
}

int VirtualMachine::tointegerns(const TValue *obj, lua_Integer *p, F2Imod mode) const {
  if (ttisfloat(obj))
    return flttointeger(fltvalue(obj), p, mode);
  else if (ttisinteger(obj)) {
    *p = ivalue(obj);
    return 1;
  }
  else
    return 0;
}

int VirtualMachine::tointeger(const TValue *obj, lua_Integer *p, F2Imod mode) const {
  TValue v;
  if (l_strton(obj, &v))  // does 'obj' point to a numerical string?
    obj = &v;  // change it to point to its corresponding number
  return tointegerns(obj, p, mode);
}

// === ARITHMETIC ===

lua_Integer VirtualMachine::idiv(lua_Integer m, lua_Integer n) const {
  if (l_unlikely(l_castS2U(n) + 1u <= 1u)) {  // special cases: -1 or 0
    if (n == 0)
      luaG_runerror(L, "attempt to divide by zero");
    return intop(-, 0, m);  // n==-1; avoid overflow with 0x80000...//-1
  }
  else {
    auto q = m / n;  // perform C division
    if ((m ^ n) < 0 && m % n != 0)  // 'm/n' would be negative non-integer?
      q -= 1;  // correct result for different rounding
    return q;
  }
}

lua_Integer VirtualMachine::mod(lua_Integer m, lua_Integer n) const {
  if (l_unlikely(l_castS2U(n) + 1u <= 1u)) {  // special cases: -1 or 0
    if (n == 0)
      luaG_runerror(L, "attempt to perform 'n%%0'");
    return 0;  // m % -1 == 0; avoid overflow with 0x80000...%-1
  }
  else {
    auto r = m % n;
    if (r != 0 && (r ^ n) < 0)  // 'm/n' would be non-integer negative?
      r += n;  // correct result for different rounding
    return r;
  }
}

lua_Number VirtualMachine::modf(lua_Number m, lua_Number n) const {
  lua_Number r;
  luai_nummod(L, m, n, r);
  return r;
}

lua_Integer VirtualMachine::shiftl(lua_Integer x, lua_Integer y) {
  // number of bits in an integer
  constexpr int NBITS = l_numbits<lua_Integer>();

  if (y < 0) {  // shift right?
    if (y <= -NBITS) return 0;
    else return intop(>>, x, -y);
  }
  else {  // shift left
    if (y >= NBITS) return 0;
    else return intop(<<, x, y);
  }
}

// === COMPARISONS ===

int VirtualMachine::lessThan(const TValue *l, const TValue *r) const {
  if (ttisnumber(l) && ttisnumber(r))  // both operands are numbers?
    return *l < *r;  // Use operator< for cleaner syntax
  else return L->lessThanOthers(l, r);
}

int VirtualMachine::lessEqual(const TValue *l, const TValue *r) const {
  if (ttisnumber(l) && ttisnumber(r))  // both operands are numbers?
    return *l <= *r;  // Use operator<= for cleaner syntax
  else return L->lessEqualOthers(l, r);
}

int VirtualMachine::equalObj(const TValue *t1, const TValue *t2) const {
  const TValue *metamethod;
  if (ttype(t1) != ttype(t2))  // not the same type?
    return 0;
  else if (ttypetag(t1) != ttypetag(t2)) {
    switch (ttypetag(t1)) {
      case LuaT::NUMINT: {  // integer == float?
        lua_Integer i2;
        return (flttointeger(fltvalue(t2), &i2, F2Imod::F2Ieq) &&
                ivalue(t1) == i2);
      }
      case LuaT::NUMFLT: {  // float == integer?
        lua_Integer i1;
        return (flttointeger(fltvalue(t1), &i1, F2Imod::F2Ieq) &&
                i1 == ivalue(t2));
      }
      case LuaT::SHRSTR: case LuaT::LNGSTR: {
        return tsvalue(t1)->equals(tsvalue(t2));
      }
      default:
        return 0;
    }
  }
  else {  // equal variants
    switch (ttypetag(t1)) {
      case LuaT::NIL: case LuaT::VFALSE: case LuaT::VTRUE:
        return 1;
      case LuaT::NUMINT:
        return (ivalue(t1) == ivalue(t2));
      case LuaT::NUMFLT:
        return (fltvalue(t1) == fltvalue(t2));
      case LuaT::LIGHTUSERDATA: return pvalue(t1) == pvalue(t2);
      case LuaT::SHRSTR:
        return shortStringsEqual(tsvalue(t1), tsvalue(t2));
      case LuaT::LNGSTR:
        return tsvalue(t1)->equals(tsvalue(t2));
      case LuaT::USERDATA: {
        if (uvalue(t1) == uvalue(t2)) return 1;
        else if (L == nullptr) return 0;
        metamethod = fasttm(L, uvalue(t1)->getMetatable(), TMS::TM_EQ);
        if (metamethod == nullptr)
          metamethod = fasttm(L, uvalue(t2)->getMetatable(), TMS::TM_EQ);
        break;  // will try TM
      }
      case LuaT::TABLE: {
        if (hvalue(t1) == hvalue(t2)) return 1;
        else if (L == nullptr) return 0;
        metamethod = fasttm(L, hvalue(t1)->getMetatable(), TMS::TM_EQ);
        if (metamethod == nullptr)
          metamethod = fasttm(L, hvalue(t2)->getMetatable(), TMS::TM_EQ);
        break;  // will try TM
      }
      case LuaT::LCF:
        return (fvalue(t1) == fvalue(t2));
      default:  // functions and threads
        return (gcvalue(t1) == gcvalue(t2));
    }
    if (metamethod == nullptr)  // no TM?
      return 0;  // objects are different
    else {
      auto tag = luaT_callTMres(L, metamethod, t1, t2, L->getTop().p);  // call TM
      return !tagisfalse(tag);
    }
  }
}

// === TABLE OPERATIONS ===

LuaT VirtualMachine::finishGet(const TValue *t, TValue *key, StkId val, LuaT tag) const {
  const TValue *metamethod;  // metamethod
  for (int loop = 0; loop < MAXTAGLOOP; loop++) {
    if (tag == LuaT::NOTABLE) {  // 't' is not a table?
      lua_assert(!ttistable(t));
      metamethod = luaT_gettmbyobj(L, t, TMS::TM_INDEX);
      if (l_unlikely(notm(metamethod)))
        luaG_typeerror(L, t, "index");  // no metamethod
      // else will try the metamethod
    }
    else {  // 't' is a table
      metamethod = fasttm(L, hvalue(t)->getMetatable(), TMS::TM_INDEX);  // table's metamethod
      if (metamethod == nullptr) {  // no metamethod?
        setnilvalue(s2v(val));  // result is nil
        return LuaT::NIL;
      }
      // else will try the metamethod
    }
    if (ttisfunction(metamethod)) {  // is metamethod a function?
      tag = luaT_callTMres(L, metamethod, t, key, val);  // call it
      return tag;  // return tag of the result
    }
    t = metamethod;  // else try to access 'metamethod[key]'
    tag = fastget(t, key, s2v(val), [](Table* tbl, const TValue* k, TValue* res) { return tbl->get(k, res); });
    if (!tagisempty(tag))
      return tag;  // done
    // else repeat (tail call 'luaV_finishget')
  }
  luaG_runerror(L, "'__index' chain too long; possible loop");
  return LuaT::NIL;  // to avoid warnings
}

void VirtualMachine::finishSet(const TValue *t, TValue *key, TValue *val, int hres) const {
  for (int loop = 0; loop < MAXTAGLOOP; loop++) {
    const TValue *metamethod;  // '__newindex' metamethod
    if (hres != HNOTATABLE) {  // is 't' a table?
      auto *h = hvalue(t);  // save 't' table
      metamethod = fasttm(L, h->getMetatable(), TMS::TM_NEWINDEX);  // get metamethod
      if (metamethod == nullptr) {  // no metamethod?
        sethvalue2s(L, L->getTop().p, h);  // anchor 't'
        L->getStackSubsystem().push();  // assume EXTRA_STACK
        h->finishSet(L, key, val, hres);  // set new value
        L->getStackSubsystem().pop();
        invalidateTMcache(h);
        luaC_barrierback(L, obj2gco(h), val);
        return;
      }
      // else will try the metamethod
    }
    else {  // not a table; check metamethod
      metamethod = luaT_gettmbyobj(L, t, TMS::TM_NEWINDEX);
      if (l_unlikely(notm(metamethod)))
        luaG_typeerror(L, t, "index");
    }
    // try the metamethod
    if (ttisfunction(metamethod)) {
      luaT_callTM(L, metamethod, t, key, val);
      return;
    }
    t = metamethod;  // else repeat assignment over 'metamethod'
    hres = fastset(t, key, val, [](Table* tbl, const TValue* k, TValue* v) { return tbl->pset(k, v); });
    if (hres == HOK) {
      finishfastset(t, val);
      return;  // done
    }
    // else 'return luaV_finishset(L, t, key, val, slot)' (loop)
  }
  luaG_runerror(L, "'__newindex' chain too long; possible loop");
}

// === STRING/OBJECT OPERATIONS ===

// Helper functions for string concatenation

// Function to ensure that element at 'o' is a string (converts if possible)
static inline bool tostring(lua_State* L, TValue* o) {
	if (ttisstring(o)) return true;
	if (!cvt2str(o)) return false;
	luaO_tostring(L, o);
	return true;
}

static inline bool isemptystr(const TValue* o) noexcept {
	return ttisshrstring(o) && tsvalue(o)->length() == 0;
}

// copy strings in stack from top - n up to top - 1 to buffer
static void copy2buff (StkId top, int n, char *buff) {
  auto tl = size_t{0};  // size already copied
  do {
    auto *st = tsvalue(s2v(top - n));
    size_t l;  // length of string being copied
    auto *s = getStringWithLength(st, l);
    std::copy_n(s, l, buff + tl);
    tl += l;
  } while (--n > 0);
}

void VirtualMachine::concat(int total) {
  if (total == 1)
    return;  // "all" values already concatenated
  do {
    auto top = L->getTop().p;
    auto n = 2;  // number of elements handled in this pass (at least 2)
    if (!(ttisstring(s2v(top - 2)) || cvt2str(s2v(top - 2))) ||
        !tostring(L, s2v(top - 1))) {
      luaT_tryconcatTM(L);  // may invalidate 'top'
      top = L->getTop().p;  // recapture after potential GC
    }
    else if (isemptystr(s2v(top - 1))) {  // second operand is empty?
      cast_void(tostring(L, s2v(top - 2)));  // result is first operand
      top = L->getTop().p;  // recapture after potential GC
    }
    else if (isemptystr(s2v(top - 2))) {  // first operand is empty string?
      *s2v(top - 2) = *s2v(top - 1);  /* result is second op. (operator=) */
    }
    else {
      // at least two non-empty string values; get as many as possible
      auto tl = getStringLength(tsvalue(s2v(top - 1)));
      TString *tstring;
      // collect total length and number of strings
      for (n = 1; n < total && tostring(L, s2v(top - n - 1)); n++) {
        top = L->getTop().p;  // recapture after tostring() which can trigger GC
        auto l = getStringLength(tsvalue(s2v(top - n - 1)));
        if (l_unlikely(l >= MAX_SIZE - sizeof(TString) - tl)) {
          L->getStackSubsystem().setTopPtr(top - total);  // pop strings to avoid wasting stack
          luaG_runerror(L, "string length overflow");
        }
        tl += l;
      }
      if (tl <= LUAI_MAXSHORTLEN) {  // is result a short string?
        char buff[LUAI_MAXSHORTLEN];
        copy2buff(top, n, buff);  // copy strings to buffer
        tstring = TString::create(L, buff, tl);
        top = L->getTop().p;  // recapture after potential GC
      }
      else {  // long string; copy strings directly to final result
        tstring = TString::createLongString(L, tl);
        top = L->getTop().p;  // recapture after potential GC
        copy2buff(top, n, getLongStringContents(tstring));
      }
      setsvalue2s(L, top - n, tstring);  // create result
    }
    total -= n - 1;  // got 'n' strings to create one new
    L->getStackSubsystem().popN(n - 1);  // popped 'n' strings and pushed one
  } while (total > 1);  // repeat until only 1 result left
}

void VirtualMachine::objlen(StkId ra, const TValue *rb) {
  const TValue *metamethod;
  switch (ttypetag(rb)) {
    case LuaT::TABLE: {
      Table *h = hvalue(rb);
      metamethod = fasttm(L, h->getMetatable(), TMS::TM_LEN);
      if (metamethod) break;  // metamethod? break switch to call it
      s2v(ra)->setInt(l_castU2S(h->getn(L)));  // else primitive len
      return;
    }
    case LuaT::SHRSTR: {
      s2v(ra)->setInt(static_cast<lua_Integer>(tsvalue(rb)->length()));
      return;
    }
    case LuaT::LNGSTR: {
      s2v(ra)->setInt(cast_st2S(tsvalue(rb)->getLnglen()));
      return;
    }
    default: {  // try metamethod
      metamethod = luaT_gettmbyobj(L, rb, TMS::TM_LEN);
      if (l_unlikely(notm(metamethod)))  // no metamethod?
        luaG_typeerror(L, rb, "get length of");
      break;
    }
  }
  luaT_callTMres(L, metamethod, rb, rb, ra);
}
