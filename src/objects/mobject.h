/*
** Type definitions for Lua objects
** See Copyright Notice in lua.h
*/


#ifndef lobject_h
#define lobject_h


#include <cstdarg>
#include <span>


#include "mlimits.h"
#include "moon.h"
#include "mtvalue.h"  // TValue class

// Include focused headers
#include "mobject_core.h"  // GCObject, GCBase<T>, Udata
#include "mstring.h"  // TString
#include "mproto.h"  // Proto
#include "mfunc.h"  // UpVal, CClosure, LClosure
#include "mtable.h"  // Table, Node - after lstring/lproto/lfunc for dependencies

// Include ltm.h for tag method support needed by ltable.h inline functions
#include "../core/mtm.h"  // TMS enum, checknoTM function

// Forward declarations
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
// index to stack elements
typedef StackValue *StkId;


/*
** When reallocating the stack, change all pointers to the stack into
** proper offsets.
*/
typedef union {
  StkId p;  // actual pointer
  ptrdiff_t offset;  // used while the stack is being reallocated
} StkIdRel;


// convert a 'StackValue' to a 'TValue'
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
** Stack assignments use MoonStack::setSlot() and copySlot().
*/

/*
** TValue member function implementations
** These must be defined here after all type constants are available
*/
inline void TValue::setNil() noexcept { setType(MoonT::NIL); }
inline void TValue::setFalse() noexcept { setType(MoonT::VFALSE); }
inline void TValue::setTrue() noexcept { setType(MoonT::VTRUE); }

inline void TValue::setInt(moon_Integer i) noexcept {
  value_.i = i;
  setType(MoonT::NUMINT);
}

inline void TValue::setFloat(moon_Number n) noexcept {
  value_.n = n;
  setType(MoonT::NUMFLT);
}

inline void TValue::setPointer(void* p) noexcept {
  value_.p = p;
  setType(MoonT::LIGHTUSERDATA);
}

inline void TValue::setFunction(moon_CFunction f) noexcept {
  value_.f = f;
  setType(MoonT::LCF);
}

inline void TValue::setString(moon_State* L, TString* s) noexcept {
  value_.gc = reinterpret_cast<GCObject*>(s);
  tt_ = ctb(s->getType());
  (void)L; // checkliveness removed - needs lstate.h
}

inline void TValue::setUserdata(moon_State* L, Udata* u) noexcept {
  value_.gc = reinterpret_cast<GCObject*>(u);
  tt_ = ctb(MoonT::USERDATA);
  (void)L;
}

inline void TValue::setTable(moon_State* L, Table* t) noexcept {
  value_.gc = reinterpret_cast<GCObject*>(t);
  tt_ = ctb(MoonT::TABLE);
  (void)L;
}

inline void TValue::setLClosure(moon_State* L, LClosure* cl) noexcept {
  value_.gc = reinterpret_cast<GCObject*>(cl);
  tt_ = ctb(MoonT::LCL);
  (void)L;
}

inline void TValue::setCClosure(moon_State* L, CClosure* cl) noexcept {
  value_.gc = reinterpret_cast<GCObject*>(cl);
  tt_ = ctb(MoonT::CCL);
  (void)L;
}

inline void TValue::setThread(moon_State* L, moon_State* th) noexcept {
  value_.gc = reinterpret_cast<GCObject*>(th);
  tt_ = ctb(MoonT::THREAD);
  (void)L;
}

inline void TValue::setGCObject(moon_State* L, GCObject* gc) noexcept {
  value_.gc = gc;
  tt_ = ctb(gc->getType());
  (void)L;
}

/*
** Base TValue setters for collectable types
** These are thin wrappers around TValue member functions
*/
inline void setpvalue(TValue* obj, void* p) noexcept { obj->setPointer(p); }
inline void setfvalue(TValue* obj, moon_CFunction f) noexcept { obj->setFunction(f); }
inline void setsvalue(moon_State* L, TValue* obj, TString* s) noexcept { obj->setString(L, s); }
inline void setuvalue(moon_State* L, TValue* obj, Udata* u) noexcept { obj->setUserdata(L, u); }
inline void sethvalue(moon_State* L, TValue* obj, Table* t) noexcept { obj->setTable(L, t); }
inline void setthvalue(moon_State* L, TValue* obj, moon_State* th) noexcept { obj->setThread(L, th); }
inline void setclLvalue(moon_State* L, TValue* obj, LClosure* cl) noexcept { obj->setLClosure(L, cl); }
inline void setclCvalue(moon_State* L, TValue* obj, CClosure* cl) noexcept { obj->setCClosure(L, cl); }
inline void setgcovalue(moon_State* L, TValue* obj, GCObject* gc) noexcept { obj->setGCObject(L, gc); }

/*
** Additional TValue setter wrapper (TValue -> TValue)
*/
inline void setsvalue2n(moon_State* L, TValue* obj, TString* s) noexcept {
	setsvalue(L, obj, s);
}

/*
** Convenience wrappers for setting TValues on the stack (StackValue -> TValue)
** These convert StackValue* to TValue* and call the base setters
*/
inline void sethvalue2s(moon_State* L, StackValue* o, Table* h) noexcept {
	sethvalue(L, s2v(o), h);
}

inline void setthvalue2s(moon_State* L, StackValue* o, moon_State* t) noexcept {
	setthvalue(L, s2v(o), t);
}

inline void setsvalue2s(moon_State* L, StackValue* o, TString* s) noexcept {
	setsvalue(L, s2v(o), s);
}

inline void setclLvalue2s(moon_State* L, StackValue* o, LClosure* cl) noexcept {
	setclLvalue(L, s2v(o), cl);
}

// }==================================================================


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
	moon_assert((size & (size - 1)) == 0);  // size must be power of 2
	return s & (size - 1);
}




// size of buffer for 'moonO_utf8esc' function
inline constexpr int UTF8BUFFSZ = 8;


// macro to call 'moonO_pushvfstring' correctly
#define pushvfstring(L, argp, fmt, msg)	\
  { va_start(argp, fmt); \
  msg = moonO_pushvfstring(L, fmt, argp); \
  va_end(argp); \
  if (msg == nullptr) (L)->doThrow(MOON_ERRMEM);  /* only after 'va_end' */ }


[[nodiscard]] MOONI_FUNC int moonO_utf8esc (char *buff, l_uint32 x);
[[nodiscard]] MOONI_FUNC lu_byte moonO_ceillog2 (unsigned int x);
[[nodiscard]] MOONI_FUNC lu_byte moonO_codeparam (unsigned int p);
[[nodiscard]] MOONI_FUNC l_mem moonO_applyparam (lu_byte p, l_mem x);

[[nodiscard]] MOONI_FUNC int moonO_rawarith (moon_State *L, int op, const TValue *p1,
                             const TValue *p2, TValue *res);
MOONI_FUNC void moonO_arith (moon_State *L, int op, const TValue *p1,
                           const TValue *p2, StkId res);
[[nodiscard]] MOONI_FUNC size_t moonO_str2num (const char *s, TValue *o);
[[nodiscard]] MOONI_FUNC unsigned moonO_tostringbuff (const TValue *obj, char *buff);
[[nodiscard]] MOONI_FUNC lu_byte moonO_hexavalue (int c);
MOONI_FUNC void moonO_tostring (moon_State *L, TValue *obj);
MOONI_FUNC const char *moonO_pushvfstring (moon_State *L, const char *fmt,
                                                       va_list argp);
MOONI_FUNC const char *moonO_pushfstring (moon_State *L, const char *fmt, ...);

// std::span-based string utilities
MOONI_FUNC void moonO_chunkid (std::span<char> out, std::span<const char> source);

// C-style wrapper for compatibility
inline void moonO_chunkid (char *out, const char *source, size_t srclen) {
	moonO_chunkid(std::span(out, MOON_IDSIZE), std::span(source, srclen));
}


/*
** {==================================================================
** TValue Operator Overloading
** ===================================================================
*/

// Forward declarations for lvm.h types/functions
#ifndef F2Imod_defined
#define F2Imod_defined
enum class F2Imod {
  F2Ieq,  // no rounding; accepts only integral values
  F2Ifloor,  // takes the floor of the number
  F2Iceil  // takes the ceiling of the number
};
#endif

// Forward declaration and extern declaration for VirtualMachine::flttointeger
class VirtualMachine;
extern int VirtualMachine_flttointeger(moon_Number n, moon_Integer *p, F2Imod mode);

// Forward declarations for comparison helpers (defined in lvm.cpp and lstring.h)
// These handle mixed int/float comparisons correctly
[[nodiscard]] MOONI_FUNC int LTintfloat (moon_Integer i, moon_Number f);
[[nodiscard]] MOONI_FUNC int LEintfloat (moon_Integer i, moon_Number f);
[[nodiscard]] MOONI_FUNC int LTfloatint (moon_Number f, moon_Integer i);
[[nodiscard]] MOONI_FUNC int LEfloatint (moon_Number f, moon_Integer i);
[[nodiscard]] MOONI_FUNC int l_strcmp (const TString* ts1, const TString* ts2);
// moonS_eqstr and shortStringsEqual declared in lstring.h

/*
** Operator< for TValue (numeric and string comparison only, no metamethods)
** For general comparison with metamethods, use moonV_lessthan()
*/
inline bool operator<(const TValue& l, const TValue& r) noexcept {
	// Both numbers?
	if (ttisnumber(&l) && ttisnumber(&r)) {
		if (ttisinteger(&l)) {
			moon_Integer li = ivalue(&l);
			if (ttisinteger(&r))
				return li < ivalue(&r);  // both integers
			else
				return LTintfloat(li, fltvalue(&r));  // int < float
		}
		else {
			moon_Number lf = fltvalue(&l);  // l is float
			if (ttisfloat(&r))
				return lf < fltvalue(&r);  // both floats
			else
				return LTfloatint(lf, ivalue(&r));  // float < int
		}
	}
	// Both strings? (no metamethods - raw comparison)
	else if (ttisstring(&l) && ttisstring(&r)) {
		return *tsvalue(&l) < *tsvalue(&r);  // Use TString operator<
	}
	// Different types or non-comparable types
	return false;
}

/*
** Operator<= for TValue (numeric and string comparison only, no metamethods)
** For general comparison with metamethods, use moonV_lessequal()
*/
inline bool operator<=(const TValue& l, const TValue& r) noexcept {
	// Both numbers?
	if (ttisnumber(&l) && ttisnumber(&r)) {
		if (ttisinteger(&l)) {
			moon_Integer li = ivalue(&l);
			if (ttisinteger(&r))
				return li <= ivalue(&r);  // both integers
			else
				return LEintfloat(li, fltvalue(&r));  // int <= float
		}
		else {
			moon_Number lf = fltvalue(&l);  // l is float
			if (ttisfloat(&r))
				return lf <= fltvalue(&r);  // both floats
			else
				return LEfloatint(lf, ivalue(&r));  // float <= int
		}
	}
	// Both strings? (no metamethods - raw comparison)
	else if (ttisstring(&l) && ttisstring(&r)) {
		return *tsvalue(&l) <= *tsvalue(&r);  // Use TString operator<=
	}
	// Different types or non-comparable types
	return false;
}

/*
** Operator== for TValue (raw equality only, no metamethods)
** For general equality with metamethods, use moonV_equalobj()
** This is similar to moonV_rawequalobj() but as an operator
*/
inline bool operator==(const TValue& l, const TValue& r) noexcept {
	if (ttype(&l) != ttype(&r))  // different base types?
		return false;
	else if (ttypetag(&l) != ttypetag(&r)) {
		// Different variants - only numbers and strings can be equal across variants
		switch (ttypetag(&l)) {
			case MoonT::NUMINT: {  // int == float?
				moon_Integer i2;
				return (VirtualMachine_flttointeger(fltvalue(&r), &i2, F2Imod::F2Ieq) &&
				        ivalue(&l) == i2);
			}
			case MoonT::NUMFLT: {  // float == int?
				moon_Integer i1;
				return (VirtualMachine_flttointeger(fltvalue(&l), &i1, F2Imod::F2Ieq) &&
				        i1 == ivalue(&r));
			}
			case MoonT::SHRSTR: case MoonT::LNGSTR: {
				// Compare strings with different variants
				return tsvalue(&l)->equals(tsvalue(&r));
			}
			default:
				return false;
		}
	}
	else {  // same variant
		switch (ttypetag(&l)) {
			case MoonT::NIL: case MoonT::VFALSE: case MoonT::VTRUE:
				return true;
			case MoonT::NUMINT:
				return ivalue(&l) == ivalue(&r);
			case MoonT::NUMFLT:
				return fltvalue(&l) == fltvalue(&r);
			case MoonT::LIGHTUSERDATA:
				return pvalue(&l) == pvalue(&r);
			case MoonT::SHRSTR:
				return shortStringsEqual(tsvalue(&l), tsvalue(&r));
			case MoonT::LNGSTR:
				return tsvalue(&l)->equals(tsvalue(&r));
			case MoonT::USERDATA:
				return uvalue(&l) == uvalue(&r);
			case MoonT::LCF:
				return fvalue(&l) == fvalue(&r);
			default:  // other collectable types (tables, closures, threads)
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

// operator< for TString - lexicographic ordering
inline bool operator<(const TString& l, const TString& r) noexcept {
	return l_strcmp(&l, &r) < 0;
}

// operator<= for TString - lexicographic ordering
inline bool operator<=(const TString& l, const TString& r) noexcept {
	return l_strcmp(&l, &r) <= 0;
}

// operator== for TString - equality check using existing equals() method
inline bool operator==(const TString& l, const TString& r) noexcept {
	// Use equals() method which handles short vs long string optimization
	return l.equals(&r);
}

// operator!= for TString - inequality check
inline bool operator!=(const TString& l, const TString& r) noexcept {
	return !(l == r);
}

// }==================================================================


/*
** ===================================================================
** GC Type Safety Notes
** ===================================================================
** All GC-managed types inherit from GCBase<Derived> (CRTP pattern) which
** provides common GC fields (next, tt, marked). The reinterpret_cast
** operations used for GC pointer conversions are safe because:
**
** 1. All GC objects have a common initial sequence (GCObject fields)
** 2. Type tags are checked before conversions (via moon_assert)
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
inline void Table::fastGeti(moon_Integer k, TValue* res, MoonT& tag) noexcept {
  moon_Unsigned u = l_castS2U(k) - 1u;
  if (u < this->arraySize()) {
    tag = *this->getArrayTag(u);
    if (!tagisempty(tag)) {
      farr2val(this, u, tag, res);
    }
  } else {
    tag = this->getInt(k, res);
  }
}

inline void Table::fastSeti(moon_Integer k, TValue* val, int& hres) noexcept {
  moon_Unsigned u = l_castS2U(k) - 1u;
  if (u < this->arraySize()) {
    MoonT* tag = this->getArrayTag(u);
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

