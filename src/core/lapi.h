/*
** Auxiliary functions from Lua API
** See Copyright Notice in lua.h
*/

#ifndef lapi_h
#define lapi_h


#include "llimits.h"
#include "lstate.h"


#if defined(LUA_USE_APICHECK)
#include <cassert>
#define api_check(l,e,msg)	assert(e)
#else  // for testing
#define api_check(l,e,msg)	((void)(l), lua_assert((e) && msg))
#endif



/*
** ============================================================
** API STACK OPERATIONS
** ============================================================
** Inline functions replacing former macros, now using LuaStack methods.
*/

// Increment top with overflow check (replaces api_incr_top macro)
inline void api_incr_top(lua_State* L) noexcept {
  L->getStackSubsystem().pushChecked(L->getCI()->topRef().p);
}

// Check if stack has at least n elements (replaces api_checknelems macro)
inline void api_checknelems(lua_State* L, int n) noexcept {
  api_check(L, L->getStackSubsystem().checkHasElements(L->getCI(), n),
            "not enough elements in the stack");
}

// Check if n elements can be popped (replaces api_checkpop macro)
inline void api_checkpop(lua_State* L, int n) noexcept {
  api_check(L, L->getStackSubsystem().checkCanPop(L->getCI(), n),
            "not enough free elements in the stack");
}


/*
** Test for a valid index (one that is not the 'nilvalue').
*/
inline bool isvalid(lua_State* L, const TValue* o) noexcept {
    return o != G(L)->getNilValue();
}


// test for pseudo index
inline constexpr bool ispseudo(int i) noexcept {
    return i <= LUA_REGISTRYINDEX;
}

// test for upvalue
inline constexpr bool isupvalue(int i) noexcept {
    return i < LUA_REGISTRYINDEX;
}


/*
** macros that are executed whenever program enters the Lua core
** ('lua_lock') and leaves the core ('lua_unlock')
*/
#if !defined(lua_lock)
#define lua_lock(L)	((void) 0)
#define lua_unlock(L)	((void) 0)
#endif



/*
** If a call returns too many multiple returns, the callee may not have
** stack space to accommodate all results. In this case, this function
** increases its stack space ('L->getCI()->getTop().p').
*/
inline void adjustresults(lua_State* L, int nres) noexcept {
	if (nres <= LUA_MULTRET && L->getCI()->topRef().p < L->getTop().p) {
		L->getCI()->topRef().p = L->getTop().p;
	}
}

#endif
