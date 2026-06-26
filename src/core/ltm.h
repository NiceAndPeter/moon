/*
** Tag methods
** See Copyright Notice in lua.h
*/

#ifndef ltm_h
#define ltm_h


#include <array>
#include "lobject.h"


/*
* WARNING: if you change the order of this enumeration,
* grep "ORDER TM" and "ORDER OP"
*/
enum class TMS {
  TM_INDEX,
  TM_NEWINDEX,
  TM_GC,
  TM_MODE,
  TM_LEN,
  TM_EQ,  // last tag method with fast access
  TM_ADD,
  TM_SUB,
  TM_MUL,
  TM_MOD,
  TM_POW,
  TM_DIV,
  TM_IDIV,
  TM_BAND,
  TM_BOR,
  TM_BXOR,
  TM_SHL,
  TM_SHR,
  TM_UNM,
  TM_BNOT,
  TM_LT,
  TM_LE,
  TM_CONCAT,
  TM_CALL,
  TM_CLOSE,
  TM_N  // number of elements in the enum
};


/*
** Mask with 1 in all fast-access methods. A 1 in any of these bits
** in the flag of a (meta)table means the metatable does not have the
** corresponding metamethod field. (Bit 6 of the flag indicates that
** the table is using the dummy node; bit 7 is used for 'isrealasize'.)
*/
inline constexpr lu_byte maskflags = cast_byte(~(~0u << (static_cast<int>(TMS::TM_EQ) + 1)));

inline void invalidateTMcache(Table* t) noexcept {
  t->clearFlagBits(maskflags);
}

/*
** Test whether there is no tagmethod.
** (Because tagmethods use raw accesses, the result may be an "empty" nil.)
*/
inline bool notm(const TValue* metamethod) noexcept {
	return ttisnil(metamethod);
}

inline bool checknoTM(const Table* mt, TMS e) noexcept {
	return mt == nullptr || (mt->getFlags() & (1u << static_cast<int>(e)));
}

// Forward declarations - definitions provided after full types are available
class GlobalState;
struct lua_State;

inline const TValue* gfasttm(GlobalState* g, const Table* mt, TMS e) noexcept;
inline const TValue* fasttm(lua_State* l, const Table* mt, TMS e) noexcept;

using TypeNamesArray = std::array<const char*, LUA_TOTALTYPES>;
LUAI_DDEC(const TypeNamesArray luaT_typenames_;)

inline const char* ttypename(int x) noexcept {
	return luaT_typenames_[static_cast<size_t>(x + 1)];
}


LUAI_FUNC const char *luaT_objtypename (lua_State *L, const TValue *o);

LUAI_FUNC const TValue *luaT_gettm (const Table *events, TMS event, TString *ename);
LUAI_FUNC const TValue *luaT_gettmbyobj (lua_State *L, const TValue *o,
                                                       TMS event);
LUAI_FUNC void luaT_init (lua_State *L);

LUAI_FUNC void luaT_callTM (lua_State *L, const TValue *f, const TValue *p1,
                            const TValue *p2, const TValue *p3);
LUAI_FUNC LuaT luaT_callTMres (lua_State *L, const TValue *f,
                               const TValue *p1, const TValue *p2, StkId p3);
LUAI_FUNC void luaT_trybinTM (lua_State *L, const TValue *p1, const TValue *p2,
                              StkId res, TMS event);
LUAI_FUNC void luaT_tryconcatTM (lua_State *L);
LUAI_FUNC void luaT_trybinassocTM (lua_State *L, const TValue *p1,
       const TValue *p2, int inv, StkId res, TMS event);
LUAI_FUNC void luaT_trybiniTM (lua_State *L, const TValue *p1, lua_Integer i2,
                               int inv, StkId res, TMS event);
LUAI_FUNC int luaT_callorderTM (lua_State *L, const TValue *p1,
                                const TValue *p2, TMS event);
LUAI_FUNC int luaT_callorderiTM (lua_State *L, const TValue *p1, int v2,
                                 int inv, int isfloat, TMS event);

LUAI_FUNC void luaT_adjustvarargs (lua_State *L, int nfixparams,
                                   struct CallInfo *callInfo, const Proto *p);
LUAI_FUNC void luaT_getvarargs (lua_State *L, struct CallInfo *callInfo,
                                              StkId where, int wanted);


#endif
