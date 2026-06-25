/*
** Opcodes for Lua virtual machine
** See Copyright Notice in lua.h
*/

#define LUA_CORE

#include "lprefix.h"


#include <array>
#include "lopcodes.h"


/* Phase 126.2: Convert opmode macro to inline constexpr function */
inline constexpr int opmode(int mm, int ot, int it, int t, int a, OpMode m) noexcept {
	return ((mm << 7) | (ot << 6) | (it << 5) | (t << 4) | (a << 3) | static_cast<int>(m));
}


/* ORDER OP */

LUAI_DDEF const OpModesArray luaP_opmodes = {
/*       MM OT IT T  A  mode		   opcode  */
  opmode(0, 0, 0, 0, 1, OpMode::iABC)		/* OP_MOVE */
 ,opmode(0, 0, 0, 0, 1, OpMode::iAsBx)		/* OP_LOADI */
 ,opmode(0, 0, 0, 0, 1, OpMode::iAsBx)		/* OP_LOADF */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABx)		/* OP_LOADK */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABx)		/* OP_LOADKX */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABC)		/* OP_LOADFALSE */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABC)		/* OP_LFALSESKIP */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABC)		/* OP_LOADTRUE */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABC)		/* OP_LOADNIL */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABC)		/* OP_GETUPVAL */
 ,opmode(0, 0, 0, 0, 0, OpMode::iABC)		/* OP_SETUPVAL */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABC)		/* OP_GETTABUP */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABC)		/* OP_GETTABLE */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABC)		/* OP_GETI */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABC)		/* OP_GETFIELD */
 ,opmode(0, 0, 0, 0, 0, OpMode::iABC)		/* OP_SETTABUP */
 ,opmode(0, 0, 0, 0, 0, OpMode::iABC)		/* OP_SETTABLE */
 ,opmode(0, 0, 0, 0, 0, OpMode::iABC)		/* OP_SETI */
 ,opmode(0, 0, 0, 0, 0, OpMode::iABC)		/* OP_SETFIELD */
 ,opmode(0, 0, 0, 0, 1, OpMode::ivABC)		/* OP_NEWTABLE */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABC)		/* OP_SELF */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABC)		/* OP_ADDI */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABC)		/* OP_ADDK */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABC)		/* OP_SUBK */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABC)		/* OP_MULK */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABC)		/* OP_MODK */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABC)		/* OP_POWK */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABC)		/* OP_DIVK */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABC)		/* OP_IDIVK */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABC)		/* OP_BANDK */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABC)		/* OP_BORK */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABC)		/* OP_BXORK */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABC)		/* OP_SHLI */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABC)		/* OP_SHRI */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABC)		/* OP_ADD */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABC)		/* OP_SUB */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABC)		/* OP_MUL */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABC)		/* OP_MOD */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABC)		/* OP_POW */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABC)		/* OP_DIV */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABC)		/* OP_IDIV */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABC)		/* OP_BAND */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABC)		/* OP_BOR */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABC)		/* OP_BXOR */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABC)		/* OP_SHL */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABC)		/* OP_SHR */
 ,opmode(1, 0, 0, 0, 0, OpMode::iABC)		/* OP_MMBIN */
 ,opmode(1, 0, 0, 0, 0, OpMode::iABC)		/* OP_MMBINI */
 ,opmode(1, 0, 0, 0, 0, OpMode::iABC)		/* OP_MMBINK */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABC)		/* OP_UNM */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABC)		/* OP_BNOT */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABC)		/* OP_NOT */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABC)		/* OP_LEN */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABC)		/* OP_CONCAT */
 ,opmode(0, 0, 0, 0, 0, OpMode::iABC)		/* OP_CLOSE */
 ,opmode(0, 0, 0, 0, 0, OpMode::iABC)		/* OP_TBC */
 ,opmode(0, 0, 0, 0, 0, OpMode::isJ)		/* OP_JMP */
 ,opmode(0, 0, 0, 1, 0, OpMode::iABC)		/* OP_EQ */
 ,opmode(0, 0, 0, 1, 0, OpMode::iABC)		/* OP_LT */
 ,opmode(0, 0, 0, 1, 0, OpMode::iABC)		/* OP_LE */
 ,opmode(0, 0, 0, 1, 0, OpMode::iABC)		/* OP_EQK */
 ,opmode(0, 0, 0, 1, 0, OpMode::iABC)		/* OP_EQI */
 ,opmode(0, 0, 0, 1, 0, OpMode::iABC)		/* OP_LTI */
 ,opmode(0, 0, 0, 1, 0, OpMode::iABC)		/* OP_LEI */
 ,opmode(0, 0, 0, 1, 0, OpMode::iABC)		/* OP_GTI */
 ,opmode(0, 0, 0, 1, 0, OpMode::iABC)		/* OP_GEI */
 ,opmode(0, 0, 0, 1, 0, OpMode::iABC)		/* OP_TEST */
 ,opmode(0, 0, 0, 1, 1, OpMode::iABC)		/* OP_TESTSET */
 ,opmode(0, 1, 1, 0, 1, OpMode::iABC)		/* OP_CALL */
 ,opmode(0, 1, 1, 0, 1, OpMode::iABC)		/* OP_TAILCALL */
 ,opmode(0, 0, 1, 0, 0, OpMode::iABC)		/* OP_RETURN */
 ,opmode(0, 0, 0, 0, 0, OpMode::iABC)		/* OP_RETURN0 */
 ,opmode(0, 0, 0, 0, 0, OpMode::iABC)		/* OP_RETURN1 */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABx)		/* OP_FORLOOP */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABx)		/* OP_FORPREP */
 ,opmode(0, 0, 0, 0, 0, OpMode::iABx)		/* OP_TFORPREP */
 ,opmode(0, 0, 0, 0, 0, OpMode::iABC)		/* OP_TFORCALL */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABx)		/* OP_TFORLOOP */
 ,opmode(0, 0, 1, 0, 0, OpMode::ivABC)		/* OP_SETLIST */
 ,opmode(0, 0, 0, 0, 1, OpMode::iABx)		/* OP_CLOSURE */
 ,opmode(0, 1, 0, 0, 1, OpMode::iABC)		/* OP_VARARG */
 ,opmode(0, 0, 1, 0, 1, OpMode::iABC)		/* OP_VARARGPREP */
 ,opmode(0, 0, 0, 0, 0, OpMode::iAx)		/* OP_EXTRAARG */
};



/*
** Check whether instruction sets top for next instruction, that is,
** it results in multiple values.
*/
int luaP_isOT (Instruction i) {
  InstructionView view(i);
  OpCode op = static_cast<OpCode>(view.opcode());
  switch (op) {
    case OP_TAILCALL: return 1;
    default:
      return view.testOTMode() && view.c() == 0;
  }
}


/*
** Check whether instruction uses top from previous instruction, that is,
** it accepts multiple results.
*/
int luaP_isIT (Instruction i) {
  InstructionView view(i);
  OpCode op = static_cast<OpCode>(view.opcode());
  switch (op) {
    case OP_SETLIST:
      return view.testITMode() && view.vb() == 0;
    default:
      return view.testITMode() && view.b() == 0;
  }
}

