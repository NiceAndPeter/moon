/*
** Tagged Values (TValue class)
** See Copyright Notice in lua.h
*/

#ifndef ltvalue_h
#define ltvalue_h

#include "mlimits.h"
#include "moon.h"

#include <utility>

/*
** tags for Tagged Values have the following use of bits:
** bits 0-3: actual tag (a MOON_T* constant)
** bits 4-5: variant bits
** bit 6: whether value is collectable
*/

// add variant bits to a type
constexpr int makevariant(int t, int v) noexcept { return (t | (v << 4)); }
constexpr int makevariant(MoonType t, int v) noexcept { return (std::to_underlying(t) | (v << 4)); }


/*
** {==================================================================
** Variant tags for all Lua types
** ===================================================================
*/

enum class MoonT : lu_byte {
  // Nil variants
  NIL     = makevariant(MoonType::Nil, 0),
  EMPTY   = makevariant(MoonType::Nil, 1),
  ABSTKEY = makevariant(MoonType::Nil, 2),
  NOTABLE = makevariant(MoonType::Nil, 3),

  // Boolean variants
  VFALSE = makevariant(MoonType::Boolean, 0),
  VTRUE  = makevariant(MoonType::Boolean, 1),

  // Number variants
  NUMINT = makevariant(MoonType::Number, 0),  // integer numbers
  NUMFLT = makevariant(MoonType::Number, 1),  // float numbers

  // String variants
  SHRSTR = makevariant(MoonType::String, 0),  // short strings
  LNGSTR = makevariant(MoonType::String, 1),  // long strings

  // Table variant
  TABLE = makevariant(MoonType::Table, 0),

  // Function variants
  LCL = makevariant(MoonType::Function, 0),  // Lua closure
  LCF = makevariant(MoonType::Function, 1),  // light C function
  CCL = makevariant(MoonType::Function, 2),  // C closure

  // Userdata variants
  LIGHTUSERDATA = makevariant(MoonType::LightUserdata, 0),
  USERDATA      = makevariant(MoonType::Userdata,      0),

  // Thread variant
  THREAD = makevariant(MoonType::Thread, 0),

  // Upvalue variant (collectable non-value)
  UPVAL = makevariant(MOON_NUMTYPES, 0),

  // Proto variant (collectable non-value)
  PROTO = makevariant(MOON_NUMTYPES + 1, 0)
};

// }==================================================================


/*
** Rounding modes for float->integer coercion (needed by TValue conversion methods)
*/
#ifndef F2Imod_defined
#define F2Imod_defined
enum class F2Imod {
  F2Ieq,  // no rounding; accepts only integral values
  F2Ifloor,  // takes the floor of the number
  F2Iceil  // takes the ceiling of the number
};
#endif


/*
** Union of all Lua values
*/
typedef union Value {
  GCObject *gc;  // collectable objects
  void *p;  // light userdata
  moon_CFunction f;  // light C functions
  moon_Integer i;  // integer numbers
  moon_Number n;  // float numbers
  // not used, but may avoid warnings for uninitialized value
  lu_byte ub;
} Value;


/*
** Forward declarations for TValue accessor methods
*/
class TString;
class Udata;
class Table;
union Closure;
class LClosure;
class CClosure;
struct moon_State;
class GCObject;


/*
** Tagged Values. This is the basic representation of values in Lua:
** an actual value plus a tag with its type.
*/
class TValue {
private:
  Value value_;
  MoonT tt_;

public:
  // Constexpr constructor for static initialization
  constexpr TValue(Value v, MoonT t) noexcept : value_(v), tt_(t) {}
  constexpr TValue(Value v, lu_byte t) noexcept : value_(v), tt_(static_cast<MoonT>(t)) {}  // for compatibility

  // Default constructor
  TValue() = default;

  // Inline accessors for hot-path access
  MoonT getType() const noexcept { return tt_; }
  lu_byte getRawType() const noexcept { return static_cast<lu_byte>(tt_); }  // for legacy code
  const Value& getValue() const noexcept { return value_; }
  Value& getValue() noexcept { return value_; }

  // Value accessors
  // Integer value (for VKINT/VNUMINT types)
  moon_Integer intValue() const noexcept { return value_.i; }

  // Float value (for VNUMFLT types)
  moon_Number floatValue() const noexcept { return value_.n; }

  // Pointer value (for VLIGHTUSERDATA)
  void* pointerValue() const noexcept { return value_.p; }

  // GC object value (for collectable types)
  GCObject* gcValue() const noexcept { return value_.gc; }

  // C function value (for light C functions)
  moon_CFunction functionValue() const noexcept { return value_.f; }

  // Type-specific value accessors (return pointers from GC union)
  TString* stringValue() const noexcept { return reinterpret_cast<TString*>(value_.gc); }
  Udata* userdataValue() const noexcept { return reinterpret_cast<Udata*>(value_.gc); }
  Table* tableValue() const noexcept { return reinterpret_cast<Table*>(value_.gc); }
  Closure* closureValue() const noexcept { return reinterpret_cast<Closure*>(value_.gc); }
  LClosure* lClosureValue() const noexcept { return reinterpret_cast<LClosure*>(value_.gc); }
  CClosure* cClosureValue() const noexcept { return reinterpret_cast<CClosure*>(value_.gc); }
  moon_State* threadValue() const noexcept { return reinterpret_cast<moon_State*>(value_.gc); }

  // Number value (returns int or float depending on type)
  // Note: Actual conversion logic is in nvalue() wrapper below (needs type constants)
  moon_Number numberValue() const noexcept;

  // Setter methods (HOT PATH - performance critical!)
  // Note: These need type constants, so implementations are below
  void setNil() noexcept;
  void setFalse() noexcept;
  void setTrue() noexcept;
  void setInt(moon_Integer i) noexcept;
  void setFloat(moon_Number n) noexcept;
  void setPointer(void* p) noexcept;
  void setFunction(moon_CFunction f) noexcept;
  void setString(moon_State* L, TString* s) noexcept;
  void setUserdata(moon_State* L, Udata* u) noexcept;
  void setTable(moon_State* L, Table* t) noexcept;
  void setLClosure(moon_State* L, LClosure* cl) noexcept;
  void setCClosure(moon_State* L, CClosure* cl) noexcept;
  void setThread(moon_State* L, moon_State* th) noexcept;
  void setGCObject(moon_State* L, GCObject* gc) noexcept;

  // Change value (no type change - for optimization)
  void changeInt(moon_Integer i) noexcept { value_.i = i; }
  void changeFloat(moon_Number n) noexcept { value_.n = n; }

  // Conversion methods (formerly moonV_tonumber_, moonV_tointeger, moonV_tointegerns)
  // Return 1 on success, 0 on failure
  int toNumber(moon_Number* n) const;
  int toInteger(moon_Integer* p, F2Imod mode) const;
  int toIntegerNoString(moon_Integer* p, F2Imod mode) const;

  // Copy from another TValue
  void copy(const TValue* other) noexcept {
    value_ = other->getValue();
    tt_ = other->getType();
  }

  // Low-level field access (for macros during transition)
  Value& valueField() noexcept { return value_; }
  const Value& valueField() const noexcept { return value_; }
  void setType(MoonT t) noexcept { tt_ = t; }
  void setType(lu_byte t) noexcept { tt_ = static_cast<MoonT>(t); }  // for legacy code

  // Type checking methods (implementations below after constants are defined)
  // Nil checks
  constexpr bool isNil() const noexcept;
  constexpr bool isStrictNil() const noexcept;
  constexpr bool isAbstKey() const noexcept;
  constexpr bool isNonStrictNil() const noexcept;
  constexpr bool isEmpty() const noexcept;

  // Boolean checks
  constexpr bool isBoolean() const noexcept;
  constexpr bool isFalse() const noexcept;
  constexpr bool isTrue() const noexcept;
  constexpr bool isFalseLike() const noexcept; // nil or false

  // Number checks
  constexpr bool isNumber() const noexcept;
  constexpr bool isInteger() const noexcept;
  constexpr bool isFloat() const noexcept;

  // String checks
  constexpr bool isString() const noexcept;
  constexpr bool isShortString() const noexcept;
  constexpr bool isLongString() const noexcept;
  bool isExtString() const noexcept;

  // Userdata checks
  constexpr bool isLightUserdata() const noexcept;
  constexpr bool isFullUserdata() const noexcept;

  // Thread check
  constexpr bool isThread() const noexcept;

  // Function checks
  constexpr bool isFunction() const noexcept;
  constexpr bool isLClosure() const noexcept;
  constexpr bool isLightCFunction() const noexcept;
  constexpr bool isCClosure() const noexcept;
  constexpr bool isClosure() const noexcept;
  constexpr bool isLuaFunction() const noexcept;

  // Table check
  constexpr bool isTable() const noexcept;

  // GC checks
  constexpr bool isCollectable() const noexcept;
  bool hasRightType() const noexcept; // GC object has same tag as value

  // Low-level type accessors
  constexpr MoonT rawType() const noexcept { return tt_; }
  constexpr int baseType() const noexcept;
  constexpr MoonT typeTag() const noexcept;

  // Operator overloads (for numeric comparisons - defined after dependencies)
  // These are declared here but implemented in lobject.h after all type helpers are available
  friend bool operator<(const TValue& l, const TValue& r) noexcept;
  friend bool operator<=(const TValue& l, const TValue& r) noexcept;
  friend bool operator==(const TValue& l, const TValue& r) noexcept;
  friend bool operator!=(const TValue& l, const TValue& r) noexcept;

  // Assignment operator (simple copy, no GC barriers)
  // For assignments that require GC barriers, use setobj2t/setobj2n
  // Implementation in lgc.h after all dependencies
  TValue& operator=(const TValue& other) noexcept;
};


// raw type tag of a TValue
constexpr MoonT rawtt(const TValue* o) noexcept { return o->getType(); }

// tag with no variants (bits 0-3)
constexpr int novariant(int t) noexcept { return (t & 0x0F); }
constexpr int novariant(MoonT t) noexcept { return (static_cast<int>(t) & 0x0F); }

// type tag of a TValue (bits 0-3 for tags + variant bits 4-5)
constexpr int withvariant(int t) noexcept { return (t & 0x3F); }
constexpr MoonT withvariant(MoonT t) noexcept { return static_cast<MoonT>(static_cast<int>(t) & 0x3F); }

constexpr MoonT ttypetag(const TValue* o) noexcept { return withvariant(rawtt(o)); }

// type of a TValue
constexpr int ttype(const TValue* o) noexcept { return novariant(rawtt(o)); }

// TValue low-level type accessor implementations
constexpr int TValue::baseType() const noexcept { return novariant(tt_); }
constexpr MoonT TValue::typeTag() const noexcept { return withvariant(tt_); }

// Macros to test type
constexpr bool checktag(const TValue* o, MoonT t) noexcept { return rawtt(o) == t; }
constexpr bool checktype(const TValue* o, int t) noexcept { return ttype(o) == t; }

// Bit mark for collectable types
inline constexpr int BIT_ISCOLLECTABLE = (1 << 6);

// mark a tag as collectable
constexpr MoonT ctb(MoonT t) noexcept { return static_cast<MoonT>(static_cast<int>(t) | BIT_ISCOLLECTABLE); }


// Macros for internal tests

// collectable object has the same tag as the original value
// NOTE: righttt() defined as inline function after gcvalue() below

// Macros to set values

// set a value's tag
inline void settt_(TValue* o, MoonT t) noexcept { o->setType(t); }


#endif
