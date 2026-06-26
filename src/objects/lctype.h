/*
** 'ctype' functions for Lua
** See Copyright Notice in lua.h
*/

#ifndef lctype_h
#define lctype_h

#include "lua.h"


/*
** WARNING: the functions defined here do not necessarily correspond
** to the similar functions in the standard C ctype.h. They are
** optimized for the specific needs of Lua.
*/

#if !defined(LUA_USE_CTYPE)

#if 'A' == 65 && '0' == 48
// ASCII case: can use its own tables; faster and fixed
#define LUA_USE_CTYPE	0
#else
// must use standard C ctype
#define LUA_USE_CTYPE	1
#endif

#endif


#if !LUA_USE_CTYPE  // {

#include <climits>

#include "llimits.h"


inline constexpr int ALPHABIT = 0;
inline constexpr int DIGITBIT = 1;
inline constexpr int PRINTBIT = 2;
inline constexpr int SPACEBIT = 3;
inline constexpr int XDIGITBIT = 4;

// one entry for each character and for -1 (EOZ)
LUAI_DDEC(const lu_byte luai_ctype_[UCHAR_MAX + 2];)

inline constexpr int MASK(int B) noexcept {
	return (1 << B);
}

/*
** add 1 to char to allow index -1 (EOZ)
*/
inline bool testprop(int c, int p) noexcept {
	return (luai_ctype_[(c)+1] & (p)) != 0;
}

/*
** 'lalpha' (Lua alphabetic) and 'lalnum' (Lua alphanumeric) both include '_'
*/
inline bool lislalpha(int c) noexcept {
	return testprop(c, MASK(ALPHABIT));
}

inline bool lislalnum(int c) noexcept {
	return testprop(c, (MASK(ALPHABIT) | MASK(DIGITBIT)));
}

inline bool lisdigit(int c) noexcept {
	return testprop(c, MASK(DIGITBIT));
}

inline bool lisspace(int c) noexcept {
	return testprop(c, MASK(SPACEBIT));
}

inline bool lisprint(int c) noexcept {
	return testprop(c, MASK(PRINTBIT));
}

inline bool lisxdigit(int c) noexcept {
	return testprop(c, MASK(XDIGITBIT));
}

/*
** In ASCII, this 'ltolower' is correct for alphabetic characters and
** for '.'. That is enough for Lua needs. ('check_exp' ensures that
** the character either is an upper-case letter or is unchanged by
** the transformation, which holds for lower-case letters and '.'.)
*/
inline int ltolower(int c) noexcept {
	return check_exp(('A' <= (c) && (c) <= 'Z') || (c) == ((c) | ('A' ^ 'a')),
	                 (c) | ('A' ^ 'a'));
}


#else  // }{

/*
** use standard C ctypes
*/

#include <ctype.h>

inline bool lislalpha(int c) noexcept {
	return isalpha(c) || (c) == '_';
}

inline bool lislalnum(int c) noexcept {
	return isalnum(c) || (c) == '_';
}

inline bool lisdigit(int c) noexcept {
	return isdigit(c) != 0;
}

inline bool lisspace(int c) noexcept {
	return isspace(c) != 0;
}

inline bool lisprint(int c) noexcept {
	return isprint(c) != 0;
}

inline bool lisxdigit(int c) noexcept {
	return isxdigit(c) != 0;
}

inline int ltolower(int c) noexcept {
	return tolower(c);
}

#endif  // }

#endif

