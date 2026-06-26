/*
** Stack and Call structure of Lua
** See Copyright Notice in lua.h
*/

#ifndef ldo_h
#define ldo_h


#include "llimits.h"
#include "lobject.h"
#include "lstate.h"
#include "lzio.h"


/*
** ============================================================
** STACK CHECKING
** ============================================================
** Inline functions and macros for ensuring sufficient stack space.
** Now delegates to LuaStack::ensureSpace() and ensureSpaceP().
*/

/* Ensure stack has space for n elements (replaces old luaD_checkstack) */
inline void luaD_checkstack(lua_State* L, int n) noexcept {
	L->getStackSubsystem().ensureSpace(L, n);
}

/* Check stack preserving pointer (replaces old checkstackp macro) */
#define checkstackp(L,n,p)  \
  (p = L->getStackSubsystem().ensureSpaceP(L, n, p))


/*
** Maximum depth for nested C calls, syntactical nested non-terminals,
** and other features implemented through recursion in C. (Value must
** fit in a 16-bit unsigned integer. It must also be compatible with
** the size of the C stack.)
*/
#if !defined(LUAI_MAXCCALLS)
inline constexpr int LUAI_MAXCCALLS = 200;
#endif


/* Removed luaD_reallocstack, luaD_growstack, luaD_shrinkstack, luaD_inctop - now lua_State methods */
/* Removed all luaD_* functions - now lua_State methods (Pfunc typedef moved to lstate.h) */

#endif

