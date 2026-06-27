/*
** String table (keeps all strings handled by Lua)
** See Copyright Notice in lua.h
*/

#define MOON_CORE

#include "mprefix.h"


#include <algorithm>
#include <cstring>

#include "moon.h"

#include "mdebug.h"
#include "mdo.h"
#include "mmem.h"
#include "mobject.h"
#include "mstate.h"
#include "mstring.h"
#include "../memory/mgc.h"


// Get offset of falloc field in TString
inline constexpr size_t tstringFallocOffset() noexcept {
  return TString::fallocOffset();
}

/*
** Maximum size for string table.
*/
inline constexpr int MAXSTRTB = cast_int(moonM_limitN<TString*>(std::numeric_limits<int>::max()));

/*
** Initial size for the string table (must be power of 2).
** The Lua core alone registers ~50 strings (reserved words +
** metaevent keys + a few others). Libraries would typically add
** a few dozens more.
*/
#if !defined(MINSTRTABSIZE)
#define MINSTRTABSIZE   128
#endif


// TString static method implementations

unsigned TString::computeHash(const char* str, size_t l, unsigned seed) {
  unsigned int h = seed ^ cast_uint(l);
  for (; l > 0; l--)
    h ^= ((h<<5) + (h>>2) + cast_byte(str[l - 1]));
  return h;
}

unsigned TString::computeHash(std::span<const char> str, unsigned seed) {
  return computeHash(str.data(), str.size(), seed);
}

size_t TString::calculateLongStringSize(size_t len, int kind) {
  switch (kind) {
    case LSTRREG:  // regular long string
      // don't need 'falloc'/'ud', but need space for content
      return tstringFallocOffset() + (len + 1) * sizeof(char);
    case LSTRFIX:  // fixed external long string
      // don't need 'falloc'/'ud'
      return tstringFallocOffset();
    default:  // external long string with deallocation
      moon_assert(kind == LSTRMEM);
      return sizeof(TString);
  }
}


static void tablerehash (TString **vect, unsigned int osize, unsigned int nsize) {
  unsigned int i;
  // clear new elements (only when growing)
  if (nsize > osize)
    std::fill_n(vect + osize, nsize - osize, nullptr);
  for (i = 0; i < osize; i++) {  // rehash old part of the array
    TString *p = vect[i];
    vect[i] = nullptr;
    while (p) {  // for each string in the list
      TString *hnext = p->getNext();  // save next
      unsigned int h = lmod(p->getHash(), nsize);  // new position
      p->setNext(vect[h]);  // chain it into array
      vect[h] = p;
      p = hnext;
    }
  }
}


void TString::resize(moon_State* L, unsigned int nsize) {
  StringTable *tb = G(L)->getStringTable();
  unsigned int osize = tb->getSize();
  TString **newvect;
  if (nsize < osize)  // shrinking table?
    tablerehash(tb->getHash(), osize, nsize);  // depopulate shrinking part
  newvect = moonM_reallocvector<TString*>(L, tb->getHash(), osize, nsize);
  if (l_unlikely(newvect == nullptr)) {  // reallocation failed?
    if (nsize < osize)  // was it shrinking table?
      tablerehash(tb->getHash(), nsize, osize);  // restore to original size
    // leave table as it was
  }
  else {  // allocation succeeded
    tb->setHash(newvect);
    tb->setSize(nsize);
    if (nsize > osize)
      tablerehash(newvect, osize, nsize);  // rehash for new size
  }
}


void TString::clearCache(GlobalState* g) {
  unsigned int i, j;
  for (i = 0; i < STRCACHE_N; i++)
    for (j = 0; j < STRCACHE_M; j++) {
      if (iswhite(g->getStrCache(i, j)))  // will entry be collected?
        g->setStrCache(i, j, g->getMemErrMsg());  // replace it with something fixed
    }
}


void TString::init(moon_State* L) {
  GlobalState *g = G(L);
  unsigned int i, j;
  StringTable *tb = G(L)->getStringTable();
  tb->setHash(moonM_newvector<TString*>(L, MINSTRTABSIZE));
  tablerehash(tb->getHash(), 0, MINSTRTABSIZE);  // clear array
  tb->setSize(MINSTRTABSIZE);
  // pre-create memory-error message
  g->setMemErrMsg(create(L, MEMERRMSG, sizeof(MEMERRMSG) - 1));
  obj2gco(g->getMemErrMsg())->fix(L);  // it should never be collected
  for (i = 0; i < STRCACHE_N; i++)  // fill cache with valid strings
    for (j = 0; j < STRCACHE_M; j++)
      g->setStrCache(i, j, g->getMemErrMsg());
}


/*
** Create a new string object of exactly 'totalsize' bytes.
**
** Strings are variable-length: a short string needs only
** contentsOffset() + length + 1 bytes, which can be smaller than sizeof(TString)
** (the long-string-only fields are absent). We therefore allocate the exact
** size via moonC_newobj() rather than a fixed-size placement-new, and manually
** initialise only the header fields that exist in the allocated memory.
*/
static TString *createstrobj (moon_State *L, size_t totalsize, MoonT tag,
                              unsigned h) {
  moon_assert(totalsize >= TString::contentsOffset());

  GCObject *o = moonC_newobj(*L, tag, totalsize);
  TString *tstring = gco2ts(o);

  // Manually initialize fields (can't use constructor - it might write to all fields)
  // Only initialize fields that actually exist in the allocated memory
  tstring->setExtra(0);
  tstring->setShrlen(0);
  tstring->setHash(h);
  tstring->setLnglen(0);  // Zero-initialize union

  // For long strings, only initialize fields that actually exist in the allocated memory
  // LSTRFIX: allocates 40 bytes (up to but not including falloc)
  // LSTRREG: allocates 40 + string length (up to but not including falloc)
  // LSTRMEM: allocates full sizeof(TString) = 56 bytes (includes falloc and ud)
  if (tag == MoonT::LNGSTR) {
    tstring->setContents(nullptr);
    // DON'T initialize falloc/ud here - they may not exist in allocated memory!
    // They will be initialized by the caller if needed (e.g., moonS_newextlstr for LSTRMEM)
  }

  return tstring;
}


TString* TString::createLongString(moon_State* L, size_t l) {
  size_t totalsize = calculateLongStringSize(l, LSTRREG);
  TString *tstring = createstrobj(L, totalsize, ctb(MoonT::LNGSTR), G(L)->getSeed());
  tstring->setLnglen(l);
  tstring->setShrlen(LSTRREG);  // signals that it is a regular long string
  tstring->setContents(cast_charp(tstring) + tstringFallocOffset());
  tstring->getContentsField()[l] = '\0';  // ending 0
  return tstring;
}


static void growstrtab (moon_State *L, StringTable *tb) {
  if (l_unlikely(tb->getNumElements() == std::numeric_limits<int>::max())) {  // too many strings?
    moonC_fullgc(*L, 1);  // try to free some...
    if (tb->getNumElements() == std::numeric_limits<int>::max())  // still too many?
      moonM_error(L);  // cannot even create a message...
  }
  if (tb->getSize() <= MAXSTRTB / 2)  // can grow string table?
    TString::resize(L, tb->getSize() * 2);
}


/*
** Checks whether short string exists and reuses it or creates a new one.
*/
static TString *internshrstr (moon_State *L, const char *str, size_t l) {
  TString *tstring;
  GlobalState *g = G(L);
  StringTable *tb = g->getStringTable();
  unsigned int h = TString::computeHash(str, l, g->getSeed());
  TString **list = &tb->getHash()[lmod(h, tb->getSize())];
  moon_assert(str != nullptr);  // otherwise 'memcmp'/'memcpy' are undefined
  for (tstring = *list; tstring != nullptr; tstring = tstring->getNext()) {
    if (l == cast_uint(tstring->getShrlen()) &&
        (memcmp(str, getShortStringContents(tstring), l * sizeof(char)) == 0)) {
      // found!
      if (isdead(g, tstring))  // dead (but not collected yet)?
        changewhite(tstring);  // resurrect it
      return tstring;
    }
  }
  // else must create a new string
  if (tb->getNumElements() >= tb->getSize()) {  // need to grow string table?
    growstrtab(L, tb);
    list = &tb->getHash()[lmod(h, tb->getSize())];  // rehash with new size
  }
  size_t allocsize = sizestrshr(l);
  tstring = createstrobj(L, allocsize, ctb(MoonT::SHRSTR), h);
  tstring->setShrlen(static_cast<ls_byte>(l));
  getShortStringContents(tstring)[l] = '\0';  // ending 0
  std::copy_n(str, l, getShortStringContents(tstring));
  tstring->setNext(*list);
  *list = tstring;
  tb->incrementNumElements();
  return tstring;
}


TString* TString::create(moon_State* L, const char* str, size_t l) {
  if (l <= MOONI_MAXSHORTLEN)  // short string?
    return internshrstr(L, str, l);
  else {
    TString *tstring;
    if (l_unlikely(l * sizeof(char) >= (MAX_SIZE - sizeof(TString))))
      moonM_toobig(L);
    tstring = createLongString(L, l);
    std::copy_n(str, l, getLongStringContents(tstring));
    return tstring;
  }
}

TString* TString::create(moon_State* L, std::span<const char> str) {
  return create(L, str.data(), str.size());
}


TString* TString::create(moon_State* L, const char* str) {
  unsigned int i = point2uint(str) % STRCACHE_N;  // hash
  unsigned int j;
  GlobalState *g = G(L);
  for (j = 0; j < STRCACHE_M; j++) {
    if (strcmp(str, getStringContents(g->getStrCache(i, j))) == 0)  // hit?
      return g->getStrCache(i, j);  // that is it
  }
  // normal route
  for (j = STRCACHE_M - 1; j > 0; j--)
    g->setStrCache(i, j, g->getStrCache(i, j - 1));  // move out last element
  // new element is first in the list
  TString *newstr = create(L, str, strlen(str));
  g->setStrCache(i, 0, newstr);
  return newstr;
}


Udata *moonS_newudata (moon_State *L, size_t s, unsigned short nuvalue) {
  Udata *u;
  if (l_unlikely(s > MAX_SIZE - udatamemoffset(nuvalue)))
    moonM_toobig(L);

  // Calculate exact size needed
  size_t totalsize = sizeudata(nuvalue, s);

  // Allocate exactly what we need (may be less than sizeof(Udata) for small data)
  GCObject *o = moonC_newobj(*L, ctb(MoonT::USERDATA), totalsize);
  u = gco2u(o);

  // Manually initialize fields (can't use constructor reliably for variable-size objects)
  // For Udata0 (nuvalue==0): only has nuvalue, len, metatable, bindata (NO gclist!)
  // For Udata (nuvalue>0): has nuvalue, len, metatable, gclist, upvalue[]
  u->setNumUserValues(nuvalue);
  u->setLen(s);
  u->setMetatable(nullptr);

  // Only set gclist if the field actually exists in allocated memory!
  if (nuvalue > 0)
    u->setGclist(nullptr);

  // Initialize user values to nil
  for (int i = 0; i < nuvalue; i++)
    setnilvalue(&u->getUserValue(i)->value);
  return u;
}


struct NewExt {
  ls_byte kind;
  const char *s;
   size_t len;
  TString *tstring;  // output
};


static void f_newext (moon_State *L, void *ud) {
  NewExt *ne = static_cast<NewExt*>(ud);
  size_t size = TString::calculateLongStringSize(0, ne->kind);
  ne->tstring = createstrobj(L, size, ctb(MoonT::LNGSTR), G(L)->getSeed());
}


TString* TString::createExternal(moon_State* L, const char* s, size_t len,
                                  moon_Alloc falloc, void* ud) {
  struct NewExt ne;
  if (!falloc) {
    ne.kind = LSTRFIX;
    f_newext(L, &ne);  // just create header
  }
  else {
    ne.kind = LSTRMEM;
    if (L->rawRunProtected( f_newext, &ne) != MOON_OK) {  // mem. error?
      (*falloc)(ud, cast_voidp(s), len + 1, 0);  // free external string
      moonM_error(L);  // re-raise memory error
    }
    ne.tstring->setFalloc(falloc);
    ne.tstring->setUserData(ud);
  }
  ne.tstring->setShrlen(ne.kind);
  ne.tstring->setLnglen(len);
  ne.tstring->setContents(cast_charp(s));
  return ne.tstring;
}


/*
** TString method implementations
*/

unsigned TString::hashLongStr() {
  moon_assert(getType() == ctb(MoonT::LNGSTR));
  if (getExtra() == 0) {  // no hash?
    size_t len = getLnglen();
    setHash(computeHash(getLongStringContents(this), len, getHash()));
    setExtra(1);  // now it has its hash
  }
  return getHash();
}

bool TString::equals(const TString* other) const {
  size_t len1, len2;
  const char *s1 = getStringWithLength(this, len1);
  const char *s2 = getStringWithLength(other, len2);
  return ((len1 == len2) &&  // equal length and ...
          (memcmp(s1, s2, len1) == 0));  // equal contents
}

void TString::remove(moon_State* L) {
  StringTable *tb = G(L)->getStringTable();
  TString **p = &tb->getHash()[lmod(getHash(), tb->getSize())];
  while (*p != this)  // find previous element
    p = &(*p)->u.hashNext;
  *p = (*p)->u.hashNext;  /* remove element from its list */
  tb->decrementNumElements();
}

TString* TString::normalize(moon_State* L) {
  size_t len = u.longLength;
  if (len > MOONI_MAXSHORTLEN)
    return this;  // long string; keep the original
  else {
    const char *str = getLongStringContents(this);
    return internshrstr(L, str, len);
  }
}

