/*
** Code generator for Lua
** See Copyright Notice in lua.h
*/

#define MOON_CORE

#include "mprefix.h"


#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdlib>

#include "moon.h"

#include "mdebug.h"
#include "mdo.h"
#include "mgc.h"
#include "mlex.h"
#include "mmem.h"
#include "mobject.h"
#include "mopcodes.h"
#include "mparser.h"
#include "mstring.h"
#include "mtable.h"
#include "mvirtualmachine.h"
#include "mvm.h"


// (note that expressions VJMP also have jumps.)
inline bool hasjumps(const ExpDesc& expr) noexcept {
	return expr.getTrueList() != expr.getFalseList();
}

// semantic error
l_noret LexState::semerror(const char *fmt, ...) {
  const char *msg;
  va_list argp;
  pushvfstring(getLuaState(), argp, fmt, msg);
  getCurrentTokenRef().token = 0;  // remove "near <token>" from final message
  syntaxError(msg);
}

/*
** If expression is a numeric constant, fills 'v' with its value
** and returns 1. Otherwise, returns 0.
*/
static int tonumeral (const ExpDesc& expr, TValue *value) {
  if (hasjumps(expr))
    return 0;  // not a numeral
  switch (expr.getKind()) {
    case VKINT:
      if (value) value->setInt(expr.getIntValue());
      return 1;
    case VKFLT:
      if (value) value->setFloat(expr.getFloatValue());
      return 1;
    default: return 0;
  }
}

/*
** Get the constant value from a constant expression
*/
TValue *FuncState::const2val(const ExpDesc& expr) {
  moon_assert(expr.getKind() == VCONST);
  return &getLexState().getDyndata()->actvar()[expr.getInfo()].k;
}

/*
** Return the previous instruction of the current code. If there
** may be a jump target between the current instruction and the
** previous one, return an invalid instruction (to avoid wrong
** optimizations).
*/
Instruction *FuncState::previousinstruction() {
  static const Instruction invalidinstruction = ~(Instruction)0;
  if (getPC() > getLastTarget())
    return &getProto().getCode()[getPC() - 1];  // previous instruction
  else
    return const_cast<Instruction*>(&invalidinstruction);
}

/*
** Gets the destination address of a jump instruction. Used to traverse
** a list of jumps.
*/
int FuncState::getjump(int position) {
  auto offset = InstructionView(getProto().getCode()[position]).sj();
  if (offset == NO_JUMP)  // point to itself represents end of list
    return NO_JUMP;  // end of list
  else
    return (position+1)+offset;  // turn offset into absolute position
}

/*
** Fix jump instruction at position 'pc' to jump to 'dest'.
** (Jump addresses are relative in Lua)
*/
void FuncState::fixjump(int position, int dest) {
  auto *jmp = &getProto().getCode()[position];
  auto offset = dest - (position + 1);
  moon_assert(dest != NO_JUMP);
  if (!(-OFFSET_sJ <= offset && offset <= MAXARG_sJ - OFFSET_sJ))
    getLexState().syntaxError("control structure too long");
  moon_assert(InstructionView(*jmp).opcode() == OP_JMP);
  SETARG_sJ(*jmp, offset);
}

/*
** Code a "conditional jump", that is, a test or comparison opcode
** followed by a jump. Return jump position.
*/
int FuncState::condjump(OpCode o, int A, int B, int C, int k) {
  codeABCk(o, A, B, C, k);  // emit test instruction - position not needed
  return jump();
}

/*
** Returns the position of the instruction "controlling" a given
** jump (that is, its condition), or the jump itself if it is
** unconditional.
*/
Instruction *FuncState::getjumpcontrol(int position) {
  auto *pi = &getProto().getCode()[position];
  if (position >= 1 && InstructionView(*(pi-1)).testTMode())
    return pi-1;
  else
    return pi;
}

/*
** Patch destination register for a TESTSET instruction.
** If instruction in position 'node' is not a TESTSET, return 0 ("fails").
** Otherwise, if 'reg' is not 'NO_REG', set it as the destination
** register. Otherwise, change instruction to a simple 'TEST' (produces
** no register value)
*/
int FuncState::patchtestreg(int node, int reg) {
  auto *i = getjumpcontrol(node);
  if (InstructionView(*i).opcode() != OP_TESTSET)
    return 0;  // cannot patch other instructions
  if (reg != NO_REG && reg != InstructionView(*i).b())
    SETARG_A(*i, static_cast<unsigned int>(reg));
  else {
     /* no register to put value or register already has the value;
        change instruction to simple test */
    *i = CREATE_ABCk(OP_TEST, InstructionView(*i).b(), 0, 0, InstructionView(*i).k());
  }
  return 1;
}

/*
** Traverse a list of tests ensuring no one produces a value
*/
int FuncState::removevalues(int list) {
  for (; list != NO_JUMP; list = getjump(list))
      patchtestreg(list, NO_REG);
  return list;
}

/*
** Traverse a list of tests, patching their destination address and
** registers: tests producing values jump to 'vtarget' (and put their
** values in 'reg'), other tests jump to 'dtarget'.
*/
void FuncState::patchlistaux(int list, int vtarget, int reg, int dtarget) {
  while (list != NO_JUMP) {
    auto next = getjump(list);
    if (patchtestreg(list, reg))
      fixjump(list, vtarget);
    else
      fixjump(list, dtarget);  // jump to default target
    list = next;
  }
}

// limit for difference between lines in relative line info.
#define LIMLINEDIFF	0x80

/*
** Save line info for a new instruction. If difference from last line
** does not fit in a byte, of after that many instructions, save a new
** absolute line info; (in that case, the special value 'ABSLINEINFO'
** in 'lineinfo' signals the existence of this absolute information.)
** Otherwise, store the difference from last line in 'lineinfo'.
*/
void FuncState::savelineinfo(Proto& proto, int line) {
  auto linedif = line - getPreviousLine();
  auto pcval = getPC() - 1;  // last instruction coded
  if (abs(linedif) >= LIMLINEDIFF || postIncrementInstructionsSinceAbsoluteLineInfo() >= MAXIWTHABS) {
    moonM_growvector<AbsLineInfo>(getLexState().getLuaState(), proto.getAbsLineInfoRef(), getNumberOfAbsoluteLineInfo(),
                    proto.getAbsLineInfoSizeRef(), std::numeric_limits<int>::max(), "lines");
    proto.getAbsLineInfo()[getNumberOfAbsoluteLineInfo()].setPC(pcval);
    proto.getAbsLineInfo()[postIncrementNumberOfAbsoluteLineInfo()].setLine(line);
    linedif = ABSLINEINFO;  // signal that there is absolute information
    setInstructionsSinceAbsoluteLineInfo(1);  // restart counter
  }
  moonM_growvector<ls_byte>(getLexState().getLuaState(), proto.getLineInfoRef(), pcval, proto.getLineInfoSizeRef(),
                  std::numeric_limits<int>::max(), "opcodes");
  proto.getLineInfo()[pcval] = static_cast<ls_byte>(linedif);
  setPreviousLine(line);  // last line saved
}

/*
** Remove line information from the last instruction.
** If line information for that instruction is absolute, set 'iwthabs'
** above its max to force the new (replacing) instruction to have
** absolute line info, too.
*/
void FuncState::removelastlineinfo() {
  auto& proto = getProto();
  auto pcval = getPC() - 1;  // last instruction coded
  if (proto.getLineInfo()[pcval] != ABSLINEINFO) {  // relative line info?
    setPreviousLine(getPreviousLine() - proto.getLineInfo()[pcval]);  // correct last line saved
    decrementInstructionsSinceAbsoluteLineInfo();  // undo previous increment
  }
  else {  // absolute line information
    moon_assert(proto.getAbsLineInfo()[getNumberOfAbsoluteLineInfo() - 1].getPC() == pcval);
    decrementNumberOfAbsoluteLineInfo();  // remove it
    setInstructionsSinceAbsoluteLineInfo(MAXIWTHABS + 1);  // force next line info to be absolute
  }
}

/*
** Remove the last instruction created, correcting line information
** accordingly.
*/
void FuncState::removelastinstruction() {
  removelastlineinfo();
  decrementPC();
}

/*
** Format and emit an 'iAsBx' instruction.
*/
int FuncState::codeAsBx(OpCode o, int A, int Bc) {
  auto b = Bc + OFFSET_sBx;
  moon_assert(getOpMode(o) == OpMode::iAsBx);
  moon_assert(A <= MAXARG_A && b <= MAXARG_Bx);
  return code(CREATE_ABx(o, A, b));
}

/*
** Emit an "extra argument" instruction (format 'iAx')
*/
int FuncState::codeextraarg(int A) {
  moon_assert(A <= MAXARG_Ax);
  return code(CREATE_Ax(OP_EXTRAARG, A));
}

/*
** Emit a "load constant" instruction, using either 'OP_LOADK'
** (if constant index 'k' fits in 18 bits) or an 'OP_LOADKX'
** instruction with "extra argument".
*/
int FuncState::codek(int reg, int k) {
  if (k <= MAXARG_Bx)
    return codeABx(OP_LOADK, reg, k);
  else {
    auto p = codeABx(OP_LOADKX, reg, 0);
    codeextraarg(k);  // emit extra arg - position not needed
    return p;
  }
}

/*
** Free register 'reg', if it is neither a constant index nor
** a local variable.
)
*/
void FuncState::freeRegister(int reg) {
  if (reg >= moonY_nvarstack(this)) {
    decrementFirstFreeRegister();
    moon_assert(reg == getFirstFreeRegister());
  }
}

/*
** Free two registers in proper order
*/
void FuncState::freeRegisters(int r1, int r2) {
  if (r1 > r2) {
    freeRegister(r1);
    freeRegister(r2);
  }
  else {
    freeRegister(r2);
    freeRegister(r1);
  }
}

/*
** Free register used by expression 'e' (if any)
*/
void FuncState::freeExpression(ExpDesc& expr) {
  if (expr.getKind() == VNONRELOC)
    freeRegister(expr.getInfo());
}

/*
** Free registers used by expressions 'e1' and 'e2' (if any) in proper
** order.
*/
void FuncState::freeExpressions(ExpDesc& leftExpr, ExpDesc& rightExpr) {
  auto leftReg = (leftExpr.getKind() == VNONRELOC) ? leftExpr.getInfo() : -1;
  auto rightReg = (rightExpr.getKind() == VNONRELOC) ? rightExpr.getInfo() : -1;
  freeRegisters(leftReg, rightReg);
}

/*
** Add constant 'v' to prototype's list of constants (field 'k').
*/
int FuncState::addk(Proto& proto, TValue *v) {
  moon_State *L = getLexState().getLuaState();
  auto oldsize = proto.getConstantsSize();
  auto k = getNumberOfConstants();
  moonM_growvector<TValue>(L, proto.getConstantsRef(), k, proto.getConstantsSizeRef(), MAXARG_Ax, "constants");
  auto constantsSpan = proto.getConstantsSpan();
  while (oldsize < static_cast<int>(constantsSpan.size()))
    setnilvalue(&constantsSpan[oldsize++]);
  constantsSpan[k] = *v;
  incrementNumberOfConstants();
  moonC_barrier(L, &proto, v);
  return k;
}

/*
** Use scanner's table to cache position of constants in constant list
** and try to reuse constants. Because some values should not be used
** as keys (nil cannot be a key, integer keys can collapse with float
** keys), the caller must provide a useful 'key' for indexing the cache.
*/
int FuncState::k2proto(TValue *key, TValue *v) {
  TValue val;
  Proto& proto = getProto();
  MoonT tag = getKCache()->get(key, &val);  // query scanner table
  if (!tagisempty(tag)) {  // is there an index there?
    auto k = cast_int(ivalue(&val));
    // collisions can happen only for float keys
    moon_assert(ttisfloat(key) || VirtualMachine::rawequalObj(&proto.getConstants()[k], v));
    return k;  // reuse index
  }
  else {  // constant not found; create a new entry
    auto k = addk(proto, v);
    /* cache it for reuse; numerical value does not need GC barrier;
       table is not a metatable, so it does not need to invalidate cache */
    val.setInt(k);
    getKCache()->set(getLexState().getLuaState(), key, &val);
    return k;
  }
}

/*
** Add a string to list of constants and return its index.
*/
int FuncState::stringK(TString& s) {
  TValue o;
  setsvalue(getLexState().getLuaState(), &o, &s);
  return k2proto(&o, &o);  // use string itself as key
}

/*
** Add an integer to list of constants and return its index.
*/
int FuncState::intK(moon_Integer n) {
  TValue o;
  o.setInt(n);
  return k2proto(&o, &o);  // use integer itself as key
}

/*
** Add a float to list of constants and return its index. Floats
** with integral values need a different key, to avoid collision
** with actual integers. To that end, we add to the number its smaller
** power-of-two fraction that is still significant in its scale.
** (For doubles, the fraction would be 2^-52).
** This method is not bulletproof: different numbers may generate the
** same key (e.g., very large numbers will overflow to 'inf') and for
** floats larger than 2^53 the result is still an integer. For those
** cases, just generate a new entry. At worst, this only wastes an entry
** with a duplicate.
*/
int FuncState::numberK(moon_Number r) {
  TValue o, kv;
  o.setFloat(r);  // value as a TValue
  if (r == 0) {  // handle zero as a special case
    setpvalue(&kv, this);  // use FuncState as index
    return k2proto(&kv, &o);  // cannot collide
  }
  else {
    const int nbm = l_floatatt(MANT_DIG);
    const moon_Number q = l_mathop(ldexp)(l_mathop(1.0), -nbm + 1);
    const moon_Number k =  r * (1 + q);  // key
    moon_Integer ik;
    kv.setFloat(k);  // key as a TValue
    if (!VirtualMachine::flttointeger(k, &ik, F2Imod::F2Ieq)) {  // not an integer value?
      auto n = k2proto(&kv, &o);  // use key
      if (VirtualMachine::rawequalObj(&getProto().getConstants()[n], &o))  // correct value?
        return n;
    }
    /* else, either key is still an integer or there was a collision;
       anyway, do not try to reuse constant; instead, create a new one */
    return addk(getProto(), &o);
  }
}

/*
** Add a false to list of constants and return its index.
*/
int FuncState::boolF() {
  TValue o;
  setbfvalue(&o);
  return k2proto(&o, &o);  // use boolean itself as key
}

/*
** Add a true to list of constants and return its index.
*/
int FuncState::boolT() {
  TValue o;
  setbtvalue(&o);
  return k2proto(&o, &o);  // use boolean itself as key
}

/*
** Add nil to list of constants and return its index.
*/
int FuncState::nilK() {
  TValue k, v;
  setnilvalue(&v);
  // cannot use nil as key; instead use table itself
  sethvalue(getLexState().getLuaState(), &k, getKCache());
  return k2proto(&k, &v);
}

/*
** Check whether 'i' can be stored in an 'sC' operand. Equivalent to
** (0 <= int2sC(i) && int2sC(i) <= MAXARG_C) but without risk of
** overflows in the hidden addition inside 'int2sC'.
*/
static int fitsC (moon_Integer i) {
  return (l_castS2U(i) + OFFSET_sC <= cast_uint(MAXARG_C));
}

/*
** Check whether 'i' can be stored in an 'sBx' operand.
*/
static int fitsBx (moon_Integer i) {
  return (-OFFSET_sBx <= i && i <= MAXARG_Bx - OFFSET_sBx);
}

void FuncState::floatCode(int reg, moon_Number flt) {
  moon_Integer fi;
  if (VirtualMachine::flttointeger(flt, &fi, F2Imod::F2Ieq) && fitsBx(fi))
    codeAsBx(OP_LOADF, reg, cast_int(fi));  // emit instruction - position not needed
  else
    codek(reg, numberK(flt));  // emit instruction - position not needed
}

/*
** Convert a constant in 'v' into an expression description 'e'
*/
static void const2exp (TValue *value, ExpDesc& expr) {
  switch (ttypetag(value)) {
    case MoonT::NUMINT:
      expr.setKind(VKINT); expr.setIntValue(ivalue(value));
      break;
    case MoonT::NUMFLT:
      expr.setKind(VKFLT); expr.setFloatValue(fltvalue(value));
      break;
    case MoonT::VFALSE:
      expr.setKind(VFALSE);
      break;
    case MoonT::VTRUE:
      expr.setKind(VTRUE);
      break;
    case MoonT::NIL:
      expr.setKind(VNIL);
      break;
    case MoonT::SHRSTR:  case MoonT::LNGSTR:
      expr.setKind(VKSTR); expr.setStringValue(tsvalue(value));
      break;
    default: moon_assert(0);
  }
}

/*
** Convert a VKSTR to a VK
*/
int FuncState::str2K(ExpDesc& expr) {
  moon_assert(expr.getKind() == VKSTR);
  expr.setInfo(stringK(*expr.getStringValue()));
  expr.setKind(VK);
  return expr.getInfo();
}

/*
** Ensure expression value is in register 'reg', making 'e' a
** non-relocatable expression.
** (Expression still may have jump lists.)
*/
void FuncState::discharge2reg(ExpDesc& expr, int targetRegister) {
  dischargevars(expr);
  switch (expr.getKind()) {
    case VNIL: {
      nil(targetRegister, 1);
      break;
    }
    case VFALSE: {
      codeABC(OP_LOADFALSE, targetRegister, 0, 0);
      break;
    }
    case VTRUE: {
      codeABC(OP_LOADTRUE, targetRegister, 0, 0);
      break;
    }
    case VKSTR: {
      str2K(expr);
    }  // FALLTHROUGH
    case VK: {
      codek(targetRegister, expr.getInfo());
      break;
    }
    case VKFLT: {
      floatCode(targetRegister, expr.getFloatValue());
      break;
    }
    case VKINT: {
      intCode(targetRegister, expr.getIntValue());
      break;
    }
    case VRELOC: {
      Instruction *instr = &getinstruction(this, expr);
      SETARG_A(*instr, static_cast<unsigned int>(targetRegister));  // instruction will put result in 'targetRegister'
      break;
    }
    case VNONRELOC: {
      if (targetRegister != expr.getInfo())
        codeABC(OP_MOVE, targetRegister, expr.getInfo(), 0);
      break;
    }
    default: {
      moon_assert(expr.getKind() == VJMP);
      return;  // nothing to do...
    }
  }
  expr.setInfo(targetRegister);
  expr.setKind(VNONRELOC);
}

/*
** Ensure expression value is in a register, making 'e' a
** non-relocatable expression.
** (Expression still may have jump lists.)
*/
void FuncState::discharge2anyreg(ExpDesc& expr) {
  if (expr.getKind() != VNONRELOC) {  // no fixed register yet?
    reserveregs(1);  // get a register
    discharge2reg(expr, getFirstFreeRegister()-1);  // put value there
  }
}

int FuncState::code_loadbool(int A, OpCode op) {
  getlabel();  // those instructions may be jump targets
  return codeABC(op, A, 0, 0);
}

/*
** check whether list has any jump that do not produce a value
** or produce an inverted value
*/
int FuncState::need_value(int list) {
  for (; list != NO_JUMP; list = getjump(list)) {
    auto i = *getjumpcontrol(list);
    if (InstructionView(i).opcode() != OP_TESTSET) return 1;
  }
  return 0;  // not found
}

/*
** Ensures final expression result (which includes results from its
** jump lists) is in register 'reg'.
** If expression has jumps, need to patch these jumps either to
** its final position or to "load" instructions (for those tests
** that do not produce values).
*/
void FuncState::exp2reg(ExpDesc& expr, int targetRegister) {
  discharge2reg(expr, targetRegister);
  if (expr.getKind() == VJMP)  // expression itself is a test?
    concat(expr.getTrueListRef(), expr.getInfo());  // put this jump in 't' list
  if (hasjumps(expr)) {
    auto falsePosition = NO_JUMP;  // position of an eventual LOAD false
    auto truePosition = NO_JUMP;  // position of an eventual LOAD true
    if (need_value(expr.getTrueList()) || need_value(expr.getFalseList())) {
      auto fallJump = (expr.getKind() == VJMP) ? NO_JUMP : jump();
      falsePosition = code_loadbool(targetRegister, OP_LFALSESKIP);  // skip next inst.
      truePosition = code_loadbool(targetRegister, OP_LOADTRUE);
      // jump around these booleans if 'expr' is not a test
      patchtohere(fallJump);
    }
    auto finalLabel = getlabel();  // position after whole expression
    patchlistaux(expr.getFalseList(), finalLabel, targetRegister, falsePosition);
    patchlistaux(expr.getTrueList(), finalLabel, targetRegister, truePosition);
  }
  expr.setFalseList(NO_JUMP); expr.setTrueList(NO_JUMP);
  expr.setInfo(targetRegister);
  expr.setKind(VNONRELOC);
}

/*
** Try to make 'e' a K expression with an index in the range of R/K
** indices. Return true iff succeeded.
*/
int FuncState::exp2K(ExpDesc& expr) {
  if (!hasjumps(expr)) {
    int constantIndex;
    switch (expr.getKind()) {  // move constants to 'k'
      case VTRUE: constantIndex = boolT(); break;
      case VFALSE: constantIndex = boolF(); break;
      case VNIL: constantIndex = nilK(); break;
      case VKINT: constantIndex = intK(expr.getIntValue()); break;
      case VKFLT: constantIndex = numberK(expr.getFloatValue()); break;
      case VKSTR: constantIndex = stringK(*expr.getStringValue()); break;
      case VK: constantIndex = expr.getInfo(); break;
      default: return 0;  // not a constant
    }
    if (constantIndex <= MAXINDEXRK) {  // does constant fit in 'argC'?
      expr.setKind(VK);  // make expression a 'K' expression
      expr.setInfo(constantIndex);
      return 1;
    }
  }
  // else, expression doesn't fit; leave it unchanged
  return 0;
}

/*
** Ensures final expression result is in a valid R/K index
** (that is, it is either in a register or in 'k' with an index
** in the range of R/K indices).
** Returns 1 iff expression is K.
*/
int FuncState::exp2RK(ExpDesc& expr) {
  if (exp2K(expr))
    return 1;
  else {  // not a constant in the right range: put it in a register
    exp2anyreg(expr);  // put in register - specific register not needed
    return 0;
  }
}

void FuncState::codeABRK(OpCode o, int A, int B, ExpDesc& ec) {
  auto k = exp2RK(ec);
  codeABCk(o, A, B, ec.getInfo(), k);
}

/*
** Negate condition 'e' (where 'e' is a comparison).
*/
void FuncState::negatecondition(ExpDesc& expr) {
  Instruction *instr = getjumpcontrol(expr.getInfo());
  InstructionView view(*instr);
  moon_assert(view.testTMode() && view.opcode() != OP_TESTSET &&
                                  view.opcode() != OP_TEST);
  SETARG_k(*instr, static_cast<unsigned int>(view.k() ^ 1));
}

/*
** Emit instruction to jump if 'e' is 'cond' (that is, if 'cond'
** is true, code will jump if 'e' is true.) Return jump position.
** Optimize when 'e' is 'not' something, inverting the condition
** and removing the 'not'.
*/
int FuncState::jumponcond(ExpDesc& expr, int condition) {
  if (expr.getKind() == VRELOC) {
    auto ie = getinstruction(this, expr);
    if (InstructionView(ie).opcode() == OP_NOT) {
      removelastinstruction();  // remove previous OP_NOT
      return condjump(OP_TEST, InstructionView(ie).b(), 0, 0, !condition);
    }
    // else go through
  }
  discharge2anyreg(expr);
  freeExpression(expr);
  return condjump(OP_TESTSET, NO_REG, expr.getInfo(), 0, condition);
}

/*
** Code 'not e', doing constant folding.
*/
void FuncState::codenot(ExpDesc& expr) {
  switch (expr.getKind()) {
    case VNIL: case VFALSE: {
      expr.setKind(VTRUE);  // true == not nil == not false
      break;
    }
    case VK: case VKFLT: case VKINT: case VKSTR: case VTRUE: {
      expr.setKind(VFALSE);  // false == not "x" == not 0.5 == not 1 == not true
      break;
    }
    case VJMP: {
      negatecondition(expr);
      break;
    }
    case VRELOC:
    case VNONRELOC: {
      discharge2anyreg(expr);
      freeExpression(expr);
      expr.setInfo(codeABC(OP_NOT, 0, expr.getInfo(), 0));
      expr.setKind(VRELOC);
      break;
    }
    default: moon_assert(0);  // cannot happen
  }
  // interchange true and false lists
  { int temp = expr.getFalseList(); expr.setFalseList(expr.getTrueList()); expr.setTrueList(temp); }
  removevalues(expr.getFalseList());  // values are useless when negated
  removevalues(expr.getTrueList());
}

/*
** Check whether expression 'e' is a short literal string
*/
int FuncState::isKstr(ExpDesc& expr) {
  return (expr.getKind() == VK && !hasjumps(expr) && expr.getInfo() <= MAXARG_B &&
          ttisshrstring(&getProto().getConstants()[expr.getInfo()]));
}

/*
** Check whether expression 'expr' is a literal integer.
*/
static bool isKint (ExpDesc& expr) {
  return (expr.getKind() == VKINT && !hasjumps(expr));
}

/*
** Check whether expression 'expr' is a literal integer in
** proper range to fit in register C
*/
static bool isCint (ExpDesc& expr) {
  return isKint(expr) && (l_castS2U(expr.getIntValue()) <= l_castS2U(MAXARG_C));
}

/*
** Check whether expression 'expr' is a literal integer in
** proper range to fit in register sC
*/
static bool isSCint (ExpDesc& expr) {
  return isKint(expr) && fitsC(expr.getIntValue());
}

/*
** Check whether expression 'e' is a literal integer or float in
** proper range to fit in a register (sB or sC).
*/
static bool isSCnumber (ExpDesc& expr, int *intResult, int *isFloat) {
  moon_Integer intValue;
  if (expr.getKind() == VKINT)
    intValue = expr.getIntValue();
  else if (expr.getKind() == VKFLT && VirtualMachine::flttointeger(expr.getFloatValue(), &intValue, F2Imod::F2Ieq))
    *isFloat = 1;
  else
    return false;  // not a number
  if (!hasjumps(expr) && fitsC(intValue)) {
    *intResult = int2sC(cast_int(intValue));
    return true;
  }
  else
    return false;
}

/*
** Return false if folding can raise an error.
** Bitwise operations need operands convertible to integers; division
** operations cannot have 0 as divisor.
*/
static bool validop (int op, TValue *v1, TValue *v2) {
  switch (op) {
    case MOON_OPBAND: case MOON_OPBOR: case MOON_OPBXOR:
    case MOON_OPSHL: case MOON_OPSHR: case MOON_OPBNOT: {  // conversion errors
      moon_Integer i;
      return (tointegerns(v1, &i) &&
              tointegerns(v2, &i));
    }
    case MOON_OPDIV: case MOON_OPIDIV: case MOON_OPMOD:  // division by 0
      return (nvalue(v2) != 0);
    default: return true;  // everything else is valid
  }
}

/*
** Try to "constant-fold" an operation; return 1 iff successful.
** (In this case, 'e1' has the final result.)
*/
int FuncState::constfolding(int op, ExpDesc& e1, const ExpDesc& e2) {
  TValue v1, v2, res;
  if (!tonumeral(e1, &v1) || !tonumeral(e2, &v2) || !validop(op, &v1, &v2))
    return 0;  // non-numeric operands or not safe to fold
  if (!moonO_rawarith(getLexState().getLuaState(), op, &v1, &v2, &res))
    return 0;  // operation failed
  if (ttisinteger(&res)) {
    e1.setKind(VKINT);
    e1.setIntValue(ivalue(&res));
  }
  else {  // folds neither NaN nor 0.0 (to avoid problems with -0.0)
    moon_Number n = fltvalue(&res);
    if (mooni_numisnan(n) || n == 0)
      return 0;
    e1.setKind(VKFLT);
    e1.setFloatValue(n);
  }
  return 1;
}

/*
** Convert a BinOpr to an OpCode  (ORDER OPR - ORDER OP)
*/
static inline OpCode binopr2op (BinOpr opr, BinOpr baser, OpCode base) {
  moon_assert(baser <= opr &&
            ((baser == BinOpr::OPR_ADD && opr <= BinOpr::OPR_SHR) ||
             (baser == BinOpr::OPR_LT && opr <= BinOpr::OPR_LE)));
  return static_cast<OpCode>((cast_int(opr) - cast_int(baser)) + cast_int(base));
}

/*
** Convert a UnOpr to an OpCode  (ORDER OPR - ORDER OP)
*/
static inline OpCode unopr2op (UnOpr opr) {
  return static_cast<OpCode>((cast_int(opr) - cast_int(UnOpr::OPR_MINUS)) +
                                       cast_int(OP_UNM));
}

/*
** Convert a BinOpr to a tag method  (ORDER OPR - ORDER TM)
*/
static inline TMS binopr2TM (BinOpr opr) {
  moon_assert(BinOpr::OPR_ADD <= opr && opr <= BinOpr::OPR_SHR);
  return static_cast<TMS>((cast_int(opr) - cast_int(BinOpr::OPR_ADD)) + cast_int(TMS::TM_ADD));
}

/*
** Emit code for unary expressions that "produce values"
** (everything but 'not').
** Expression to produce final result will be encoded in 'e'.
*/
void FuncState::codeunexpval(OpCode operation, ExpDesc& expr, int line) {
  auto targetRegister = exp2anyreg(expr);  // opcodes operate only on registers
  freeExpression(expr);
  expr.setInfo(codeABC(operation, 0, targetRegister, 0));  // generate opcode
  expr.setKind(VRELOC);  // all those operations are relocatable
  fixline(line);
}

/*
** Emit code for binary expressions that "produce values"
** (everything but logical operators 'and'/'or' and comparison
** operators).
** Expression to produce final result will be encoded in 'e1'.
*/
void FuncState::finishbinexpval(ExpDesc& leftExpr, ExpDesc& rightExpr, OpCode operation, int rightValue,
                                 int flip, int line, OpCode metaOpcode, TMS event) {
  auto leftRegister = exp2anyreg(leftExpr);
  auto instructionPosition = codeABCk(operation, 0, leftRegister, rightValue, 0);
  freeExpressions(leftExpr, rightExpr);
  leftExpr.setInfo(instructionPosition);
  leftExpr.setKind(VRELOC);  // all those operations are relocatable
  fixline(line);
  codeABCk(metaOpcode, leftRegister, rightValue, cast_int(event), flip);  // metamethod
  fixline(line);
}

/*
** Emit code for binary expressions that "produce values" over
** two registers.
*/
void FuncState::codebinexpval(BinOpr opr, ExpDesc& leftExpr, ExpDesc& rightExpr, int line) {
  auto operation = binopr2op(opr, BinOpr::OPR_ADD, OP_ADD);
  auto rightRegister = exp2anyreg(rightExpr);  // make sure 'rightExpr' is in a register
  // 'leftExpr' must be already in a register or it is a constant
  moon_assert((VNIL <= leftExpr.getKind() && leftExpr.getKind() <= VKSTR) ||
             leftExpr.getKind() == VNONRELOC || leftExpr.getKind() == VRELOC);
  moon_assert(OP_ADD <= operation && operation <= OP_SHR);
  finishbinexpval(leftExpr, rightExpr, operation, rightRegister, 0, line, OP_MMBIN, binopr2TM(opr));
}

/*
** Code binary operators with immediate operands.
*/
void FuncState::codebini(OpCode operation, ExpDesc& leftExpr, ExpDesc& rightExpr, int flip,
                          int line, TMS event) {
  int rightValue = int2sC(cast_int(rightExpr.getIntValue()));  // immediate operand
  moon_assert(rightExpr.getKind() == VKINT);
  finishbinexpval(leftExpr, rightExpr, operation, rightValue, flip, line, OP_MMBINI, event);
}

/*
** Code binary operators with K operand.
*/
void FuncState::codebinK(BinOpr opr, ExpDesc& leftExpr, ExpDesc& rightExpr, int flip, int line) {
  TMS event = binopr2TM(opr);
  int constantIndex = rightExpr.getInfo();  // K index
  OpCode operation = binopr2op(opr, BinOpr::OPR_ADD, OP_ADDK);
  finishbinexpval(leftExpr, rightExpr, operation, constantIndex, flip, line, OP_MMBINK, event);
}

/* Try to code a binary operator negating its second operand.
** For the metamethod, 2nd operand must keep its original value.
*/
int FuncState::finishbinexpneg(ExpDesc& e1, ExpDesc& e2, OpCode op, int line, TMS event) {
  if (!isKint(e2))
    return 0;  // not an integer constant
  else {
    moon_Integer i2 = e2.getIntValue();
    if (!(fitsC(i2) && fitsC(-i2)))
      return 0;  // not in the proper range
    else {  // operating a small integer constant
      int v2 = cast_int(i2);
      finishbinexpval(e1, e2, op, int2sC(-v2), 0, line, OP_MMBINI, event);
      // correct metamethod argument
      SETARG_B(getProto().getCode()[getPC() - 1], static_cast<unsigned int>(int2sC(v2)));
      return 1;  // successfully coded
    }
  }
}

static void swapexps (ExpDesc& e1, ExpDesc& e2) {
  ExpDesc temp = e1; e1 = e2; e2 = temp;  // swap 'e1' and 'e2'
}

/*
** Code binary operators with no constant operand.
*/
void FuncState::codebinNoK(BinOpr opr, ExpDesc& e1, ExpDesc& e2, int flip, int line) {
  if (flip)
    swapexps(e1, e2);  // back to original order
  codebinexpval(opr, e1, e2, line);  // use standard operators
}

/*
** Code arithmetic operators ('+', '-', ...). If second operand is a
** constant in the proper range, use variant opcodes with K operands.
*/
void FuncState::codearith(BinOpr opr, ExpDesc& leftExpr, ExpDesc& rightExpr, int flip, int line) {
  if (tonumeral(rightExpr, nullptr) && exp2K(rightExpr))  // K operand?
    codebinK(opr, leftExpr, rightExpr, flip, line);
  else  // 'rightExpr' is neither an immediate nor a K operand
    codebinNoK(opr, leftExpr, rightExpr, flip, line);
}

/*
** Code commutative operators ('+', '*'). If first operand is a
** numeric constant, change order of operands to try to use an
** immediate or K operator.
*/
void FuncState::codecommutative(BinOpr op, ExpDesc& e1, ExpDesc& e2, int line) {
  int flip = 0;
  if (tonumeral(e1, nullptr)) {  // is first operand a numeric constant?
    swapexps(e1, e2);  // change order
    flip = 1;
  }
  if (op == BinOpr::OPR_ADD && isSCint(e2))  // immediate operand?
    codebini(OP_ADDI, e1, e2, flip, line, TMS::TM_ADD);
  else
    codearith(op, e1, e2, flip, line);
}

/*
** Code bitwise operations; they are all commutative, so the function
** tries to put an integer constant as the 2nd operand (a K operand).
*/
void FuncState::codebitwise(BinOpr opr, ExpDesc& e1, ExpDesc& e2, int line) {
  int flip = 0;
  if (e1.getKind() == VKINT) {
    swapexps(e1, e2);  // 'e2' will be the constant operand
    flip = 1;
  }
  if (e2.getKind() == VKINT && exp2K(e2))  // K operand?
    codebinK(opr, e1, e2, flip, line);
  else  // no constants
    codebinNoK(opr, e1, e2, flip, line);
}

/*
** Emit code for order comparisons. When using an immediate operand,
** 'isfloat' tells whether the original value was a float.
*/
void FuncState::codeorder(BinOpr opr, ExpDesc& e1, ExpDesc& e2) {
  int r1, r2;
  int im;
  int isfloat = 0;
  OpCode op;
  if (isSCnumber(e2, &im, &isfloat)) {
    // use immediate operand
    r1 = exp2anyreg(e1);
    r2 = im;
    op = binopr2op(opr, BinOpr::OPR_LT, OP_LTI);
  }
  else if (isSCnumber(e1, &im, &isfloat)) {
    // transform (A < B) to (B > A) and (A <= B) to (B >= A)
    r1 = exp2anyreg(e2);
    r2 = im;
    op = binopr2op(opr, BinOpr::OPR_LT, OP_GTI);
  }
  else {  // regular case, compare two registers
    r1 = exp2anyreg(e1);
    r2 = exp2anyreg(e2);
    op = binopr2op(opr, BinOpr::OPR_LT, OP_LT);
  }
  freeExpressions(e1, e2);
  e1.setInfo(condjump(op, r1, r2, isfloat, 1));
  e1.setKind(VJMP);
}

/*
** Emit code for equality comparisons ('==', '~=').
** 'e1' was already put as RK by 'moonK_infix'.
*/
void FuncState::codeeq(BinOpr opr, ExpDesc& e1, ExpDesc& e2) {
  int r1, r2;
  int im;
  int isfloat = 0;  // not needed here, but kept for symmetry
  OpCode op;
  if (e1.getKind() != VNONRELOC) {
    moon_assert(e1.getKind() == VK || e1.getKind() == VKINT || e1.getKind() == VKFLT);
    swapexps(e1, e2);
  }
  r1 = exp2anyreg(e1);  // 1st expression must be in register
  if (isSCnumber(e2, &im, &isfloat)) {
    op = OP_EQI;
    r2 = im;  // immediate operand
  }
  else if (exp2RK(e2)) {  // 2nd expression is constant?
    op = OP_EQK;
    r2 = e2.getInfo();  // constant index
  }
  else {
    op = OP_EQ;  // will compare two registers
    r2 = exp2anyreg(e2);
  }
  freeExpressions(e1, e2);
  e1.setInfo(condjump(op, r1, r2, isfloat, (opr == BinOpr::OPR_EQ)));
  e1.setKind(VJMP);
}

/*
** Create code for '(e1 .. e2)'.
** For '(e1 .. e2.1 .. e2.2)' (which is '(e1 .. (e2.1 .. e2.2))',
** because concatenation is right associative), merge both CONCATs.
*/
void FuncState::codeconcat(ExpDesc& e1, ExpDesc& e2, int line) {
  Instruction *ie2 = previousinstruction();
  if (InstructionView(*ie2).opcode() == OP_CONCAT) {  // is 'e2' a concatenation?
    int n = InstructionView(*ie2).b();  // # of elements concatenated in 'e2'
    moon_assert(e1.getInfo() + 1 == InstructionView(*ie2).a());
    freeExpression(e2);
    SETARG_A(*ie2, static_cast<unsigned int>(e1.getInfo()));  // correct first element ('e1')
    SETARG_B(*ie2, static_cast<unsigned int>(n + 1));  // will concatenate one more element
  }
  else {  // 'e2' is not a concatenation
    codeABC(OP_CONCAT, e1.getInfo(), 2, 0);  // new concat opcode
    freeExpression(e2);
    fixline(line);
  }
}

/*
** return the final target of a jump (skipping jumps to jumps)
*/
int FuncState::finaltarget(int i) {
  auto codeSpan = getProto().getCodeSpan();
  for (int count = 0; count < 100; count++) {  // avoid infinite loops
    Instruction instr = codeSpan[i];
    if (InstructionView(instr).opcode() != OP_JMP)
      break;
    else
      i += InstructionView(instr).sj() + 1;
  }
  return i;
}

/*
** =====================================================================
** FuncState Method Implementations
** Simple wrappers that forward to existing moonK_* functions
** =====================================================================
*/

int FuncState::code(Instruction i) {
  Proto& proto = getProto();
  // put new instruction in code array
  moonM_growvector<Instruction>(getLexState().getLuaState(), proto.getCodeRef(), getPC(), proto.getCodeSizeRef(),
                  std::numeric_limits<int>::max(), "opcodes");
  proto.getCode()[postIncrementPC()] = i;
  savelineinfo(proto, getLexState().getLastLine());
  return getPC() - 1;  // index of new instruction
}

int FuncState::codeABx(int o, int A, int Bx) {
  OpCode op = static_cast<OpCode>(o);
  moon_assert(getOpMode(op) == OpMode::iABx);
  moon_assert(A <= MAXARG_A && Bx <= MAXARG_Bx);
  return code(CREATE_ABx(op, A, Bx));
}

int FuncState::codeABCk(int o, int A, int B, int C, int k) {
  OpCode op = static_cast<OpCode>(o);
  moon_assert(getOpMode(op) == OpMode::iABC);
  moon_assert(A <= MAXARG_A && B <= MAXARG_B &&
             C <= MAXARG_C && (k & ~1) == 0);
  return code(CREATE_ABCk(op, A, B, C, k));
}

int FuncState::codevABCk(int o, int A, int B, int C, int k) {
  OpCode op = static_cast<OpCode>(o);
  moon_assert(getOpMode(op) == OpMode::ivABC);
  moon_assert(A <= MAXARG_A && B <= MAXARG_vB &&
             C <= MAXARG_vC && (k & ~1) == 0);
  return code(CREATE_vABCk(op, A, B, C, k));
}

int FuncState::codesJ(int o, int sj, int k) {
  int j = sj + OFFSET_sJ;
  moon_assert(getOpMode(static_cast<OpCode>(o)) == OpMode::isJ);
  moon_assert(j <= MAXARG_sJ && (k & ~1) == 0);
  return code(CREATE_sJ(static_cast<OpCode>(o), j, k));
}

int FuncState::exp2const(const ExpDesc& expr, TValue *value) {
  if (hasjumps(expr))
    return 0;  // not a constant
  switch (expr.getKind()) {
    case VFALSE:
      setbfvalue(value);
      return 1;
    case VTRUE:
      setbtvalue(value);
      return 1;
    case VNIL:
      setnilvalue(value);
      return 1;
    case VKSTR: {
      setsvalue(getLexState().getLuaState(), value, expr.getStringValue());
      return 1;
    }
    case VCONST: {
      *value = *const2val(expr);
      return 1;
    }
    default: return tonumeral(expr, value);
  }
}

void FuncState::fixline(int line) {
  removelastlineinfo();
  savelineinfo(getProto(), line);
}

void FuncState::nil(int from, int n) {
  int l = from + n - 1;  // last register to set nil
  Instruction *previous = previousinstruction();
  if (InstructionView(*previous).opcode() == OP_LOADNIL) {  // previous is LOADNIL?
    int pfrom = InstructionView(*previous).a();  // get previous range
    int pl = pfrom + InstructionView(*previous).b();
    if ((pfrom <= from && from <= pl + 1) ||
        (from <= pfrom && pfrom <= l + 1)) {  // can connect both?
      if (pfrom < from) from = pfrom;  // from = min(from, pfrom)
      if (pl > l) l = pl;  // l = max(l, pl)
      SETARG_A(*previous, static_cast<unsigned int>(from));
      SETARG_B(*previous, static_cast<unsigned int>(l - from));
      return;
    }  // else go through
  }
  codeABC(OP_LOADNIL, from, n - 1, 0);  // else no optimization
}

void FuncState::reserveregs(int n) {
  checkstack(n);
  setFirstFreeRegister(cast_byte(getFirstFreeRegister() + n));
}

void FuncState::checkstack(int n) {
  int newstack = getFirstFreeRegister() + n;
  if (newstack > getProto().getMaxStackSize()) {
    moonY_checklimit(this, newstack, MAX_FSTACK, "registers");
    getProto().setMaxStackSize(cast_byte(newstack));
  }
}

void FuncState::intCode(int reg, moon_Integer i) {
  if (fitsBx(i))
    codeAsBx(OP_LOADI, reg, cast_int(i));
  else
    codek(reg, intK(i));
}

void FuncState::dischargevars(ExpDesc& expr) {
  switch (expr.getKind()) {
    case VCONST: {
      const2exp(const2val(expr), expr);
      break;
    }
    case VLOCAL: {  // already in a register
      int temp = expr.getLocalRegister();
      expr.setInfo(temp);  // (can't do a direct assignment; values overlap)
      expr.setKind(VNONRELOC);  // becomes a non-relocatable value
      break;
    }
    case VUPVAL: {  // move value to some (pending) register
      expr.setInfo(codeABC(OP_GETUPVAL, 0, expr.getInfo(), 0));
      expr.setKind(VRELOC);
      break;
    }
    case VINDEXUP: {
      expr.setInfo(codeABC(OP_GETTABUP, 0, expr.getIndexedTableReg(), expr.getIndexedKeyIndex()));
      expr.setKind(VRELOC);
      break;
    }
    case VINDEXI: {
      freeRegister(expr.getIndexedTableReg());
      expr.setInfo(codeABC(OP_GETI, 0, expr.getIndexedTableReg(), expr.getIndexedKeyIndex()));
      expr.setKind(VRELOC);
      break;
    }
    case VINDEXSTR: {
      freeRegister(expr.getIndexedTableReg());
      expr.setInfo(codeABC(OP_GETFIELD, 0, expr.getIndexedTableReg(), expr.getIndexedKeyIndex()));
      expr.setKind(VRELOC);
      break;
    }
    case VINDEXED: {
      freeRegisters(expr.getIndexedTableReg(), expr.getIndexedKeyIndex());
      expr.setInfo(codeABC(OP_GETTABLE, 0, expr.getIndexedTableReg(), expr.getIndexedKeyIndex()));
      expr.setKind(VRELOC);
      break;
    }
    case VVARARG: case VCALL: {
      setoneret(expr);
      break;
    }
    default: break;  // there is one value available (somewhere)
  }
}

int FuncState::exp2anyreg(ExpDesc& expr) {
  dischargevars(expr);
  if (expr.getKind() == VNONRELOC) {  // expression already has a register?
    if (!hasjumps(expr))  // no jumps?
      return expr.getInfo();  // result is already in a register
    if (expr.getInfo() >= moonY_nvarstack(this)) {  // reg. is not a local?
      exp2reg(expr, expr.getInfo());  // put final result in it
      return expr.getInfo();
    }
    /* else expression has jumps and cannot change its register
       to hold the jump values, because it is a local variable.
       Go through to the default case. */
  }
  exp2nextreg(expr);  // default: use next available register
  return expr.getInfo();
}

void FuncState::exp2anyregup(ExpDesc& expr) {
  if (expr.getKind() != VUPVAL || hasjumps(expr))
    exp2anyreg(expr);
}

void FuncState::exp2nextreg(ExpDesc& expr) {
  dischargevars(expr);
  freeExpression(expr);
  reserveregs(1);
  exp2reg(expr, getFirstFreeRegister() - 1);
}

void FuncState::exp2val(ExpDesc& expr) {
  if (expr.getKind() == VJMP || hasjumps(expr))
    exp2anyreg(expr);
  else
    dischargevars(expr);
}

void FuncState::self(ExpDesc& receiver, ExpDesc& methodKey) {
  exp2anyreg(receiver);  // result available via receiver.getInfo()
  int receiverReg = receiver.getInfo();  // register where 'receiver' was placed
  freeExpression(receiver);
  int baseRegister = getFirstFreeRegister();
  receiver.setInfo(baseRegister);  // base register for op_self
  receiver.setKind(VNONRELOC);  // self expression has a fixed register
  reserveregs(2);  // method and 'self' produced by op_self
  moon_assert(methodKey.getKind() == VKSTR);
  // is method name a short string in a valid K index?
  if (strisshr(methodKey.getStringValue()) && exp2K(methodKey)) {
    // can use 'self' opcode
    codeABCk(OP_SELF, baseRegister, receiverReg, methodKey.getInfo(), 0);
  }
  else {  // cannot use 'self' opcode; use move+gettable
    exp2anyreg(methodKey);  // put method name in a register - result via methodKey.getInfo()
    codeABC(OP_MOVE, baseRegister + 1, receiverReg, 0);  // copy self to base+1
    codeABC(OP_GETTABLE, baseRegister, receiverReg, methodKey.getInfo());  // get method
  }
  freeExpression(methodKey);
}

void FuncState::indexed(ExpDesc& t, ExpDesc& k) {
  int keystr = (k.getKind() == VKSTR) ? str2K(k) : -1;
  moon_assert(!hasjumps(t) &&
             (t.getKind() == VLOCAL || t.getKind() == VNONRELOC || t.getKind() == VUPVAL));
  if (t.getKind() == VUPVAL && !isKstr(k))  // upvalue indexed by non 'Kstr'?
    exp2anyreg(t);  // put it in a register - result via t.getInfo()
  if (t.getKind() == VUPVAL) {
    lu_byte temp = cast_byte(t.getInfo());  // upvalue index
    t.setIndexedTableReg(temp);  // (can't do a direct assignment; values overlap)
    moon_assert(isKstr(k));
    t.setIndexedKeyIndex(cast_short(k.getInfo()));  // literal short string
    t.setKind(VINDEXUP);
  }
  else {
    // register index of the table
    t.setIndexedTableReg(cast_byte((t.getKind() == VLOCAL) ? t.getLocalRegister(): t.getInfo()));
    if (isKstr(k)) {
      t.setIndexedKeyIndex(cast_short(k.getInfo()));  // literal short string
      t.setKind(VINDEXSTR);
    }
    else if (isCint(k)) {  // int. constant in proper range?
      t.setIndexedKeyIndex(cast_short(k.getIntValue()));
      t.setKind(VINDEXI);
    }
    else {
      t.setIndexedKeyIndex(cast_short(exp2anyreg(k)));  // register
      t.setKind(VINDEXED);
    }
  }
  t.setIndexedStringKeyIndex(keystr);  // string index in 'k'
  t.setIndexedReadOnly(0);  // by default, not read-only
}

void FuncState::goiftrue(ExpDesc& expr) {
  dischargevars(expr);
  int jumpPosition;  // pc of new jump
  switch (expr.getKind()) {
    case VJMP: {  // condition?
      negatecondition(expr);  // jump when it is false
      jumpPosition = expr.getInfo();  // save jump position
      break;
    }
    case VK: case VKFLT: case VKINT: case VKSTR: case VTRUE: {
      jumpPosition = NO_JUMP;  // always true; do nothing
      break;
    }
    default: {
      jumpPosition = jumponcond(expr, 0);  // jump when false
      break;
    }
  }
  concat(expr.getFalseListRef(), jumpPosition);  // insert new jump in false list
  patchtohere(expr.getTrueList());  // true list jumps to here (to go through)
  expr.setTrueList(NO_JUMP);
}

void FuncState::goiffalse(ExpDesc& expr) {
  dischargevars(expr);
  int jumpPosition;  // pc of new jump
  switch (expr.getKind()) {
    case VJMP: {
      jumpPosition = expr.getInfo();  // already jump if true
      break;
    }
    case VNIL: case VFALSE: {
      jumpPosition = NO_JUMP;  // always false; do nothing
      break;
    }
    default: {
      jumpPosition = jumponcond(expr, 1);  // jump if true
      break;
    }
  }
  concat(expr.getTrueListRef(), jumpPosition);  // insert new jump in 't' list
  patchtohere(expr.getFalseList());  // false list jumps to here (to go through)
  expr.setFalseList(NO_JUMP);
}

void FuncState::storevar(ExpDesc& var, ExpDesc& ex) {
  switch (var.getKind()) {
    case VLOCAL: {
      freeExpression(ex);
      exp2reg(ex, var.getLocalRegister());  // compute 'ex' into proper place
      return;
    }
    case VUPVAL: {
      int e = exp2anyreg(ex);
      codeABC(OP_SETUPVAL, e, var.getInfo(), 0);
      break;
    }
    case VINDEXUP: {
      codeABRK(OP_SETTABUP, var.getIndexedTableReg(), var.getIndexedKeyIndex(), ex);
      break;
    }
    case VINDEXI: {
      codeABRK(OP_SETI, var.getIndexedTableReg(), var.getIndexedKeyIndex(), ex);
      break;
    }
    case VINDEXSTR: {
      codeABRK(OP_SETFIELD, var.getIndexedTableReg(), var.getIndexedKeyIndex(), ex);
      break;
    }
    case VINDEXED: {
      codeABRK(OP_SETTABLE, var.getIndexedTableReg(), var.getIndexedKeyIndex(), ex);
      break;
    }
    default: moon_assert(0);  // invalid var kind to store
  }
  freeExpression(ex);
}

void FuncState::setreturns(ExpDesc& expr, int resultCount) {
  Instruction *instr = &getinstruction(this, expr);
  moonY_checklimit(this, resultCount + 1, MAXARG_C, "multiple results");
  if (expr.getKind() == VCALL)  // expression is an open function call?
    SETARG_C(*instr, static_cast<unsigned int>(resultCount + 1));
  else {
    moon_assert(expr.getKind() == VVARARG);
    SETARG_C(*instr, static_cast<unsigned int>(resultCount + 1));
    SETARG_A(*instr, static_cast<unsigned int>(getFirstFreeRegister()));
    reserveregs(1);
  }
}

void FuncState::setoneret(ExpDesc& expr) {
  if (expr.getKind() == VCALL) {  // expression is an open function call?
    // already returns 1 value
    moon_assert(InstructionView(getinstruction(this, expr)).c() == 2);
    expr.setKind(VNONRELOC);  // result has fixed position
    expr.setInfo(InstructionView(getinstruction(this, expr)).a());
  }
  else if (expr.getKind() == VVARARG) {
    SETARG_C(getinstruction(this, expr), 2);
    expr.setKind(VRELOC);  // can relocate its simple result
  }
}

int FuncState::jump() {
  return codesJ(OP_JMP, NO_JUMP, 0);
}

void FuncState::ret(int first, int nret) {
  OpCode op;
  switch (nret) {
    case 0: op = OP_RETURN0; break;
    case 1: op = OP_RETURN1; break;
    default: op = OP_RETURN; break;
  }
  moonY_checklimit(this, nret + 1, MAXARG_B, "returns");
  codeABC(op, first, nret + 1, 0);
}

void FuncState::patchlist(int list, int target) {
  moon_assert(target <= getPC());
  patchlistaux(list, target, NO_REG, target);
}

void FuncState::patchtohere(int list) {
  int hr = getlabel();  // mark "here" as a jump target
  patchlist(list, hr);
}

void FuncState::concat(int *l1, int l2) {
  if (l2 == NO_JUMP) return;  // nothing to concatenate?
  else if (*l1 == NO_JUMP)  // no original list?
    *l1 = l2;  /* 'l1' points to 'l2' */
  else {
    int list = *l1;
    int next;
    while ((next = getjump(list)) != NO_JUMP)  // find last element
      list = next;
    fixjump(list, l2);  // last element links to 'l2'
  }
}

int FuncState::getlabel() {
  setLastTarget(getPC());
  return getPC();
}

void FuncState::prefix(UnOpr operation, ExpDesc& expr, int line) {
  ExpDesc fakeOperand;
  fakeOperand.setKind(VKINT);
  fakeOperand.setIntValue(0);
  fakeOperand.setFalseList(NO_JUMP);
  fakeOperand.setTrueList(NO_JUMP);
  dischargevars(expr);
  switch (operation) {
    case UnOpr::OPR_MINUS: case UnOpr::OPR_BNOT:  // use 'fakeOperand' as fake 2nd operand
      if (constfolding(cast_int(operation) + MOON_OPUNM, expr, fakeOperand))
        break;
      /* else */ /* FALLTHROUGH */
    case UnOpr::OPR_LEN:
      codeunexpval(unopr2op(operation), expr, line);
      break;
    case UnOpr::OPR_NOT: codenot(expr); break;
    default: moon_assert(0);
  }
}

void FuncState::infix(BinOpr op, ExpDesc& v) {
  dischargevars(v);
  switch (op) {
    case BinOpr::OPR_AND: {
      goiftrue(v);  // go ahead only if 'v' is true
      break;
    }
    case BinOpr::OPR_OR: {
      goiffalse(v);  // go ahead only if 'v' is false
      break;
    }
    case BinOpr::OPR_CONCAT: {
      exp2nextreg(v);  // operand must be on the stack
      break;
    }
    case BinOpr::OPR_ADD: case BinOpr::OPR_SUB:
    case BinOpr::OPR_MUL: case BinOpr::OPR_DIV: case BinOpr::OPR_IDIV:
    case BinOpr::OPR_MOD: case BinOpr::OPR_POW:
    case BinOpr::OPR_BAND: case BinOpr::OPR_BOR: case BinOpr::OPR_BXOR:
    case BinOpr::OPR_SHL: case BinOpr::OPR_SHR: {
      if (!tonumeral(v, nullptr))
        exp2anyreg(v);
      /* else keep numeral, which may be folded or used as an immediate
         operand */
      break;
    }
    case BinOpr::OPR_EQ: case BinOpr::OPR_NE: {
      if (!tonumeral(v, nullptr))
        exp2RK(v);
      // else keep numeral, which may be an immediate operand
      break;
    }
    case BinOpr::OPR_LT: case BinOpr::OPR_LE:
    case BinOpr::OPR_GT: case BinOpr::OPR_GE: {
      int dummy, dummy2;
      if (!isSCnumber(v, &dummy, &dummy2))
        exp2anyreg(v);
      // else keep numeral, which may be an immediate operand
      break;
    }
    default: moon_assert(0);
  }
}

void FuncState::posfix(BinOpr op, ExpDesc& e1, ExpDesc& e2, int line) {
  dischargevars(e2);
  if (foldbinop(op) && constfolding(cast_int(op) + MOON_OPADD, e1, e2))
    return;  // done by folding
  switch (op) {
    case BinOpr::OPR_AND: {
      moon_assert(e1.getTrueList() == NO_JUMP);  // list closed by 'moonK_infix'
      concat(e2.getFalseListRef(), e1.getFalseList());
      e1 = e2;
      break;
    }
    case BinOpr::OPR_OR: {
      moon_assert(e1.getFalseList() == NO_JUMP);  // list closed by 'moonK_infix'
      concat(e2.getTrueListRef(), e1.getTrueList());
      e1 = e2;
      break;
    }
    case BinOpr::OPR_CONCAT: {  // e1 .. e2
      exp2nextreg(e2);
      codeconcat(e1, e2, line);
      break;
    }
    case BinOpr::OPR_ADD: case BinOpr::OPR_MUL: {
      codecommutative(op, e1, e2, line);
      break;
    }
    case BinOpr::OPR_SUB: {
      if (finishbinexpneg(e1, e2, OP_ADDI, line, TMS::TM_SUB))
        break;  // coded as (r1 + -I)
      // ELSE
    }  // FALLTHROUGH
    case BinOpr::OPR_DIV: case BinOpr::OPR_IDIV: case BinOpr::OPR_MOD: case BinOpr::OPR_POW: {
      codearith(op, e1, e2, 0, line);
      break;
    }
    case BinOpr::OPR_BAND: case BinOpr::OPR_BOR: case BinOpr::OPR_BXOR: {
      codebitwise(op, e1, e2, line);
      break;
    }
    case BinOpr::OPR_SHL: {
      if (isSCint(e1)) {
        swapexps(e1, e2);
        codebini(OP_SHLI, e1, e2, 1, line, TMS::TM_SHL);  // I << r2
      }
      else if (finishbinexpneg(e1, e2, OP_SHRI, line, TMS::TM_SHL)) {
        /* coded as (r1 >> -I) */;
      }
      else  // regular case (two registers)
       codebinexpval(op, e1, e2, line);
      break;
    }
    case BinOpr::OPR_SHR: {
      if (isSCint(e2))
        codebini(OP_SHRI, e1, e2, 0, line, TMS::TM_SHR);  // r1 >> I
      else  // regular case (two registers)
        codebinexpval(op, e1, e2, line);
      break;
    }
    case BinOpr::OPR_EQ: case BinOpr::OPR_NE: {
      codeeq(op, e1, e2);
      break;
    }
    case BinOpr::OPR_GT: case BinOpr::OPR_GE: {
      // '(a > b)' <=> '(b < a)';  '(a >= b)' <=> '(b <= a)'
      swapexps(e1, e2);
      op = static_cast<BinOpr>((cast_int(op) - cast_int(BinOpr::OPR_GT)) + cast_int(BinOpr::OPR_LT));
    }  // FALLTHROUGH
    case BinOpr::OPR_LT: case BinOpr::OPR_LE: {
      codeorder(op, e1, e2);
      break;
    }
    default: moon_assert(0);
  }
}

void FuncState::settablesize(int pcpos, unsigned ra, unsigned asize, unsigned hsize) {
  Instruction *inst = &getProto().getCode()[pcpos];
  int extra = asize / (MAXARG_vC + 1);  // higher bits of array size
  int rc = asize % (MAXARG_vC + 1);  // lower bits of array size
  int k = (extra > 0);  // true iff needs extra argument
  int hsize_coded = (hsize != 0) ? moonO_ceillog2(cast_uint(hsize)) + 1 : 0;
  *inst = CREATE_vABCk(OP_NEWTABLE, static_cast<int>(ra), hsize_coded, rc, k);
  *(inst + 1) = CREATE_Ax(OP_EXTRAARG, extra);
}

void FuncState::setlist(int base, int nelems, int tostore) {
  moon_assert(tostore != 0);
  if (tostore == MOON_MULTRET)
    tostore = 0;
  if (nelems <= MAXARG_vC)
    codevABCk(OP_SETLIST, base, tostore, nelems, 0);
  else {
    int extra = nelems / (MAXARG_vC + 1);
    nelems %= (MAXARG_vC + 1);
    codevABCk(OP_SETLIST, base, tostore, nelems, 1);
    codeextraarg(extra);
  }
  setFirstFreeRegister(cast_byte(base + 1));  // free registers with list values
}

void FuncState::finish() {
  Proto& p = getProto();
  auto codeSpan = p.getCodeSpan();
  for (int i = 0; i < getPC(); i++) {
    Instruction *instr = &codeSpan[i];
    // avoid "not used" warnings when assert is off (for 'onelua.c')
    (void)moonP_isOT; (void)moonP_isIT;
    moon_assert(i == 0 || moonP_isOT(*(instr - 1)) == moonP_isIT(*instr));
    switch (InstructionView(*instr).opcode()) {
      case OP_RETURN0: case OP_RETURN1: {
        if (!(getNeedClose() || (p.getFlag() & PF_ISVARARG)))
          break;  // no extra work
        // else use OP_RETURN to do the extra work
        SET_OPCODE(*instr, OP_RETURN);
      }  // FALLTHROUGH
      case OP_RETURN: case OP_TAILCALL: {
        if (getNeedClose())
          SETARG_k(*instr, 1);  // signal that it needs to close
        if (p.getFlag() & PF_ISVARARG)
          SETARG_C(*instr, static_cast<unsigned int>(p.getNumParams()) + 1);  // signal that it is vararg
        break;
      }
      case OP_JMP: {
        int target = finaltarget(i);
        fixjump(i, target);
        break;
      }
      default: break;
    }
  }
}
