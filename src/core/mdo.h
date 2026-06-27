/*
** Stack and Call structure of Lua
** See Copyright Notice in lua.h
*/

#ifndef ldo_h
#define ldo_h


#include "mlimits.h"
#include "mobject.h"
#include "mstate.h"
#include "mzio.h"


/*
** ============================================================
** STACK CHECKING
** ============================================================
** Inline functions and macros for ensuring sufficient stack space.
** Now delegates to MoonStack::ensureSpace() and ensureSpaceP().
*/

// Ensure stack has space for n elements (replaces old moonD_checkstack)
inline void moonD_checkstack(moon_State* L, int n) noexcept {
	L->getStackSubsystem().ensureSpace(L, n);
}

// Check stack preserving pointer (replaces old checkstackp macro)
#define checkstackp(L,n,p)  \
  (p = L->getStackSubsystem().ensureSpaceP(L, n, p))


/*
** Maximum depth for nested C calls, syntactical nested non-terminals,
** and other features implemented through recursion in C. (Value must
** fit in a 16-bit unsigned integer. It must also be compatible with
** the size of the C stack.)
*/
#if !defined(MOONI_MAXCCALLS)
inline constexpr int MOONI_MAXCCALLS = 200;
#endif


// Removed moonD_reallocstack, moonD_growstack, moonD_shrinkstack, moonD_inctop - now moon_State methods
// Removed all moonD_* functions - now moon_State methods (Pfunc typedef moved to lstate.h)

#endif

