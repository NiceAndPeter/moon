/*
** Interface to Memory Manager
** See Copyright Notice in lua.h
*/

#ifndef lmem_h
#define lmem_h


#include <cstddef>

#include "llimits.h"
#include "lua.h"


// Note: moonM_error must remain a macro due to moon_State forward declaration
#define moonM_error(L)	(L)->doThrow(MOON_ERRMEM)

// Forward declarations of underlying memory functions
MOONI_FUNC l_noret moonM_toobig (moon_State *L);
[[nodiscard]] MOONI_FUNC void *moonM_realloc_ (moon_State *L, void *block, size_t oldsize,
                                                          size_t size);
[[nodiscard]] MOONI_FUNC void *moonM_saferealloc_ (moon_State *L, void *block, size_t oldsize,
                                                              size_t size);
MOONI_FUNC void moonM_free_ (moon_State *L, void *block, size_t osize);
[[nodiscard]] MOONI_FUNC void *moonM_growaux_ (moon_State *L, void *block, int nelems,
                               int *size, unsigned size_elem, int limit,
                               const char *what);
[[nodiscard]] MOONI_FUNC void *moonM_shrinkvector_ (moon_State *L, void *block, int *nelem,
                                    int final_n, unsigned size_elem);
[[nodiscard]] MOONI_FUNC void *moonM_malloc_ (moon_State *L, size_t size, int tag);

/*
** This function tests whether it is safe to multiply 'n' by the size of
** type 't' without overflows. Because 'e' is always constant, it avoids
** the runtime division MAX_SIZET/(e).
** (The implementation is somewhat complex to avoid warnings:  The 'sizeof'
** comparison avoids a runtime comparison when overflow cannot occur.
** The compiler should be able to optimize the real test by itself, but
** when it does it, it may give a warning about "comparison is always
** false due to limited range of data type"; the +1 tricks the compiler,
** avoiding this warning but also this optimization.)
*/
template<typename T>
inline constexpr bool moonM_testsize(T n, size_t e) noexcept {
	return sizeof(n) >= sizeof(size_t) && cast_sizet(n) + 1 > MAX_SIZET / e;
}

template<typename T>
inline void moonM_checksize(moon_State* L, T n, size_t e) {
	if (moonM_testsize(n, e))
		moonM_toobig(L);
}


/*
** Computes the minimum between 'n' and 'MAX_SIZET/sizeof(T)', so that
** the result is not larger than 'n' and cannot overflow a 'size_t'
** when multiplied by the size of type 'T'. (Assumes that 'n' is an
** 'int' and that 'int' is not larger than 'size_t'.)
*/
template<typename T>
inline constexpr int moonM_limitN(int n) noexcept {
  return (cast_sizet(n) <= MAX_SIZET/sizeof(T)) ? n :
     cast_int((MAX_SIZET/sizeof(T)));
}


/*
** Arrays of chars do not need any test
*/
[[nodiscard]] inline char* moonM_reallocvchar(moon_State* L, void* b, size_t on, size_t n) {
	return cast_charp(moonM_saferealloc_(L, b, on * sizeof(char), n * sizeof(char)));
}

inline void moonM_freemem(moon_State* L, void* b, size_t s) {
	moonM_free_(L, b, s);
}

[[nodiscard]] inline void* moonM_newobject(moon_State* L, int tag, size_t s) {
	return moonM_malloc_(L, s, tag);
}

/*
** Template-based memory management functions for type safety.
** These replace the old macros with proper C++ templates.
*/

// Free a single object of type T
template<typename T>
inline void moonM_free(moon_State* L, T* b) noexcept {
	moonM_free_(L, static_cast<void*>(b), sizeof(T));
}

// Free an array of n objects of type T
template<typename T>
inline void moonM_freearray(moon_State* L, T* b, size_t n) noexcept {
	moonM_free_(L, static_cast<void*>(b), n * sizeof(T));
}

// Allocate a single object of type T
template<typename T>
[[nodiscard]] inline T* moonM_new(moon_State* L) {
	return static_cast<T*>(moonM_malloc_(L, sizeof(T), 0));
}

// Allocate an array of n objects of type T
template<typename T>
[[nodiscard]] inline T* moonM_newvector(moon_State* L, size_t n) {
	return static_cast<T*>(moonM_malloc_(L, cast_sizet(n) * sizeof(T), 0));
}

// Allocate an array with size check
template<typename T>
[[nodiscard]] inline T* moonM_newvectorchecked(moon_State* L, size_t n) {
	moonM_checksize(L, n, sizeof(T));
	return moonM_newvector<T>(L, n);
}

// Allocate a block of size bytes (char array)
[[nodiscard]] inline char* moonM_newblock(moon_State* L, size_t size) {
	return moonM_newvector<char>(L, size);
}

// Reallocate an array from oldn to n elements
template<typename T>
[[nodiscard]] inline T* moonM_reallocvector(moon_State* L, T* v, size_t oldn, size_t n) {
	return static_cast<T*>(moonM_realloc_(L, v, cast_sizet(oldn) * sizeof(T),
	                                             cast_sizet(n) * sizeof(T)));
}

// Grow a vector, updating size and checking limit
template<typename T>
inline void moonM_growvector(moon_State* L, T*& v, int nelems, int& size, int limit, const char* e) {
	v = static_cast<T*>(moonM_growaux_(L, v, nelems, &size, sizeof(T),
	                                   moonM_limitN<T>(limit), e));
}

// Shrink a vector to final_n elements, updating size
template<typename T>
inline void moonM_shrinkvector(moon_State* L, T*& v, int& size, int final_n) {
	v = static_cast<T*>(moonM_shrinkvector_(L, v, &size, final_n, sizeof(T)));
}

// Note: Function declarations moved above template definitions

#endif

