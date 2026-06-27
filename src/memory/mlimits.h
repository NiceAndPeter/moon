/*
** Limits, basic types, and some other 'installation-dependent' definitions
** See Copyright Notice in lua.h
*/

#ifndef llimits_h
#define llimits_h


#include <climits>
#include <cstddef>
#include <cmath>
#include <limits>


#include "moon.h"

/*
** 'l_mem' is a signed integer big enough to count the total memory
** used by Lua.  (It is signed due to the use of debt in several
** computations.) 'lu_mem' is a corresponding unsigned type.  Usually,
** 'ptrdiff_t' should work, but we use 'long' for 16-bit machines.
*/
#if defined(MOONI_MEM)  // { external definitions?
typedef MOONI_MEM l_mem;
typedef MOONI_UMEM lu_mem;
#elif MOONI_IS32INT  // }{
typedef ptrdiff_t l_mem;
typedef size_t lu_mem;
#else  /* 16-bit ints */	/* }{ */
typedef long l_mem;
typedef unsigned long lu_mem;
#endif  // }

// MAX_LMEM defined later in this file after cast functions and l_numbits


// chars used as small naturals (so that 'char' is reserved for characters)
typedef unsigned char lu_byte;
typedef signed char ls_byte;


// Type for thread status/error codes
typedef lu_byte TStatus;

// The C API still uses 'int' for status/error codes - defined after cast_int

// maximum value for size_t
inline constexpr size_t MAX_SIZET = ((size_t)(~(size_t)0));

/*
** Maximum size for strings and userdata visible for Lua; should be
** representable as a moon_Integer and as a size_t.
** Defined later in this file after cast functions.
*/

/* floor of the log2 of the maximum signed value for integral type 't'.
** (That is, maximum 'n' such that '2^n' fits in the given signed type.)
** Defined later in this file after cast functions.
*/


/*
** test whether an unsigned value is a power of 2 (or zero)
*/
template<typename T>
inline constexpr bool ispow2(T x) noexcept {
	return ((x) & ((x) - 1)) == 0;
}


/*
** Safe multiplication helpers - check for overflow before multiplication
** These functions return true if the multiplication would overflow
*/

// Check if multiplication a * b would overflow size_t
inline constexpr bool wouldMultiplyOverflow(size_t a, size_t b) noexcept {
	if (a == 0 || b == 0)
		return false;
	return a > MAX_SIZET / b;
}

// Check if multiplication a * b would overflow for a specific type size
inline constexpr bool wouldSizeMultiplyOverflow(size_t count, size_t elemSize) noexcept {
	return wouldMultiplyOverflow(count, elemSize);
}

// Safe multiplication - returns 0 on overflow (for allocation failure path)
inline constexpr size_t safeMul(size_t a, size_t b) noexcept {
	if (wouldMultiplyOverflow(a, b))
		return 0;
	return a * b;
}


// number of chars of a literal string without the ending \0
template<size_t N>
inline constexpr size_t LL(const char (&)[N]) noexcept {
	return N - 1;
}


/*
** conversion of pointer to unsigned integer: this is for hashing only;
** there is no problem if the integer cannot hold the whole pointer
** value. (In strict ISO C this may cause undefined behavior, but no
** actual machine seems to bother.)
*/
#if !defined(MOON_USE_C89) && defined(__STDC_VERSION__) && \
    __STDC_VERSION__ >= 199901L
#include <cstdint>
#if defined(UINTPTR_MAX)  // even in C99 this type is optional
#define L_P2I	uintptr_t
#else  // no 'intptr'?
#define L_P2I	uintmax_t  // use the largest available integer
#endif
#else  // C89 option
#define L_P2I	size_t
#endif



// types of 'usual argument conversions' for moon_Number and moon_Integer
typedef MOONI_UACNUMBER l_uacNumber;
typedef MOONI_UACINT l_uacInt;


/*
** Internal assertions for in-house debugging
*/
#if defined MOONI_ASSERT
#undef NDEBUG
#include <assert.h>
#define moon_assert(c)           assert(c)
#define assert_code(c)		c
#endif

#if defined(moon_assert)
#else
#define moon_assert(c)		((void)0)
#define assert_code(c)		((void)0)
#endif

#define check_exp(c,e)		(moon_assert(c), (e))
// to avoid problems with conditions too long
#define moon_longassert(c)	assert_code((c) ? (void)0 : moon_assert(0))


// macro to avoid warnings about unused variables
#if !defined(UNUSED)
#define UNUSED(x)	((void)(x))
#endif


/*
** type casts - C++ template versions for type safety
** Note: Macros kept for backward compatibility during transition
*/

/*
** type casts - Following C++ Core Guidelines:
** - Use static_cast for value conversions
** - Use reinterpret_cast for pointer type conversions
** - Avoid C-style casts in C++ code
*/

// Generic cast - Keep C-style cast for backward compatibility
// (Lua code uses cast() for many purposes including const-casting)
#define cast(t, exp)	((t)(exp))

// C++ constexpr inline functions for type-safe numeric conversions
// These replace macros with zero-overhead functions that provide:
// - Better type safety through template parameter deduction
// - Improved compiler diagnostics
// - Debugger-friendly code (can step through functions)
// - constexpr allows compile-time evaluation where possible

// cast_void kept as macro - used in comma expressions and ternary operators
#define cast_void(i)	static_cast<void>(i)

constexpr inline moon_Number cast_num(auto i) noexcept {
    return static_cast<moon_Number>(i);
}

constexpr inline int cast_int(auto i) noexcept {
    return static_cast<int>(i);
}

// The C API uses 'int' for status/error codes
inline int APIstatus(TStatus st) noexcept {
	return cast_int(st);
}

constexpr inline short cast_short(auto i) noexcept {
    return static_cast<short>(i);
}

constexpr inline unsigned int cast_uint(auto i) noexcept {
    return static_cast<unsigned int>(i);
}

constexpr inline lu_byte cast_byte(auto i) noexcept {
    return static_cast<lu_byte>(i);
}

constexpr inline unsigned char cast_uchar(auto i) noexcept {
    return static_cast<unsigned char>(i);
}

constexpr inline char cast_char(auto i) noexcept {
    return static_cast<char>(i);
}

constexpr inline moon_Integer cast_Integer(auto i) noexcept {
    return static_cast<moon_Integer>(i);
}

// cast_sizet kept as macro - used in contexts where result may be passed to
// functions expecting different integer types (avoiding conversion warnings)
#define cast_sizet(i)	cast(size_t, (i))

// cast_Inst kept as macro - Instruction type defined in lobject.h
#define cast_Inst(i)	static_cast<Instruction>(i)

// These need C-style cast for compatibility (used with pointers, etc.)
#define cast_voidp(i)	cast(void*, (i))
#define cast_charp(i)	cast(char*, (i))

template<typename T>
inline constexpr unsigned int point2uint(T* p) noexcept {
	return cast_uint((L_P2I)(p) & std::numeric_limits<unsigned int>::max());
}

template<typename T>
inline constexpr int l_numbits() noexcept {
	return cast_int(sizeof(T) * CHAR_BIT);
}

template<typename T>
inline constexpr int log2maxs() noexcept {
	return l_numbits<T>() - 2;
}

inline constexpr size_t MAX_SIZE = (sizeof(size_t) < sizeof(moon_Integer) ? MAX_SIZET
			  : cast_sizet(MOON_MAXINTEGER));

inline constexpr l_mem MAX_LMEM = cast(l_mem, (cast(lu_mem, 1) << (l_numbits<l_mem>() - 1)) - 1);


// cast a signed moon_Integer to moon_Unsigned
#if !defined(l_castS2U)
inline constexpr moon_Unsigned l_castS2U(moon_Integer i) noexcept {
	return static_cast<moon_Unsigned>(i);
}
#endif

/*
** cast a moon_Unsigned to a signed moon_Integer; this cast is
** not strict ISO C, but two-complement architectures should
** work fine.
*/
#if !defined(l_castU2S)
inline constexpr moon_Integer l_castU2S(moon_Unsigned i) noexcept {
	return static_cast<moon_Integer>(i);
}
#endif

/*
** cast a size_t to moon_Integer: These casts are always valid for
** sizes of Lua objects (see MAX_SIZE)
*/
inline constexpr moon_Integer cast_st2S(size_t sz) noexcept {
	return static_cast<moon_Integer>(sz);
}

/* Cast a ptrdiff_t to size_t, when it is known that the minuend
** comes from the subtrahend (the base)
*/
inline constexpr size_t ct_diff2sz(ptrdiff_t df) noexcept {
	return static_cast<size_t>(df);
}

// ptrdiff_t to moon_Integer
inline constexpr moon_Integer ct_diff2S(ptrdiff_t df) noexcept {
	return cast_st2S(ct_diff2sz(df));
}

/*
** Special type equivalent to '(void*)' for functions (to suppress some
** warnings when converting function pointers)
*/
typedef void (*voidf)(void);

/*
** Macro to convert pointer-to-void* to pointer-to-function. This cast
** is undefined according to ISO C, but POSIX assumes that it works.
** (The '__extension__' in gnu compilers is only to avoid warnings.)
*/
#if defined(__GNUC__)
#define cast_func(p) (__extension__ (voidf)(p))
#else
#define cast_func(p) ((voidf)(p))
#endif



/*
** non-return type
*/
#if !defined(l_noret)

#if defined(__GNUC__)
#define l_noret		void __attribute__((noreturn))
#elif defined(_MSC_VER) && _MSC_VER >= 1200
#define l_noret		void __declspec(noreturn)
#else
#define l_noret		void
#endif

#endif


/*
** Inline functions (l_inline and l_sinline removed - use 'inline' directly in C++)
*/


/*
** An unsigned with (at least) 4 bytes
*/
#if MOONI_IS32INT
typedef unsigned int l_uint32;
#else
typedef unsigned long l_uint32;
#endif


/*
** The mooni_num* operations define the primitive operations over numbers.
** Converted from macros to inline functions for better type safety and debugging.
*/

// float division
#if !defined(mooni_numdiv)
inline moon_Number mooni_numdiv([[maybe_unused]] moon_State *L, moon_Number a, moon_Number b) {
    return a / b;
}
#endif

// floor division (defined as 'floor(a/b)')
#if !defined(mooni_numidiv)
inline moon_Number mooni_numidiv([[maybe_unused]] moon_State *L, moon_Number a, moon_Number b) {
    return l_floor(mooni_numdiv(L, a, b));
}
#endif

/*
** modulo: defined as 'a - floor(a/b)*b'; the direct computation
** using this definition has several problems with rounding errors,
** so it is better to use 'fmod'. 'fmod' gives the result of
** 'a - trunc(a/b)*b', and therefore must be corrected when
** 'trunc(a/b) ~= floor(a/b)'. That happens when the division has a
** non-integer negative result: non-integer result is equivalent to
** a non-zero remainder 'm'; negative result is equivalent to 'a' and
** 'b' with different signs, or 'm' and 'b' with different signs
** (as the result 'm' of 'fmod' has the same sign of 'a').
*/
#if !defined(mooni_nummod)
inline void mooni_nummod([[maybe_unused]] moon_State *L, moon_Number a, moon_Number b, moon_Number &m) {
    m = l_mathop(fmod)(a, b);
    if ((m > 0) ? (b < 0) : (m < 0 && b > 0))
        m += b;
}
#endif

// exponentiation
#if !defined(mooni_numpow)
inline moon_Number mooni_numpow([[maybe_unused]] moon_State *L, moon_Number a, moon_Number b) {
    return (b == 2) ? a * a : l_mathop(pow)(a, b);
}
#endif

// the others are quite standard operations
#if !defined(mooni_numadd)
inline moon_Number mooni_numadd([[maybe_unused]] moon_State *L, moon_Number a, moon_Number b) {
    return a + b;
}

inline moon_Number mooni_numsub([[maybe_unused]] moon_State *L, moon_Number a, moon_Number b) {
    return a - b;
}

inline moon_Number mooni_nummul([[maybe_unused]] moon_State *L, moon_Number a, moon_Number b) {
    return a * b;
}

inline moon_Number mooni_numunm([[maybe_unused]] moon_State *L, moon_Number a) {
    return -a;
}

inline bool mooni_numeq(moon_Number a, moon_Number b) {
    return a == b;
}

inline bool mooni_numlt(moon_Number a, moon_Number b) {
    return a < b;
}

inline bool mooni_numle(moon_Number a, moon_Number b) {
    return a <= b;
}

inline bool mooni_numgt(moon_Number a, moon_Number b) {
    return a > b;
}

inline bool mooni_numge(moon_Number a, moon_Number b) {
    return a >= b;
}

inline bool mooni_numisnan(moon_Number a) {
    return !mooni_numeq(a, a);
}
#endif



/*
** moon_numbertointeger converts a float number with an integral value
** to an integer, or returns false if the float is not within the range of
** a moon_Integer.  (The range comparisons are tricky because of
** rounding. The tests here assume a two-complement representation,
** where MININTEGER always has an exact representation as a float;
** MAXINTEGER may not have one, and therefore its conversion to float
** may have an ill-defined value.)
*/
inline bool moon_numbertointeger(moon_Number n, moon_Integer* p) noexcept {
	if (n >= static_cast<moon_Number>(MOON_MININTEGER) &&
	    n < -static_cast<moon_Number>(MOON_MININTEGER)) {
		*p = static_cast<moon_Integer>(n);
		return true;
	}
	return false;
}



/*
** MOONI_FUNC is a mark for all extern functions that are not to be
** exported to outside modules.
** MOONI_DDEF and MOONI_DDEC are marks for all extern (const) variables,
** none of which to be exported to outside modules (MOONI_DDEF for
** definitions and MOONI_DDEC for declarations).
** Elf/gcc (versions 3.2 and later) mark them as "hidden" to optimize
** access when Lua is compiled as a shared library. Not all elf targets
** support this attribute. Unfortunately, gcc does not offer a way to
** check whether the target offers that support, and those without
** support give a warning about it. To avoid these warnings, change to
** the default definition.
*/
#if !defined(MOONI_FUNC)

#if defined(__GNUC__) && ((__GNUC__*100 + __GNUC_MINOR__) >= 302) && \
    defined(__ELF__)  // {
#define MOONI_FUNC	__attribute__((visibility("internal"))) extern
#else  // }{
#define MOONI_FUNC	extern
#endif  // }

#define MOONI_DDEC(dec)	MOONI_FUNC dec
#define MOONI_DDEF  // empty

#endif


// Give these macros simpler names for internal use
#define l_likely(x)	mooni_likely(x)
#define l_unlikely(x)	mooni_unlikely(x)

/*
** {==================================================================
** "Abstraction Layer" for basic report of messages and errors
** ===================================================================
*/

// print a string
#if !defined(moon_writestring)
#define moon_writestring(s,l)   fwrite((s), sizeof(char), (l), stdout)
#endif

// print a newline and flush the output
#if !defined(moon_writeline)
#define moon_writeline()        (moon_writestring("\n", 1), fflush(stdout))
#endif

// print an error message
#if !defined(moon_writestringerror)
#define moon_writestringerror(s,p) \
        (fprintf(stderr, (s), (p)), fflush(stderr))
#endif

// }==================================================================

#endif

