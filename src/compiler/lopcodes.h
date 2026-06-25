/*
** Opcodes for Lua virtual machine
** See Copyright Notice in lua.h
*/

#ifndef lopcodes_h
#define lopcodes_h

#include <array>
#include "llimits.h"
#include "lobject.h"


/*===========================================================================
  We assume that instructions are unsigned 32-bit integers.
  All instructions have an opcode in the first 7 bits.
  Instructions can have the following formats:

        3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0
        1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
iABC          C(8)     |      B(8)     |k|     A(8)      |   Op(7)     |
ivABC         vC(10)     |     vB(6)   |k|     A(8)      |   Op(7)     |
iABx                Bx(17)               |     A(8)      |   Op(7)     |
iAsBx              sBx (signed)(17)      |     A(8)      |   Op(7)     |
iAx                           Ax(25)                     |   Op(7)     |
isJ                           sJ (signed)(25)            |   Op(7)     |

  ('v' stands for "variant", 's' for "signed", 'x' for "extended".)
  A signed argument is represented in excess K: The represented value is
  the written unsigned value minus K, where K is half (rounded down) the
  maximum value for the corresponding unsigned argument.
===========================================================================*/


/* basic instruction formats */
enum class OpMode {iABC, ivABC, iABx, iAsBx, iAx, isJ};


/*
** size and position of opcode arguments.
** (SIZE_* must remain macros for use in preprocessor conditionals)
*/
#define SIZE_C		8
#define SIZE_vC		10
#define SIZE_B		8
#define SIZE_vB		6
#define SIZE_Bx		(SIZE_C + SIZE_B + 1)
#define SIZE_A		8
#define SIZE_Ax		(SIZE_Bx + SIZE_A)
#define SIZE_sJ		(SIZE_Bx + SIZE_A)

#define SIZE_OP		7

#define POS_OP		0

/* Position constants can be constexpr */
inline constexpr int POS_A = (POS_OP + SIZE_OP);
inline constexpr int POS_k = (POS_A + SIZE_A);
inline constexpr int POS_B = (POS_k + 1);
inline constexpr int POS_vB = (POS_k + 1);
inline constexpr int POS_C = (POS_B + SIZE_B);
inline constexpr int POS_vC = (POS_vB + SIZE_vB);

inline constexpr int POS_Bx = POS_k;

inline constexpr int POS_Ax = POS_A;

inline constexpr int POS_sJ = POS_A;


/*
** limits for opcode arguments.
** we use (signed) 'int' to manipulate most arguments,
** so they must fit in ints.
*/

/*
** Check whether type 'int' has at least 'b' + 1 bits.
** 'b' < 32; +1 for the sign bit.
*/
#define L_INTHASBITS(b)		((UINT_MAX >> (b)) >= 1)


#if L_INTHASBITS(SIZE_Bx)
inline constexpr int MAXARG_Bx = ((1<<SIZE_Bx)-1);
#else
inline constexpr int MAXARG_Bx = std::numeric_limits<int>::max();
#endif

inline constexpr int OFFSET_sBx = (MAXARG_Bx>>1);         /* 'sBx' is signed */


/* MAXARG_Ax must remain macro (used in preprocessor conditionals) */
#if L_INTHASBITS(SIZE_Ax)
#define MAXARG_Ax	((1<<SIZE_Ax)-1)
#else
#define MAXARG_Ax	INT_MAX
#endif

#if L_INTHASBITS(SIZE_sJ)
inline constexpr int MAXARG_sJ = ((1 << SIZE_sJ) - 1);
#else
inline constexpr int MAXARG_sJ = std::numeric_limits<int>::max();
#endif

inline constexpr int OFFSET_sJ = (MAXARG_sJ >> 1);


inline constexpr int MAXARG_A = ((1<<SIZE_A)-1);
inline constexpr int MAXARG_B = ((1<<SIZE_B)-1);
inline constexpr int MAXARG_vB = ((1<<SIZE_vB)-1);
inline constexpr int MAXARG_C = ((1<<SIZE_C)-1);
/* MAXARG_vC must remain macro (used in preprocessor conditionals) */
#define MAXARG_vC	((1<<SIZE_vC)-1)
inline constexpr int OFFSET_sC = (MAXARG_C >> 1);

inline constexpr int int2sC(int i) noexcept {
	return i + OFFSET_sC;
}

inline constexpr int sC2int(int i) noexcept {
	return i - OFFSET_sC;
}


/* creates a mask with 'n' 1 bits at position 'p' */
inline constexpr Instruction MASK1(int n, int p) noexcept {
	return (~((~(Instruction)0) << n)) << p;
}

/* creates a mask with 'n' 0 bits at position 'p' */
inline constexpr Instruction MASK0(int n, int p) noexcept {
	return ~MASK1(n, p);
}

/*
** the following macros help to manipulate instructions
*/

inline constexpr int GET_OPCODE(Instruction i) noexcept {
	return cast_int((i >> POS_OP) & MASK1(SIZE_OP, 0));
}
inline void SET_OPCODE(Instruction& i, int o) noexcept {
	i = ((i & MASK0(SIZE_OP, POS_OP)) | ((cast_Inst(o) << POS_OP) & MASK1(SIZE_OP, POS_OP)));
}

/* Forward declaration for getOpMode (defined later after OpCode enum) */
inline OpMode getOpMode(int m) noexcept;

inline constexpr bool checkopm(Instruction i, OpMode m) noexcept {
	return getOpMode(GET_OPCODE(i)) == m;
}


/* Core helper functions for instruction field manipulation */
inline constexpr int getarg(Instruction i, int pos, int size) noexcept {
	return cast_int((i >> pos) & MASK1(size, 0));
}

inline void setarg(Instruction& i, unsigned int v, int pos, int size) noexcept {
	i = ((i & MASK0(size, pos)) | ((cast_Inst(v) << pos) & MASK1(size, pos)));
}

/* SETARG functions for instruction creation/modification */
inline void SETARG_A(Instruction& i, unsigned int v) noexcept {
	setarg(i, v, POS_A, SIZE_A);
}

inline void SETARG_B(Instruction& i, unsigned int v) noexcept {
	setarg(i, v, POS_B, SIZE_B);
}

inline void SETARG_vB(Instruction& i, unsigned int v) noexcept {
	setarg(i, v, POS_vB, SIZE_vB);
}

inline void SETARG_C(Instruction& i, unsigned int v) noexcept {
	setarg(i, v, POS_C, SIZE_C);
}

inline void SETARG_vC(Instruction& i, unsigned int v) noexcept {
	setarg(i, v, POS_vC, SIZE_vC);
}

inline void SETARG_k(Instruction& i, unsigned int v) noexcept {
	setarg(i, v, POS_k, 1);
}

inline void SETARG_Bx(Instruction& i, unsigned int v) noexcept {
	setarg(i, v, POS_Bx, SIZE_Bx);
}

inline void SETARG_Ax(Instruction& i, unsigned int v) noexcept {
	setarg(i, v, POS_Ax, SIZE_Ax);
}

inline void SETARG_sBx(Instruction& i, int b) noexcept {
	SETARG_Bx(i, cast_uint(b + OFFSET_sBx));
}

inline void SETARG_sJ(Instruction& i, int j) noexcept {
	setarg(i, cast_uint(j + OFFSET_sJ), POS_sJ, SIZE_sJ);
}


/*
** InstructionView class - Modern C++ interface for instruction field access
** Provides clean, object-oriented access to instruction fields
** Zero-cost: all methods are inline constexpr
*/
class InstructionView {
private:
	Instruction inst_;

public:
	/* Constructor from raw instruction */
	constexpr InstructionView(Instruction i) noexcept : inst_(i) {}

	/* Get raw instruction value */
	constexpr Instruction raw() const noexcept { return inst_; }

	/* Opcode accessor */
	constexpr int opcode() const noexcept {
		return cast_int((inst_ >> POS_OP) & MASK1(SIZE_OP, 0));
	}

	/* Field A accessor (8 bits) */
	constexpr int a() const noexcept {
		return getarg(inst_, POS_A, SIZE_A);
	}

	/* Field B accessor (8 bits, iABC mode) */
	constexpr int b() const noexcept {
		return getarg(inst_, POS_B, SIZE_B);
	}

	/* Field vB accessor (6 bits, ivABC mode) */
	constexpr int vb() const noexcept {
		return getarg(inst_, POS_vB, SIZE_vB);
	}

	/* Signed field B accessor */
	constexpr int sb() const noexcept {
		return sC2int(b());
	}

	/* Field C accessor (8 bits, iABC mode) */
	constexpr int c() const noexcept {
		return getarg(inst_, POS_C, SIZE_C);
	}

	/* Field vC accessor (10 bits, ivABC mode) */
	constexpr int vc() const noexcept {
		return getarg(inst_, POS_vC, SIZE_vC);
	}

	/* Signed field C accessor */
	constexpr int sc() const noexcept {
		return sC2int(c());
	}

	/* Field k accessor (1 bit) */
	constexpr int k() const noexcept {
		return getarg(inst_, POS_k, 1);
	}

	/* Test field k (non-zero if set) */
	constexpr int testk() const noexcept {
		return cast_int(inst_ & (1u << POS_k));
	}

	/* Field Bx accessor (17 bits) */
	constexpr int bx() const noexcept {
		return getarg(inst_, POS_Bx, SIZE_Bx);
	}

	/* Signed field Bx accessor */
	constexpr int sbx() const noexcept {
		return getarg(inst_, POS_Bx, SIZE_Bx) - OFFSET_sBx;
	}

	/* Field Ax accessor (25 bits) */
	constexpr int ax() const noexcept {
		return getarg(inst_, POS_Ax, SIZE_Ax);
	}

	/* Signed field sJ accessor (25 bits) */
	constexpr int sj() const noexcept {
		return getarg(inst_, POS_sJ, SIZE_sJ) - OFFSET_sJ;
	}

	/* Instruction property accessors - encapsulate luaP_opmodes array access */
	/* Defined below after luaP_opmodes declaration */
	inline OpMode getOpMode() const noexcept;
	inline bool testAMode() const noexcept;
	inline bool testTMode() const noexcept;
	inline bool testITMode() const noexcept;
	inline bool testOTMode() const noexcept;
	inline bool testMMMode() const noexcept;
};


inline constexpr Instruction CREATE_ABCk(int o, int a, int b, int c, int k) noexcept {
	return (cast_Inst(o) << POS_OP)
		| (cast_Inst(a) << POS_A)
		| (cast_Inst(b) << POS_B)
		| (cast_Inst(c) << POS_C)
		| (cast_Inst(k) << POS_k);
}

inline constexpr Instruction CREATE_vABCk(int o, int a, int b, int c, int k) noexcept {
	return (cast_Inst(o) << POS_OP)
		| (cast_Inst(a) << POS_A)
		| (cast_Inst(b) << POS_vB)
		| (cast_Inst(c) << POS_vC)
		| (cast_Inst(k) << POS_k);
}

inline constexpr Instruction CREATE_ABx(int o, int a, int bc) noexcept {
	return (cast_Inst(o) << POS_OP)
		| (cast_Inst(a) << POS_A)
		| (cast_Inst(bc) << POS_Bx);
}

inline constexpr Instruction CREATE_Ax(int o, int a) noexcept {
	return (cast_Inst(o) << POS_OP)
		| (cast_Inst(a) << POS_Ax);
}

inline constexpr Instruction CREATE_sJ(int o, int j, int k) noexcept {
	return (cast_Inst(o) << POS_OP)
		| (cast_Inst(j) << POS_sJ)
		| (cast_Inst(k) << POS_k);
}


#if !defined(MAXINDEXRK)  /* (for debugging only) */
inline constexpr int MAXINDEXRK = MAXARG_B;
#endif


/*
** Maximum size for the stack of a Lua function. It must fit in 8 bits.
** The highest valid register is one less than this value.
*/
inline constexpr int MAX_FSTACK = MAXARG_A;

/*
** Invalid register (one more than last valid register).
*/
inline constexpr int NO_REG = MAX_FSTACK;



/*
** R[x] - register
** K[x] - constant (in constant table)
** RK(x) == if k(i) then K[x] else R[x]
*/


/*
** Grep "ORDER OP" if you change these enums. Opcodes marked with a (*)
** has extra descriptions in the notes after the enumeration.
*/

typedef enum {
/*----------------------------------------------------------------------
  name		args	description
------------------------------------------------------------------------*/
OP_MOVE,/*	A B	R[A] := R[B]					*/
OP_LOADI,/*	A sBx	R[A] := sBx					*/
OP_LOADF,/*	A sBx	R[A] := (lua_Number)sBx				*/
OP_LOADK,/*	A Bx	R[A] := K[Bx]					*/
OP_LOADKX,/*	A	R[A] := K[extra arg]				*/
OP_LOADFALSE,/*	A	R[A] := false					*/
OP_LFALSESKIP,/*A	R[A] := false; pc++	(*)			*/
OP_LOADTRUE,/*	A	R[A] := true					*/
OP_LOADNIL,/*	A B	R[A], R[A+1], ..., R[A+B] := nil		*/
OP_GETUPVAL,/*	A B	R[A] := UpValue[B]				*/
OP_SETUPVAL,/*	A B	UpValue[B] := R[A]				*/

OP_GETTABUP,/*	A B C	R[A] := UpValue[B][K[C]:shortstring]		*/
OP_GETTABLE,/*	A B C	R[A] := R[B][R[C]]				*/
OP_GETI,/*	A B C	R[A] := R[B][C]					*/
OP_GETFIELD,/*	A B C	R[A] := R[B][K[C]:shortstring]			*/

OP_SETTABUP,/*	A B C	UpValue[A][K[B]:shortstring] := RK(C)		*/
OP_SETTABLE,/*	A B C	R[A][R[B]] := RK(C)				*/
OP_SETI,/*	A B C	R[A][B] := RK(C)				*/
OP_SETFIELD,/*	A B C	R[A][K[B]:shortstring] := RK(C)			*/

OP_NEWTABLE,/*	A vB vC k	R[A] := {}				*/

OP_SELF,/*	A B C	R[A+1] := R[B]; R[A] := R[B][K[C]:shortstring]	*/

OP_ADDI,/*	A B sC	R[A] := R[B] + sC				*/

OP_ADDK,/*	A B C	R[A] := R[B] + K[C]:number			*/
OP_SUBK,/*	A B C	R[A] := R[B] - K[C]:number			*/
OP_MULK,/*	A B C	R[A] := R[B] * K[C]:number			*/
OP_MODK,/*	A B C	R[A] := R[B] % K[C]:number			*/
OP_POWK,/*	A B C	R[A] := R[B] ^ K[C]:number			*/
OP_DIVK,/*	A B C	R[A] := R[B] / K[C]:number			*/
OP_IDIVK,/*	A B C	R[A] := R[B] // K[C]:number			*/

OP_BANDK,/*	A B C	R[A] := R[B] & K[C]:integer			*/
OP_BORK,/*	A B C	R[A] := R[B] | K[C]:integer			*/
OP_BXORK,/*	A B C	R[A] := R[B] ~ K[C]:integer			*/

OP_SHLI,/*	A B sC	R[A] := sC << R[B]				*/
OP_SHRI,/*	A B sC	R[A] := R[B] >> sC				*/

OP_ADD,/*	A B C	R[A] := R[B] + R[C]				*/
OP_SUB,/*	A B C	R[A] := R[B] - R[C]				*/
OP_MUL,/*	A B C	R[A] := R[B] * R[C]				*/
OP_MOD,/*	A B C	R[A] := R[B] % R[C]				*/
OP_POW,/*	A B C	R[A] := R[B] ^ R[C]				*/
OP_DIV,/*	A B C	R[A] := R[B] / R[C]				*/
OP_IDIV,/*	A B C	R[A] := R[B] // R[C]				*/

OP_BAND,/*	A B C	R[A] := R[B] & R[C]				*/
OP_BOR,/*	A B C	R[A] := R[B] | R[C]				*/
OP_BXOR,/*	A B C	R[A] := R[B] ~ R[C]				*/
OP_SHL,/*	A B C	R[A] := R[B] << R[C]				*/
OP_SHR,/*	A B C	R[A] := R[B] >> R[C]				*/

OP_MMBIN,/*	A B C	call C metamethod over R[A] and R[B]	(*)	*/
OP_MMBINI,/*	A sB C k	call C metamethod over R[A] and sB	*/
OP_MMBINK,/*	A B C k		call C metamethod over R[A] and K[B]	*/

OP_UNM,/*	A B	R[A] := -R[B]					*/
OP_BNOT,/*	A B	R[A] := ~R[B]					*/
OP_NOT,/*	A B	R[A] := not R[B]				*/
OP_LEN,/*	A B	R[A] := #R[B] (length operator)			*/

OP_CONCAT,/*	A B	R[A] := R[A].. ... ..R[A + B - 1]		*/

OP_CLOSE,/*	A	close all upvalues >= R[A]			*/
OP_TBC,/*	A	mark variable A "to be closed"			*/
OP_JMP,/*	sJ	pc += sJ					*/
OP_EQ,/*	A B k	if ((R[A] == R[B]) ~= k) then pc++		*/
OP_LT,/*	A B k	if ((R[A] <  R[B]) ~= k) then pc++		*/
OP_LE,/*	A B k	if ((R[A] <= R[B]) ~= k) then pc++		*/

OP_EQK,/*	A B k	if ((R[A] == K[B]) ~= k) then pc++		*/
OP_EQI,/*	A sB k	if ((R[A] == sB) ~= k) then pc++		*/
OP_LTI,/*	A sB k	if ((R[A] < sB) ~= k) then pc++			*/
OP_LEI,/*	A sB k	if ((R[A] <= sB) ~= k) then pc++		*/
OP_GTI,/*	A sB k	if ((R[A] > sB) ~= k) then pc++			*/
OP_GEI,/*	A sB k	if ((R[A] >= sB) ~= k) then pc++		*/

OP_TEST,/*	A k	if (not R[A] == k) then pc++			*/
OP_TESTSET,/*	A B k	if (not R[B] == k) then pc++ else R[A] := R[B] (*) */

OP_CALL,/*	A B C	R[A], ... ,R[A+C-2] := R[A](R[A+1], ... ,R[A+B-1]) */
OP_TAILCALL,/*	A B C k	return R[A](R[A+1], ... ,R[A+B-1])		*/

OP_RETURN,/*	A B C k	return R[A], ... ,R[A+B-2]	(see note)	*/
OP_RETURN0,/*		return						*/
OP_RETURN1,/*	A	return R[A]					*/

OP_FORLOOP,/*	A Bx	update counters; if loop continues then pc-=Bx; */
OP_FORPREP,/*	A Bx	<check values and prepare counters>;
                        if not to run then pc+=Bx+1;			*/

OP_TFORPREP,/*	A Bx	create upvalue for R[A + 3]; pc+=Bx		*/
OP_TFORCALL,/*	A C	R[A+4], ... ,R[A+3+C] := R[A](R[A+1], R[A+2]);	*/
OP_TFORLOOP,/*	A Bx	if R[A+2] ~= nil then { R[A]=R[A+2]; pc -= Bx }	*/

OP_SETLIST,/*	A vB vC k	R[A][vC+i] := R[A+i], 1 <= i <= vB	*/

OP_CLOSURE,/*	A Bx	R[A] := closure(KPROTO[Bx])			*/

OP_VARARG,/*	A C	R[A], R[A+1], ..., R[A+C-2] = vararg		*/

OP_VARARGPREP,/*A	(adjust vararg parameters)			*/

OP_EXTRAARG/*	Ax	extra (larger) argument for previous opcode	*/
} OpCode;


inline constexpr int NUM_OPCODES = ((int)(OP_EXTRAARG) + 1);



/*===========================================================================
  Notes:

  (*) Opcode OP_LFALSESKIP is used to convert a condition to a boolean
  value, in a code equivalent to (not cond ? false : true).  (It
  produces false and skips the next instruction producing true.)

  (*) Opcodes OP_MMBIN and variants follow each arithmetic and
  bitwise opcode. If the operation succeeds, it skips this next
  opcode. Otherwise, this opcode calls the corresponding metamethod.

  (*) Opcode OP_TESTSET is used in short-circuit expressions that need
  both to jump and to produce a value, such as (a = b or c).

  (*) In OP_CALL, if (B == 0) then B = top - A. If (C == 0), then
  'top' is set to last_result+1, so next open instruction (OP_CALL,
  OP_RETURN*, OP_SETLIST) may use 'top'.

  (*) In OP_VARARG, if (C == 0) then use actual number of varargs and
  set top (like in OP_CALL with C == 0).

  (*) In OP_RETURN, if (B == 0) then return up to 'top'.

  (*) In OP_LOADKX and OP_NEWTABLE, the next instruction is always
  OP_EXTRAARG.

  (*) In OP_SETLIST, if (B == 0) then real B = 'top'; if k, then
  real C = EXTRAARG _ C (the bits of EXTRAARG concatenated with the
  bits of C).

  (*) In OP_NEWTABLE, vB is log2 of the hash size (which is always a
  power of 2) plus 1, or zero for size zero. If not k, the array size
  is vC. Otherwise, the array size is EXTRAARG _ vC.

  (*) For comparisons, k specifies what condition the test should accept
  (true or false).

  (*) In OP_MMBINI/OP_MMBINK, k means the arguments were flipped
   (the constant is the first operand).

  (*) All 'skips' (pc++) assume that next instruction is a jump.

  (*) In instructions OP_RETURN/OP_TAILCALL, 'k' specifies that the
  function builds upvalues, which may need to be closed. C > 0 means
  the function is vararg, so that its 'func' must be corrected before
  returning; in this case, (C - 1) is its number of fixed parameters.

  (*) In comparisons with an immediate operand, C signals whether the
  original operand was a float. (It must be corrected in case of
  metamethods.)

===========================================================================*/


/*
** masks for instruction properties. The format is:
** bits 0-2: op mode
** bit 3: instruction set register A
** bit 4: operator is a test (next instruction must be a jump)
** bit 5: instruction uses 'L->getTop()' set by previous instruction (when B == 0)
** bit 6: instruction sets 'L->getTop()' for next instruction (when C == 0)
** bit 7: instruction is an MM instruction (call a metamethod)
*/

using OpModesArray = std::array<lu_byte, NUM_OPCODES>;
LUAI_DDEC(const OpModesArray luaP_opmodes;)

inline OpMode getOpMode(int m) noexcept {
	return static_cast<OpMode>(luaP_opmodes[m] & 7);
}

inline bool testAMode(int m) noexcept {
	return (luaP_opmodes[m] & (1 << 3)) != 0;
}

inline bool testTMode(int m) noexcept {
	return (luaP_opmodes[m] & (1 << 4)) != 0;
}

inline bool testITMode(int m) noexcept {
	return (luaP_opmodes[m] & (1 << 5)) != 0;
}

inline bool testOTMode(int m) noexcept {
	return (luaP_opmodes[m] & (1 << 6)) != 0;
}

inline bool testMMMode(int m) noexcept {
	return (luaP_opmodes[m] & (1 << 7)) != 0;
}

/* InstructionView property method implementations (defined after luaP_opmodes) */
inline OpMode InstructionView::getOpMode() const noexcept {
	return static_cast<OpMode>(luaP_opmodes[opcode()] & 7);
}

inline bool InstructionView::testAMode() const noexcept {
	return (luaP_opmodes[opcode()] & (1 << 3)) != 0;
}

inline bool InstructionView::testTMode() const noexcept {
	return (luaP_opmodes[opcode()] & (1 << 4)) != 0;
}

inline bool InstructionView::testITMode() const noexcept {
	return (luaP_opmodes[opcode()] & (1 << 5)) != 0;
}

inline bool InstructionView::testOTMode() const noexcept {
	return (luaP_opmodes[opcode()] & (1 << 6)) != 0;
}

inline bool InstructionView::testMMMode() const noexcept {
	return (luaP_opmodes[opcode()] & (1 << 7)) != 0;
}


LUAI_FUNC int luaP_isOT (Instruction i);
LUAI_FUNC int luaP_isIT (Instruction i);


#endif
