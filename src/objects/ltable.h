/*
** Lua tables (hash)
** See Copyright Notice in lua.h
*/

#ifndef ltable_h
#define ltable_h

#include <span>

#include "lobject_core.h"  /* GCBase, TValue, GCObject */

/* Forward declarations */
class lua_State;
class TString;
typedef union StackValue *StkId;


/*
** {==================================================================
** Tables
** ===================================================================
** Note: LUA_VTABLE now defined in ltvalue.h
*/

constexpr bool ttistable(const TValue* o) noexcept { return checktag(o, ctb(LuaT::TABLE)); }

constexpr bool TValue::isTable() const noexcept { return checktag(this, ctb(LuaT::TABLE)); }

inline Table* hvalue(const TValue* o) noexcept { return o->tableValue(); }


/*
** Nodes for Hash tables: A pack of two TValue's (key-value pairs)
** plus a 'next' field to link colliding entries. The distribution
** of the key's fields ('key_tt' and 'key_val') not forming a proper
** 'TValue' allows for a smaller size for 'Node' both in 4-byte
** and 8-byte alignments.
*/
class Node {
private:
  union {
    struct {
      Value value_;  /* value */
      LuaT tt_;   /* value type tag */
      LuaT key_tt;  /* key type */
      int next;  /* for chaining */
      Value key_val;  /* key value */
    } u;
    TValue i_val;  /* direct access to node's value as a proper 'TValue' */
  };

public:
  // Default constructor
  constexpr Node() noexcept : u{{0}, LuaT::NIL, static_cast<LuaT>(LUA_TNIL), 0, {0}} {}

  // Constructor for initializing with explicit values
  constexpr Node(Value val, LuaT val_tt, LuaT key_tt, int next_val, Value key_val) noexcept
    : u{val, val_tt, key_tt, next_val, key_val} {}

  // Copy assignment operator (needed because union contains TValue with user-declared operator=)
  Node& operator=(const Node& other) noexcept {
    u = other.u;  // Copy the union
    return *this;
  }

  // Value access
  TValue* getValue() noexcept { return &i_val; }
  const TValue* getValue() const noexcept { return &i_val; }

  // Next chain access
  int& getNext() noexcept { return u.next; }
  int getNext() const noexcept { return u.next; }
  void setNext(int n) noexcept { u.next = n; }

  // Key type access
  LuaT getKeyType() const noexcept { return u.key_tt; }
  void setKeyType(LuaT tt) noexcept { u.key_tt = tt; }

  // Key value access
  const Value& getKeyValue() const noexcept { return u.key_val; }
  Value& getKeyValue() noexcept { return u.key_val; }
  void setKeyValue(const Value& v) noexcept { u.key_val = v; }

  // Key type checks
  bool isKeyNil() const noexcept {
    return novariant(u.key_tt) == LUA_TNIL;
  }

  bool isKeyInteger() const noexcept {
    return u.key_tt == LuaT::NUMINT;
  }

  bool isKeyShrStr() const noexcept {
    return u.key_tt == ctb(LuaT::SHRSTR);
  }

  bool isKeyDead() const noexcept {
    return novariant(u.key_tt) == LUA_TDEADKEY;
  }

  bool isKeyCollectable() const noexcept {
    return (static_cast<int>(u.key_tt) & BIT_ISCOLLECTABLE) != 0;
  }

  // Key value getters (typed)
  lua_Integer getKeyIntValue() const noexcept {
    return u.key_val.i;
  }

  TString* getKeyStrValue() const noexcept {
    return reinterpret_cast<TString*>(u.key_val.gc);
  }

  GCObject* getKeyGC() const noexcept {
    return u.key_val.gc;
  }

  GCObject* getKeyGCOrNull() const noexcept {
    return isKeyCollectable() ? u.key_val.gc : nullptr;
  }

  // Key setters
  void setKeyNil() noexcept {
    u.key_tt = LuaT::NIL;
  }

  void setKeyDead() noexcept {
    u.key_tt = static_cast<LuaT>(LUA_TDEADKEY);
  }

  // Copy TValue to key
  void setKey(const TValue* obj) noexcept {
    u.key_val = obj->getValue();
    u.key_tt = obj->getType();
  }

  // Copy key to TValue
  void getKey(lua_State* L, TValue* obj) const noexcept {
    obj->valueField() = u.key_val;
    obj->setType(u.key_tt);
    (void)L; // checkliveness removed to avoid forward declaration issues
  }
};



// Table inherits from GCBase (CRTP)
class Table : public GCBase<Table> {
private:
  mutable lu_byte flags;  /* 1<<p means tagmethod(p) is not present (mutable for metamethod caching) */
  lu_byte logSizeOfNodeArray;  /* log2 of number of slots of 'node' array */
  unsigned int asize;  /* number of slots in 'array' array */
  Value *array;  /* array part */
  Node *node;
  Table *metatable;
  GCObject *gclist;

public:
  // Constructor - initializes all fields to safe defaults
  Table() noexcept
    : flags(0), logSizeOfNodeArray(0), asize(0), array(nullptr),
      node(nullptr), metatable(nullptr), gclist(nullptr) {
  }

  // Destructor - trivial (GC handles deallocation)
  ~Table() noexcept = default;

  // Placement new operator - integrates with Lua's GC (implemented in lgc.h)
  static void* operator new(size_t size, lua_State* L, LuaT tt);

  // Disable regular new/delete (must use placement new with GC)
  static void* operator new(size_t) = delete;
  static void operator delete(void*) = delete;

  // Inline accessors
  lu_byte getFlags() const noexcept { return flags; }
  void setFlags(lu_byte f) noexcept { flags = f; }

  // Flags field bit manipulation methods (const - flags is mutable)
  void setFlagBits(int mask) const noexcept { flags |= cast_byte(mask); }
  void clearFlagBits(int mask) const noexcept { flags &= cast_byte(~mask); }

  // Flags field reference accessor (for backward compatibility)
  lu_byte& getFlagsRef() noexcept { return flags; }

  lu_byte getLogSizeOfNodeArray() const noexcept { return logSizeOfNodeArray; }
  void setLogSizeOfNodeArray(lu_byte ls) noexcept { logSizeOfNodeArray = ls; }

  unsigned int arraySize() const noexcept { return asize; }
  void setArraySize(unsigned int sz) noexcept { asize = sz; }

  Value* getArray() noexcept { return array; }
  const Value* getArray() const noexcept { return array; }
  void setArray(Value* arr) noexcept { array = arr; }

  // std::span accessors for array part
  std::span<Value> getArraySpan() noexcept {
    return std::span(array, asize);
  }
  std::span<const Value> getArraySpan() const noexcept {
    return std::span(array, asize);
  }

  Node* getNodeArray() noexcept { return node; }
  const Node* getNodeArray() const noexcept { return node; }
  void setNodeArray(Node* n) noexcept { node = n; }

  unsigned int nodeSize() const noexcept { return (1u << logSizeOfNodeArray); }
  Table* getMetatable() const noexcept { return metatable; }
  void setMetatable(Table* mt) noexcept { metatable = mt; }

  GCObject* getGclist() const noexcept { return gclist; }
  void setGclist(GCObject* gc) noexcept { gclist = gc; }
  // For GC gray list traversal - allows efficient list manipulation
  GCObject** getGclistPtr() noexcept { return &gclist; }

  // Flag operations (inline for performance)
  // Note: BITDUMMY = (1 << 6), defined in ltable.h
  bool isDummy() const noexcept { return (flags & (1 << 6)) != 0; }
  void setDummy() noexcept { flags |= (1 << 6); }
  void setNoDummy() noexcept { flags &= cast_byte(~(1 << 6)); }
  // invalidateTMCache uses maskflags from ltm.h, so can't inline here - use macro instead

  // Additional table helper methods
  unsigned int allocatedNodeSize() const noexcept {
    return isDummy() ? 0 : nodeSize();
  }

  unsigned int* getLenHint() noexcept {
    return static_cast<unsigned int*>(static_cast<void*>(array));
  }

  const unsigned int* getLenHint() const noexcept {
    return static_cast<const unsigned int*>(static_cast<const void*>(array));
  }

  LuaT* getArrayTag(lua_Unsigned k) noexcept {
    return reinterpret_cast<LuaT*>(static_cast<lu_byte*>(static_cast<void*>(array)) + sizeof(unsigned) + k);
  }

  const LuaT* getArrayTag(lua_Unsigned k) const noexcept {
    return reinterpret_cast<const LuaT*>(static_cast<const lu_byte*>(static_cast<const void*>(array)) + sizeof(unsigned) + k);
  }

  Value* getArrayVal(lua_Unsigned k) noexcept {
    return array - 1 - k;
  }

  const Value* getArrayVal(lua_Unsigned k) const noexcept {
    return array - 1 - k;
  }

  static constexpr unsigned int powerOfTwo(unsigned int x) noexcept {
    return (1u << x);
  }

  // Node accessors
  Node* getNode(unsigned int i) noexcept { return &node[i]; }
  const Node* getNode(unsigned int i) const noexcept { return &node[i]; }

  // Method declarations (implemented in ltable.cpp)
  [[nodiscard]] LuaT get(const TValue* key, TValue* res) const;
  [[nodiscard]] LuaT getInt(lua_Integer key, TValue* res) const;
  [[nodiscard]] LuaT getShortStr(TString* key, TValue* res) const;
  [[nodiscard]] LuaT getStr(TString* key, TValue* res) const;
  [[nodiscard]] TValue* HgetShortStr(TString* key) const;

  [[nodiscard]] int pset(const TValue* key, TValue* val);
  [[nodiscard]] int psetInt(lua_Integer key, TValue* val);
  [[nodiscard]] int psetShortStr(TString* key, TValue* val);
  [[nodiscard]] int psetStr(TString* key, TValue* val);

  void set(lua_State* L, const TValue* key, TValue* value);
  void setInt(lua_State* L, lua_Integer key, TValue* value);
  void finishSet(lua_State* L, const TValue* key, TValue* value, int hres);

  void resize(lua_State* L, unsigned newArraySize, unsigned newHashSize);
  void resizeArray(lua_State* L, unsigned newArraySize);
  [[nodiscard]] lu_mem size() const;
  [[nodiscard]] int tableNext(lua_State* L, StkId key) const;  // renamed from next() to avoid conflict with GC field
  [[nodiscard]] lua_Unsigned getn(lua_State* L);

  // Factory and helper methods
  [[nodiscard]] static Table* create(lua_State* L);  // Factory method (replaces luaH_new)
  void destroy(lua_State* L);  // Explicit destructor (replaces luaH_free)
  [[nodiscard]] Node* mainPosition(const TValue* key) const;  // replaces luaH_mainposition

  // Hot-path fast access methods (defined in lobject.h due to TMS dependency)
  inline void fastGeti(lua_Integer k, TValue* res, LuaT& tag) noexcept;
  inline void fastSeti(lua_Integer k, TValue* val, int& hres) noexcept;
};


/* }================================================================== */


// Note: Returns Node* even for const Table* to support Lua's read/write lookup semantics
inline Node* gnode(Table* t, unsigned int i) noexcept { return t->getNode(i); }
inline Node* gnode(const Table* t, unsigned int i) noexcept {
  return const_cast<Table*>(t)->getNode(i);  /* Lookup functions need mutable access */
}
inline TValue* gval(Node* n) noexcept { return n->getValue(); }
inline const TValue* gval(const Node* n) noexcept { return n->getValue(); }
// gnext returns reference to allow modification
inline int& gnext(Node* n) noexcept { return n->getNext(); }
inline int gnext(const Node* n) noexcept { return n->getNext(); }


/*
** Clear all bits of fast-access metamethods, which means that the table
** may have any of these metamethods. (First access that fails after the
** clearing will set the bit again.)
** Note: invalidateTMcache is now an inline function defined in ltm.h
*/


/*
** Bit BITDUMMY set in 'flags' means the table is using the dummy node
** for its hash part.
*/

inline constexpr lu_byte BITDUMMY = (1 << 6);
inline constexpr lu_byte NOTBITDUMMY = cast_byte(~BITDUMMY);



// Definitions moved after farr2val and fval2arr are defined (see below)


/* results from pset */
inline constexpr int HOK = 0;
inline constexpr int HNOTFOUND = 1;
inline constexpr int HNOTATABLE = 2;
inline constexpr int HFIRSTNODE = 3;

/*
** 'luaH_get*' operations set 'res', unless the value is absent, and
** return the tag of the result.
** The 'luaH_pset*' (pre-set) operations set the given value and return
** HOK, unless the original value is absent. In that case, if the key
** is really absent, they return HNOTFOUND. Otherwise, if there is a
** slot with that key but with no value, 'luaH_pset*' return an encoding
** of where the key is (usually called 'hres'). (pset cannot set that
** value because there might be a metamethod.) If the slot is in the
** hash part, the encoding is (HFIRSTNODE + hash index); if the slot is
** in the array part, the encoding is (~array index), a negative value.
** The value HNOTATABLE is used by the fast macros to signal that the
** value being indexed is not a table.
** (The size for the array part is limited by the maximum power of two
** that fits in an unsigned integer; that is INT_MAX+1. So, the C-index
** ranges from 0, which encodes to -1, to INT_MAX, which encodes to
** INT_MIN. The size of the hash part is limited by the maximum power of
** two that fits in a signed integer; that is (INT_MAX+1)/2. So, it is
** safe to add HFIRSTNODE to any index there.)
*/


/*
** The array part of a table is represented by an inverted array of
** values followed by an array of tags, to avoid wasting space with
** padding. In between them there is an unsigned int, explained later.
** The 'array' pointer points between the two arrays, so that values are
** indexed with negative indices and tags with non-negative indices.

             Values                              Tags
  --------------------------------------------------------
  ...  |   Value 1     |   Value 0     |unsigned|0|1|...
  --------------------------------------------------------
                                       ^ t->array

** All accesses to 't->array' should be through Table methods getArrayTag()
** and getArrayVal().
*/


/*
** The unsigned between the two arrays is used as a hint for #t;
** see luaH_getn. It is stored there to avoid wasting space in
** the structure Table for tables with no array part.
*/


/*
** Move TValues to/from arrays, using C indices
*/
inline void arr2obj(const Table* h, lua_Unsigned k, TValue* val) noexcept {
  val->setType(*h->getArrayTag(k));
  val->valueField() = *h->getArrayVal(k);
}

inline void obj2arr(Table* h, lua_Unsigned k, const TValue* val) noexcept {
  *h->getArrayTag(k) = val->getType();
  *h->getArrayVal(k) = val->getValue();
}

/*
** Often, we need to check the tag of a value before moving it. The
** following inline functions also move TValues to/from arrays, but receive the
** precomputed tag value or address as an extra argument.
*/
inline void farr2val(const Table* h, lua_Unsigned k, LuaT tag, TValue* res) noexcept {
  res->setType(tag);
  res->valueField() = *h->getArrayVal(k);
}

inline void fval2arr(Table* h, lua_Unsigned k, LuaT* tag, const TValue* val) noexcept {
  *tag = val->getType();
  *h->getArrayVal(k) = val->getValue();
}

/*
** Hot-path table access inline methods
** Implementations moved to lobject.h (after ltm.h include) to access TMS enum
*/

#if defined(LUA_DEBUG)
// For debug builds only - mainPosition is already a Table method
LUAI_FUNC Node *luaH_mainposition (const Table *t, const TValue *key);
#endif


#endif
