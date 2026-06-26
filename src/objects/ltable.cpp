/*
** Lua tables (hash)
** See Copyright Notice in lua.h
*/

#define LUA_CORE

#include "lprefix.h"


/*
** Implementation of tables (aka arrays, objects, or hash tables).
**
** DUAL-REPRESENTATION OPTIMIZATION:
** Tables keep elements in two parts: an array part and a hash part.
** - Array part: Dense storage for integer keys [1..n]
** - Hash part: Hash table for all other keys (strings, floats, negative ints, etc.)
**
** Non-negative integer keys are all candidates to be kept in the array
** part. The actual size of the array is the largest 'n' such that
** more than half the slots between 1 and n are in use.
**
** RATIONALE: Lua tables are used both as arrays and dictionaries. The dual
** representation provides O(1) access for both use cases:
** - Array-like tables: t[1], t[2], t[3]... use direct array indexing
** - Dictionary-like tables: t["key"], t[obj]... use hash table lookup
** - Mixed tables: Both parts are used simultaneously
**
** HASH COLLISION RESOLUTION:
** Hash uses a mix of chained scatter table with Brent's variation.
** A main invariant of these tables is that, if an element is not
** in its main position (i.e. the 'original' position that its hash gives
** to it), then the colliding element is in its own main position.
** Hence even when the load factor reaches 100%, performance remains good.
**
** COLLISION ALGORITHM (Brent's variation):
** When inserting key K that collides with existing key C:
** 1. If C is in its main position: move K to next free slot, chain via 'next'
** 2. If C is NOT in its main position: move C to free slot, put K in C's place
** This minimizes chain lengths by preferring to displace colliding keys
** rather than the new key.
**
** PERFORMANCE:
** - Array access: O(1) with no hashing overhead
** - Hash access: O(1) average, good cache locality due to Brent's variation
** - Resize: Amortized O(1) per insertion over many operations
*/

#include <algorithm>
#include <cmath>
#include <climits>
#include <cstdint>
#include <cstring>

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "lvirtualmachine.h"
#include "lvm.h"


/*
** Only hash parts with at least 2^LIMFORLAST have a 'lastfree' field
** that optimizes finding a free slot. That field is stored just before
** the array of nodes, in the same block. Smaller tables do a complete
** search when looking for a free slot.
*/
inline constexpr int LIMFORLAST = 3;  // log2 of real limit (8)

/*
** The union 'Limbox' stores 'lastfree' and ensures that what follows it
** is properly aligned to store a Node.
*/
typedef struct { Node *dummy; Node follows_pNode; } Limbox_aux;

typedef union {
  Node *lastfree;
  char padding[offsetof(Limbox_aux, follows_pNode)];
} Limbox;

/*
** NodeArray: Zero-overhead wrapper for hash table node storage.
** Provides encapsulated access to nodes and optional Limbox metadata.
** Layout: [Limbox?][Node array] - NodeArray* points to first Node.
**
** This is a zero-size helper class - it adds no memory overhead.
** It simply provides methods to access the memory layout.
**
** MEMORY LAYOUT ASSUMPTIONS (C++17 Standard Compliance):
** 1. Pointer arithmetic on char* is well-defined (C++17 §8.7 [expr.add])
** 2. reinterpret_cast from char* to properly aligned object pointer is valid
**    when the memory contains an object of that type (C++17 §8.2.10 [expr.reinterpret.cast])
** 3. Pointer subtraction (limbox - 1) is valid when both pointers point to
**    elements of the same array object or one past the end (C++17 §8.7 [expr.add])
** 4. The allocated block is large enough and properly aligned for both
**    Limbox and Node array, ensuring no undefined behavior.
*/
class NodeArray {
public:
  // Allocate node storage with optional Limbox metadata
  static Node* allocate(lua_State* L, unsigned int n, bool withLastfree) {
    if (withLastfree) {
      // Large table: allocate Limbox + Node[]
      // LAYOUT: [Limbox header][Node array of size n]
      // Verify no overflow in size calculation
      if (static_cast<size_t>(n) > (MAX_SIZET - sizeof(Limbox)) / sizeof(Node)) {
        luaG_runerror(L, "table size overflow");
      }
      size_t total = sizeof(Limbox) + n * sizeof(Node);
      char* block = luaM_newblock(L, total);
      // Verify alignment assumptions (critical for type punning safety)
      lua_assert(reinterpret_cast<uintptr_t>(block) % alignof(Limbox) == 0);
      lua_assert(reinterpret_cast<uintptr_t>(block + sizeof(Limbox)) % alignof(Node) == 0);
      // Limbox is at the start, nodes follow
      // Safe per C++17 §8.2.10: reinterpret_cast to properly aligned type
      Limbox* limbox = reinterpret_cast<Limbox*>(block);
      Node* nodeStart = reinterpret_cast<Node*>(block + sizeof(Limbox));
      // Initialize Limbox
      limbox->lastfree = nodeStart + n;  // all positions are free
      return nodeStart;
    } else {
      // Small table: just Node[] (no Limbox)
      return luaM_newvector<Node>(L, n);
    }
  }

  // Access lastfree from node pointer (only valid if table has Limbox)
  static Node*& getLastFree(Node* nodeStart) {
    // lastfree is stored in Limbox before the nodes
    // Safe per C++17 §8.7: pointer arithmetic within allocated block
    // nodeStart points to element after Limbox, so (nodeStart - 1) conceptually
    // points to the Limbox (treating the block as Limbox array for arithmetic purposes)
    Limbox* limbox = reinterpret_cast<Limbox*>(nodeStart) - 1;
    // Verify we're not accessing uninitialized memory
    lua_assert(limbox->lastfree >= nodeStart);
    return limbox->lastfree;
  }
};

inline bool haslastfree(const Table* t) noexcept {
	return t->getLogSizeOfNodeArray() >= LIMFORLAST;
}

inline Node*& getlastfree(Table* t) noexcept {
	return NodeArray::getLastFree(t->getNodeArray());
}


/*
** MAXABITS is the largest integer such that 2^MAXABITS fits in an
** unsigned int.
*/
inline constexpr int MAXABITS = l_numbits<int>() - 1;


/*
** MAXASIZEB is the maximum number of elements in the array part such
** that the size of the array fits in 'size_t'.
*/
inline constexpr size_t MAXASIZEB = MAX_SIZET / (sizeof(Value) + 1);


/*
** MAXASIZE is the maximum size of the array part. It is the minimum
** between 2^MAXABITS and MAXASIZEB.
*/
inline constexpr unsigned int MAXASIZE = ((1u << MAXABITS) < MAXASIZEB) ? (1u << MAXABITS) : cast_uint(MAXASIZEB);

/*
** MAXHBITS is the largest integer such that 2^MAXHBITS fits in a
** signed int.
*/
inline constexpr int MAXHBITS = MAXABITS - 1;


/*
** MAXHSIZE is the maximum size of the hash part. It is the minimum
** between 2^MAXHBITS and the maximum size such that, measured in bytes,
** it fits in a 'size_t'.
*/
inline constexpr size_t MAXHSIZE = luaM_limitN<Node>(1 << MAXHBITS);


/*
** When the original hash value is good, hashing by a power of 2
** avoids the cost of '%'.
*/
inline Node* hashpow2(Table* t, unsigned int n) noexcept {
	return gnode(t, lmod(n, t->nodeSize()));
}

inline Node* hashpow2(const Table* t, unsigned int n) noexcept {
	return gnode(t, lmod(n, t->nodeSize()));
}

/*
** for other types, it is better to avoid modulo by power of 2, as
** they can have many 2 factors.
*/
inline Node* hashmod(Table* t, lua_Unsigned n) noexcept {
	return gnode(t, cast_uint(n % ((t->nodeSize()-1u)|1u)));
}

inline Node* hashmod(const Table* t, lua_Unsigned n) noexcept {
	return gnode(t, cast_uint(n % ((t->nodeSize()-1u)|1u)));
}

inline Node* hashstr(Table* t, const TString* str) noexcept {
	return hashpow2(t, str->getHash());
}

inline Node* hashstr(const Table* t, const TString* str) noexcept {
	return hashpow2(t, str->getHash());
}

inline Node* hashboolean(Table* t, unsigned int p) noexcept {
	return hashpow2(t, p);
}

inline Node* hashboolean(const Table* t, unsigned int p) noexcept {
	return hashpow2(t, p);
}

template<typename T>
inline Node* hashpointer(Table* t, T* p) noexcept {
	return hashmod(t, point2uint(p));
}

template<typename T>
inline Node* hashpointer(const Table* t, T* p) noexcept {
	return hashmod(t, point2uint(p));
}


#define dummynode		(&dummynode_)

/*
** Common hash part for tables with empty hash parts. That allows all
** tables to have a hash part, avoiding an extra check ("is there a hash
** part?") when indexing. Its sole node has an empty value and a key
** (DEADKEY, nullptr) that is different from any valid TValue.
*/
static Node dummynode_ = Node(
  {nullptr}, LuaT::EMPTY,  // value's value and type
  static_cast<LuaT>(LUA_TDEADKEY), 0, {nullptr}  // key type, next, and key value
);


static TValue absentkey = {ABSTKEYCONSTANT};


/*
** Hash for integers. To allow a good hash, use the remainder operator
** ('%'). If integer fits as a non-negative int, compute an int
** remainder, which is faster. Otherwise, use an unsigned-integer
** remainder, which uses all bits and ensures a non-negative result.
*/
static Node *hashint (const Table& t, lua_Integer i) {
  lua_Unsigned ui = l_castS2U(i);
  if (ui <= cast_uint(std::numeric_limits<int>::max()))
    return gnode(&t, cast_uint(ui) % ((t.nodeSize()-1) | 1));
  else
    return hashmod(&t, ui);
}


/*
** Hash for floating-point numbers.
** The main computation should be just
**     n = frexp(n, &i); return (n * INT_MAX) + i
** but there are some numerical subtleties.
** In a two-complement representation, INT_MAX may not have an exact
** representation as a float, but INT_MIN does; because the absolute
** value of 'frexp' is smaller than 1 (unless 'n' is inf/NaN), the
** absolute value of the product 'frexp * -INT_MIN' is smaller or equal
** to INT_MAX. Next, the use of 'unsigned int' avoids overflows when
** adding 'i'; the use of '~u' (instead of '-u') avoids problems with
** INT_MIN.
*/
#if !defined(l_hashfloat)
static unsigned l_hashfloat (lua_Number n) {
  int i;
  n = l_mathop(frexp)(n, &i) * -cast_num(INT_MIN);
  lua_Integer ni;
  if (!lua_numbertointeger(n, &ni)) {  // is 'n' inf/-inf/NaN?
    lua_assert(luai_numisnan(n) || l_mathop(fabs)(n) == cast_num(HUGE_VAL));
    return 0;
  }
  else {  // normal case
    unsigned int u = cast_uint(i) + cast_uint(ni);
    return (u <= cast_uint(std::numeric_limits<int>::max()) ? u : ~u);
  }
}
#endif


/*
** returns the 'main' position of an element in a table (that is,
** the index of its hash value).
*/
static Node *mainpositionTV (const Table& t, const TValue *key) {
  switch (ttypetag(key)) {
    case LuaT::NUMINT: {
      lua_Integer i = ivalue(key);
      return hashint(t, i);
    }
    case LuaT::NUMFLT: {
      lua_Number n = fltvalue(key);
      return hashmod(&t, l_hashfloat(n));
    }
    case LuaT::SHRSTR: {
      TString *ts = tsvalue(key);
      return hashstr(&t, ts);
    }
    case LuaT::LNGSTR: {
      TString *ts = tsvalue(key);
      return hashpow2(&t, ts->hashLongStr());
    }
    case LuaT::VFALSE:
      return hashboolean(&t, 0);
    case LuaT::VTRUE:
      return hashboolean(&t, 1);
    case LuaT::LIGHTUSERDATA: {
      void *p = pvalue(key);
      return hashpointer(&t, p);
    }
    case LuaT::LCF: {
      lua_CFunction f = fvalue(key);
      return hashpointer(&t, f);
    }
    default: {
      GCObject *o = gcvalue(key);
      return hashpointer(&t, o);
    }
  }
}


static inline Node *mainpositionfromnode (const Table& t, Node *nd) {
  TValue key;
  nd->getKey(static_cast<lua_State*>(nullptr), &key);
  return mainpositionTV(t, &key);
}


/*
** Check whether key 'k1' is equal to the key in node 'n2'. This
** equality is raw, so there are no metamethods. Floats with integer
** values have been normalized, so integers cannot be equal to
** floats. It is assumed that 'shortStringsEqual' is simply pointer equality,
** so that short strings are handled in the default case.  The flag
** 'deadok' means to accept dead keys as equal to their original values.
** (Only collectable objects can produce dead keys.) Note that dead
** long strings are also compared by identity.  Once a key is dead,
** its corresponding value may be collected, and then another value
** can be created with the same address. If this other value is given
** to 'next', 'equalkey' will signal a false positive. In a regular
** traversal, this situation should never happen, as all keys given to
** 'next' came from the table itself, and therefore could not have been
** collected. Outside a regular traversal, we have garbage in, garbage
** out. What is relevant is that this false positive does not break
** anything.  (In particular, 'next' will return some other valid item
** on the table or nil.)
*/
static bool equalkey (const TValue *k1, const Node *n2, int deadok) {
  if (rawtt(k1) != n2->getKeyType()) {  // not the same variants?
    if (n2->isKeyShrStr() && ttislngstring(k1)) {
      // an external string can be equal to a short-string key
      return tsvalue(k1)->equals(n2->getKeyStrValue());
    }
    else if (deadok && n2->isKeyDead() && iscollectable(k1)) {
      // a collectable value can be equal to a dead key
      return gcvalue(k1) == gcvalueraw(n2->getKeyValue());
   }
   else
     return false;  // otherwise, different variants cannot be equal
  }
  else {  // equal variants
    switch (static_cast<int>(n2->getKeyType())) {
      case static_cast<int>(LuaT::NIL): case static_cast<int>(LuaT::VFALSE): case static_cast<int>(LuaT::VTRUE):
        return true;
      case static_cast<int>(LuaT::NUMINT):
        return (ivalue(k1) == n2->getKeyIntValue());
      case static_cast<int>(LuaT::NUMFLT):
        return luai_numeq(fltvalue(k1), fltvalueraw(n2->getKeyValue()));
      case static_cast<int>(LuaT::LIGHTUSERDATA):
        return pvalue(k1) == pvalueraw(n2->getKeyValue());
      case static_cast<int>(LuaT::LCF):
        return fvalue(k1) == fvalueraw(n2->getKeyValue());
      case static_cast<int>(ctb(LuaT::LNGSTR)):
        return tsvalue(k1)->equals(n2->getKeyStrValue());
      default:
        return gcvalue(k1) == gcvalueraw(n2->getKeyValue());
    }
  }
}


/*
** "Generic" get version. (Not that generic: not valid for integers,
** which may be in array part, nor for floats with integral values.)
** See explanation about 'deadok' in function 'equalkey'.
*/
static TValue *getgeneric (const Table& t, const TValue *key, int deadok) {
  Node *n = mainpositionTV(t, key);
  const Node *base = gnode(&t, 0);
  const Node *limit = base + t.nodeSize();
  for (;;) {  // check whether 'key' is somewhere in the chain
    if (equalkey(key, n, deadok))
      return gval(n);  // that's it
    else {
      int nextIndex = gnext(n);
      if (nextIndex == 0)
        return &absentkey;  // not found
      n += nextIndex;
      lua_assert(n >= base && n < limit);  // ensure we stay in bounds
    }
  }
}


/*
** Return the index 'k' (converted to an unsigned) if it is inside
** the range [1, limit].
*/
static unsigned checkrange (lua_Integer k, unsigned limit) {
  return (l_castS2U(k) - 1u < limit) ? cast_uint(k) : 0;
}


/*
** Return the index 'k' if 'k' is an appropriate key to live in the
** array part of a table, 0 otherwise.
*/
inline unsigned arrayindex(lua_Integer k) noexcept {
	return checkrange(k, MAXASIZE);
}


/*
** Check whether an integer key is in the array part of a table and
** return its index there, or zero.
*/
inline unsigned ikeyinarray(const Table* t, lua_Integer k) noexcept {
	return checkrange(k, t->arraySize());
}


/*
** Check whether a key is in the array part of a table and return its
** index there, or zero.
*/
static unsigned keyinarray (const Table& t, const TValue *key) {
  return (ttisinteger(key)) ? ikeyinarray(&t, ivalue(key)) : 0;
}


/*
** returns the index of a 'key' for table traversals. First goes all
** elements in the array part, then elements in the hash part. The
** beginning of a traversal is signaled by 0.
*/
static unsigned findindex (lua_State *L, const Table& t, TValue *key,
                               unsigned asize) {
  if (ttisnil(key)) return 0;  // first iteration
  unsigned int i = keyinarray(t, key);
  if (i != 0)  // is 'key' inside array part?
    return i;  // yes; that's the index
  else {
    const TValue *n = getgeneric(t, key, 1);
    if (l_unlikely(isabstkey(n)))
      luaG_runerror(L, "invalid key to 'next'");  // key not found
    // Calculate index in hash table with bounds checking
    const Node* node_ptr = reinterpret_cast<const Node*>(n);
    const Node* base = gnode(&t, 0);
    ptrdiff_t diff = node_ptr - base;
    lua_assert(diff >= 0 && static_cast<size_t>(diff) < t.nodeSize());
    i = cast_uint(diff);
    // hash elements are numbered after array ones
    return (i + 1) + asize;
  }
}


// Extra space in Node array if it has a lastfree entry
inline size_t extraLastfree(const Table* t) noexcept {
	return haslastfree(t) ? sizeof(Limbox) : 0;
}

// 'node' size in bytes
static size_t sizehash (const Table& t) {
  return cast_sizet(t.nodeSize()) * sizeof(Node) + extraLastfree(&t);
}


static void freehash (lua_State& L, Table& t) {
  if (!t.isDummy()) {
    // get pointer to the beginning of Node array
    char *arr = cast_charp(t.getNodeArray()) - extraLastfree(&t);
    luaM_freearray(&L, arr, sizehash(t));
  }
}


/*
** {=============================================================
** Rehash
** ==============================================================
*/

static int insertkey (Table& t, const TValue *key, TValue *value);
static void newcheckedkey (Table& t, const TValue *key, TValue *value);


/*
** Structure to count the keys in a table.
** 'total' is the total number of keys in the table.
** 'arrayCount' is the number of *array indices* in the table (see 'arrayindex').
** 'deleted' is true if there are deleted nodes in the hash part.
** 'nums' is a "count array" where 'nums[i]' is the number of integer
** keys between 2^(i - 1) + 1 and 2^i. Note that 'arrayCount' is the summation
** of 'nums'.
*/
typedef struct {
  unsigned total;
  unsigned arrayCount;
  int deleted;
  unsigned nums[MAXABITS + 1];
} Counters;


/*
** Check whether it is worth to use 'arrayCount' array entries instead of 'hashCount'
** hash nodes. (A hash node uses ~3 times more memory than an array
** entry: Two values plus 'next' versus one value.) Evaluate with size_t
** to avoid overflows.
*/
inline bool arrayXhash(unsigned arrayCount, unsigned hashCount) noexcept {
	return cast_sizet(arrayCount) <= cast_sizet(hashCount) * 3;
}

/*
** Compute the optimal size for the array part of table 't'.
** This size maximizes the number of elements going to the array part
** while satisfying the condition 'arrayXhash' with the use of memory if
** all those elements went to the hash part.
** 'ct->arrayCount' enters with the total number of array indices in the table
** and leaves with the number of keys that will go to the array part;
** return the optimal size for the array part.
*/
static unsigned computesizes (Counters *ct) {
  unsigned int accumulatedCount = 0;  // number of elements smaller than 2^i
  unsigned int arrayCount = 0;  // number of elements to go to array part
  unsigned int optimalSize = 0;  // optimal size for array part
  /* traverse slices while 'powerOfTwo' does not overflow and total of array
     indices still can satisfy 'arrayXhash' against the array size */
  for (unsigned int i = 0, powerOfTwo = 1;  // 2^i (candidate for optimal size)
       powerOfTwo > 0 && arrayXhash(powerOfTwo, ct->arrayCount);
       i++, powerOfTwo *= 2) {
    unsigned elementCount = ct->nums[i];
    accumulatedCount += elementCount;
    if (elementCount > 0 &&  // grows array only if it gets more elements...
        arrayXhash(powerOfTwo, accumulatedCount)) {  // ...while using "less memory"
      optimalSize = powerOfTwo;  // optimal size (till now)
      arrayCount = accumulatedCount;  // all elements up to 'optimalSize' will go to array part
    }
  }
  ct->arrayCount = arrayCount;
  return optimalSize;
}


static void countint (lua_Integer key, Counters *ct) {
  unsigned int k = arrayindex(key);
  if (k != 0) {  // is 'key' an array index?
    ct->nums[luaO_ceillog2(k)]++;  // count as such
    ct->arrayCount++;
  }
}


static inline int arraykeyisempty (const Table& t, unsigned key) {
  LuaT tag = *t.getArrayTag(key - 1);
  return tagisempty(tag);
}


/*
** Count keys in array part of table 't'.
*/
static void numusearray (const Table& t, Counters *ct) {
  unsigned int arrayUseCount = 0;  // summation of 'nums'
  unsigned int i = 1;  // index to traverse all array keys
  // traverse each slice
  unsigned int arraySize = t.arraySize();
  for (unsigned int logIndex = 0, powerOfTwo = 1; logIndex <= MAXABITS; logIndex++, powerOfTwo *= 2) {  // 2^logIndex
    unsigned int sliceCount = 0;  // counter
    unsigned int limit = powerOfTwo;
    if (limit > arraySize) {
      limit = arraySize;  // adjust upper limit
      if (i > limit)
        break;  // no more elements to count
    }
    // count elements in range (2^(logIndex - 1), 2^logIndex]
    for (; i <= limit; i++) {
      if (!arraykeyisempty(t, i))
        sliceCount++;
    }
    ct->nums[logIndex] += sliceCount;
    arrayUseCount += sliceCount;
  }
  ct->total += arrayUseCount;
  ct->arrayCount += arrayUseCount;
}


/*
** Count keys in hash part of table 't'. As this only happens during
** a rehash, all nodes have been used. A node can have a nil value only
** if it was deleted after being created.
*/
static void numusehash (const Table& t, Counters *ct) {
  unsigned i = t.nodeSize();
  unsigned totalNodes = 0;
  while (i--) {
    const Node *node = &t.getNodeArray()[i];
    if (isempty(gval(node))) {
      lua_assert(!node->isKeyNil());  // entry was deleted; key cannot be nil
      ct->deleted = 1;
    }
    else {
      totalNodes++;
      if (node->isKeyInteger())
        countint(node->getKeyIntValue(), ct);
    }
  }
  ct->total += totalNodes;
}


/*
** Convert an "abstract size" (number of slots in an array) to
** "concrete size" (number of bytes in the array).
*/
static size_t concretesize (unsigned int size) {
  if (size == 0)
    return 0;
  else {
    // space for the two arrays plus an unsigned in between
    size_t elemSize = sizeof(Value) + 1;
    // Check for overflow in multiplication
    if (wouldSizeMultiplyOverflow(size, elemSize))
      return 0;  // Signal overflow to caller
    return size * elemSize + sizeof(unsigned);
  }
}


/*
** Resize the array part of a table. If new size is equal to the old,
** do nothing. Else, if new size is zero, free the old array. (It must
** be present, as the sizes are different.) Otherwise, allocate a new
** array, move the common elements to new proper position, and then
** frees the old array.
** We could reallocate the array, but we still would need to move the
** elements to their new position, so the copy implicit in realloc is a
** waste. Moreover, most allocators will move the array anyway when the
** new size is double the old one (the most common case).
*/
static Value *resizearray (lua_State *L , Table& t,
                               unsigned oldArraySize,
                               unsigned newasize) {
  if (oldArraySize == newasize)
    return t.getArray();  // nothing to be done
  else if (newasize == 0) {  // erasing array?
    Value *op = t.getArray() - oldArraySize;  // original array's real address
    luaM_freemem(L, op, concretesize(oldArraySize));  // free it
    return nullptr;
  }
  else {
    size_t newasizeb = concretesize(newasize);
    Value *np = static_cast<Value*>(
                  static_cast<void*>(luaM_reallocvector<lu_byte>(L, nullptr, 0, newasizeb)));
    if (np == nullptr)  // allocation error?
      return nullptr;
    np += newasize;  // shift pointer to the end of value segment
    if (oldArraySize > 0) {
      // move common elements to new position
      size_t oldasizeb = concretesize(oldArraySize);
      Value *op = t.getArray();  // original array
      unsigned tomove = (oldArraySize < newasize) ? oldArraySize : newasize;
      size_t tomoveb = (oldArraySize < newasize) ? oldasizeb : newasizeb;
      lua_assert(tomoveb > 0);
      lua_assert(tomove <= newasize);  // ensure destination bounds
      lua_assert(tomove <= oldArraySize);  // ensure source bounds
      lua_assert(tomoveb <= newasizeb);  // verify size calculation
      memcpy(np - tomove, op - tomove, tomoveb);
      luaM_freemem(L, op - oldArraySize, oldasizeb);  // free old block
    }
    return np;
  }
}


/*
** Creates an array for the hash part of a table with the given
** size, or reuses the dummy node if size is zero.
** The computation for size overflow is in two steps: the first
** comparison ensures that the shift in the second one does not
** overflow.
*/
static void setnodevector (lua_State& L, Table& t, unsigned size) {
  if (size == 0) {  // no elements to hash part?
    t.setNodeArray(dummynode);  // use common 'dummynode'
    t.setLogSizeOfNodeArray(0);
    t.setDummy();  // signal that it is using dummy node
  }
  else {
    unsigned int lsize = luaO_ceillog2(size);
    if (lsize > MAXHBITS)
      luaG_runerror(&L, "table overflow");
    if ((1u << lsize) > MAXHSIZE)
      luaG_runerror(&L, "table overflow");
    size = Table::powerOfTwo(lsize);
    bool needsLastfree = (lsize >= LIMFORLAST);
    Node* nodes = NodeArray::allocate(&L, size, needsLastfree);
    t.setNodeArray(nodes);
    t.setLogSizeOfNodeArray(cast_byte(lsize));
    t.setNoDummy();
    for (unsigned int i = 0; i < size; i++) {
      Node *n = gnode(&t, i);
      gnext(n) = 0;
      n->setKeyNil();
      setempty(gval(n));
    }
  }
}


/*
** (Re)insert all elements from the hash part of 'ot' into table 't'.
*/
static void reinserthash (lua_State& L, Table& ot, Table& t) {
  unsigned size = ot.nodeSize();
  for (unsigned j = 0; j < size; j++) {
    Node *old = gnode(&ot, j);
    if (!isempty(gval(old))) {
      /* doesn't need barrier/invalidate cache, as entry was
         already present in the table */
      TValue k;
      old->getKey(&L, &k);
      newcheckedkey(t, &k, gval(old));
    }
  }
}


/*
** Exchange the hash part of 't1' and 't2'. (In 'flags', only the
** dummy bit must be exchanged: The 'isrealasize' is not related
** to the hash part, and the metamethod bits do not change during
** a resize, so the "real" table can keep their values.)
*/
static void exchangehashpart (Table& t1, Table& t2) {
  lu_byte logSizeOfNodeArray = t1.getLogSizeOfNodeArray();
  Node *node = t1.getNodeArray();
  int bitdummy1 = t1.getFlags() & BITDUMMY;
  t1.setLogSizeOfNodeArray(t2.getLogSizeOfNodeArray());
  t1.setNodeArray(t2.getNodeArray());
  t1.setFlags(cast_byte((t1.getFlags() & NOTBITDUMMY) | (t2.getFlags() & BITDUMMY)));
  t2.setLogSizeOfNodeArray(logSizeOfNodeArray);
  t2.setNodeArray(node);
  t2.setFlags(cast_byte((t2.getFlags() & NOTBITDUMMY) | bitdummy1));
}


/*
** Re-insert into the new hash part of a table the elements from the
** vanishing slice of the array part.
*/
static void reinsertOldSlice (Table& t, unsigned oldArraySize,
                                        unsigned newasize) {
  for (unsigned i = newasize; i < oldArraySize; i++) {  // traverse vanishing slice
    LuaT tag = *t.getArrayTag(i);
    if (!tagisempty(tag)) {  // a non-empty entry?
      TValue key, aux;
      key.setInt(l_castU2S(i) + 1);  // make the key
      farr2val(&t, i, tag, &aux);  // copy value into 'aux'
      insertkey(t, &key, &aux);  // insert entry into the hash part
    }
  }
}


/*
** Clear new slice of the array.
*/
static void clearNewSlice (Table& t, unsigned oldArraySize, unsigned newasize) {
  for (; oldArraySize < newasize; oldArraySize++)
    *t.getArrayTag(oldArraySize) = LuaT::EMPTY;
}


/*
** Resize table 't' for the new given sizes. Both allocations (for
** the hash part and for the array part) can fail, which creates some
** subtleties. If the first allocation, for the hash part, fails, an
** error is raised and that is it. Otherwise, it copies the elements from
** the shrinking part of the array (if it is shrinking) into the new
** hash. Then it reallocates the array part.  If that fails, the table
** is in its original state; the function frees the new hash part and then
** raises the allocation error. Otherwise, it sets the new hash part
** into the table, initializes the new part of the array (if any) with
** nils and reinserts the elements of the old hash back into the new
** parts of the table.
** Note that if the new size for the array part ('newArraySize') is equal to
** the old one ('oldArraySize'), this function will do nothing with that
** part.
*/
/*
** Resize a table to the given array and hash sizes.
**
** PARAMETERS:
** - newArraySize: New size for the array part
** - newHashSize: New size for the hash part (number of hash nodes)
**
** ALGORITHM:
** 1. Allocate new hash part (into temporary 'newt')
** 2. If array is shrinking, move displaced elements to new hash
** 3. Allocate new array part
** 4. Swap hash parts (t gets new hash, newt gets old hash)
** 5. Re-insert all elements from old hash into new structure
** 6. Free old hash part
**
** COMPLEXITY:
** O(n) where n is the total number of elements. Every element may need
** to be re-hashed or moved between array and hash parts.
**
** EXCEPTION SAFETY:
** If allocation fails, the table remains in its original state (no partial update).
** The temporary 'newt' structure allows us to prepare the new parts before
** committing to the change.
**
** PHASE 30 ENCAPSULATION:
** This function now uses Table accessor methods (setArray, setArraySize, etc.)
** instead of direct field access, following the encapsulation modernization.
*/
/*
** Rehash a table. First, count its keys. If there are array indices
** outside the array part, compute the new best size for that part.
** Then, resize the table.
**
** PARAMETERS:
** - ek: Extra key being inserted (triggers the rehash)
**
** ALGORITHM:
** 1. Count all existing keys by logarithmic buckets (powers of 2)
** 2. Determine optimal array size using ">50% full" heuristic
** 3. Compute hash size as (total keys - array keys)
** 4. Add 25% padding if table has had deletions (to avoid thrashing)
** 5. Call luaH_resize with computed sizes
**
** ARRAY SIZE OPTIMIZATION:
** The array part size is chosen to be the largest power of 2 such that
** more than 50% of slots in range [1..n] are occupied. This balances:
** - Memory usage: Don't waste space on mostly-empty arrays
** - Access speed: Keep frequently used integer indices in O(1) array part
**
** Example: keys {1,2,3,10,11,12}
** - Size 16: only 6/16 = 37.5% full → too sparse
** - Size 8: only 6/8 = 75% full → chosen (>50%)
** - Size 4: only 3/4 = 75% full, but doesn't fit all keys
**
** DELETION PADDING:
** If the table has undergone deletions, we add 25% extra hash capacity.
** This prevents resize thrashing in insert-delete-insert patterns.
** Trade-off: Uses more memory to avoid repeated O(n) rehashing.
*/
static void rehash (lua_State *L, Table& t, const TValue *extraKey) {
  Counters counters;
  // reset counts
  std::fill_n(counters.nums, MAXABITS + 1, 0);
  counters.arrayCount = 0;
  counters.deleted = 0;
  counters.total = 1;  // count extra key
  if (ttisinteger(extraKey))
    countint(ivalue(extraKey), &counters);  // extra key may go to array
  numusehash(t, &counters);  // count keys in hash part
  unsigned arraySize;  // optimal size for array part
  if (counters.arrayCount == 0) {
    // no new keys to enter array part; keep it with the same size
    arraySize = t.arraySize();
  }
  else {  // compute best size for array part
    numusearray(t, &counters);  // count keys in array part
    arraySize = computesizes(&counters);  // compute new size for array part
  }
  // all keys not in the array part go to the hash part
  unsigned hashSize = counters.total - counters.arrayCount;  // size for the hash part
  if (counters.deleted) {  // table has deleted entries?
    /* insertion-deletion-insertion: give hash some extra size to
       avoid repeated resizings */
    hashSize += hashSize >> 2;
  }
  // resize the table to new computed sizes
  t.resize(L, arraySize, hashSize);
}

/*
** }=============================================================
*/


static Node *getfreepos (Table& t) {
  if (haslastfree(&t)) {  // does it have 'lastfree' information?
    // look for a spot before 'lastfree', updating 'lastfree'
    while (getlastfree(&t) > t.getNodeArray()) {
      Node *freeNode = --getlastfree(&t);
      if (freeNode->isKeyNil())
        return freeNode;
    }
  }
  else {  // no 'lastfree' information
    unsigned i = t.nodeSize();
    while (i > 0) {  // do a linear search
      i--;
      Node *freeNode = gnode(&t, i);
      if (freeNode->isKeyNil())
        return freeNode;
    }
  }
  return nullptr;  // could not find a free place
}



/*
** Inserts a new key into a hash table; first, check whether key's main
** position is free. If not, check whether colliding node is in its main
** position or not: if it is not, move colliding node to an empty place
** and put new key in its main position; otherwise (colliding node is in
** its main position), new key goes to an empty position. Return 0 if
** could not insert key (could not find a free space).
*/
static int insertkey (Table& t, const TValue *key, TValue *value) {
  Node *mainPositionNode = mainpositionTV(t, key);
  // table cannot already contain the key
  lua_assert(isabstkey(getgeneric(t, key, 0)));
  if (!isempty(gval(mainPositionNode)) || t.isDummy()) {  // main position is taken?
    Node *freeNode = getfreepos(t);  // get a free place
    if (freeNode == nullptr)  // cannot find a free place?
      return 0;
    lua_assert(!t.isDummy());
    Node *collidingNode = mainpositionfromnode(t, mainPositionNode);
    if (collidingNode != mainPositionNode) {  // is colliding node out of its main position?
      // yes; move colliding node into free position
      while (collidingNode + gnext(collidingNode) != mainPositionNode)  // find previous
        collidingNode += gnext(collidingNode);
      gnext(collidingNode) = cast_int(freeNode - collidingNode);  // rechain to point to 'freeNode'
      *freeNode = *mainPositionNode;  /* copy colliding node into free pos. (mainPositionNode->next also goes) */
      if (gnext(mainPositionNode) != 0) {
        gnext(freeNode) += cast_int(mainPositionNode - freeNode);  // correct 'next'
        gnext(mainPositionNode) = 0;  // now 'mainPositionNode' is free
      }
      setempty(gval(mainPositionNode));
    }
    else {  // colliding node is in its own main position
      // new node will go into free position
      if (gnext(mainPositionNode) != 0)
        gnext(freeNode) = cast_int((mainPositionNode + gnext(mainPositionNode)) - freeNode);  // chain new position
      else lua_assert(gnext(freeNode) == 0);
      gnext(mainPositionNode) = cast_int(freeNode - mainPositionNode);
      mainPositionNode = freeNode;
    }
  }
  mainPositionNode->setKey(key);
  lua_assert(isempty(gval(mainPositionNode)));
  *gval(mainPositionNode) = *value;
  return 1;
}


/*
** Insert a key in a table where there is space for that key, the
** key is valid, and the value is not nil.
*/
static void newcheckedkey (Table& t, const TValue *key, TValue *value) {
  unsigned i = keyinarray(t, key);
  if (i > 0)  // is key in the array part?
    obj2arr(&t, i - 1, value);  // set value in the array
  else {
    int done = insertkey(t, key, value);  // insert key in the hash part
    lua_assert(done);  // it cannot fail
    static_cast<void>(done);  // to avoid warnings
  }
}


static void luaH_newkey (lua_State *L, Table& t, const TValue *key,
                                                 TValue *value) {
  if (!ttisnil(value)) {  // do not insert nil values
    int done = insertkey(t, key, value);
    if (!done) {  // could not find a free place?
      rehash(L, t, key);  // grow table
      newcheckedkey(t, key, value);  // insert key in grown table
    }
    luaC_barrierback(L, obj2gco(&t), key);
    // for debugging only: any new key may force an emergency collection
    condchangemem(L, [](){}, [](){}, 1);
  }
}


static TValue *getintfromhash (const Table& t, lua_Integer key) {
  Node *n = hashint(t, key);
  lua_assert(!ikeyinarray(&t, key));
  for (;;) {  // check whether 'key' is somewhere in the chain
    if (n->isKeyInteger() && n->getKeyIntValue() == key)
      return gval(n);  // that's it
    else {
      int nextIndex = gnext(n);
      if (nextIndex == 0) break;
      n += nextIndex;
    }
  }
  return &absentkey;
}


static bool hashkeyisempty (Table& t, lua_Unsigned key) {
  const TValue *val = getintfromhash(t, l_castU2S(key));
  return isempty(val);
}


static LuaT finishnodeget (const TValue *val, TValue *res) {
  if (!ttisnil(val)) {
    *res = *val;
  }
  return ttypetag(val);
}


static TValue *Hgetlongstr (const Table& t, TString *key) {
  TValue ko;
  lua_assert(!strisshr(key));
  setsvalue(static_cast<lua_State*>(nullptr), &ko, key);
  return getgeneric(t, &ko, 0);  // for long strings, use generic case
}


static TValue *Hgetstr (const Table& t, TString *key) {
  if (strisshr(key))
    return t.HgetShortStr(key);
  else
    return Hgetlongstr(t, key);
}


/*
** When a 'pset' cannot be completed, this function returns an encoding
** of its result, to be used by 'luaH_finishset'.
*/
static int retpsetcode (Table& t, const TValue *slot) {
  if (isabstkey(slot))
    return HNOTFOUND;  // no slot with that key
  else  // return node encoded
    return cast_int((reinterpret_cast<const Node*>(slot) - t.getNodeArray())) + HFIRSTNODE;
}


static int finishnodeset (Table& t, TValue *slot, TValue *val) {
  if (!ttisnil(slot)) {
    *slot = *val;
    return HOK;  // success
  }
  else
    return retpsetcode(t, slot);
}


static bool rawfinishnodeset (TValue *slot, TValue *val) {
  if (isabstkey(slot))
    return false;  // no slot with that key
  else {
    *slot = *val;
    return true;  // success
  }
}


/*
** Try to find a boundary in the hash part of table 't'. From the
** caller, we know that 'asize + 1' is present. We want to find a larger
** key that is absent from the table, so that we can do a binary search
** between the two keys to find a boundary. We keep doubling 'j' until
** we get an absent index.  If the doubling would overflow, we try
** LUA_MAXINTEGER. If it is absent, we are ready for the binary search.
** ('j', being max integer, is larger or equal to 'i', but it cannot be
** equal because it is absent while 'i' is present.) Otherwise, 'j' is a
** boundary. ('j + 1' cannot be a present integer key because it is not
** a valid integer in Lua.)
** About 'rnd': If we used a fixed algorithm, a bad actor could fill
** a table with only the keys that would be probed, in such a way that
** a small table could result in a huge length. To avoid that, we use
** the state's seed as a source of randomness. For the first probe,
** we "randomly double" 'i' by adding to it a random number roughly its
** width.
*/
static lua_Unsigned hash_search (lua_State *L, Table& t, unsigned asize) {
  lua_Unsigned i = asize + 1;  // caller ensures t[i] is present
  unsigned rnd = G(L)->getSeed();
  int n = (asize > 0) ? luaO_ceillog2(asize) : 0;  // width of 'asize'
  lua_assert(n >= 0 && n < 32);  // ensure shift is safe (avoid UB)
  unsigned mask = (1u << n) - 1;  // 11...111 with the width of 'asize'
  unsigned incr = (rnd & mask) + 1;  // first increment (at least 1)
  lua_Unsigned j = (incr <= l_castS2U(LUA_MAXINTEGER) - i) ? i + incr : i + 1;
  rnd >>= n;  // used 'n' bits from 'rnd'
  while (!hashkeyisempty(t, j)) {  // repeat until an absent t[j]
    i = j;  // 'i' is a present index
    if (j <= l_castS2U(LUA_MAXINTEGER)/2 - 1) {
      lua_Unsigned old_j = j;
      j = j*2 + (rnd & 1);  // try again with 2j or 2j+1
      lua_assert(j > old_j && j <= l_castS2U(LUA_MAXINTEGER));  // no wrap
      rnd >>= 1;
    }
    else {
      j = LUA_MAXINTEGER;
      if (hashkeyisempty(t, j))  // t[j] not present?
        break;  // 'j' now is an absent index
      else  // weird case
        return j;  // well, max integer is a boundary...
    }
  }
  // i < j  &&  t[i] present  &&  t[j] absent
  while (j - i > 1u) {  // do a binary search between them
    lua_Unsigned m = (i + j) / 2;
    if (hashkeyisempty(t, m)) j = m;
    else i = m;
  }
  return i;
}


static unsigned int binsearch (Table& array, unsigned int i, unsigned int j) {
  lua_assert(i <= j);
  while (j - i > 1u) {  // binary search
    unsigned int m = (i + j) / 2;
    if (arraykeyisempty(array, m)) j = m;
    else i = m;
  }
  return i;
}


// return a border, saving it as a hint for next call
static lua_Unsigned newhint (Table& t, unsigned hint) {
  lua_assert(hint <= t.arraySize());
  *t.getLenHint() = hint;
  return hint;
}


/*
** Try to find a border in table 't'. (A 'border' is an integer index
** such that t[i] is present and t[i+1] is absent, or 0 if t[1] is absent,
** or 'maxinteger' if t[maxinteger] is present.)
** If there is an array part, try to find a border there. First try
** to find it in the vicinity of the previous result (hint), to handle
** cases like 't[#t + 1] = val' or 't[#t] = nil', that move the border
** by one entry. Otherwise, do a binary search to find the border.
** If there is no array part, or its last element is non empty, the
** border may be in the hash part.
*/

#if defined(LUA_DEBUG)

// export this function for the test library

// Wrapper for Table::mainPosition
Node *luaH_mainposition (const Table *t, const TValue *key) {
  return t->mainPosition(key);
}

#endif


/*
** Table method implementations
*/

LuaT Table::get(const TValue* key, TValue* res) const {
  const TValue *slot;
  switch (ttypetag(key)) {
    case LuaT::SHRSTR:
      slot = HgetShortStr(tsvalue(key));
      break;
    case LuaT::NUMINT:
      return getInt(ivalue(key), res);
    case LuaT::NIL:
      slot = &absentkey;
      break;
    case LuaT::NUMFLT: {
      lua_Integer k;
      if (VirtualMachine::flttointeger(fltvalue(key), &k, F2Imod::F2Ieq))  // integral index?
        return getInt(k, res);  // use specialized version
      // else...
    }  // FALLTHROUGH
    default:
      slot = getgeneric(*this, key, 0);
      break;
  }
  return finishnodeget(slot, res);
}

LuaT Table::getInt(lua_Integer key, TValue* res) const {
  unsigned k = ikeyinarray(this, key);
  if (k > 0) {
    LuaT tag = *this->getArrayTag(k - 1);
    if (!tagisempty(tag))
      farr2val(this, k - 1, tag, res);
    return tag;
  }
  else
    return finishnodeget(getintfromhash(*this, key), res);
}

LuaT Table::getShortStr(TString* key, TValue* res) const {
  return finishnodeget(HgetShortStr(key), res);
}

LuaT Table::getStr(TString* key, TValue* res) const {
  return finishnodeget(Hgetstr(*this, key), res);
}

TValue* Table::HgetShortStr(TString* key) const {
  Node *n = hashstr(this, key);
  lua_assert(strisshr(key));
  for (;;) {  // check whether 'key' is somewhere in the chain
    if (n->isKeyShrStr() && shortStringsEqual(n->getKeyStrValue(), key))
      return gval(n);  // that's it
    else {
      int nextIndex = gnext(n);
      if (nextIndex == 0)
        return &absentkey;  // not found
      n += nextIndex;
    }
  }
}

int Table::pset(const TValue* key, TValue* val) {
  switch (ttypetag(key)) {
    case LuaT::SHRSTR: return psetShortStr(tsvalue(key), val);
    case LuaT::NUMINT: {
      int hres;
      this->fastSeti(ivalue(key), val, hres);
      return hres;
    }
    case LuaT::NIL: return HNOTFOUND;
    case LuaT::NUMFLT: {
      lua_Integer k;
      if (VirtualMachine::flttointeger(fltvalue(key), &k, F2Imod::F2Ieq)) {  // integral index?
        int hres;
        this->fastSeti(k, val, hres);
        return hres;
      }
      // else...
    }  // FALLTHROUGH
    default:
      return finishnodeset(*this, getgeneric(*this, key, 0), val);
  }
}

int Table::psetInt(lua_Integer key, TValue* val) {
  lua_assert(!ikeyinarray(this, key));
  return finishnodeset(*this, getintfromhash(*this, key), val);
}

int Table::psetShortStr(TString* key, TValue* val) {
  TValue *slot = HgetShortStr(key);
  if (!ttisnil(slot)) {  // key already has a value? (all too common)
    *slot = *val;  /* update it */
    return HOK;  // done
  }
  else if (checknoTM(getMetatable(), TMS::TM_NEWINDEX)) {  // no metamethod?
    if (ttisnil(val))  // new value is nil?
      return HOK;  // done (value is already nil/absent)
    if (isabstkey(slot) &&  // key is absent?
       !(isblack(this) && iswhite(key))) {  // and don't need barrier?
      TValue tk;  // key as a TValue
      setsvalue(static_cast<lua_State*>(nullptr), &tk, key);
      if (insertkey(*this, &tk, val)) {  // insert key, if there is space
        invalidateTMcache(this);
        return HOK;
      }
    }
  }
  /* Else, either table has new-index metamethod, or it needs barrier,
     or it needs to rehash for the new key. In any of these cases, the
     operation cannot be completed here. Return a code for the caller. */
  return retpsetcode(*this, slot);
}

int Table::psetStr(TString* key, TValue* val) {
  if (strisshr(key))
    return psetShortStr(key, val);
  else
    return finishnodeset(*this, Hgetlongstr(*this, key), val);
}

void Table::set(lua_State* L, const TValue* key, TValue* value) {
  int hres = pset(key, value);
  if (hres != HOK)
    finishSet(L, key, value, hres);
}

void Table::setInt(lua_State* L, lua_Integer key, TValue* value) {
  unsigned ik = ikeyinarray(this, key);
  if (ik > 0)
    obj2arr(this, ik - 1, value);
  else {
    bool ok = rawfinishnodeset(getintfromhash(*this, key), value);
    if (!ok) {
      TValue k;
      k.setInt(key);
      luaH_newkey(L, *this, &k, value);
    }
  }
}

void Table::finishSet(lua_State* L, const TValue* key, TValue* value, int hres) {
  lua_assert(hres != HOK);
  if (hres == HNOTFOUND) {
    TValue aux;
    if (l_unlikely(ttisnil(key)))
      luaG_runerror(L, "table index is nil");
    else if (ttisfloat(key)) {
      lua_Number f = fltvalue(key);
      lua_Integer k;
      if (VirtualMachine::flttointeger(f, &k, F2Imod::F2Ieq)) {
        aux.setInt(k);  // key is equal to an integer
        key = &aux;  // insert it as an integer
      }
      else if (l_unlikely(luai_numisnan(f)))
        luaG_runerror(L, "table index is NaN");
    }
    else if (isextstr(key)) {  // external string?
      // If string is short, must internalize it to be used as table key
      TString *ts = tsvalue(key)->normalize(L);
      setsvalue2s(L, L->getTop().p, ts);  // anchor 'ts' (EXTRA_STACK)
      L->getStackSubsystem().push();
      luaH_newkey(L, *this, s2v(L->getTop().p - 1), value);
      L->getStackSubsystem().pop();
      return;
    }
    luaH_newkey(L, *this, key, value);
  }
  else if (hres > 0) {  // regular Node?
    *gval(gnode(this, static_cast<unsigned int>(hres - HFIRSTNODE))) = *value;
  }
  else {  // array entry
    hres = ~hres;  // real index
    obj2arr(this, cast_uint(hres), value);
  }
}

void Table::resize(lua_State* L, unsigned newArraySize, unsigned newHashSize) {
  if (newArraySize > MAXASIZE)
    luaG_runerror(L, "table overflow");
  // create new hash part with appropriate size into 'newt'
  Table newt;  // to keep the new hash part
  newt.setFlags(0);
  setnodevector(*L, newt, newHashSize);
  unsigned oldArraySize = this->arraySize();
  if (newArraySize < oldArraySize) {  // will array shrink?
    // re-insert into the new hash the elements from vanishing slice
    exchangehashpart(*this, newt);  // pretend table has new hash
    reinsertOldSlice(*this, oldArraySize, newArraySize);
    exchangehashpart(*this, newt);  // restore old hash (in case of errors)
  }
  // allocate new array
  Value *newarray = resizearray(L, *this, oldArraySize, newArraySize);
  if (l_unlikely(newarray == nullptr && newArraySize > 0)) {  // allocation failed?
    freehash(*L, newt);  // release new hash part
    luaM_error(L);  // raise error (with array unchanged)
  }
  // allocation ok; initialize new part of the array
  exchangehashpart(*this, newt);  // 't' has the new hash ('newt' has the old)
  this->setArray(newarray);  // set new array part
  this->setArraySize(newArraySize);
  if (newarray != nullptr)
    *this->getLenHint() = newArraySize / 2u;  /* set an initial hint */
  clearNewSlice(*this, oldArraySize, newArraySize);
  // re-insert elements from old hash part into new parts
  reinserthash(*L, newt, *this);  // 'newt' now has the old hash
  freehash(*L, newt);  // free old hash part
}

void Table::resizeArray(lua_State* L, unsigned newArraySize) {
  unsigned nsize = (this->isDummy()) ? 0 : this->nodeSize();
  this->resize(L, newArraySize, nsize);
}

lu_mem Table::size() const {
  lu_mem sz = static_cast<lu_mem>(sizeof(Table)) + concretesize(this->arraySize());
  if (!this->isDummy())
    sz += sizehash(*this);
  return sz;
}

int Table::tableNext(lua_State* L, StkId key) const {
  unsigned int arraysize = this->arraySize();
  unsigned int i = findindex(L, *this, s2v(key), arraysize);  // find original key
  for (; i < arraysize; i++) {  // try first array part
    LuaT tag = *this->getArrayTag(i);
    if (!tagisempty(tag)) {  // a non-empty entry?
      s2v(key)->setInt(cast_int(i) + 1);
      farr2val(this, i, tag, s2v(key + 1));
      return 1;
    }
  }
  for (i -= arraysize; i < this->nodeSize(); i++) {  // hash part
    if (!isempty(gval(gnode(this, i)))) {  // a non-empty entry?
      Node *n = gnode(this, i);
      n->getKey(L, s2v(key));
      L->getStackSubsystem().setSlot(key + 1, gval(n));
      return 1;
    }
  }
  return 0;  // no more elements
}

lua_Unsigned Table::getn(lua_State* L) {
  unsigned arraysize = this->arraySize();
  if (arraysize > 0) {  // is there an array part?
    const unsigned maxvicinity = 4;
    unsigned limit = *this->getLenHint();  // start with the hint
    if (limit == 0)
      limit = 1;  // make limit a valid index in the array
    if (arraykeyisempty(*this, limit)) {  // t[limit] empty?
      // there must be a border before 'limit'
      // look for a border in the vicinity of the hint
      for (unsigned i = 0; i < maxvicinity && limit > 1; i++) {
        limit--;
        if (!arraykeyisempty(*this, limit))
          return newhint(*this, limit);  // 'limit' is a border
      }
      // t[limit] still empty; search for a border in [0, limit)
      return newhint(*this, binsearch(*this, 0, limit));
    }
    else {  // 'limit' is present in table; look for a border after it
      // look for a border in the vicinity of the hint
      for (unsigned i = 0; i < maxvicinity && limit < arraysize; i++) {
        limit++;
        if (arraykeyisempty(*this, limit))
          return newhint(*this, limit - 1);  // 'limit - 1' is a border
      }
      if (arraykeyisempty(*this, arraysize)) {  // last element empty?
        // t[limit] not empty; search for a border in [limit, arraysize)
        return newhint(*this, binsearch(*this, limit, arraysize));
      }
    }
    // last element non empty; set a hint to speed up finding that again
    // (keys in the hash part cannot be hints)
    *this->getLenHint() = arraysize;
  }
  // no array part or t[arraysize] is not empty; check the hash part
  lua_assert(arraysize == 0 || !arraykeyisempty(*this, arraysize));
  if (this->isDummy() || hashkeyisempty(*this, arraysize + 1))
    return arraysize;  // 'arraysize + 1' is empty
  else  // 'arraysize + 1' is also non empty
    return hash_search(L, *this, arraysize);
}

// Factory pattern with placement new operator
Table* Table::create(lua_State* L) {
  // Use placement new operator - calls constructor from lobject.h
  Table *t = new (L, ctb(LuaT::TABLE)) Table();

  // Set non-default values
  t->setFlags(maskflags);  // table has no metamethod fields

  // Initialize node vector (needs L for allocation)
  setnodevector(*L, *t, 0);

  return t;
}

void Table::destroy(lua_State* L) {
  // Explicit destructor: free resources
  freehash(*L, *this);
  resizearray(L, *this, arraySize(), 0);
  luaM_free(L, this);
}

Node* Table::mainPosition(const TValue* key) const {
  return mainpositionTV(*this, key);
}
