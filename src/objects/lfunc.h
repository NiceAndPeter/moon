/*
** Auxiliary functions to manipulate prototypes and closures
** See Copyright Notice in lua.h
*/

#ifndef lfunc_h
#define lfunc_h


#include "lobject_core.h"  // GCBase, GCObject, TValue
#include "lproto.h"  // Proto

// Forward declarations
struct lua_State;
class TString;
typedef union StackValue *StkId;


/*
** {==================================================================
** Functions
** ===================================================================
** Note: LUA_VUPVAL, LUA_VLCL, LUA_VLCF, LUA_VCCL now defined in ltvalue.h
*/

constexpr bool ttisfunction(const TValue* o) noexcept { return checktype(o, LUA_TFUNCTION); }
constexpr bool ttisLclosure(const TValue* o) noexcept { return checktag(o, ctb(LuaT::LCL)); }
constexpr bool ttislcf(const TValue* o) noexcept { return checktag(o, LuaT::LCF); }
constexpr bool ttisCclosure(const TValue* o) noexcept { return checktag(o, ctb(LuaT::CCL)); }
constexpr bool ttisclosure(const TValue* o) noexcept { return ttisLclosure(o) || ttisCclosure(o); }

constexpr bool TValue::isFunction() const noexcept { return checktype(this, LUA_TFUNCTION); }
constexpr bool TValue::isLClosure() const noexcept { return checktag(this, ctb(LuaT::LCL)); }
constexpr bool TValue::isLightCFunction() const noexcept { return checktag(this, LuaT::LCF); }
constexpr bool TValue::isCClosure() const noexcept { return checktag(this, ctb(LuaT::CCL)); }
constexpr bool TValue::isClosure() const noexcept { return isLClosure() || isCClosure(); }

inline constexpr bool isLfunction(const TValue* o) noexcept {
	return ttisLclosure(o);
}

constexpr bool TValue::isLuaFunction() const noexcept { return isLClosure(); }

inline Closure* clvalue(const TValue* o) noexcept { return o->closureValue(); }
inline LClosure* clLvalue(const TValue* o) noexcept { return o->lClosureValue(); }
inline CClosure* clCvalue(const TValue* o) noexcept { return o->cClosureValue(); }

inline lua_CFunction fvalue(const TValue* o) noexcept { return o->functionValue(); }

constexpr lua_CFunction fvalueraw(const Value& v) noexcept { return v.f; }


// setfvalue now defined as inline function below

// setclCvalue now defined as inline function below


/*
** Upvalues for Lua closures
*/
// UpVal inherits from GCBase (CRTP)
class UpVal : public GCBase<UpVal> {
private:
  union {
    TValue *p;  // points to stack or to its own value
    ptrdiff_t offset;  // used while the stack is being reallocated
  } v;
  union {
    struct {  // (when open)
      UpVal *next;  // linked list
      UpVal **previous;
    } open;
    TValue value;  // the value (when closed)
  } u;

public:
  // Constructor - initializes all fields to safe defaults
  UpVal() noexcept
    : v{nullptr}, u{} {
    // Initialize u union as closed upvalue with nil
    u.value.valueField().n = 0;  // Zero-initialize Value union
    u.value.setType(LUA_TNIL);
  }

  // Destructor - trivial (GC handles deallocation)
  ~UpVal() noexcept = default;

  // Placement new operator - integrates with Lua's GC (implemented in lgc.h)
  static void* operator new(size_t size, lua_State* L, LuaT tt);

  // Disable regular new/delete (must use placement new with GC)
  static void* operator new(size_t) = delete;
  static void operator delete(void*) = delete;

  // Inline accessors for v union
  TValue* getVP() noexcept { return v.p; }
  const TValue* getVP() const noexcept { return v.p; }
  void setVP(TValue* ptr) noexcept { v.p = ptr; }

  ptrdiff_t getOffset() const noexcept { return v.offset; }
  void setOffset(ptrdiff_t off) noexcept { v.offset = off; }

  // Inline accessors for u union (open upvalues)
  UpVal* getOpenNext() const noexcept { return u.open.next; }
  void setOpenNext(UpVal* next_uv) noexcept { u.open.next = next_uv; }
  UpVal** getOpenNextPtr() noexcept { return &u.open.next; }

  UpVal** getOpenPrevious() const noexcept { return u.open.previous; }
  void setOpenPrevious(UpVal** prev) noexcept { u.open.previous = prev; }

  // Accessor for u.value (closed upvalues)
  TValue* getValueSlot() noexcept { return &u.value; }
  const TValue* getValueSlot() const noexcept { return &u.value; }

  // Status check
  bool isOpen() const noexcept { return v.p != &u.value; }

  // Level accessor for open upvalues
  StkId getLevel() const noexcept {
    lua_assert(isOpen());
    return reinterpret_cast<StkId>(v.p);
  }

  // Backward compatibility (getValue returns current value pointer)
  TValue* getValue() noexcept { return v.p; }
  const TValue* getValue() const noexcept { return v.p; }

  // Methods (implemented in lfunc.cpp)
  void unlink();
};



// Closures inherit from GCBase (CRTP)
// ClosureHeader fields: nupvalues, gclist (GC fields inherited from GCBase)

class CClosure : public GCBase<CClosure> {
private:
  lu_byte numberOfUpvalues;
  GCObject *gclist;
  lua_CFunction f;
  TValue upvalue[1];  // list of upvalues

public:
  // Member placement new operator for GC allocation (defined in lgc.h)
  static void* operator new(size_t size, lua_State* L, LuaT tt, size_t extra = 0);

  // Constructor
  CClosure(int nupvals);

  // Factory method
  [[nodiscard]] static CClosure* create(lua_State* L, int nupvals);

  // Inline accessors
  lua_CFunction getFunction() const noexcept { return f; }
  void setFunction(lua_CFunction func) noexcept { f = func; }

  lu_byte getNumUpvalues() const noexcept { return numberOfUpvalues; }
  void setNumUpvalues(lu_byte n) noexcept { numberOfUpvalues = n; }

  TValue* getUpvalue(int idx) noexcept { return &upvalue[idx]; }
  const TValue* getUpvalue(int idx) const noexcept { return &upvalue[idx]; }

  GCObject* getGclist() const noexcept { return gclist; }
  void setGclist(GCObject* gc) noexcept { gclist = gc; }
  // For GC gray list traversal - allows efficient list manipulation
  GCObject** getGclistPtr() noexcept { return &gclist; }

  // Static helper for size calculation (can access private upvalue field)
  static constexpr size_t sizeForUpvalues(int n) noexcept {
    return offsetof(CClosure, upvalue) + sizeof(TValue) * cast_uint(n);
  }
};

class LClosure : public GCBase<LClosure> {
private:
  lu_byte numberOfUpvalues;
  GCObject *gclist;
  Proto *p;
  UpVal *upvals[1];  // list of upvalues

public:
  // Member placement new operator for GC allocation (defined in lgc.h)
  static void* operator new(size_t size, lua_State* L, LuaT tt, size_t extra = 0);

  // Constructor
  LClosure(int nupvals);

  // Factory method
  [[nodiscard]] static LClosure* create(lua_State* L, int nupvals);

  // Inline accessors
  Proto* getProto() const noexcept { return p; }
  void setProto(Proto* proto) noexcept { p = proto; }

  lu_byte getNumUpvalues() const noexcept { return numberOfUpvalues; }
  void setNumUpvalues(lu_byte n) noexcept { numberOfUpvalues = n; }

  UpVal* getUpval(int idx) const noexcept { return upvals[idx]; }
  void setUpval(int idx, UpVal* upvalue) noexcept { upvals[idx] = upvalue; }
  UpVal** getUpvalPtr(int idx) noexcept { return &upvals[idx]; }

  GCObject* getGclist() const noexcept { return gclist; }
  void setGclist(GCObject* gc) noexcept { gclist = gc; }
  // For GC gray list traversal - allows efficient list manipulation
  GCObject** getGclistPtr() noexcept { return &gclist; }

  // Static helper for size calculation (can access private upvals field)
  static constexpr size_t sizeForUpvalues(int n) noexcept {
    return offsetof(LClosure, upvals) + sizeof(UpVal *) * cast_uint(n);
  }

  // Methods (implemented in lfunc.cpp)
  void initUpvals(lua_State* L);
};


typedef union Closure {
  CClosure c;
  LClosure l;
} Closure;

inline Proto* getproto(const TValue* o) noexcept {
	return clLvalue(o)->getProto();
}

// }==================================================================


// Simple forwarding calls to the static size helpers.
inline constexpr lu_mem sizeCclosure(int n) noexcept {
	return CClosure::sizeForUpvalues(n);
}

inline constexpr lu_mem sizeLclosure(int n) noexcept {
	return LClosure::sizeForUpvalues(n);
}

/*
** maximum number of upvalues in a closure (both C and Lua). (Value
** must fit in a VM register.)
*/
inline constexpr int MAXUPVAL = 255;

/*
** maximum number of misses before giving up the cache of closures
** in prototypes
*/
inline constexpr int MAXMISS = 10;



// special status to close upvalues preserving the top of the stack
inline constexpr int CLOSEKTOP = (LUA_ERRERR + 1);


[[nodiscard]] LUAI_FUNC Proto *luaF_newproto (lua_State *L);
[[nodiscard]] LUAI_FUNC UpVal *luaF_findupval (lua_State *L, StkId level);
LUAI_FUNC void luaF_newtbcupval (lua_State *L, StkId level);
LUAI_FUNC void luaF_closeupval (lua_State *L, StkId level);
[[nodiscard]] LUAI_FUNC StkId luaF_close (lua_State *L, StkId level, TStatus status, int yy);
LUAI_FUNC void luaF_unlinkupval (UpVal *upvalue);
[[nodiscard]] LUAI_FUNC lu_mem luaF_protosize (Proto *p);
LUAI_FUNC void luaF_freeproto (lua_State *L, Proto *f);
[[nodiscard]] LUAI_FUNC const char *luaF_getlocalname (const Proto *func, int local_number,
                                         int pc);


#endif
