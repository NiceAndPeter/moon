/*
** String table (keep all strings handled by Lua)
** See Copyright Notice in lua.h
*/

#ifndef lstring_h
#define lstring_h

#include <span>

#include "mobject_core.h"  // GCBase, TValue

// Forward declarations
struct moon_State;
class GlobalState;

/*
** Memory-allocation error message must be preallocated (it cannot
** be created after memory is exhausted)
*/
#define MEMERRMSG       "not enough memory"


/*
** Maximum length for short strings, that is, strings that are
** internalized. (Cannot be smaller than reserved words or tags for
** metamethods, as these strings must be internalized;
** #("function") = 8, #("__newindex") = 10.)
*/
#if !defined(MOONI_MAXSHORTLEN)
#define MOONI_MAXSHORTLEN	40
#endif


/*
** {==================================================================
** Strings
** ===================================================================
** Note: MOON_VSHRSTR, MOON_VLNGSTR now defined in ltvalue.h
*/

constexpr bool ttisstring(const TValue* o) noexcept { return checktype(o, MOON_TSTRING); }
constexpr bool ttisshrstring(const TValue* o) noexcept { return checktag(o, ctb(MoonT::SHRSTR)); }
constexpr bool ttislngstring(const TValue* o) noexcept { return checktag(o, ctb(MoonT::LNGSTR)); }

constexpr bool TValue::isString() const noexcept { return checktype(this, MOON_TSTRING); }
constexpr bool TValue::isShortString() const noexcept { return checktag(this, ctb(MoonT::SHRSTR)); }
constexpr bool TValue::isLongString() const noexcept { return checktag(this, ctb(MoonT::LNGSTR)); }

inline TString* tsvalue(const TValue* o) noexcept { return o->stringValue(); }


// Kinds of long strings (stored in 'shrlen')
inline constexpr int LSTRREG = -1;  // regular long string
inline constexpr int LSTRFIX = -2;  // fixed external long string
inline constexpr int LSTRMEM = -3;  // external long string with deallocation


/*
** Header for a string value.
*/
// TString inherits from GCBase (CRTP)
class TString : public GCBase<TString> {
private:
  lu_byte extra;  // reserved words for short strings; "has hash" for longs
  ls_byte shortLength;  // length for short strings, negative for long strings
  unsigned int hash;
  union {
    size_t longLength;  // length for long strings
    TString *hashNext;  // linked list for hash table
  } u;
  char *contents;  // pointer to content in long strings
  moon_Alloc falloc;  // deallocation function for external strings
  void *ud;  // user data for external strings

public:
  // Constructor - initializes only fields common to both short and long strings
  // For short strings: only fields up to 'u' exist (contents/falloc/ud are overlay for string data)
  // For long strings: all fields exist
  TString() noexcept
    : extra(0), shortLength(0), hash(0), u{0} {
    // Note: contents, falloc, ud are NOT initialized here!
    // They will be initialized by the caller only for long strings.
  }

  // Destructor - trivial (GC handles deallocation)
  // MUST be empty (not = default) because for short strings, not all fields exist in memory!
  ~TString() noexcept {}

  // Special placement new for variable-size objects
  // This is used when we need exact size control (for short strings)
  static void* operator new(size_t /*size*/, void* ptr) noexcept {
    return ptr;  // Just return the pointer, no allocation
  }

  // Placement new operator - integrates with Lua's GC (implemented in lgc.h)
  // Note: For TString, this may allocate less than sizeof(TString) for short strings!
  static void* operator new(size_t size, moon_State* L, MoonT tt, size_t extra = 0);

  // Disable regular new/delete (must use placement new with GC)
  static void* operator new(size_t) = delete;
  static void operator delete(void*) = delete;

  // Type checks
  bool isShort() const noexcept { return shortLength >= 0; }
  bool isLong() const noexcept { return shortLength < 0; }
  bool isExternal() const noexcept { return isLong() && shortLength != LSTRREG; }

  // Accessors
  size_t length() const noexcept {
    return isShort() ? static_cast<size_t>(shortLength) : u.longLength;
  }
  ls_byte getShrlen() const noexcept { return shortLength; }
  size_t getLnglen() const noexcept { return u.longLength; }
  unsigned int getHash() const noexcept { return hash; }
  lu_byte getExtra() const noexcept { return extra; }
  const char* c_str() const noexcept {
    return isShort() ? getContentsAddr() : contents;
  }
  char* getContentsPtr() noexcept { return isShort() ? getContentsAddr() : contents; }
  char* getContentsField() noexcept { return contents; }
  const char* getContentsField() const noexcept { return contents; }
  // For short strings: return address where inline string data starts (after 'u' union)
  // For long strings: would return same address (where contents pointer is stored)
  char* getContentsAddr() noexcept { return cast_charp(this) + contentsOffset(); }
  const char* getContentsAddr() const noexcept { return cast_charp(this) + contentsOffset(); }
  moon_Alloc getFalloc() const noexcept { return falloc; }
  void* getUserData() const noexcept { return ud; }

  // Setters
  void setExtra(lu_byte e) noexcept { extra = e; }
  void setShrlen(ls_byte len) noexcept { shortLength = len; }
  void setHash(unsigned int h) noexcept { hash = h; }
  void setLnglen(size_t len) noexcept { u.longLength = len; }
  void setContents(char* c) noexcept { contents = c; }
  void setFalloc(moon_Alloc f) noexcept { falloc = f; }
  void setUserData(void* data) noexcept { ud = data; }

  // Hash table operations
  TString* getNext() const noexcept { return u.hashNext; }
  void setNext(TString* next_str) noexcept { u.hashNext = next_str; }

  // Helper for offset calculations
  static constexpr size_t fallocOffset() noexcept {
    // Offset of falloc field accounting for alignment
    // Must include GCObject base!
    struct OffsetHelper {
      GCObject base;
      lu_byte extra;
      ls_byte shortLength;
      unsigned int hash;
      union { size_t longLength; TString* hashNext; } u;
      char* contents;
    };
    return sizeof(OffsetHelper);
  }

  static constexpr size_t contentsOffset() noexcept {
    // Offset of contents field accounting for GCObject base and alignment
    struct OffsetHelper {
      GCObject base;
      lu_byte extra;
      ls_byte shortLength;
      unsigned int hash;
      union { size_t longLength; TString* hashNext; } u;
    };
    return sizeof(OffsetHelper);
  }

  // Instance methods (implemented in lstring.cpp)
  [[nodiscard]] unsigned hashLongStr();
  [[nodiscard]] bool equals(const TString* other) const;
  void remove(moon_State* L);           // from moonS_remove
  [[nodiscard]] TString* normalize(moon_State* L);    // from moonS_normstr

  // Static helpers and factory methods (from moonS_*)
  [[nodiscard]] static unsigned computeHash(const char* str, size_t l, unsigned seed);
  [[nodiscard]] static unsigned computeHash(std::span<const char> str, unsigned seed);
  [[nodiscard]] static size_t calculateLongStringSize(size_t len, int kind);
  [[nodiscard]] static TString* create(moon_State* L, const char* str, size_t l);
  [[nodiscard]] static TString* create(moon_State* L, std::span<const char> str);
  [[nodiscard]] static TString* create(moon_State* L, const char* str);  // null-terminated
  [[nodiscard]] static TString* createLongString(moon_State* L, size_t l);
  [[nodiscard]] static TString* createExternal(moon_State* L, const char* s, size_t len,
                                  moon_Alloc falloc, void* ud);

  // Global string table management
  static void init(moon_State* L);
  static void resize(moon_State* L, unsigned int newsize);
  static void clearCache(GlobalState* g);

  // Comparison operator overloads (defined after l_strcmp declaration)
  friend bool operator<(const TString& l, const TString& r) noexcept;
  friend bool operator<=(const TString& l, const TString& r) noexcept;
  friend bool operator==(const TString& l, const TString& r) noexcept;
  friend bool operator!=(const TString& l, const TString& r) noexcept;
  friend struct TStringLayoutCheck;  // layout guard (offsetof needs private access)
};

// Layout guard: short strings store their data as an overlay that begins at
// contentsOffset() (computed from a mirror struct). That offset must match where
// the real 'contents' field sits, or short-string data and the long-string
// 'contents' pointer would disagree if a field were ever added/reordered.
struct TStringLayoutCheck {
  static_assert(TString::contentsOffset() == offsetof(TString, contents),
      "short-string overlay must start exactly at the 'contents' field");
};


// Check if string is short (wrapper for backward compatibility)
inline bool strisshr(const TString* tstring) noexcept { return tstring->isShort(); }

// Check if string is external (fixed or with custom deallocator)
inline bool isextstr(const TValue* v) noexcept {
	return ttislngstring(v) && tsvalue(v)->isExternal();
}

inline bool TValue::isExtString() const noexcept {
	return isLongString() && stringValue()->isExternal();
}

/*
** Get the actual string (array of bytes) from a 'TString'. (Generic
** version and specialized versions for long and short strings.)
*/
inline char* rawGetShortStringContents(TString* tstring) noexcept {
	return tstring->getContentsAddr();
}
inline const char* rawGetShortStringContents(const TString* tstring) noexcept {
	return tstring->getContentsAddr();
}

/*
** String accessor functions
** These provide type-safe access to string contents with assertions.
*/

// Get short string contents (asserts string is short)
inline char* getShortStringContents(TString* tstring) noexcept {
	moon_assert(tstring->isShort());
	return tstring->getContentsAddr();
}
inline const char* getShortStringContents(const TString* tstring) noexcept {
	moon_assert(tstring->isShort());
	return tstring->getContentsAddr();
}

// Get long string contents (asserts string is long)
inline char* getLongStringContents(TString* tstring) noexcept {
	moon_assert(tstring->isLong());
	return tstring->getContentsField();
}
inline const char* getLongStringContents(const TString* tstring) noexcept {
	moon_assert(tstring->isLong());
	return tstring->getContentsField();
}

// Get string contents (works for both short and long strings)
inline char* getStringContents(TString* tstring) noexcept {
	return tstring->getContentsPtr();
}
inline const char* getStringContents(const TString* tstring) noexcept {
	return tstring->c_str();
}


// get string length from 'TString *tstring'
inline size_t getStringLength(const TString* tstring) noexcept {
	return tstring->length();
}

/*
** Get string and length */
inline const char* getStringWithLength(const TString* tstring, size_t& len) noexcept {
	len = tstring->length();
	return tstring->c_str();
}

// }==================================================================


/*
** Size of a short TString: Size of the header plus space for the string
** itself (including final '\0').
*/
// Size of short string including the struct itself and string data
inline constexpr size_t sizestrshr(size_t l) noexcept {
	return TString::contentsOffset() + ((l) + 1) * sizeof(char);
}


// Create a new string from a string literal, computing length at compile time
template<size_t N>
inline TString* moonS_newliteral(moon_State *L, const char (&s)[N]) {
    return TString::create(L, s, N - 1);
}


/*
** test whether a string is a reserved word
*/
inline bool isreserved(const TString* s) noexcept {
	return strisshr(s) && (s)->getExtra() > 0;
}


/*
** equality for short strings, which are always internalized
*/
inline bool shortStringsEqual(const TString* a, const TString* b) noexcept {
	return check_exp((a)->getType() == ctb(MoonT::SHRSTR), (a) == (b));
}


// Non-TString functions
[[nodiscard]] MOONI_FUNC Udata *moonS_newudata (moon_State *L, size_t s, unsigned short nuvalue);

#endif
