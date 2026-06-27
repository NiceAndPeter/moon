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
struct moon_State;

inline const TValue* gfasttm(GlobalState* g, const Table* mt, TMS e) noexcept;
inline const TValue* fasttm(moon_State* l, const Table* mt, TMS e) noexcept;

using TypeNamesArray = std::array<const char*, MOON_TOTALTYPES>;
MOONI_DDEC(const TypeNamesArray moonT_typenames_;)

inline const char* ttypename(int x) noexcept {
	return moonT_typenames_[static_cast<size_t>(x + 1)];
}


MOONI_FUNC const char *moonT_objtypename (moon_State *L, const TValue *o);

MOONI_FUNC const TValue *moonT_gettm (const Table *events, TMS event, TString *ename);
MOONI_FUNC const TValue *moonT_gettmbyobj (moon_State *L, const TValue *o,
                                                       TMS event);
MOONI_FUNC void moonT_init (moon_State *L);

MOONI_FUNC void moonT_callTM (moon_State *L, const TValue *f, const TValue *p1,
                            const TValue *p2, const TValue *p3);
MOONI_FUNC MoonT moonT_callTMres (moon_State *L, const TValue *f,
                               const TValue *p1, const TValue *p2, StkId p3);
MOONI_FUNC void moonT_trybinTM (moon_State *L, const TValue *p1, const TValue *p2,
                              StkId res, TMS event);
MOONI_FUNC void moonT_tryconcatTM (moon_State *L);
MOONI_FUNC void moonT_trybinassocTM (moon_State *L, const TValue *p1,
       const TValue *p2, int inv, StkId res, TMS event);
MOONI_FUNC void moonT_trybiniTM (moon_State *L, const TValue *p1, moon_Integer i2,
                               int inv, StkId res, TMS event);
MOONI_FUNC int moonT_callorderTM (moon_State *L, const TValue *p1,
                                const TValue *p2, TMS event);
MOONI_FUNC int moonT_callorderiTM (moon_State *L, const TValue *p1, int v2,
                                 int inv, int isfloat, TMS event);

MOONI_FUNC void moonT_adjustvarargs (moon_State *L, int nfixparams,
                                   struct CallInfo *callInfo, const Proto *p);
MOONI_FUNC void moonT_getvarargs (moon_State *L, struct CallInfo *callInfo,
                                              StkId where, int wanted);


#endif
