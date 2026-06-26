/*
** Type definitions for Lua objects
** See Copyright Notice in lua.h
*/


#ifndef lobject_h
#define lobject_h


#include <cstdarg>
#include <span>


#include "llimits.h"
#include "lua.h"
#include "ltvalue.h"  /* TValue class */

/* Include focused headers */
#include "lobject_core.h"  /* GCObject, GCBase<T>, Udata */
#include "lstring.h"       /* TString */
#include "lproto.h"        /* Proto */
#include "lfunc.h"         /* UpVal, CClosure, LClosure */
#include "ltable.h"        /* Table, Node - after lstring/lproto/lfunc for dependencies */

/* Include ltm.h for tag method support needed by ltable.h inline functions */
#include "../core/ltm.h"   /* TMS enum, checknoTM function */

/* Forward declarations */
enum class GCAge : lu_byte;


/*
** setobj() moved to after type check functions are defined.
** See below after Collectable Types section.
*/


/*
** Entries in a Lua stack. Field 'tbclist' forms a list of all
** to-be-closed variables active in this stack. Dummy entries are
** used when the distance between two tbc variables does not fit
** in an unsigned short. They are represented by delta==0, and
** their real delta is always the maximum value that fits in
** that field.
*/
// old version of StackValue
typedef union StackValue {
  TValue val;
  struct {
    Value value_;
    lu_byte tt_;
    unsigned short delta;
  } tbclist;
} StackValue;

/* new version
clase StackValue: public TValue 
{
  TValue val;
  unsigned short delta;
} StackValue;
*/
/* index to stack elements */
typedef StackValue *StkId;


/*
** When reallocating the stack, change all pointers to the stack into
** proper offsets.
*/
typedef union {
  StkId p;  /* actual pointer */
  ptrdiff_t offset;  /* used while the stack is being reallocated */
} StkIdRel;


/* convert a 'StackValue' to a 'TValue' */
constexpr TValue* s2v(StackValue* o) noexcept { return &(o)->val; }
constexpr const TValue* s2v(const StackValue* o) noexcept { return &(o)->val; }


/*
** Note: Nil, Boolean, Thread, and Number type constants and helpers
** are now defined in lobject_core.h
*/




/*
** {==================================================================
** TValue assignment functions
** ===================================================================
*/

/*
** TValue assignment now uses the operator= defined in lgc.h.
** Stack assignments use LuaStack::setSlot() and copySlot().
*/

/*
** TValue member function implementations
** These must be defined here after all type constants are available
*/
inline void TValue::setNil() noexcept { setType(LuaT::NIL); }
inline void TValue::setFalse() noexcept { setType(LuaT::VFALSE); }
inline void TValue::setTrue() noexcept { setType(LuaT::VTRUE); }

inline void TValue::setInt(lua_Integer i) noexcept {
  value_.i = i;
  setType(LuaT::NUMINT);
}

inline void TValue::setFloat(lua_Number n) noexcept {
  value_.n = n;
  setType(LuaT::NUMFLT);
}

inline void TValue::setPointer(void* p) noexcept {
  value_.p = p;
  setType(LuaT::LIGHTUSERDATA);
}

inline void TValue::setFunction(lua_CFunction f) noexcept {
  value_.f = f;
  setType(LuaT::LCF);
}

inline void TValue::setString(lua_State* L, TString* s) noexcept {
  value_.gc = reinterpret_cast<GCObject*>(s);
  tt_ = ctb(s->getType());
  (void)L; // checkliveness removed - needs lstate.h
}

inline void TValue::setUserdata(lua_State* L, Udata* u) noexcept {
  value_.gc = reinterpret_cast<GCObject*>(u);
  tt_ = ctb(LuaT::USERDATA);
  (void)L;
}

inline void TValue::setTable(lua_State* L, Table* t) noexcept {
  value_.gc = reinterpret_cast<GCObject*>(t);
  tt_ = ctb(LuaT::TABLE);
  (void)L;
}

inline void TValue::setLClosure(lua_State* L, LClosure* cl) noexcept {
  value_.gc = reinterpret_cast<GCObject*>(cl);
  tt_ = ctb(LuaT::LCL);
  (void)L;
}

inline void TValue::setCClosure(lua_State* L, CClosure* cl) noexcept {
  value_.gc = reinterpret_cast<GCObject*>(cl);
  tt_ = ctb(LuaT::CCL);
  (void)L;
}

inline void TValue::setThread(lua_State* L, lua_State* th) noexcept {
  value_.gc = reinterpret_cast<GCObject*>(th);
  tt_ = ctb(LuaT::THREAD);
  (void)L;
}

inline void TValue::setGCObject(lua_State* L, GCObject* gc) noexcept {
  value_.gc = gc;
  tt_ = ctb(gc->getType());
  (void)L;
}

/*
** Base TValue setters for collectable types
** These are thin wrappers around TValue member functions
*/
inline void setpvalue(TValue* obj, void* p) noexcept { obj->setPointer(p); }
inline void setfvalue(TValue* obj, lua_CFunction f) noexcept { obj->setFunction(f); }
inline void setsvalue(lua_State* L, TValue* obj, TString* s) noexcept { obj->setString(L, s); }
inline void setuvalue(lua_State* L, TValue* obj, Udata* u) noexcept { obj->setUserdata(L, u); }
inline void sethvalue(lua_State* L, TValue* obj, Table* t) noexcept { obj->setTable(L, t); }
inline void setthvalue(lua_State* L, TValue* obj, lua_State* th) noexcept { obj->setThread(L, th); }
inline void setclLvalue(lua_State* L, TValue* obj, LClosure* cl) noexcept { obj->setLClosure(L, cl); }
inline void setclCvalue(lua_State* L, TValue* obj, CClosure* cl) noexcept { obj->setCClosure(L, cl); }
inline void setgcovalue(lua_State* L, TValue* obj, GCObject* gc) noexcept { obj->setGCObject(L, gc); }

/*
** Additional TValue setter wrapper (TValue -> TValue)
*/
inline void setsvalue2n(lua_State* L, TValue* obj, TString* s) noexcept {
	setsvalue(L, obj, s);
}

/*
** Convenience wrappers for setting TValues on the stack (StackValue -> TValue)
** These convert StackValue* to TValue* and call the base setters
*/
inline void sethvalue2s(lua_State* L, StackValue* o, Table* h) noexcept {
	sethvalue(L, s2v(o), h);
}

inline void setthvalue2s(lua_State* L, StackValue* o, lua_State* t) noexcept {
	setthvalue(L, s2v(o), t);
}

inline void setsvalue2s(lua_State* L, StackValue* o, TString* s) noexcept {
	setsvalue(L, s2v(o), s);
}

inline void setclLvalue2s(lua_State* L, StackValue* o, LClosure* cl) noexcept {
	setclLvalue(L, s2v(o), cl);
}

/* }================================================================== */


/*
** Note: Prototypes (Proto, Upvaldesc, LocVar, AbsLineInfo, ProtoDebugInfo)
** are now defined in lproto.h
*/

/*
** Note: Functions (UpVal, CClosure, LClosure, Closure union)
** are now defined in lfunc.h
*/

/*
** Note: Tables (Table, Node classes)
** are now defined in ltable.h
*/






/*
** 'module' operation for hashing (size is always a power of 2)
*/
inline unsigned int lmod(unsigned int s, unsigned int size) noexcept {
	lua_assert((size & (size - 1)) == 0);  /* size must be power of 2 */
	return s & (size - 1);
}




/* size of buffer for 'luaO_utf8esc' function */
inline constexpr int UTF8BUFFSZ = 8;


/* macro to call 'luaO_pushvfstring' correctly */
#define pushvfstring(L, argp, fmt, msg)	\
  { va_start(argp, fmt); \
  msg = luaO_pushvfstring(L, fmt, argp); \
  va_end(argp); \
  if (msg == nullptr) (L)->doThrow(LUA_ERRMEM);  /* only after 'va_end' */ }


[[nodiscard]] LUAI_FUNC int luaO_utf8esc (char *buff, l_uint32 x);
[[nodiscard]] LUAI_FUNC lu_byte luaO_ceillog2 (unsigned int x);
[[nodiscard]] LUAI_FUNC lu_byte luaO_codeparam (unsigned int p);
[[nodiscard]] LUAI_FUNC l_mem luaO_applyparam (lu_byte p, l_mem x);

[[nodiscard]] LUAI_FUNC int luaO_rawarith (lua_State *L, int op, const TValue *p1,
                             const TValue *p2, TValue *res);
LUAI_FUNC void luaO_arith (lua_State *L, int op, const TValue *p1,
                           const TValue *p2, StkId res);
[[nodiscard]] LUAI_FUNC size_t luaO_str2num (const char *s, TValue *o);
[[nodiscard]] LUAI_FUNC unsigned luaO_tostringbuff (const TValue *obj, char *buff);
[[nodiscard]] LUAI_FUNC lu_byte luaO_hexavalue (int c);
LUAI_FUNC void luaO_tostring (lua_State *L, TValue *obj);
LUAI_FUNC const char *luaO_pushvfstring (lua_State *L, const char *fmt,
                                                       va_list argp);
LUAI_FUNC const char *luaO_pushfstring (lua_State *L, const char *fmt, ...);

// std::span-based string utilities
LUAI_FUNC void luaO_chunkid (std::span<char> out, std::span<const char> source);

// C-style wrapper for compatibility
inline void luaO_chunkid (char *out, const char *source, size_t srclen) {
	luaO_chunkid(std::span(out, LUA_IDSIZE), std::span(source, srclen));
}


/*
** {==================================================================
** TValue Operator Overloading
** ===================================================================
*/

/* Forward declarations for lvm.h types/functions */
#ifndef F2Imod_defined
#define F2Imod_defined
enum class F2Imod {
  F2Ieq,     /* no rounding; accepts only integral values */
  F2Ifloor,  /* takes the floor of the number */
  F2Iceil    /* takes the ceiling of the number */
};
#endif

/* Forward declaration and extern declaration for VirtualMachine::flttointeger */
class VirtualMachine;
extern int VirtualMachine_flttointeger(lua_Number n, lua_Integer *p, F2Imod mode);

/* Forward declarations for comparison helpers (defined in lvm.cpp and lstring.h) */
/* These handle mixed int/float comparisons correctly */
[[nodiscard]] LUAI_FUNC int LTintfloat (lua_Integer i, lua_Number f);
[[nodiscard]] LUAI_FUNC int LEintfloat (lua_Integer i, lua_Number f);
[[nodiscard]] LUAI_FUNC int LTfloatint (lua_Number f, lua_Integer i);
[[nodiscard]] LUAI_FUNC int LEfloatint (lua_Number f, lua_Integer i);
[[nodiscard]] LUAI_FUNC int l_strcmp (const TString* ts1, const TString* ts2);
/* luaS_eqstr and shortStringsEqual declared in lstring.h */

/*
** Operator< for TValue (numeric and string comparison only, no metamethods)
** For general comparison with metamethods, use luaV_lessthan()
*/
inline bool operator<(const TValue& l, const TValue& r) noexcept {
	// Both numbers?
	if (ttisnumber(&l) && ttisnumber(&r)) {
		if (ttisinteger(&l)) {
			lua_Integer li = ivalue(&l);
			if (ttisinteger(&r))
				return li < ivalue(&r);  /* both integers */
			else
				return LTintfloat(li, fltvalue(&r));  /* int < float */
		}
		else {
			lua_Number lf = fltvalue(&l);  /* l is float */
			if (ttisfloat(&r))
				return lf < fltvalue(&r);  /* both floats */
			else
				return LTfloatint(lf, ivalue(&r));  /* float < int */
		}
	}
	// Both strings? (no metamethods - raw comparison)
	else if (ttisstring(&l) && ttisstring(&r)) {
		return *tsvalue(&l) < *tsvalue(&r);  /* Use TString operator< */
	}
	// Different types or non-comparable types
	return false;
}

/*
** Operator<= for TValue (numeric and string comparison only, no metamethods)
** For general comparison with metamethods, use luaV_lessequal()
*/
inline bool operator<=(const TValue& l, const TValue& r) noexcept {
	// Both numbers?
	if (ttisnumber(&l) && ttisnumber(&r)) {
		if (ttisinteger(&l)) {
			lua_Integer li = ivalue(&l);
			if (ttisinteger(&r))
				return li <= ivalue(&r);  /* both integers */
			else
				return LEintfloat(li, fltvalue(&r));  /* int <= float */
		}
		else {
			lua_Number lf = fltvalue(&l);  /* l is float */
			if (ttisfloat(&r))
				return lf <= fltvalue(&r);  /* both floats */
			else
				return LEfloatint(lf, ivalue(&r));  /* float <= int */
		}
	}
	// Both strings? (no metamethods - raw comparison)
	else if (ttisstring(&l) && ttisstring(&r)) {
		return *tsvalue(&l) <= *tsvalue(&r);  /* Use TString operator<= */
	}
	// Different types or non-comparable types
	return false;
}

/*
** Operator== for TValue (raw equality only, no metamethods)
** For general equality with metamethods, use luaV_equalobj()
** This is similar to luaV_rawequalobj() but as an operator
*/
inline bool operator==(const TValue& l, const TValue& r) noexcept {
	if (ttype(&l) != ttype(&r))  /* different base types? */
		return false;
	else if (ttypetag(&l) != ttypetag(&r)) {
		/* Different variants - only numbers and strings can be equal across variants */
		switch (ttypetag(&l)) {
			case LuaT::NUMINT: {  /* int == float? */
				lua_Integer i2;
				return (VirtualMachine_flttointeger(fltvalue(&r), &i2, F2Imod::F2Ieq) &&
				        ivalue(&l) == i2);
			}
			case LuaT::NUMFLT: {  /* float == int? */
				lua_Integer i1;
				return (VirtualMachine_flttointeger(fltvalue(&l), &i1, F2Imod::F2Ieq) &&
				        i1 == ivalue(&r));
			}
			case LuaT::SHRSTR: case LuaT::LNGSTR: {
				/* Compare strings with different variants */
				return tsvalue(&l)->equals(tsvalue(&r));
			}
			default:
				return false;
		}
	}
	else {  /* same variant */
		switch (ttypetag(&l)) {
			case LuaT::NIL: case LuaT::VFALSE: case LuaT::VTRUE:
				return true;
			case LuaT::NUMINT:
				return ivalue(&l) == ivalue(&r);
			case LuaT::NUMFLT:
				return fltvalue(&l) == fltvalue(&r);
			case LuaT::LIGHTUSERDATA:
				return pvalue(&l) == pvalue(&r);
			case LuaT::SHRSTR:
				return shortStringsEqual(tsvalue(&l), tsvalue(&r));
			case LuaT::LNGSTR:
				return tsvalue(&l)->equals(tsvalue(&r));
			case LuaT::USERDATA:
				return uvalue(&l) == uvalue(&r);
			case LuaT::LCF:
				return fvalue(&l) == fvalue(&r);
			default:  /* other collectable types (tables, closures, threads) */
				return gcvalue(&l) == gcvalue(&r);
		}
	}
}

/*
** Operator!= for TValue
*/
inline bool operator!=(const TValue& l, const TValue& r) noexcept {
	return !(l == r);
}


/*
** TString comparison operators
** Provide idiomatic C++ comparison syntax for TString objects
*/

/* operator< for TString - lexicographic ordering */
inline bool operator<(const TString& l, const TString& r) noexcept {
	return l_strcmp(&l, &r) < 0;
}

/* operator<= for TString - lexicographic ordering */
inline bool operator<=(const TString& l, const TString& r) noexcept {
	return l_strcmp(&l, &r) <= 0;
}

/* operator== for TString - equality check using existing equals() method */
inline bool operator==(const TString& l, const TString& r) noexcept {
	// Use equals() method which handles short vs long string optimization
	return l.equals(&r);
}

/* operator!= for TString - inequality check */
inline bool operator!=(const TString& l, const TString& r) noexcept {
	return !(l == r);
}

/* }================================================================== */


/*
** ===================================================================
** GC Type Safety Notes
** ===================================================================
** All GC-managed types inherit from GCBase<Derived> (CRTP pattern) which
** provides common GC fields (next, tt, marked). The reinterpret_cast
** operations used for GC pointer conversions are safe because:
**
** 1. All GC objects have a common initial sequence (GCObject fields)
** 2. Type tags are checked before conversions (via lua_assert)
** 3. Memory is allocated with proper alignment for all types
** 4. The CRTP pattern ensures type safety at compile time
**
** Note: These types do NOT have C++ standard layout due to CRTP and
** other C++ features, but the GC conversions remain safe through careful
** design and runtime type checking.
*/


/*
** Table fast-access inline methods
** These are performance-critical and used throughout the VM
** Defined here (after ltm.h include) to access TMS enum
*/
inline void Table::fastGeti(lua_Integer k, TValue* res, LuaT& tag) noexcept {
  lua_Unsigned u = l_castS2U(k) - 1u;
  if (u < this->arraySize()) {
    tag = *this->getArrayTag(u);
    if (!tagisempty(tag)) {
      farr2val(this, u, tag, res);
    }
  } else {
    tag = this->getInt(k, res);
  }
}

inline void Table::fastSeti(lua_Integer k, TValue* val, int& hres) noexcept {
  lua_Unsigned u = l_castS2U(k) - 1u;
  if (u < this->arraySize()) {
    LuaT* tag = this->getArrayTag(u);
    if (checknoTM(this->getMetatable(), TMS::TM_NEWINDEX) || !tagisempty(*tag)) {
      fval2arr(this, u, tag, val);
      hres = HOK;
    } else {
      hres = ~cast_int(u);
    }
  } else {
    hres = this->psetInt(k, val);
  }
}


#endif

