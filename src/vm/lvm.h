/*
** Lua virtual machine
** See Copyright Notice in lua.h
*/

#ifndef lvm_h
#define lvm_h


#include <cfloat>

#include "ldo.h"
#include "lgc.h"
#include "lobject.h"
#include "ltm.h"
#include "ltable.h"


inline constexpr bool cvt2str(const TValue* o) noexcept {
#if !defined(LUA_NOCVTN2S)
	return ttisnumber(o);
#else
	(void)o;  // suppress unused parameter warning
	return false;  // no conversion from numbers to strings
#endif
}


inline constexpr bool cvt2num(const TValue* o) noexcept {
#if !defined(LUA_NOCVTS2N)
	return ttisstring(o);
#else
	(void)o;  // suppress unused parameter warning
	return false;  // no conversion from strings to numbers
#endif
}


/*
** You can define LUA_FLOORN2I if you want to convert floats to integers
** by flooring them (instead of raising an error if they are not
** integral values)
*/
#if !defined(LUA_FLOORN2I)
#define LUA_FLOORN2I		F2Imod::F2Ieq
#endif


/*
** Rounding modes for float->integer coercion
 */
#ifndef F2Imod_defined
#define F2Imod_defined
enum class F2Imod {
  F2Ieq,  // no rounding; accepts only integral values
  F2Ifloor,  // takes the floor of the number
  F2Iceil  // takes the ceiling of the number
};
#endif


/*
** 'l_intfitsf' checks whether a given integer is in the range that
** can be converted to a float without rounding. Used in comparisons.
*/

// number of bits in the mantissa of a float (kept as macro for preprocessor #if)
#define NBM		(l_floatatt(MANT_DIG))

/*
** Check whether some integers may not fit in a float, testing whether
** (maxinteger >> NBM) > 0. (That implies (1 << NBM) <= maxinteger.)
** (The shifts are done in parts, to avoid shifting by more than the size
** of an integer. In a worst case, NBM == 113 for long double and
** sizeof(long) == 32.)
*/
#if ((((LUA_MAXINTEGER >> (NBM / 4)) >> (NBM / 4)) >> (NBM / 4)) \
	>> (NBM - (3 * (NBM / 4))))  >  0

// limit for integers that fit in a float
inline constexpr lua_Unsigned MAXINTFITSF = (static_cast<lua_Unsigned>(1) << NBM);

// check whether 'i' is in the interval [-MAXINTFITSF, MAXINTFITSF]
inline constexpr bool l_intfitsf(lua_Integer i) noexcept {
	return (MAXINTFITSF + l_castS2U(i)) <= (2 * MAXINTFITSF);
}

#else  // all integers fit in a float precisely

inline constexpr bool l_intfitsf(lua_Integer i) noexcept {
	(void)i;  // suppress unused parameter warning
	return true;
}

#endif


// convert an object to a float (including string coercion)
inline bool tonumber(const TValue* o, lua_Number* n) noexcept {
	if (ttisfloat(o)) {
		*n = fltvalue(o);
		return true;
	}
	return o->toNumber(n);  // use TValue method
}


// convert an object to a float (without string coercion)
inline bool tonumberns(const TValue* o, lua_Number& n) noexcept {
	if (ttisfloat(o)) {
		n = fltvalue(o);
		return true;
	}
	if (ttisinteger(o)) {
		n = cast_num(ivalue(o));
		return true;
	}
	return false;
}


// convert an object to an integer (including string coercion)
inline bool tointeger(const TValue* o, lua_Integer* i) noexcept {
	if (l_likely(ttisinteger(o))) {
		*i = ivalue(o);
		return true;
	}
	return o->toInteger(i, LUA_FLOORN2I);  // use TValue method
}


// convert an object to an integer (without string coercion)
inline bool tointegerns(const TValue* o, lua_Integer* i) noexcept {
	if (l_likely(ttisinteger(o))) {
		*i = ivalue(o);
		return true;
	}
	return o->toIntegerNoString(i, LUA_FLOORN2I);  // use TValue method
}


/* Note: intop cannot be a function template because 'op' is an operator, not a value.
   This must remain a macro to support operator token pasting. */
#define intop(op,v1,v2) l_castU2S(l_castS2U(v1) op l_castS2U(v2))


// All luaV_* wrapper functions removed - use VirtualMachine methods directly

#endif
