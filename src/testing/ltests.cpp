/*
** Internal Module for Debugging of the Lua Implementation
** See Copyright Notice in lua.h
*/

#define LUA_CORE

#include "lprefix.h"


#include <climits>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "lua.h"

// When assertions are disabled, many check functions have unused parameters/variables
#ifndef LUAI_ASSERT
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#include "lapi.h"
#include "lauxlib.h"
#include "lctype.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lmem.h"
#include "lopcodes.h"
#include "lopnames.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "lualib.h"
#include "LuaVector.h"
#include "../memory/lgc.h"



/*
** The whole module only makes sense with LUA_DEBUG on
*/
#if defined(LUA_DEBUG)


void *l_Trick = 0;


#define obj_at(L,k)	s2v(L->getCI()->funcRef().p + (k))


static int runC (lua_State *L, lua_State *L1, const char *pc);


static void setnameval (lua_State *L, const char *name, int val) {
  lua_pushinteger(L, val);
  lua_setfield(L, -2, name);
}


static void pushobject (lua_State *L, const TValue *o) {
  L->getStackSubsystem().setSlot(L->getTop().p, o);
  api_incr_top(L);
}


static void badexit (const char *fmt, const char *s1, const char *s2) {
  fprintf(stderr, fmt, s1);
  if (s2)
    fprintf(stderr, "extra info: %s\n", s2);
  // avoid assertion failures when exiting
  l_memcontrol.numblocks = l_memcontrol.total = 0;
  exit(EXIT_FAILURE);
}


static int tpanic (lua_State *L) {
  const char *msg = (lua_type(L, -1) == LUA_TSTRING)
                  ? lua_tostring(L, -1)
                  : "error object is not a string";
  return (badexit("PANIC: unprotected error in call to Lua API (%s)\n",
                   msg, nullptr),
          0);  // do not return to Lua
}


/*
** Warning function for tests. First, it concatenates all parts of
** a warning in buffer 'buff'. Then, it has three modes:
** - 0.normal: messages starting with '#' are shown on standard output;
** - other messages abort the tests (they represent real warning
** conditions; the standard tests should not generate these conditions
** unexpectedly);
** - 1.allow: all messages are shown;
** - 2.store: all warnings go to the global '_WARN';
*/
static void warnf (void *ud, const char *msg, int tocont) {
  lua_State *L = static_cast<lua_State*>(ud);
  static char buff[200] = "";  // should be enough for tests...
  static int onoff = 0;
  static int mode = 0;  // start in normal mode
  static int lasttocont = 0;
  if (!lasttocont && !tocont && *msg == '@') {  // control message?
    if (buff[0] != '\0')
      badexit("Control warning during warning: %s\naborting...\n", msg, buff);
    if (strcmp(msg, "@off") == 0)
      onoff = 0;
    else if (strcmp(msg, "@on") == 0)
      onoff = 1;
    else if (strcmp(msg, "@normal") == 0)
      mode = 0;
    else if (strcmp(msg, "@allow") == 0)
      mode = 1;
    else if (strcmp(msg, "@store") == 0)
      mode = 2;
    else
      badexit("Invalid control warning in test mode: %s\naborting...\n",
              msg, nullptr);
    return;
  }
  lasttocont = tocont;
  if (strlen(msg) >= sizeof(buff) - strlen(buff))
    badexit("warnf-buffer overflow (%s)\n", msg, buff);
  strcat(buff, msg);  // add new message to current warning
  if (!tocont) {  // message finished?
    lua_unlock(L);
    luaL_checkstack(L, 1, "warn stack space");
    lua_getglobal(L, "_WARN");
    if (!lua_toboolean(L, -1))
      lua_pop(L, 1);  // ok, no previous unexpected warning
    else {
      badexit("Unhandled warning in store mode: %s\naborting...\n",
              lua_tostring(L, -1), buff);
    }
    lua_lock(L);
    switch (mode) {
      case 0: {  // normal
        if (buff[0] != '#' && onoff)  // unexpected warning?
          badexit("Unexpected warning in test mode: %s\naborting...\n",
                  buff, nullptr);
      }  // FALLTHROUGH
      case 1: {  // allow
        if (onoff)
          fprintf(stderr, "Lua warning: %s\n", buff);  // print warning
        break;
      }
      case 2: {  // store
        lua_unlock(L);
        luaL_checkstack(L, 1, "warn stack space");
        lua_pushstring(L, buff);
        lua_setglobal(L, "_WARN");  // assign message to global '_WARN'
        lua_lock(L);
        break;
      }
    }
    buff[0] = '\0';  // prepare buffer for next warning
  }
}


/*
** {======================================================================
** Controlled version for realloc.
** =======================================================================
*/

#define MARK		0x55  // 01010101 (a nice pattern)

typedef union memHeader {
  LUAI_MAXALIGN;
  struct {
    size_t size;
    int type;
  } d;
} memHeader;


#if !defined(EXTERNMEMCHECK)

// full memory check
#define MARKSIZE	16  // size of marks after each block
#define fillmem(mem,size)	memset(mem, -MARK, size)

#else

// external memory check: don't do it twice
#define MARKSIZE	0
#define fillmem(mem,size)  // empty

#endif


Memcontrol l_memcontrol =
  {0, 0UL, 0UL, 0UL, 0UL, (~0UL),
   {0UL, 0UL, 0UL, 0UL, 0UL, 0UL, 0UL, 0UL, 0UL}};


static void freeblock (Memcontrol *mc, memHeader *block) {
  if (block) {
    size_t size = block->d.size;
    int i;
    for (i = 0; i < MARKSIZE; i++) {  // check marks after block
      lua_assert(*(cast_charp(block + 1) + size + i) == MARK);
    }
    mc->objcount[block->d.type]--;
    fillmem(block, sizeof(memHeader) + size + MARKSIZE);  // erase block
    free(block);  // actually free block
    mc->numblocks--;  // update counts
    mc->total -= size;
  }
}


void *debug_realloc (void *ud, void *b, size_t oldsize, size_t size) {
  Memcontrol *mc = static_cast<Memcontrol*>(ud);
  memHeader *block = static_cast<memHeader*>(b);
  int type;
  if (mc->memlimit == 0) {  // first time?
    char *limit = getenv("MEMLIMIT");  // initialize memory limit
    mc->memlimit = limit ? strtoul(limit, nullptr, 10) : ULONG_MAX;
  }
  if (block == nullptr) {
    type = (oldsize < LUA_NUMTYPES) ? cast_int(oldsize) : 0;
    oldsize = 0;
  }
  else {
    block--;  // go to real header
    type = block->d.type;
    lua_assert(oldsize == block->d.size);
  }
  if (size == 0) {
    freeblock(mc, block);
    return nullptr;
  }
  if (mc->failnext) {
    mc->failnext = 0;
    return nullptr;  // fake a single memory allocation error
  }
  if (mc->countlimit != ~0UL && size != oldsize) {  // count limit in use?
    if (mc->countlimit == 0)
      return nullptr;  // fake a memory allocation error
    mc->countlimit--;
  }
  if (size > oldsize && mc->total+size-oldsize > mc->memlimit)
    return nullptr;  // fake a memory allocation error
  else {
    memHeader *newblock;
    int i;
    size_t commonsize = (oldsize < size) ? oldsize : size;
    size_t realsize = sizeof(memHeader) + size + MARKSIZE;
    if (realsize < size) return nullptr;  // arithmetic overflow!
    newblock = static_cast<memHeader*>(malloc(realsize));  // alloc a new block
    if (newblock == nullptr)
      return nullptr;  // really out of memory?
    if (block) {
      memcpy(newblock + 1, block + 1, commonsize);  // copy old contents
      freeblock(mc, block);  // erase (and check) old copy
    }
    // initialize new part of the block with something weird
    fillmem(cast_charp(newblock + 1) + commonsize, size - commonsize);
    // initialize marks after block
    for (i = 0; i < MARKSIZE; i++)
      *(cast_charp(newblock + 1) + size + i) = MARK;
    newblock->d.size = size;
    newblock->d.type = type;
    mc->total += size;
    if (mc->total > mc->maxmem)
      mc->maxmem = mc->total;
    mc->numblocks++;
    mc->objcount[type]++;
    return newblock + 1;
  }
}


// }======================================================================



/*
** {=====================================================================
** Functions to check memory consistency.
** Most of these checks are done through asserts, so this code does
** not make sense with asserts off. For this reason, it uses 'assert'
** directly, instead of 'lua_assert'.
** ======================================================================
*/

#include <assert.h>

/*
** Check GC invariants. For incremental mode, a black object cannot
** point to a white one. For generational mode, really old objects
** cannot point to young objects. Both old1 and touched2 objects
** cannot point to new objects (but can point to survivals).
** (Threads and open upvalues, despite being marked "really old",
** continue to be visited in all collections, and therefore can point to
** new objects. They, and only they, are old but gray.)
*/
static bool testobjref1 (GlobalState *g, GCObject *f, GCObject *t) {
  if (isdead(g,t)) return false;
  if (g->isSweepPhase())
    return true;  // no invariants
  else if (g->getGCKind() != GCKind::GenerationalMinor)
    return !(isblack(f) && iswhite(t));  // basic incremental invariant
  else {  // generational mode
    if ((getage(f) == GCAge::Old && isblack(f)) && !isold(t))
      return false;
    if ((getage(f) == GCAge::Old1 || getage(f) == GCAge::Touched2) &&
         getage(t) == GCAge::New)
      return false;
    return true;
  }
}


static void printobj (GlobalState *g, GCObject *o) {
  printf("||%s(%p)-%c%c(%02X)||",
           ttypename(novariant(o->getType())), static_cast<void *>(o),
           isdead(g,o) ? 'd' : isblack(o) ? 'b' : iswhite(o) ? 'w' : 'g',
           "ns01oTt"[static_cast<size_t>(getage(o))], o->getMarked());
  if (o->getType() == ctb(LuaT::SHRSTR) || o->getType() == ctb(LuaT::LNGSTR))
    printf(" '%s'", getStringContents(gco2ts(o)));
}


void lua_printobj (lua_State *L, GCObject *o) {
  printobj(G(L), o);
}


void lua_printvalue (TValue *v) {
  switch (ttypetag(v)) {
    case LuaT::NUMINT: case LuaT::NUMFLT: {
      char buff[LUA_N2SBUFFSZ];
      unsigned len = luaO_tostringbuff(v, buff);
      buff[len] = '\0';
      printf("%s", buff);
      break;
    }
    case LuaT::SHRSTR:
      printf("'%s'", getStringContents(tsvalue(v))); break;
    case LuaT::LNGSTR:
      printf("'%.30s...'", getStringContents(tsvalue(v))); break;
    case LuaT::VFALSE:
      printf("%s", "false"); break;
    case LuaT::VTRUE:
      printf("%s", "true"); break;
    case LuaT::LIGHTUSERDATA:
      printf("light udata: %p", pvalue(v)); break;
    case LuaT::USERDATA:
      printf("full udata: %p", uvalue(v)); break;
    case LuaT::NIL:
      printf("nil"); break;
    case LuaT::LCF:
      printf("light C function: %p", fvalue(v)); break;
    case LuaT::CCL:
      printf("C closure: %p", clCvalue(v)); break;
    case LuaT::LCL:
      printf("Lua function: %p", clLvalue(v)); break;
    case LuaT::THREAD:
      printf("thread: %p", thvalue(v)); break;
    case LuaT::TABLE:
      printf("table: %p", hvalue(v)); break;
    default:
      lua_assert(0);
  }
}


static bool testobjref (GlobalState *g, GCObject *f, GCObject *t) {
  bool r1 = testobjref1(g, f, t);
  if (!r1) {
    printf("%d(%02X) - ", static_cast<int>(g->getGCState()), g->getCurrentWhite());
    printobj(g, f);
    printf("  ->  ");
    printobj(g, t);
    printf("\n");
  }
  return r1;
}


static void checkobjref (GlobalState *g, GCObject *f, GCObject *t) {
#ifdef LUAI_ASSERT
    assert(testobjref(g, f, t));
#else
    UNUSED(g); UNUSED(f); UNUSED(t);
#endif
}


/*
** Version where 't' can be nullptr. In that case, it should not apply the
** macro 'obj2gco' over the object. ('t' may have several types, so this
** definition must be a macro.)  Most checks need this version, because
** the check may run while an object is still being created.
*/
#define checkobjrefN(g,f,t)	{ if (t) checkobjref(g,f,obj2gco(t)); }


static void checkvalref (GlobalState *g, GCObject *f, const TValue *t) {
  assert(!iscollectable(t) || (righttt(t) && testobjref(g, f, gcvalue(t))));
}


static void checktable (GlobalState *g, Table *h) {
  unsigned int i;
  unsigned int asize = h->arraySize();
  Node *n, *limit = gnode(h, h->nodeSize());
  GCObject *hgc = obj2gco(h);
  checkobjrefN(g, hgc, h->getMetatable());
  for (i = 0; i < asize; i++) {
    TValue aux;
    arr2obj(h, i, &aux);
    checkvalref(g, hgc, &aux);
  }
  for (n = gnode(h, 0); n < limit; n++) {
    if (!isempty(gval(n))) {
      TValue k;
      n->getKey(mainthread(g), &k);
      assert(!n->isKeyNil());
      checkvalref(g, hgc, &k);
      checkvalref(g, hgc, gval(n));
    }
  }
}


static void checkudata (GlobalState *g, Udata *u) {
  int i;
  GCObject *hgc = obj2gco(u);
  checkobjrefN(g, hgc, u->getMetatable());
  for (i = 0; i < u->getNumUserValues(); i++)
    checkvalref(g, hgc, &u->getUserValue(i)->uv);
}


static void checkproto (GlobalState *g, Proto *f) {
  GCObject *fgc = obj2gco(f);
  checkobjrefN(g, fgc, f->getSource());
  for (const auto& constant : f->getConstantsSpan()) {
    if (iscollectable(&constant))
      checkobjref(g, fgc, gcvalue(&constant));
  }
  for (const auto& upval : f->getUpvaluesSpan())
    checkobjrefN(g, fgc, upval.getName());
  for (Proto* proto : f->getProtosSpan())
    checkobjrefN(g, fgc, proto);
  for (const auto& locvar : f->getDebugInfo().getLocVarsSpan())
    checkobjrefN(g, fgc, locvar.getVarName());
}


static void checkCclosure (GlobalState *g, CClosure *cl) {
  GCObject *clgc = obj2gco(cl);
  int i;
  for (i = 0; i < cl->getNumUpvalues(); i++)
    checkvalref(g, clgc, cl->getUpvalue(i));
}


static void checkLclosure (GlobalState *g, LClosure *cl) {
  GCObject *clgc = obj2gco(cl);
  int i;
  checkobjrefN(g, clgc, cl->getProto());
  for (i=0; i<cl->getNumUpvalues(); i++) {
    UpVal *uv = cl->getUpval(i);
    if (uv) {
      checkobjrefN(g, clgc, uv);
      if (!uv->isOpen())
        checkvalref(g, obj2gco(uv), uv->getVP());
    }
  }
}


static int lua_checkpc (CallInfo *callInfo) {
  if (!callInfo->isLua()) return 1;
  else {
    StkId f = callInfo->funcRef().p;
    Proto *p = clLvalue(s2v(f))->getProto();
    auto codeSpan = p->getCodeSpan();
    const Instruction* savedPC = callInfo->getSavedPC();
    return codeSpan.data() <= savedPC &&
           savedPC <= codeSpan.data() + codeSpan.size();
  }
}


static void check_stack (GlobalState *g, lua_State *L1) {
  StkId o;
  CallInfo *callInfo;
  UpVal *uv;
  assert(!isdead(g, L1));
  if (L1->getStack().p == nullptr) {  // incomplete thread?
    assert(L1->getOpenUpval() == nullptr && L1->getCI() == nullptr);
    return;
  }
  for (uv = L1->getOpenUpval(); uv != nullptr; uv = uv->getOpenNext())
    assert(uv->isOpen());  // must be open
  assert(L1->getTop().p <= L1->getStackLast().p);
  assert(L1->getTbclist().p <= L1->getTop().p);
  for (callInfo = L1->getCI(); callInfo != nullptr; callInfo = callInfo->getPrevious()) {
    assert(callInfo->topRef().p <= L1->getStackLast().p);
    assert(lua_checkpc(callInfo));
  }
  for (o = L1->getStack().p; o < L1->getStackLast().p; o++)
    checkliveness(L1, s2v(o));  // entire stack must have valid values
}


static void checkrefs (GlobalState *g, GCObject *o) {
  switch (static_cast<int>(o->getType())) {
    case static_cast<int>(ctb(LuaT::USERDATA)): {
      checkudata(g, gco2u(o));
      break;
    }
    case static_cast<int>(ctb(LuaT::UPVAL)): {
      checkvalref(g, o, gco2upv(o)->getVP());
      break;
    }
    case static_cast<int>(ctb(LuaT::TABLE)): {
      checktable(g, gco2t(o));
      break;
    }
    case static_cast<int>(ctb(LuaT::THREAD)): {
      check_stack(g, gco2th(o));
      break;
    }
    case static_cast<int>(ctb(LuaT::LCL)): {
      checkLclosure(g, gco2lcl(o));
      break;
    }
    case static_cast<int>(ctb(LuaT::CCL)): {
      checkCclosure(g, gco2ccl(o));
      break;
    }
    case static_cast<int>(ctb(LuaT::PROTO)): {
      checkproto(g, gco2p(o));
      break;
    }
    case static_cast<int>(ctb(LuaT::SHRSTR)):
    case static_cast<int>(ctb(LuaT::LNGSTR)): {
      assert(!isgray(o));  // strings are never gray
      break;
    }
    default: assert(0);
  }
}


/*
** Check consistency of an object:
** - Dead objects can only happen in the 'allgc' list during a sweep
** phase (controlled by the caller through 'maybedead').
** - During pause, all objects must be white.
** - In generational mode:
**   * objects must be old enough for their lists ('listage').
**   * old objects cannot be white.
**   * old objects must be black, except for 'touched1', 'old0',
**     threads, and open upvalues.
**   * 'touched1' objects must be gray.
*/
static void checkobject (GlobalState *g, GCObject *o, int maybedead,
                         GCAge listage) {
  if (isdead(g, o))
    assert(maybedead);
  else {
    assert(g->getGCState() != GCState::Pause || iswhite(o));
    if (g->getGCKind() == GCKind::GenerationalMinor) {  // generational mode?
      assert(getage(o) >= listage);
      if (isold(o)) {
        assert(!iswhite(o));
        assert(isblack(o) ||
        getage(o) == GCAge::Touched1 ||
        getage(o) == GCAge::Old0 ||
        o->getType() == ctb(LuaT::THREAD) ||
        (o->getType() == ctb(LuaT::UPVAL) && gco2upv(o)->isOpen()));
      }
      assert(getage(o) != GCAge::Touched1 || isgray(o));
    }
    checkrefs(g, o);
  }
}


static l_mem checkgraylist (GlobalState *g, GCObject *o) {
  int total = 0;  // count number of elements in the list
  cast_void(g);  // better to keep it if we need to print an object
  while (o) {
    assert(!!isgray(o) ^ (getage(o) == GCAge::Touched2));
    assert(!testbit(o->getMarked(), TESTBIT));
    if (g->keepInvariant())
      o->setMarkedBit(TESTBIT);  // mark that object is in a gray list
    total++;
    switch (static_cast<int>(o->getType())) {
      case static_cast<int>(ctb(LuaT::TABLE)): o = gco2t(o)->getGclist(); break;
      case static_cast<int>(ctb(LuaT::LCL)): o = gco2lcl(o)->getGclist(); break;
      case static_cast<int>(ctb(LuaT::CCL)): o = gco2ccl(o)->getGclist(); break;
      case static_cast<int>(ctb(LuaT::THREAD)): o = gco2th(o)->getGclist(); break;
      case static_cast<int>(ctb(LuaT::PROTO)): o = gco2p(o)->getGclist(); break;
      case static_cast<int>(ctb(LuaT::USERDATA)):
        assert(gco2u(o)->getNumUserValues() > 0);
        o = gco2u(o)->getGclist();
        break;
      default: assert(0);  // other objects cannot be in a gray list
    }
  }
  return total;
}


/*
** Check objects in gray lists.
*/
static l_mem checkgrays (GlobalState *g) {
  l_mem total = 0;  // count number of elements in all lists
  if (!g->keepInvariant()) return total;
  total += checkgraylist(g, g->getGray());
  total += checkgraylist(g, g->getGrayAgain());
  total += checkgraylist(g, g->getWeak());
  total += checkgraylist(g, g->getAllWeak());
  total += checkgraylist(g, g->getEphemeron());
  return total;
}


/*
** Check whether 'o' should be in a gray list. If so, increment
** 'count' and check its TESTBIT. (It must have been previously set by
** 'checkgraylist'.)
*/
static void incifingray (GlobalState *g, GCObject *o, l_mem *count) {
  if (!g->keepInvariant())
    return;  // gray lists not being kept in these phases
  if (o->getType() == ctb(LuaT::UPVAL)) {
    // only open upvalues can be gray
    assert(!isgray(o) || gco2upv(o)->isOpen());
    return;  // upvalues are never in gray lists
  }
  // these are the ones that must be in gray lists
  if (isgray(o) || getage(o) == GCAge::Touched2) {
    (*count)++;
    assert(testbit(o->getMarked(), TESTBIT));
    o->clearMarkedBit(TESTBIT);  // prepare for next cycle
  }
}


static l_mem checklist (GlobalState *g, int maybedead, int tof,
  GCObject *newl, GCObject *survival, GCObject *old, GCObject *reallyold) {
  GCObject *o;
  l_mem total = 0;  // number of object that should be in  gray lists
  for (o = newl; o != survival; o = o->getNext()) {
    checkobject(g, o, maybedead, GCAge::New);
    incifingray(g, o, &total);
    assert(!tof == !tofinalize(o));
  }
  for (o = survival; o != old; o = o->getNext()) {
    checkobject(g, o, 0, GCAge::Survival);
    incifingray(g, o, &total);
    assert(!tof == !tofinalize(o));
  }
  for (o = old; o != reallyold; o = o->getNext()) {
    checkobject(g, o, 0, GCAge::Old1);
    incifingray(g, o, &total);
    assert(!tof == !tofinalize(o));
  }
  for (o = reallyold; o != nullptr; o = o->getNext()) {
    checkobject(g, o, 0, GCAge::Old);
    incifingray(g, o, &total);
    assert(!tof == !tofinalize(o));
  }
  return total;
}


int lua_checkmemory (lua_State *L) {
  GlobalState *g = G(L);
  GCObject *o;
  int maybedead;
  l_mem totalin;  // total of objects that are in gray lists
  l_mem totalshould;  // total of objects that should be in gray lists
  if (g->keepInvariant()) {
    assert(!iswhite(mainthread(g)));
    assert(!iswhite(gcvalue(g->getRegistry())));
  }
  assert(!isdead(g, gcvalue(g->getRegistry())));
  assert(g->getSweepGC() == nullptr || g->isSweepPhase());
  totalin = checkgrays(g);

  // check 'fixedgc' list
  for (o = g->getFixedGC(); o != nullptr; o = o->getNext()) {
    assert(o->getType() == ctb(LuaT::SHRSTR) && isgray(o) && getage(o) == GCAge::Old);
  }

  // check 'allgc' list
  maybedead = (GCState::Atomic < g->getGCState() && g->getGCState() <= GCState::SweepAllGC);
  totalshould = checklist(g, maybedead, 0, g->getAllGC(),
                             g->getSurvival(), g->getOld1(), g->getReallyOld());

  // check 'finobj' list
  totalshould += checklist(g, 0, 1, g->getFinObj(),
                              g->getFinObjSur(), g->getFinObjOld1(), g->getFinObjROld());

  // check 'tobefnz' list
  for (o = g->getToBeFnz(); o != nullptr; o = o->getNext()) {
    checkobject(g, o, 0, GCAge::New);
    incifingray(g, o, &totalshould);
    assert(tofinalize(o));
    assert(o->getType() == ctb(LuaT::USERDATA) || o->getType() == ctb(LuaT::TABLE));
  }
  if (g->keepInvariant())
    assert(totalin == totalshould);
  return 0;
}

// }======================================================



/*
** {======================================================
** Disassembler
** =======================================================
*/


static char *buildop (Proto *p, int pc, char *buff) {
  char *obuff = buff;
  Instruction i = p->getCode()[pc];
  OpCode o = static_cast<OpCode>(InstructionView(i).opcode());
  const char *name = opnames[o];
  int line = luaG_getfuncline(p, pc);
  int lineinfo = (p->getLineInfo() != nullptr) ? p->getLineInfo()[pc] : 0;
  if (lineinfo == ABSLINEINFO)
    buff += sprintf(buff, "(__");
  else
    buff += sprintf(buff, "(%2d", lineinfo);
  buff += sprintf(buff, " - %4d) %4d - ", line, pc);
  switch (getOpMode(o)) {
    case OpMode::iABC:
      sprintf(buff, "%-12s%4d %4d %4d%s", name,
              InstructionView(i).a(), InstructionView(i).b(), InstructionView(i).c(),
              InstructionView(i).k() ? " (k)" : "");
      break;
    case OpMode::ivABC:
      sprintf(buff, "%-12s%4d %4d %4d%s", name,
              InstructionView(i).a(), InstructionView(i).vb(), InstructionView(i).vc(),
              InstructionView(i).k() ? " (k)" : "");
      break;
    case OpMode::iABx:
      sprintf(buff, "%-12s%4d %4d", name, InstructionView(i).a(), InstructionView(i).bx());
      break;
    case OpMode::iAsBx:
      sprintf(buff, "%-12s%4d %4d", name, InstructionView(i).a(), InstructionView(i).sbx());
      break;
    case OpMode::iAx:
      sprintf(buff, "%-12s%4d", name, InstructionView(i).ax());
      break;
    case OpMode::isJ:
      sprintf(buff, "%-12s%4d", name, InstructionView(i).sj());
      break;
  }
  return obuff;
}


static int listcode (lua_State *L) {
  int pc;
  Proto *p;
  luaL_argcheck(L, lua_isfunction(L, 1) && !lua_iscfunction(L, 1),
                 1, "Lua function expected");
  p = getproto(obj_at(L, 1));
  lua_newtable(L);
  setnameval(L, "maxstack", p->getMaxStackSize());
  setnameval(L, "numparams", p->getNumParams());
  pc = 0;
  for (const auto& instr : p->getCodeSpan()) {
    (void)instr;  // unused
    char buff[100];
    lua_pushinteger(L, pc+1);
    lua_pushstring(L, buildop(p, pc, buff));
    lua_settable(L, -3);
    pc++;
  }
  return 1;
}


static int printcode (lua_State *L) {
  int pc;
  Proto *p;
  luaL_argcheck(L, lua_isfunction(L, 1) && !lua_iscfunction(L, 1),
                 1, "Lua function expected");
  p = getproto(obj_at(L, 1));
  printf("maxstack: %d\n", p->getMaxStackSize());
  printf("numparams: %d\n", p->getNumParams());
  pc = 0;
  for (const auto& instr : p->getCodeSpan()) {
    (void)instr;  // unused
    char buff[100];
    printf("%s\n", buildop(p, pc, buff));
    pc++;
  }
  return 0;
}


static int listk (lua_State *L) {
  Proto *p;
  int i;
  luaL_argcheck(L, lua_isfunction(L, 1) && !lua_iscfunction(L, 1),
                 1, "Lua function expected");
  p = getproto(obj_at(L, 1));
  auto constantsSpan = p->getConstantsSpan();
  lua_createtable(L, static_cast<int>(constantsSpan.size()), 0);
  i = 0;
  for (const auto& constant : constantsSpan) {
    pushobject(L, &constant);
    lua_rawseti(L, -2, i+1);
    i++;
  }
  return 1;
}


static int listabslineinfo (lua_State *L) {
  Proto *p;
  int i;
  luaL_argcheck(L, lua_isfunction(L, 1) && !lua_iscfunction(L, 1),
                 1, "Lua function expected");
  p = getproto(obj_at(L, 1));
  luaL_argcheck(L, p->getAbsLineInfo() != nullptr, 1, "function has no debug info");
  auto absLineInfoSpan = p->getDebugInfo().getAbsLineInfoSpan();
  lua_createtable(L, 2 * static_cast<int>(absLineInfoSpan.size()), 0);
  i = 0;
  for (const auto& absline : absLineInfoSpan) {
    lua_pushinteger(L, absline.getPC());
    lua_rawseti(L, -2, 2 * i + 1);
    lua_pushinteger(L, absline.getLine());
    lua_rawseti(L, -2, 2 * i + 2);
    i++;
  }
  return 1;
}


static int listlocals (lua_State *L) {
  Proto *p;
  int pc = cast_int(luaL_checkinteger(L, 2)) - 1;
  int i = 0;
  const char *name;
  luaL_argcheck(L, lua_isfunction(L, 1) && !lua_iscfunction(L, 1),
                 1, "Lua function expected");
  p = getproto(obj_at(L, 1));
  while ((name = p->getLocalName(++i, pc)) != nullptr)
    lua_pushstring(L, name);
  return i-1;
}

// }======================================================



void lua_printstack (lua_State *L) {
  int i;
  int n = lua_gettop(L);
  printf("stack: >>\n");
  for (i = 1; i <= n; i++) {
    printf("%3d: ", i);
    lua_printvalue(s2v(L->getCI()->funcRef().p + i));
    printf("\n");
  }
  printf("<<\n");
}


int lua_printallstack (lua_State *L) {
  StkId p;
  int i = 1;
  CallInfo *callInfo = L->getBaseCI();
  printf("stack: >>\n");
  for (p = L->getStack().p; p < L->getTop().p; p++) {
    if (callInfo != nullptr && p == callInfo->funcRef().p) {
      printf("  ---\n");
      if (callInfo == L->getCI())
        callInfo = nullptr;  // printed last frame
      else
        callInfo = callInfo->getNext();
    }
    printf("%3d: ", i++);
    lua_printvalue(s2v(p));
    printf("\n");
  }
  printf("<<\n");
  return 0;
}


static int get_limits (lua_State *L) {
  lua_createtable(L, 0, 5);
  setnameval(L, "IS32INT", LUAI_IS32INT);
  setnameval(L, "MAXARG_Ax", MAXARG_Ax);
  setnameval(L, "MAXARG_Bx", MAXARG_Bx);
  setnameval(L, "OFFSET_sBx", OFFSET_sBx);
  setnameval(L, "NUM_OPCODES", NUM_OPCODES);
  return 1;
}


static int get_sizes (lua_State *L) {
  lua_newtable(L);
  setnameval(L, "Lua state", sizeof(lua_State));
  setnameval(L, "global state", sizeof(GlobalState));
  setnameval(L, "TValue", sizeof(TValue));
  setnameval(L, "Node", sizeof(Node));
  setnameval(L, "stack Value", sizeof(StackValue));
  return 1;
}


static int mem_query (lua_State *L) {
  if (lua_isnone(L, 1)) {
    lua_pushinteger(L, cast_Integer(l_memcontrol.total));
    lua_pushinteger(L, cast_Integer(l_memcontrol.numblocks));
    lua_pushinteger(L, cast_Integer(l_memcontrol.maxmem));
    return 3;
  }
  else if (lua_isnumber(L, 1)) {
    unsigned long limit = static_cast<unsigned long>(luaL_checkinteger(L, 1));
    if (limit == 0) limit = ULONG_MAX;
    l_memcontrol.memlimit = limit;
    return 0;
  }
  else {
    const char *t = luaL_checkstring(L, 1);
    int i;
    for (i = LUA_NUMTYPES - 1; i >= 0; i--) {
      if (strcmp(t, ttypename(i)) == 0) {
        lua_pushinteger(L, cast_Integer(l_memcontrol.objcount[i]));
        return 1;
      }
    }
    return luaL_error(L, "unknown type '%s'", t);
  }
}


static int alloc_count (lua_State *L) {
  if (lua_isnone(L, 1))
    l_memcontrol.countlimit = ULONG_MAX;
  else
    l_memcontrol.countlimit = static_cast<unsigned long>(luaL_checkinteger(L, 1));
  return 0;
}


static int alloc_failnext (lua_State *L) {
  UNUSED(L);
  l_memcontrol.failnext = 1;
  return 0;
}


static int settrick (lua_State *L) {
  if (ttisnil(obj_at(L, 1)))
    l_Trick = nullptr;
  else
    l_Trick = gcvalue(obj_at(L, 1));
  return 0;
}


static int gc_color (lua_State *L) {
  TValue *o;
  luaL_checkany(L, 1);
  o = obj_at(L, 1);
  if (!iscollectable(o))
    lua_pushstring(L, "no collectable");
  else {
    GCObject *obj = gcvalue(o);
    lua_pushstring(L, isdead(G(L), obj) ? "dead" :
                      iswhite(obj) ? "white" :
                      isblack(obj) ? "black" : "gray");
  }
  return 1;
}


static int gc_age (lua_State *L) {
  TValue *o;
  luaL_checkany(L, 1);
  o = obj_at(L, 1);
  if (!iscollectable(o))
    lua_pushstring(L, "no collectable");
  else {
    static const char *gennames[] = {"new", "survival", "old0", "old1",
                                     "old", "touched1", "touched2"};
    GCObject *obj = gcvalue(o);
    lua_pushstring(L, gennames[static_cast<size_t>(getage(obj))]);
  }
  return 1;
}


static int gc_printobj (lua_State *L) {
  TValue *o;
  luaL_checkany(L, 1);
  o = obj_at(L, 1);
  if (!iscollectable(o))
    printf("no collectable\n");
  else {
    GCObject *obj = gcvalue(o);
    printobj(G(L), obj);
    printf("\n");
  }
  return 0;
}


static const char *const statenames[] = {
  "propagate", "enteratomic", "atomic", "sweepallgc", "sweepfinobj",
  "sweeptobefnz", "sweepend", "callfin", "pause", ""};

static int gc_state (lua_State *L) {
  static const int states[] = {
    static_cast<int>(GCState::Propagate), static_cast<int>(GCState::EnterAtomic), static_cast<int>(GCState::Atomic), static_cast<int>(GCState::SweepAllGC), static_cast<int>(GCState::SweepFinObj),
    static_cast<int>(GCState::SweepToBeFnz), static_cast<int>(GCState::SweepEnd), static_cast<int>(GCState::CallFin), static_cast<int>(GCState::Pause), -1};
  int option = states[luaL_checkoption(L, 1, "", statenames)];
  GlobalState *g = G(L);
  if (option == -1) {
    lua_pushstring(L, statenames[static_cast<int>(g->getGCState())]);
    return 1;
  }
  else {
    if (g->getGCKind() != GCKind::Incremental)
      luaL_error(L, "cannot change states in generational mode");
    lua_lock(L);
    if (option < static_cast<int>(g->getGCState())) {  // must cross 'pause'?
      luaC_runtilstate(*L, GCState::Pause, 1);  // run until pause
    }
    luaC_runtilstate(*L, static_cast<GCState>(option), 0);  // do not skip propagation state
    lua_assert(static_cast<int>(g->getGCState()) == option);
    lua_unlock(L);
    return 0;
  }
}


static int tracinggc = 0;
void luai_tracegctest (lua_State *L, int first) {
  if (!tracinggc) return;
  else {
    GlobalState *g = G(L);
    lua_unlock(L);
    g->setGCStp(GCSTPGC);
    lua_checkstack(L, 10);
    lua_getfield(L, LUA_REGISTRYINDEX, "tracegc");
    lua_pushboolean(L, first);
    lua_call(L, 1, 0);
    g->setGCStp(0);
    lua_lock(L);
  }
}


static int tracegc (lua_State *L) {
  if (lua_isnil(L, 1))
    tracinggc = 0;
  else {
    tracinggc = 1;
    lua_setfield(L, LUA_REGISTRYINDEX, "tracegc");
  }
  return 0;
}


static int hash_query (lua_State *L) {
  if (lua_isnone(L, 2)) {
    TString *ts;
    luaL_argcheck(L, lua_type(L, 1) == LUA_TSTRING, 1, "string expected");
    ts = tsvalue(obj_at(L, 1));
    if (ts->getType() == ctb(LuaT::LNGSTR))
      (void)ts->hashLongStr();  // make sure long string has a hash - hash also stored in object
    lua_pushinteger(L, cast_int(ts->getHash()));
  }
  else {
    TValue *o = obj_at(L, 1);
    Table *t;
    luaL_checktype(L, 2, LUA_TTABLE);
    t = hvalue(obj_at(L, 2));
    lua_pushinteger(L, cast_Integer(t->mainPosition(o) - t->getNodeArray()));
  }
  return 1;
}


static int stacklevel (lua_State *L) {
  int a = 0;
  lua_pushinteger(L, cast_Integer(L->getTop().p - L->getStack().p));
  lua_pushinteger(L, L->getStackSize());
  lua_pushinteger(L, cast_Integer(L->getNumberOfCCalls()));
  lua_pushinteger(L, L->getNumberOfCallInfos());
  lua_pushinteger(L, (lua_Integer)(size_t)&a);
  return 5;
}


static int table_query (lua_State *L) {
  const Table *t;
  int i = cast_int(luaL_optinteger(L, 2, -1));
  unsigned int asize;
  luaL_checktype(L, 1, LUA_TTABLE);
  t = hvalue(obj_at(L, 1));
  asize = t->arraySize();
  if (i == -1) {
    lua_pushinteger(L, cast_Integer(asize));
    lua_pushinteger(L, cast_Integer(t->allocatedNodeSize()));
    lua_pushinteger(L, cast_Integer(asize > 0 ? *t->getLenHint() : 0));
    return 3;
  }
  else if (cast_uint(i) < asize) {
    lua_pushinteger(L, i);
    if (!tagisempty(*t->getArrayTag(cast_uint(i))))
      arr2obj(t, cast_uint(i), s2v(L->getTop().p));
    else
      setnilvalue(s2v(L->getTop().p));
    api_incr_top(L);
    lua_pushnil(L);
  }
  else if (cast_uint(i -= cast_int(asize)) < t->nodeSize()) {
    TValue k;
    gnode(t, cast_uint(i))->getKey(L, &k);
    if (!isempty(gval(gnode(t, cast_uint(i)))) ||
        ttisnil(&k) ||
        ttisnumber(&k)) {
      pushobject(L, &k);
    }
    else
      lua_pushliteral(L, "<undef>");
    if (!isempty(gval(gnode(t, cast_uint(i)))))
      pushobject(L, gval(gnode(t, cast_uint(i))));
    else
      lua_pushnil(L);
    lua_pushinteger(L, gnext(&t->getNodeArray()[cast_uint(i)]));
  }
  return 3;
}


static int gc_query (lua_State *L) {
  GlobalState *g = G(L);
  lua_pushstring(L, g->getGCKind() == GCKind::Incremental ? "inc"
                  : g->getGCKind() == GCKind::GenerationalMajor ? "genmajor"
                  : "genminor");
  lua_pushstring(L, statenames[static_cast<int>(g->getGCState())]);
  lua_pushinteger(L, cast_st2S(g->getTotalBytes()));
  lua_pushinteger(L, cast_st2S(g->getGCDebt()));
  lua_pushinteger(L, cast_st2S(g->getGCMarked()));
  lua_pushinteger(L, cast_st2S(g->getGCMajorMinor()));
  return 6;
}


static int test_codeparam (lua_State *L) {
  lua_Integer p = luaL_checkinteger(L, 1);
  lua_pushinteger(L, luaO_codeparam(cast_uint(p)));
  return 1;
}


static int test_applyparam (lua_State *L) {
  lua_Integer p = luaL_checkinteger(L, 1);
  lua_Integer x = luaL_checkinteger(L, 2);
  lua_pushinteger(L, cast_Integer(luaO_applyparam(cast_byte(p), x)));
  return 1;
}


static int string_query (lua_State *L) {
  StringTable *tb = G(L)->getStringTable();
  int s = cast_int(luaL_optinteger(L, 1, 0)) - 1;
  if (s == -1) {
    lua_pushinteger(L ,tb->getSize());
    lua_pushinteger(L ,tb->getNumElements());
    return 2;
  }
  else if (cast_uint(s) < tb->getSize()) {
    TString *ts;
    int n = 0;
    for (ts = tb->getHash()[s]; ts != nullptr; ts = ts->getNext()) {
      setsvalue2s(L, L->getTop().p, ts);
      api_incr_top(L);
      n++;
    }
    return n;
  }
  else return 0;
}


static int getreftable (lua_State *L) {
  if (lua_istable(L, 2))  // is there a table as second argument?
    return 2;  // use it as the table
  else
    return LUA_REGISTRYINDEX;  // default is to use the register
}


static int tref (lua_State *L) {
  int t = getreftable(L);
  int level = lua_gettop(L);
  luaL_checkany(L, 1);
  lua_pushvalue(L, 1);
  lua_pushinteger(L, luaL_ref(L, t));
  cast_void(level);  // to avoid warnings
  lua_assert(lua_gettop(L) == level+1);  // +1 for result
  return 1;
}


static int getref (lua_State *L) {
  int t = getreftable(L);
  int level = lua_gettop(L);
  lua_rawgeti(L, t, luaL_checkinteger(L, 1));
  cast_void(level);  // to avoid warnings
  lua_assert(lua_gettop(L) == level+1);
  return 1;
}

static int unref (lua_State *L) {
  int t = getreftable(L);
  int level = lua_gettop(L);
  luaL_unref(L, t, cast_int(luaL_checkinteger(L, 1)));
  cast_void(level);  // to avoid warnings
  lua_assert(lua_gettop(L) == level);
  return 0;
}


static int upvalue (lua_State *L) {
  int n = cast_int(luaL_checkinteger(L, 2));
  luaL_checktype(L, 1, LUA_TFUNCTION);
  if (lua_isnone(L, 3)) {
    const char *name = lua_getupvalue(L, 1, n);
    if (name == nullptr) return 0;
    lua_pushstring(L, name);
    return 2;
  }
  else {
    const char *name = lua_setupvalue(L, 1, n);
    lua_pushstring(L, name);
    return 1;
  }
}


static int newuserdata (lua_State *L) {
  size_t size = cast_sizet(luaL_optinteger(L, 1, 0));
  int nuv = cast_int(luaL_optinteger(L, 2, 0));
  char *p = cast_charp(lua_newuserdatauv(L, size, nuv));
  while (size--) *p++ = '\0';
  return 1;
}


static int pushuserdata (lua_State *L) {
  lua_Integer u = luaL_checkinteger(L, 1);
  lua_pushlightuserdata(L, cast_voidp(cast_sizet(u)));
  return 1;
}


static int udataval (lua_State *L) {
  lua_pushinteger(L, cast_st2S(cast_sizet(lua_touserdata(L, 1))));
  return 1;
}


static int doonnewstack (lua_State *L) {
  lua_State *L1 = lua_newthread(L);
  size_t l;
  const char *s = luaL_checklstring(L, 1, &l);
  int status = luaL_loadbuffer(L1, s, l, s);
  if (status == LUA_OK)
    status = lua_pcall(L1, 0, 0, 0);
  lua_pushinteger(L, status);
  return 1;
}


static int s2d (lua_State *L) {
  lua_pushnumber(L, cast_num(*reinterpret_cast<const double*>(luaL_checkstring(L, 1))));
  return 1;
}


static int d2s (lua_State *L) {
  double d = static_cast<double>(luaL_checknumber(L, 1));
  lua_pushlstring(L, cast_charp(&d), sizeof(d));
  return 1;
}


static int num2int (lua_State *L) {
  lua_pushinteger(L, lua_tointeger(L, 1));
  return 1;
}


static int makeseed (lua_State *L) {
  lua_pushinteger(L, cast_Integer(luaL_makeseed(L)));
  return 1;
}


static int newstate (lua_State *L) {
  void *ud;
  lua_Alloc f = lua_getallocf(L, &ud);
  lua_State *L1 = lua_newstate(f, ud, 0);
  if (L1) {
    lua_atpanic(L1, tpanic);
    lua_pushlightuserdata(L, L1);
  }
  else
    lua_pushnil(L);
  return 1;
}


static lua_State *getstate (lua_State *L) {
  lua_State *L1 = static_cast<lua_State*>(lua_touserdata(L, 1));
  luaL_argcheck(L, L1 != nullptr, 1, "state expected");
  return L1;
}


static int loadlib (lua_State *L) {
  lua_State *L1 = getstate(L);
  int load = cast_int(luaL_checkinteger(L, 2));
  int preload = cast_int(luaL_checkinteger(L, 3));
  luaL_openselectedlibs(L1, load, preload);
  luaL_requiref(L1, "T", luaB_opentests, 0);
  lua_assert(lua_type(L1, -1) == LUA_TTABLE);
  // 'requiref' should not reload module already loaded...
  luaL_requiref(L1, "T", nullptr, 1);  // seg. fault if it reloads
  // ...but should return the same module
  lua_assert(lua_compare(L1, -1, -2, LUA_OPEQ));
  return 0;
}

static int closestate (lua_State *L) {
  lua_State *L1 = getstate(L);
  lua_close(L1);
  return 0;
}

static int doremote (lua_State *L) {
  lua_State *L1 = getstate(L);
  size_t lcode;
  const char *code = luaL_checklstring(L, 2, &lcode);
  int status;
  lua_settop(L1, 0);
  status = luaL_loadbuffer(L1, code, lcode, code);
  if (status == LUA_OK)
    status = lua_pcall(L1, 0, LUA_MULTRET, 0);
  if (status != LUA_OK) {
    lua_pushnil(L);
    lua_pushstring(L, lua_tostring(L1, -1));
    lua_pushinteger(L, status);
    return 3;
  }
  else {
    int i = 0;
    while (!lua_isnone(L1, ++i))
      lua_pushstring(L, lua_tostring(L1, i));
    lua_pop(L1, i-1);
    return i-1;
  }
}


static int log2_aux (lua_State *L) {
  unsigned int x = (unsigned int)luaL_checkinteger(L, 1);
  lua_pushinteger(L, luaO_ceillog2(x));
  return 1;
}


struct Aux { jmp_buf jb; const char *paniccode; lua_State *L; };

/*
** does a long-jump back to "main program".
*/
static int panicback (lua_State *L) {
  struct Aux *b;
  lua_checkstack(L, 1);  // open space for 'Aux' struct
  lua_getfield(L, LUA_REGISTRYINDEX, "_jmpbuf");  // get 'Aux' struct
  b = static_cast<struct Aux *>(lua_touserdata(L, -1));
  lua_pop(L, 1);  // remove 'Aux' struct
  runC(b->L, L, b->paniccode);  // run optional panic code
  longjmp(b->jb, 1);
  return 1;  // to avoid warnings
}

static int checkpanic (lua_State *L) {
  struct Aux b;
  void *ud;
  lua_State *L1;
  const char *code = luaL_checkstring(L, 1);
  lua_Alloc f = lua_getallocf(L, &ud);
  b.paniccode = luaL_optstring(L, 2, "");
  b.L = L;
  L1 = lua_newstate(f, ud, 0);  // create new state
  if (L1 == nullptr) {  // error?
    lua_pushstring(L, MEMERRMSG);
    return 1;
  }
  lua_atpanic(L1, panicback);  // set its panic function
  lua_pushlightuserdata(L1, &b);
  lua_setfield(L1, LUA_REGISTRYINDEX, "_jmpbuf");  // store 'Aux' struct
  if (setjmp(b.jb) == 0) {  // set jump buffer
    runC(L, L1, code);  // run code unprotected
    lua_pushliteral(L, "no errors");
  }
  else {  // error handling
    // move error message to original state
    lua_pushstring(L, lua_tostring(L1, -1));
  }
  lua_close(L1);
  return 1;
}


static int externKstr (lua_State *L) {
  size_t len;
  const char *s = luaL_checklstring(L, 1, &len);
  lua_pushexternalstring(L, s, len, nullptr, nullptr);
  return 1;
}


/*
** Create a buffer with the content of a given string and then
** create an external string using that buffer. Use the allocation
** function from Lua to create and free the buffer.
*/
static int externstr (lua_State *L) {
  size_t len;
  const char *s = luaL_checklstring(L, 1, &len);
  void *ud;
  lua_Alloc allocf = lua_getallocf(L, &ud);  // get allocation function
  // create the buffer
  char *buff = cast_charp((*allocf)(ud, nullptr, 0, len + 1));
  if (buff == nullptr) {  // memory error?
    lua_pushliteral(L, "not enough memory");
    lua_error(L);  // raise a memory error
  }
  // copy string content to buffer, including ending 0
  memcpy(buff, s, (len + 1) * sizeof(char));
  // create external string
  lua_pushexternalstring(L, buff, len, allocf, ud);
  return 1;
}


/*
** {====================================================================
** function to test the API with C. It interprets a kind of assembler
** language with calls to the API, so the test can be driven by Lua code
** =====================================================================
*/


static void sethookaux (lua_State *L, int mask, int count, const char *code);

static const char *const delimits = " \t\n,;";

static void skip (const char **pc) {
  for (;;) {
    if (**pc != '\0' && strchr(delimits, **pc)) (*pc)++;
    else if (**pc == '#') {  // comment?
      while (**pc != '\n' && **pc != '\0') (*pc)++;  // until end-of-line
    }
    else break;
  }
}

static int getnum_aux (lua_State *L, lua_State *L1, const char **pc) {
  int res = 0;
  int sig = 1;
  skip(pc);
  if (**pc == '.') {
    res = cast_int(lua_tointeger(L1, -1));
    lua_pop(L1, 1);
    (*pc)++;
    return res;
  }
  else if (**pc == '*') {
    res = lua_gettop(L1);
    (*pc)++;
    return res;
  }
  else if (**pc == '!') {
    (*pc)++;
    if (**pc == 'G')
      res = LUA_RIDX_GLOBALS;
    else if (**pc == 'M')
      res = LUA_RIDX_MAINTHREAD;
    else lua_assert(0);
    (*pc)++;
    return res;
  }
  else if (**pc == '-') {
    sig = -1;
    (*pc)++;
  }
  if (!lisdigit(cast_uchar(**pc)))
    luaL_error(L, "number expected (%s)", *pc);
  while (lisdigit(cast_uchar(**pc))) res = res*10 + (*(*pc)++) - '0';
  return sig*res;
}

static const char *getstring_aux (lua_State *L, char *buff, const char **pc) {
  int i = 0;
  skip(pc);
  if (**pc == '"' || **pc == '\'') {  /* quoted string? */
    int quote = *(*pc)++;
    while (**pc != quote) {
      if (**pc == '\0') luaL_error(L, "unfinished string in C script");
      buff[i++] = *(*pc)++;
    }
    (*pc)++;
  }
  else {
    while (**pc != '\0' && !strchr(delimits, **pc))
      buff[i++] = *(*pc)++;
  }
  buff[i] = '\0';
  return buff;
}


static int getindex_aux (lua_State *L, lua_State *L1, const char **pc) {
  skip(pc);
  switch (*(*pc)++) {
    case 'R': return LUA_REGISTRYINDEX;
    case 'U': return lua_upvalueindex(getnum_aux(L, L1, pc));
    default: {
      int n;
      (*pc)--;  // to read again
      n = getnum_aux(L, L1, pc);
      if (n == 0) return 0;
      else return lua_absindex(L1, n);
    }
  }
}


static const char *const statcodes[] = {"OK", "YIELD", "ERRRUN",
    "ERRSYNTAX", MEMERRMSG, "ERRERR"};

/*
** Avoid these stat codes from being collected, to avoid possible
** memory error when pushing them.
*/
static void regcodes (lua_State *L) {
  unsigned int i;
  for (i = 0; i < sizeof(statcodes) / sizeof(statcodes[0]); i++) {
    lua_pushboolean(L, 1);
    lua_setfield(L, LUA_REGISTRYINDEX, statcodes[i]);
  }
}


#define EQ(s1)	(strcmp(s1, inst) == 0)

#define getnum		(getnum_aux(L, L1, &pc))
#define getstring	(getstring_aux(L, buff, &pc))
#define getindex	(getindex_aux(L, L1, &pc))


static int testC (lua_State *L);
static int Cfunck (lua_State *L, int status, lua_KContext ctx);

/*
** arithmetic operation encoding for 'arith' instruction
** LUA_OPIDIV  -> \
** LUA_OPSHL   -> <
** LUA_OPSHR   -> >
** LUA_OPUNM   -> _
** LUA_OPBNOT  -> !
*/
static const char ops[] = "+-*%^/\\&|~<>_!";

static int runC (lua_State *L, lua_State *L1, const char *pc) {
  char buff[300];
  int status = 0;
  if (pc == nullptr) return luaL_error(L, "attempt to runC null script");
  for (;;) {
    const char *inst = getstring;
    if EQ("") return 0;
    else if EQ("absindex") {
      lua_pushinteger(L1, getindex);
    }
    else if EQ("append") {
      int t = getindex;
      int i = cast_int(lua_rawlen(L1, t));
      lua_rawseti(L1, t, i + 1);
    }
    else if EQ("arith") {
      int op;
      skip(&pc);
      op = cast_int(strchr(ops, *pc++) - ops);
      lua_arith(L1, op);
    }
    else if EQ("call") {
      int narg = getnum;
      int nres = getnum;
      lua_call(L1, narg, nres);
    }
    else if EQ("callk") {
      int narg = getnum;
      int nres = getnum;
      int i = getindex;
      lua_callk(L1, narg, nres, i, Cfunck);
    }
    else if EQ("checkstack") {
      int sz = getnum;
      const char *msg = getstring;
      if (*msg == '\0')
        msg = nullptr;  // to test 'luaL_checkstack' with no message
      luaL_checkstack(L1, sz, msg);
    }
    else if EQ("rawcheckstack") {
      int sz = getnum;
      lua_pushboolean(L1, lua_checkstack(L1, sz));
    }
    else if EQ("compare") {
      const char *opt = getstring;  // EQ, LT, or LE
      int op = (opt[0] == 'E') ? LUA_OPEQ
                               : (opt[1] == 'T') ? LUA_OPLT : LUA_OPLE;
      int a = getindex;
      int b = getindex;
      lua_pushboolean(L1, lua_compare(L1, a, b, op));
    }
    else if EQ("concat") {
      lua_concat(L1, getnum);
    }
    else if EQ("copy") {
      int f = getindex;
      lua_copy(L1, f, getindex);
    }
    else if EQ("func2num") {
      lua_CFunction func = lua_tocfunction(L1, getindex);
      lua_pushinteger(L1, cast_st2S(cast_sizet(func)));
    }
    else if EQ("getfield") {
      int t = getindex;
      int tp = lua_getfield(L1, t, getstring);
      lua_assert(tp == lua_type(L1, -1));
    }
    else if EQ("getglobal") {
      lua_getglobal(L1, getstring);
    }
    else if EQ("getmetatable") {
      if (lua_getmetatable(L1, getindex) == 0)
        lua_pushnil(L1);
    }
    else if EQ("gettable") {
      int tp = lua_gettable(L1, getindex);
      lua_assert(tp == lua_type(L1, -1));
    }
    else if EQ("gettop") {
      lua_pushinteger(L1, lua_gettop(L1));
    }
    else if EQ("gsub") {
      int a = getnum; int b = getnum; int c = getnum;
      luaL_gsub(L1, lua_tostring(L1, a),
                    lua_tostring(L1, b),
                    lua_tostring(L1, c));
    }
    else if EQ("insert") {
      lua_insert(L1, getnum);
    }
    else if EQ("iscfunction") {
      lua_pushboolean(L1, lua_iscfunction(L1, getindex));
    }
    else if EQ("isfunction") {
      lua_pushboolean(L1, lua_isfunction(L1, getindex));
    }
    else if EQ("isnil") {
      lua_pushboolean(L1, lua_isnil(L1, getindex));
    }
    else if EQ("isnull") {
      lua_pushboolean(L1, lua_isnone(L1, getindex));
    }
    else if EQ("isnumber") {
      lua_pushboolean(L1, lua_isnumber(L1, getindex));
    }
    else if EQ("isstring") {
      lua_pushboolean(L1, lua_isstring(L1, getindex));
    }
    else if EQ("istable") {
      lua_pushboolean(L1, lua_istable(L1, getindex));
    }
    else if EQ("isudataval") {
      lua_pushboolean(L1, lua_islightuserdata(L1, getindex));
    }
    else if EQ("isuserdata") {
      lua_pushboolean(L1, lua_isuserdata(L1, getindex));
    }
    else if EQ("len") {
      lua_len(L1, getindex);
    }
    else if EQ("Llen") {
      lua_pushinteger(L1, luaL_len(L1, getindex));
    }
    else if EQ("loadfile") {
      luaL_loadfile(L1, luaL_checkstring(L1, getnum));
    }
    else if EQ("loadstring") {
      size_t slen;
      const char *s = luaL_checklstring(L1, getnum, &slen);
      const char *name = getstring;
      const char *mode = getstring;
      luaL_loadbufferx(L1, s, slen, name, mode);
    }
    else if EQ("newmetatable") {
      lua_pushboolean(L1, luaL_newmetatable(L1, getstring));
    }
    else if EQ("newtable") {
      lua_newtable(L1);
    }
    else if EQ("newthread") {
      lua_newthread(L1);
    }
    else if EQ("resetthread") {
      lua_pushinteger(L1, lua_resetthread(L1));  // deprecated
    }
    else if EQ("newuserdata") {
      lua_newuserdata(L1, cast_sizet(getnum));
    }
    else if EQ("next") {
      lua_next(L1, -2);
    }
    else if EQ("objsize") {
      lua_pushinteger(L1, l_castU2S(lua_rawlen(L1, getindex)));
    }
    else if EQ("pcall") {
      int narg = getnum;
      int nres = getnum;
      status = lua_pcall(L1, narg, nres, getnum);
    }
    else if EQ("pcallk") {
      int narg = getnum;
      int nres = getnum;
      int i = getindex;
      status = lua_pcallk(L1, narg, nres, 0, i, Cfunck);
    }
    else if EQ("pop") {
      lua_pop(L1, getnum);
    }
    else if EQ("printstack") {
      int n = getnum;
      if (n != 0) {
        lua_printvalue(s2v(L->getCI()->funcRef().p + n));
        printf("\n");
      }
      else lua_printstack(L1);
    }
    else if EQ("print") {
      const char *msg = getstring;
      printf("%s\n", msg);
    }
    else if EQ("warningC") {
      const char *msg = getstring;
      lua_warning(L1, msg, 1);
    }
    else if EQ("warning") {
      const char *msg = getstring;
      lua_warning(L1, msg, 0);
    }
    else if EQ("pushbool") {
      lua_pushboolean(L1, getnum);
    }
    else if EQ("pushcclosure") {
      lua_pushcclosure(L1, testC, getnum);
    }
    else if EQ("pushint") {
      lua_pushinteger(L1, getnum);
    }
    else if EQ("pushnil") {
      lua_pushnil(L1);
    }
    else if EQ("pushnum") {
      lua_pushnumber(L1, (lua_Number)getnum);
    }
    else if EQ("pushstatus") {
      lua_pushstring(L1, statcodes[status]);
    }
    else if EQ("pushstring") {
      lua_pushstring(L1, getstring);
    }
    else if EQ("pushupvalueindex") {
      lua_pushinteger(L1, lua_upvalueindex(getnum));
    }
    else if EQ("pushvalue") {
      lua_pushvalue(L1, getindex);
    }
    else if EQ("pushfstringI") {
      lua_pushfstring(L1, lua_tostring(L, -2), (int)lua_tointeger(L, -1));
    }
    else if EQ("pushfstringS") {
      lua_pushfstring(L1, lua_tostring(L, -2), lua_tostring(L, -1));
    }
    else if EQ("pushfstringP") {
      lua_pushfstring(L1, lua_tostring(L, -2), lua_topointer(L, -1));
    }
    else if EQ("rawget") {
      int t = getindex;
      lua_rawget(L1, t);
    }
    else if EQ("rawgeti") {
      int t = getindex;
      lua_rawgeti(L1, t, getnum);
    }
    else if EQ("rawgetp") {
      int t = getindex;
      lua_rawgetp(L1, t, cast_voidp(cast_sizet(getnum)));
    }
    else if EQ("rawset") {
      int t = getindex;
      lua_rawset(L1, t);
    }
    else if EQ("rawseti") {
      int t = getindex;
      lua_rawseti(L1, t, getnum);
    }
    else if EQ("rawsetp") {
      int t = getindex;
      lua_rawsetp(L1, t, cast_voidp(cast_sizet(getnum)));
    }
    else if EQ("remove") {
      lua_remove(L1, getnum);
    }
    else if EQ("replace") {
      lua_replace(L1, getindex);
    }
    else if EQ("resume") {
      int i = getindex;
      int nres;
      status = lua_resume(lua_tothread(L1, i), L, getnum, &nres);
    }
    else if EQ("traceback") {
      const char *msg = getstring;
      int level = getnum;
      luaL_traceback(L1, L1, msg, level);
    }
    else if EQ("threadstatus") {
      lua_pushstring(L1, statcodes[lua_status(L1)]);
    }
    else if EQ("alloccount") {
      l_memcontrol.countlimit = cast_uint(getnum);
    }
    else if EQ("return") {
      int n = getnum;
      if (L1 != L) {
        int i;
        for (i = 0; i < n; i++) {
          int idx = -(n - i);
          switch (lua_type(L1, idx)) {
            case LUA_TBOOLEAN:
              lua_pushboolean(L, lua_toboolean(L1, idx));
              break;
            default:
              lua_pushstring(L, lua_tostring(L1, idx));
              break;
          }
        }
      }
      return n;
    }
    else if EQ("rotate") {
      int i = getindex;
      lua_rotate(L1, i, getnum);
    }
    else if EQ("setfield") {
      int t = getindex;
      const char *s = getstring;
      lua_setfield(L1, t, s);
    }
    else if EQ("seti") {
      int t = getindex;
      lua_seti(L1, t, getnum);
    }
    else if EQ("setglobal") {
      const char *s = getstring;
      lua_setglobal(L1, s);
    }
    else if EQ("sethook") {
      int mask = getnum;
      int count = getnum;
      const char *s = getstring;
      sethookaux(L1, mask, count, s);
    }
    else if EQ("setmetatable") {
      int idx = getindex;
      lua_setmetatable(L1, idx);
    }
    else if EQ("settable") {
      lua_settable(L1, getindex);
    }
    else if EQ("settop") {
      lua_settop(L1, getnum);
    }
    else if EQ("testudata") {
      int i = getindex;
      lua_pushboolean(L1, luaL_testudata(L1, i, getstring) != nullptr);
    }
    else if EQ("error") {
      lua_error(L1);
    }
    else if EQ("abort") {
      abort();
    }
    else if EQ("throw") {
#if defined(__cplusplus)
static struct X { int x; } x;
      throw x;
#else
      luaL_error(L1, "C++");
#endif
      break;
    }
    else if EQ("tobool") {
      lua_pushboolean(L1, lua_toboolean(L1, getindex));
    }
    else if EQ("tocfunction") {
      lua_pushcfunction(L1, lua_tocfunction(L1, getindex));
    }
    else if EQ("tointeger") {
      lua_pushinteger(L1, lua_tointeger(L1, getindex));
    }
    else if EQ("tonumber") {
      lua_pushnumber(L1, lua_tonumber(L1, getindex));
    }
    else if EQ("topointer") {
      lua_pushlightuserdata(L1, cast_voidp(lua_topointer(L1, getindex)));
    }
    else if EQ("touserdata") {
      lua_pushlightuserdata(L1, lua_touserdata(L1, getindex));
    }
    else if EQ("tostring") {
      const char *s = lua_tostring(L1, getindex);
      const char *s1 = lua_pushstring(L1, s);
      cast_void(s1);  // to avoid warnings
      lua_longassert((s == nullptr && s1 == nullptr) || strcmp(s, s1) == 0);
    }
    else if EQ("Ltolstring") {
      luaL_tolstring(L1, getindex, nullptr);
    }
    else if EQ("type") {
      lua_pushstring(L1, luaL_typename(L1, getnum));
    }
    else if EQ("xmove") {
      int f = getindex;
      int t = getindex;
      lua_State *fs = (f == 0) ? L1 : lua_tothread(L1, f);
      lua_State *ts = (t == 0) ? L1 : lua_tothread(L1, t);
      int n = getnum;
      if (n == 0) n = lua_gettop(fs);
      lua_xmove(fs, ts, n);
    }
    else if EQ("isyieldable") {
      lua_pushboolean(L1, lua_isyieldable(lua_tothread(L1, getindex)));
    }
    else if EQ("yield") {
      return lua_yield(L1, getnum);
    }
    else if EQ("yieldk") {
      int nres = getnum;
      int i = getindex;
      return lua_yieldk(L1, nres, i, Cfunck);
    }
    else if EQ("toclose") {
      lua_toclose(L1, getnum);
    }
    else if EQ("closeslot") {
      lua_closeslot(L1, getnum);
    }
    else if EQ("argerror") {
      int arg = getnum;
      luaL_argerror(L1, arg, getstring);
    }
    else luaL_error(L, "unknown instruction %s", buff);
  }
  return 0;
}


static int testC (lua_State *L) {
  lua_State *L1;
  const char *pc;
  if (lua_isuserdata(L, 1)) {
    L1 = getstate(L);
    pc = luaL_checkstring(L, 2);
  }
  else if (lua_isthread(L, 1)) {
    L1 = lua_tothread(L, 1);
    pc = luaL_checkstring(L, 2);
  }
  else {
    L1 = L;
    pc = luaL_checkstring(L, 1);
  }
  return runC(L, L1, pc);
}


static int Cfunc (lua_State *L) {
  return runC(L, L, lua_tostring(L, lua_upvalueindex(1)));
}


static int Cfunck (lua_State *L, int status, lua_KContext ctx) {
  lua_pushstring(L, statcodes[status]);
  lua_setglobal(L, "status");
  lua_pushinteger(L, cast_Integer(ctx));
  lua_setglobal(L, "ctx");
  return runC(L, L, lua_tostring(L, cast_int(ctx)));
}


static int makeCfunc (lua_State *L) {
  luaL_checkstring(L, 1);
  lua_pushcclosure(L, Cfunc, lua_gettop(L));
  return 1;
}


// }======================================================


/*
** {======================================================
** tests for C hooks
** =======================================================
*/

/*
** C hook that runs the C script stored in registry.C_HOOK[L]
*/
static void Chook (lua_State *L, lua_Debug *ar) {
  const char *scpt;
  const char *const events [] = {"call", "ret", "line", "count", "tailcall"};
  lua_getfield(L, LUA_REGISTRYINDEX, "C_HOOK");
  lua_pushlightuserdata(L, L);
  lua_gettable(L, -2);  // get C_HOOK[L] (script saved by sethookaux)
  scpt = lua_tostring(L, -1);  // not very religious (string will be popped)
  lua_pop(L, 2);  // remove C_HOOK and script
  lua_pushstring(L, events[ar->event]);  // may be used by script
  lua_pushinteger(L, ar->currentline);  // may be used by script
  runC(L, L, scpt);  // run script from C_HOOK[L]
}


/*
** sets 'registry.C_HOOK[L] = scpt' and sets 'Chook' as a hook
*/
static void sethookaux (lua_State *L, int mask, int count, const char *scpt) {
  if (*scpt == '\0') {  // no script?
    lua_sethook(L, nullptr, 0, 0);  // turn off hooks
    return;
  }
  lua_getfield(L, LUA_REGISTRYINDEX, "C_HOOK");  // get C_HOOK table
  if (!lua_istable(L, -1)) {  // no hook table?
    lua_pop(L, 1);  // remove previous value
    lua_newtable(L);  // create new C_HOOK table
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, "C_HOOK");  // register it
  }
  lua_pushlightuserdata(L, L);
  lua_pushstring(L, scpt);
  lua_settable(L, -3);  // C_HOOK[L] = script
  lua_sethook(L, Chook, mask, count);
}


static int sethook (lua_State *L) {
  if (lua_isnoneornil(L, 1))
    lua_sethook(L, nullptr, 0, 0);  // turn off hooks
  else {
    const char *scpt = luaL_checkstring(L, 1);
    const char *smask = luaL_checkstring(L, 2);
    int count = cast_int(luaL_optinteger(L, 3, 0));
    int mask = 0;
    if (strchr(smask, 'c')) mask |= LUA_MASKCALL;
    if (strchr(smask, 'r')) mask |= LUA_MASKRET;
    if (strchr(smask, 'l')) mask |= LUA_MASKLINE;
    if (count > 0) mask |= LUA_MASKCOUNT;
    sethookaux(L, mask, count, scpt);
  }
  return 0;
}


static int coresume (lua_State *L) {
  int status, nres;
  lua_State *co = lua_tothread(L, 1);
  luaL_argcheck(L, co, 1, "coroutine expected");
  status = lua_resume(co, L, 0, &nres);
  if (status != LUA_OK && status != LUA_YIELD) {
    lua_pushboolean(L, 0);
    lua_insert(L, -2);
    return 2;  // return false + error message
  }
  else {
    lua_pushboolean(L, 1);
    return 1;
  }
}

#if !defined(LUA_USE_POSIX)

#define nonblock	nullptr

#else

#include <unistd.h>
#include <fcntl.h>

static int nonblock (lua_State *L) {
  FILE *f = static_cast<luaL_Stream*>(luaL_checkudata(L, 1, LUA_FILEHANDLE))->f;
  int fd = fileno(f);
  int flags = fcntl(fd, F_GETFL, 0);
  flags |= O_NONBLOCK;
  fcntl(fd, F_SETFL, flags);
  return 0;
}
#endif

// }======================================================


/*
** Test LuaVector - demonstrates std::vector with LuaAllocator
** Usage: testvector(n)
** Creates a vector with n integers, verifies correctness,
** and returns total memory allocated
*/
static int testvector (lua_State *L) {
  int n = cast_int(luaL_checkinteger(L, 1));
  luaL_argcheck(L, n >= 0 && n <= 1000000, 1, "value out of range");

  GlobalState *g = G(L);

  // Get memory before allocation
  lua_Integer membefore = static_cast<lua_Integer>(g->getTotalBytes());

  // Create and populate a LuaVector
  {
    LuaVector<int> vec(L);

    // Reserve to avoid multiple reallocations
    vec.reserve(static_cast<size_t>(n));

    // Fill with values
    for (int i = 0; i < n; i++) {
      vec.push_back(i * 2);
    }

    // Verify contents
    for (int i = 0; i < n; i++) {
      luaL_argcheck(L, vec[static_cast<size_t>(i)] == i * 2, 1, "vector content mismatch");
    }

    // Check size
    luaL_argcheck(L, vec.size() == static_cast<size_t>(n), 1, "vector size mismatch");

    // Get memory during allocation
    lua_Integer memduring = static_cast<lua_Integer>(g->getTotalBytes());

    // Push allocated bytes
    lua_pushinteger(L, memduring - membefore);
  }

  // Vector is now destroyed, memory should be freed
  return 1;
}


static const struct luaL_Reg tests_funcs[] = {
  {"checkmemory", lua_checkmemory},
  {"closestate", closestate},
  {"d2s", d2s},
  {"doonnewstack", doonnewstack},
  {"doremote", doremote},
  {"gccolor", gc_color},
  {"gcage", gc_age},
  {"gcstate", gc_state},
  {"tracegc", tracegc},
  {"pobj", gc_printobj},
  {"getref", getref},
  {"hash", hash_query},
  {"log2", log2_aux},
  {"limits", get_limits},
  {"listcode", listcode},
  {"printcode", printcode},
  {"printallstack", lua_printallstack},
  {"listk", listk},
  {"listabslineinfo", listabslineinfo},
  {"listlocals", listlocals},
  {"loadlib", loadlib},
  {"checkpanic", checkpanic},
  {"newstate", newstate},
  {"newuserdata", newuserdata},
  {"num2int", num2int},
  {"makeseed", makeseed},
  {"pushuserdata", pushuserdata},
  {"gcquery", gc_query},
  {"querystr", string_query},
  {"querytab", table_query},
  {"codeparam", test_codeparam},
  {"applyparam", test_applyparam},
  {"ref", tref},
  {"resume", coresume},
  {"s2d", s2d},
  {"sethook", sethook},
  {"stacklevel", stacklevel},
  {"sizes", get_sizes},
  {"testC", testC},
  {"makeCfunc", makeCfunc},
  {"totalmem", mem_query},
  {"alloccount", alloc_count},
  {"allocfailnext", alloc_failnext},
  {"trick", settrick},
  {"udataval", udataval},
  {"unref", unref},
  {"upvalue", upvalue},
  {"externKstr", externKstr},
  {"externstr", externstr},
  {"testvector", testvector},
  {"nonblock", nonblock},
  {nullptr, nullptr}
};


static void checkfinalmem (void) {
  lua_assert(l_memcontrol.numblocks == 0);
  lua_assert(l_memcontrol.total == 0);
}


int luaB_opentests (lua_State *L) {
  void *ud;
  lua_Alloc f = lua_getallocf(L, &ud);
  lua_atpanic(L, &tpanic);
  lua_setwarnf(L, &warnf, L);
  lua_pushboolean(L, 0);
  lua_setglobal(L, "_WARN");  // _WARN = false
  regcodes(L);
  atexit(checkfinalmem);
  lua_assert(f == debug_realloc && ud == cast_voidp(&l_memcontrol));
  lua_setallocf(L, f, ud);  // exercise this function
  luaL_newlib(L, tests_funcs);
  return 1;
}

#endif

