/*
** Garbage Collector
** See Copyright Notice in lua.h
*/

#define LUA_CORE

#include "lprefix.h"

#include <cstring>


#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "gc/gc_core.h"
#include "gc/gc_marking.h"
#include "gc/gc_sweeping.h"
#include "gc/gc_finalizer.h"
#include "gc/gc_weak.h"
#include "gc/gc_collector.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"


/*
** Maximum number of elements to sweep in each single step.
** (Large enough to dissipate fixed overheads but small enough
** to allow small steps for the collector.)
*/
#define GCSWEEPMAX	20


/*
** Cost (in work units) of running one finalizer.
*/
#define CWUFIN	10


/*
** TRI-COLOR MARKING ALGORITHM:
**
** Lua uses a tri-color incremental mark-and-sweep garbage collector.
** Each object has one of three colors stored in its 'marked' field:
**
** WHITE: Not yet visited in this GC cycle (candidates for collection)
**   - Two white shades alternate between GC cycles to handle new allocations
**   - Objects allocated during marking use the "other" white shade
**   - At sweep time, only the "old" white shade is collected
**
** GRAY: Visited but not yet processed (in the work queue)
**   - Object is reachable but its children haven't been marked yet
**   - Stored in various gray lists (gray, grayagain, etc.)
**   - Ensures incremental progress: each work unit processes some gray objects
**
** BLACK: Visited and fully processed (definitely reachable)
**   - Object and all its children have been marked
**   - Collector invariant: no black object points to white object
**   - Barrier operations (write barriers) maintain this invariant
**
** INCREMENTAL COLLECTION:
** Instead of stopping the world, Lua interleaves GC work with program execution.
** The tri-color scheme ensures we never collect reachable objects even though
** the program modifies the object graph during collection.
*/

/* Note: Color manipulation functions (makewhite, set2gray, set2black, etc.)
** are now in lgc.h for use by all GC modules. */


/* Phase 127: Convert gcvalarr macro to inline function
** Access to collectable objects in array part of tables
** Note: markvalue, markkey, markobject, markobjectN are already defined in gc_marking.h
*/
inline GCObject* gcvalarr(Table* t, unsigned int i) noexcept {
	return (static_cast<lu_byte>(*(t)->getArrayTag(i)) & BIT_ISCOLLECTABLE) ? (t)->getArrayVal(i)->gc : nullptr;
}

static void reallymarkobject (global_State& g, GCObject *o);


/*
** {======================================================
** Generic functions
** =======================================================
*/


/*
** one after last element in a hash array
*/
inline Node* gnodelast(Table* h) noexcept {
	return gnode(h, h->nodeSize());
}

inline Node* gnodelast(const Table* h) noexcept {
	return gnode(h, h->nodeSize());
}


/* Wrapper for GCCore::objsize - now in gc_core module */
static l_mem objsize(GCObject* o) {
  return GCCore::objsize(o);
}


/* Wrapper for GCCore::getgclist - now in gc_core module */
static GCObject** getgclist(GCObject* o) {
  return GCCore::getgclist(o);
}


/* Wrapper for GCCore::linkgclist_ - now in gc_core module */
static void linkgclist_(GCObject* o, GCObject** pnext, GCObject** list) {
  GCCore::linkgclist_(o, pnext, list);
}

/* Phase 128: Convert linkgclist and linkobjgclist macros to template functions
** Link a collectable object 'o' with a known type into the list 'p'.
*/
template<typename T>
inline void linkgclist(T* o, GCObject*& p) {
	linkgclist_(obj2gco(o), &(o)->gclist, &p);
}

/* Specialized version for Table (with encapsulated gclist) */
inline void linkgclistTable(Table *h, GCObject *&p) {
  linkgclist_(obj2gco(h), h->getGclistPtr(), &p);
}

/* Specialized version for lua_State (with encapsulated gclist) */
inline void linkgclistThread(lua_State *th, GCObject *&p) {
  linkgclist_(obj2gco(th), th->getGclistPtr(), &p);
}

/* Link a generic collectable object 'o' into the list 'p'. */
template<typename T>
inline void linkobjgclist(T* o, GCObject*& p) {
	linkgclist_(obj2gco(o), getgclist(o), &p);
}



/* Note: clearkey() is now in GCCore module, used by GCMarking::traversestrongtable */


/*
** Barrier that moves collector forward, that is, marks the white object
** 'v' being pointed by the black object 'o'.  In the generational
** mode, 'v' must also become old, if 'o' is old; however, it cannot
** be changed directly to OLD, because it may still point to non-old
** objects. So, it is marked as OLD0. In the next cycle it will become
** OLD1, and in the next it will finally become OLD (regular old). By
** then, any object it points to will also be old.  If called in the
** incremental sweep phase, it clears the black object to white (sweep
** it) to avoid other barrier calls for this same object. (That cannot
** be done is generational mode, as its sweep does not distinguish
** white from dead.)
*/
void luaC_barrier_ (lua_State& L, GCObject *o, GCObject *v) {
  global_State *g = G(L);
  lua_assert(isblack(o) && iswhite(v) && !isdead(g, v) && !isdead(g, o));
  if (g->keepInvariant()) {  /* must keep invariant? */
    reallymarkobject(*g, v);  /* restore invariant */
    if (isold(o)) {
      lua_assert(!isold(v));  /* white object could not be old */
      setage(v, GCAge::Old0);  /* restore generational invariant */
    }
  }
  else {  /* sweep phase */
    lua_assert(g->isSweepPhase());
    if (g->getGCKind() != GCKind::GenerationalMinor)  /* incremental mode? */
      makewhite(g, o);  /* mark 'o' as white to avoid other barriers */
  }
}


/*
** barrier that moves collector backward, that is, mark the black object
** pointing to a white object as gray again.
*/
void luaC_barrierback_ (lua_State& L, GCObject *o) {
  global_State *g = G(L);
  lua_assert(isblack(o) && !isdead(g, o));
  lua_assert((g->getGCKind() != GCKind::GenerationalMinor)
          || (isold(o) && getage(o) != GCAge::Touched1));
  if (getage(o) == GCAge::Touched2)  /* already in gray list? */
    set2gray(o);  /* make it gray to become touched1 */
  else  /* link it in 'grayagain' and paint it gray */
    linkobjgclist(o, *g->getGrayAgainPtr());
  if (isold(o))  /* generational mode? */
    setage(o, GCAge::Touched1);  /* touched in current cycle */
}




/*
** create a new collectable object (with given type, size, and offset)
** and link it to 'allgc' list.
*/
GCObject *luaC_newobjdt (lua_State& L, LuaT tt, size_t sz, size_t offset) {
  global_State *g = G(L);
  char *p = cast_charp(luaM_newobject(&L, novariant(tt), sz));
  GCObject *o = reinterpret_cast<GCObject*>(p + offset);
  o->setMarked(g->getWhite());
  o->setType(tt);
  o->setNext(g->getAllGC());
  g->setAllGC(o);
  return o;
}


/*
** create a new collectable object with no offset.
*/
GCObject *luaC_newobj (lua_State& L, LuaT tt, size_t sz) {
  return luaC_newobjdt(L, tt, sz, 0);
}

/* }====================================================== */



/*
** {======================================================
** Mark functions
** =======================================================
*/


/*
** Mark an object.  Userdata with no user values, strings, and closed
** upvalues are visited and turned black here.  Open upvalues are
** already indirectly linked through their respective threads in the
** 'twups' list, so they don't go to the gray list; nevertheless, they
** are kept gray to avoid barriers, as their values will be revisited
** by the thread or by 'remarkupvals'.  Other objects are added to the
** gray list to be visited (and turned black) later.  Both userdata and
** upvalues can call this function recursively, but this recursion goes
** for at most two levels: An upvalue cannot refer to another upvalue
** (only closures can), and a userdata's metatable must be a table.
*/
static void reallymarkobject (global_State& g, GCObject *o) {
  g.setGCMarked(g.getGCMarked() + objsize(o));
  switch (static_cast<int>(o->getType())) {
    case static_cast<int>(ctb(LuaT::SHRSTR)):
    case static_cast<int>(ctb(LuaT::LNGSTR)): {
      set2black(o);  /* nothing to visit */
      break;
    }
    case static_cast<int>(ctb(LuaT::UPVAL)): {
      UpVal *uv = gco2upv(o);
      if (uv->isOpen())
        set2gray(uv);  /* open upvalues are kept gray */
      else
        set2black(uv);  /* closed upvalues are visited here */
      markvalue(g, uv->getVP());  /* mark its content */
      break;
    }
    case static_cast<int>(ctb(LuaT::USERDATA)): {
      Udata *u = gco2u(o);
      if (u->getNumUserValues() == 0) {  /* no user values? */
        markobjectN(g, u->getMetatable());  /* mark its metatable */
        set2black(u);  /* nothing else to mark */
        break;
      }
      /* else... */
    }  /* FALLTHROUGH */
    case static_cast<int>(ctb(LuaT::LCL)): case static_cast<int>(ctb(LuaT::CCL)): case static_cast<int>(ctb(LuaT::TABLE)):
    case static_cast<int>(ctb(LuaT::THREAD)): case static_cast<int>(ctb(LuaT::PROTO)): {
      linkobjgclist(o, *g.getGrayPtr());  /* to be visited later */
      break;
    }
    default: lua_assert(0); break;
  }
}


/* Note: markmt is now in GCMarking module, called from GCCollector */


/* Note: markbeingfnz is now in GCMarking module, called from GCCollector */


/* Note: remarkupvals is now in GCMarking module, called from GCCollector */


/* Note: cleargraylists is now global_State::clearGrayLists() method */


/* Note: restartcollection is now in GCMarking module, called from GCCollector */

/* }====================================================== */


/*
** {======================================================
** Traverse functions
** =======================================================
*/


/* Note: genlink() is now in GCMarking module, called from traverse functions */


/*
** Traverse a table with weak values and link it to proper list. During
** propagate phase, keep it in 'grayagain' list, to be revisited in the
** atomic phase. In the atomic phase, if table has any white value,
** put it in 'weak' list, to be cleared; otherwise, call 'genlink'
** to check table age in generational mode.
*/
/*
** Wrapper for traverseweakvalue - delegates to GCWeak module.
** See gc_weak.cpp for implementation.
*/
void traverseweakvalue (global_State& g, Table *h) {
  GCWeak::traverseweakvalue(g, h);
}


/* Note: All traverse*() functions (traversearray, traversestrongtable, traversetable,
** traverseudata, traverseproto, traverseCclosure, traverseLclosure, traversethread)
** are now in GCMarking module and called from GCMarking::propagatemark() */


/*
** traverse one gray object, turning it to black. Return an estimate
** of the number of slots traversed.
*/
/* Wrapper for GCMarking::propagatemark() - now in gc_marking module */
static l_mem propagatemark(global_State& g) {
  return GCMarking::propagatemark(g);
}


/* Made non-static for use by GCCollector module */
void propagateall (global_State& g) {
  while (g.getGray())
    propagatemark(g);
}


/* Note: convergeephemerons is now in GCWeak module, called from GCCollector */

/* }====================================================== */


/*
** {======================================================
** Sweep Functions
** =======================================================
*/


/* Note: clearbykeys is now in GCWeak module, called from GCCollector */


/* Note: clearbyvalues is now in GCWeak module, called from GCCollector */


/* Wrapper for GCCore::freeupval - now in gc_core module */
static void freeupval(lua_State* L, UpVal* uv) {
  GCCore::freeupval(L, uv);
}


// Phase 50: Call destructors before freeing memory (proper RAII)
// Made non-static for use by gc_sweeping module (Phase 2)
void freeobj (lua_State& L, GCObject *o) {
  assert_code(l_mem newmem = G(L)->getTotalBytes() - objsize(o));
  switch (static_cast<int>(o->getType())) {
    case static_cast<int>(ctb(LuaT::PROTO)): {
      Proto *p = gco2p(o);
      p->free(&L);  /* Phase 25b - frees internal arrays */
      // Proto destructor is trivial, but call it for completeness
      p->~Proto();
      break;
    }
    case static_cast<int>(ctb(LuaT::UPVAL)): {
      UpVal *uv = gco2upv(o);
      freeupval(&L, uv);  // Note: freeupval calls destructor internally
      break;
    }
    case static_cast<int>(ctb(LuaT::LCL)): {
      LClosure *cl = gco2lcl(o);
      cl->~LClosure();  // Call destructor
      luaM_freemem(&L, cl, sizeLclosure(cl->getNumUpvalues()));
      break;
    }
    case static_cast<int>(ctb(LuaT::CCL)): {
      CClosure *cl = gco2ccl(o);
      cl->~CClosure();  // Call destructor
      luaM_freemem(&L, cl, sizeCclosure(cl->getNumUpvalues()));
      break;
    }
    case static_cast<int>(ctb(LuaT::TABLE)): {
      Table *t = gco2t(o);
      t->destroy(&L);  // Destroy table and cleanup
      break;
    }
    case static_cast<int>(ctb(LuaT::THREAD)):
      luaE_freethread(&L, gco2th(o));
      break;
    case static_cast<int>(ctb(LuaT::USERDATA)): {
      Udata *u = gco2u(o);
      u->~Udata();  // Call destructor
      luaM_freemem(&L, o, sizeudata(u->getNumUserValues(), u->getLen()));
      break;
    }
    case static_cast<int>(ctb(LuaT::SHRSTR)): {
      TString *ts = gco2ts(o);
      size_t sz = sizestrshr(cast_uint(ts->getShrlen()));
      ts->remove(&L);  /* use method instead of free function */
      // DON'T call destructor for TString - it's empty and might cause issues with variable-size objects
      // ts->~TString();
      luaM_freemem(&L, ts, sz);
      break;
    }
    case static_cast<int>(ctb(LuaT::LNGSTR)): {
      TString *ts = gco2ts(o);
      if (ts->getShrlen() == LSTRMEM)  /* must free external string? */
        (*ts->getFalloc())(ts->getUserData(), ts->getContentsField(), ts->getLnglen() + 1, 0);
      ts->~TString();  // Call destructor
      luaM_freemem(&L, ts, TString::calculateLongStringSize(ts->getLnglen(), ts->getShrlen()));
      break;
    }
    default: lua_assert(0);
  }
  lua_assert(G(L)->getTotalBytes() == newmem);
}


/*
** sweep at most 'countin' elements from a list of GCObjects erasing dead
** objects, where a dead object is one marked with the old (non current)
** white; change all non-dead objects back to white (and new), preparing
** for next collection cycle. Return where to continue the traversal or
** nullptr if list is finished.
*/
/*
** Sweep a linked list of GC objects, freeing dead objects.
**
** PARAMETERS:
** - p: Pointer to the head of the linked list (indirection allows list modification)
** - countin: Maximum number of objects to process (for incremental sweeping)
**
** RETURN:
** - nullptr if list is fully swept
** - Pointer to next position to continue sweeping (for incremental work)
**
** TWO-WHITE SCHEME:
** Lua uses two white colors that alternate each GC cycle. During marking,
** new objects are allocated with the "other" white (currentwhite XOR 1).
** During sweeping, we only collect objects with the "old" white (otherwhite).
** This prevents collecting newly allocated objects before they can be marked.
**
** SWEEP PROCESS:
** 1. Check if object is dead (has the old white color)
** 2. If dead: remove from list and free memory
** 3. If alive: reset to current white and mark age as GCAge::New
**
** INCREMENTAL SWEEPING:
** The countin parameter limits work per step. This allows sweeping to be
** interleaved with program execution, preventing long pauses.
*/
/* sweeplist now in GCSweeping module */
/* }====================================================== */


/*
** {======================================================
** Finalization
** =======================================================
*/

/* Note: checkSizes is now in GCFinalizer module, called from GCCollector */


/* Note: udata2finalize, dothecall, GCTM now in GCFinalizer module */


/*
** Wrapper for callallpendingfinalizers - delegates to GCFinalizer module.
** See gc_finalizer.cpp for implementation.
*/
static void callallpendingfinalizers (lua_State& L) {
  GCFinalizer::callallpendingfinalizers(&L);
}


/* findlast, checkpointer now in GCFinalizer module */


/*
** Wrapper for separatetobefnz - delegates to GCFinalizer module.
** See gc_finalizer.cpp for implementation.
*/
static void separatetobefnz (global_State& g, int all) {
  GCFinalizer::separatetobefnz(g, all);
}


/*
** Wrapper for correctpointers - delegates to GCFinalizer module.
** See gc_finalizer.cpp for implementation.
*/
static void correctpointers (global_State& g, GCObject *o) {
  GCFinalizer::correctpointers(g, o);
}


/*
** if object 'o' has a finalizer, remove it from 'allgc' list (must
** search the list to find it) and link it in 'finobj' list.
*/

/* }====================================================== */


/*
** {======================================================
** Generational Collector
** =======================================================
*/

/*
** Fields 'GCmarked' and 'GCmajorminor' are used to control the pace and
** the mode of the collector. They play several roles, depending on the
** mode of the collector:
** * GCKind::Incremental:
**     GCmarked: number of marked bytes during a cycle.
**     GCmajorminor: not used.
** * GCKind::GenerationalMinor
**     GCmarked: number of bytes that became old since last major collection.
**     GCmajorminor: number of bytes marked in last major collection.
** * GCKind::GenerationalMajor
**     GCmarked: number of bytes that became old since last major collection.
**     GCmajorminor: number of bytes marked in last major collection.
*/


/* Note: setpause is now global_State::setPause() method */


/* Note: sweep2old is now in GCSweeping module, called from GCCollector */

/*
** Correct a list of gray objects. Return a pointer to the last element
** left on the list, so that we can link another list to the end of
** this one.
** Because this correction is done after sweeping, young objects might
** be turned white and still be in the list. They are only removed.
** 'TOUCHED1' objects are advanced to 'TOUCHED2' and remain on the list;
** Non-white threads also remain on the list. 'TOUCHED2' objects and
** anything else become regular old, are marked black, and are removed
** from the list.
*/
static GCObject **correctgraylist (GCObject **p) {
  GCObject *curr;
  while ((curr = *p) != nullptr) {
    GCObject **next = getgclist(curr);
    if (iswhite(curr))
      goto remove;  /* remove all white objects */
    else if (getage(curr) == GCAge::Touched1) {  /* touched in this cycle? */
      lua_assert(isgray(curr));
      nw2black(curr);  /* make it black, for next barrier */
      setage(curr, GCAge::Touched2);
      goto remain;  /* keep it in the list and go to next element */
    }
    else if (curr->getType() == ctb(LuaT::THREAD)) {
      lua_assert(isgray(curr));
      goto remain;  /* keep non-white threads on the list */
    }
    else {  /* everything else is removed */
      lua_assert(isold(curr));  /* young objects should be white here */
      if (getage(curr) == GCAge::Touched2)  /* advance from TOUCHED2... */
        setage(curr, GCAge::Old);  /* ... to OLD */
      nw2black(curr);  /* make object black (to be removed) */
      goto remove;
    }
    remove: *p = *next; continue;
    remain: p = next; continue;
  }
  return p;
}


/* Note: correctgraylists is now global_State::correctGrayLists() method */


/* Note: markold is now in GCMarking module */


/* Note: finishgencycle is now in GCCollector module */


/*
** Wrapper for GCCollector::minor2inc() - now in gc_collector module
*/
static void minor2inc (lua_State *L, global_State *g, GCKind kind) {
  GCCollector::minor2inc(L, g, kind);
}


/* Note: checkminormajor is now global_State::checkMinorMajor() method */

/*
** Wrapper for GCCollector::youngcollection() - now in gc_collector module
*/
static void youngcollection (lua_State& L, global_State& g) {
  GCCollector::youngcollection(&L, &g);
}


/* Note: atomic2gen is now in GCCollector module */


/*
** Set debt for the next minor collection, which will happen when
** total number of bytes grows 'genminormul'% in relation to
** the base, GCmajorminor, which is the number of bytes being used
** after the last major collection.
*/
/* Wrapper for global_State::setMinorDebt() - now a method */
static void setminordebt(global_State* g) {
  g->setMinorDebt();
}


/*
** Wrapper for GCCollector::entergen() - now in gc_collector module
*/
static void entergen (lua_State& L, global_State& g) {
  GCCollector::entergen(&L, &g);
}


/*
** Change collector mode to 'newmode'.
*/
void luaC_changemode (lua_State& L, GCKind newmode) {
  global_State *g = G(L);
  if (g->getGCKind() == GCKind::GenerationalMajor)  /* doing major collections? */
    g->setGCKind(GCKind::Incremental);  /* already incremental but in name */
  if (newmode != g->getGCKind()) {  /* does it need to change? */
    if (newmode == GCKind::Incremental)  /* entering incremental mode? */
      minor2inc(&L, g, GCKind::Incremental);  /* entering incremental mode */
    else {
      lua_assert(newmode == GCKind::GenerationalMinor);
      entergen(L, *g);
    }
  }
}


/*
** Wrapper for GCCollector::fullgen() - now in gc_collector module
*/
static void fullgen (lua_State& L, global_State& g) {
  GCCollector::fullgen(&L, &g);
}


/* Note: checkmajorminor is now in GCCollector module */

/* }====================================================== */


/*
** {======================================================
** GC control
** =======================================================
*/


/* Note: entersweep is now in GCSweeping module, called from GCCollector */


/*
** Wrapper for deletelist - delegates to GCSweeping module.
** See gc_sweeping.cpp for implementation.
*/
static void deletelist (lua_State& L, GCObject *p, GCObject *limit) {
  GCSweeping::deletelist(&L, p, limit);
}


/*
** Call all finalizers of the objects in the given Lua state, and
** then free all objects, except for the main thread.
*/
void luaC_freeallobjects (lua_State& L) {
  global_State *g = G(L);
  g->setGCStp(GCSTPCLS);  /* no extra finalizers after here */
  luaC_changemode(L, GCKind::Incremental);
  separatetobefnz(*g, 1);  /* separate all objects with finalizers */
  lua_assert(g->getFinObj() == nullptr);
  callallpendingfinalizers(L);
  deletelist(L, g->getAllGC(), obj2gco(mainthread(g)));
  lua_assert(g->getFinObj() == nullptr);  /* no new finalizers */
  deletelist(L, g->getFixedGC(), nullptr);  /* collect fixed objects */
  lua_assert(g->getStringTable()->getNumElements() == 0);
}


/* Note: atomic is now in GCCollector module */


/* Note: sweepstep is now in GCSweeping module, called from GCCollector::singlestep */


/*
** Wrapper for GCCollector::singlestep() - now in gc_collector module
*/
static l_mem singlestep (lua_State& L, int fast) {
  return GCCollector::singlestep(&L, fast);
}

/* Special return values (now in GCCollector class as constants) */
#define step2pause	GCCollector::STEP_2_PAUSE
#define atomicstep	GCCollector::ATOMIC_STEP
#define step2minor	GCCollector::STEP_2_MINOR


/*
** Advances the garbage collector until it reaches the given state.
** (The option 'fast' is only for testing; in normal code, 'fast'
** here is always true.)
*/
void luaC_runtilstate (lua_State& L, GCState state, int fast) {
  global_State *g = G(L);
  lua_assert(g->getGCKind() == GCKind::Incremental);
  while (state != g->getGCState())
    singlestep(L, fast);
}



/*
** Wrapper for GCCollector::incstep() - now in gc_collector module
*/
static void incstep (lua_State& L, global_State& g) {
  GCCollector::incstep(&L, &g);
}


#if !defined(luai_tracegc)
#define luai_tracegc(L,f)		((void)0)
#endif

/*
** Performs a basic GC step if collector is running. (If collector was
** stopped by the user, set a reasonable debt to avoid it being called
** at every single check.)
*/
void luaC_step (lua_State& L) {
  global_State *g = G(L);
  lua_assert(!g->getGCEmergency());
  if (!g->isGCRunning()) {  /* not running? */
    if (g->getGCStp() & GCSTPUSR)  /* stopped by the user? */
      luaE_setdebt(g, 20000);
  }
  else {
    luai_tracegc(&L, 1);  /* for internal debugging */
    switch (g->getGCKind()) {
      case GCKind::Incremental: case GCKind::GenerationalMajor:
        incstep(L, *g);
        break;
      case GCKind::GenerationalMinor:
        youngcollection(L, *g);
        setminordebt(g);
        break;
    }
    luai_tracegc(&L, 0);  /* for internal debugging */
  }
}


/*
** Wrapper for GCCollector::fullinc() - now in gc_collector module
*/
static void fullinc (lua_State& L, global_State& g) {
  GCCollector::fullinc(&L, &g);
}


/*
** Performs a full GC cycle; if 'isemergency', set a flag to avoid
** some operations which could change the interpreter state in some
** unexpected ways (running finalizers and shrinking some structures).
*/
void luaC_fullgc (lua_State& L, int isemergency) {
  global_State *g = G(L);
  lua_assert(!g->getGCEmergency());
  g->setGCEmergency(cast_byte(isemergency));  /* set flag */
  switch (g->getGCKind()) {
    case GCKind::GenerationalMinor: fullgen(L, *g); break;
    case GCKind::Incremental: fullinc(L, *g); break;
    case GCKind::GenerationalMajor:
      g->setGCKind(GCKind::Incremental);
      fullinc(L, *g);
      g->setGCKind(GCKind::GenerationalMajor);
      break;
  }
  g->setGCEmergency(0);
}

/* }====================================================== */


/*
** {======================================================
** global_State GC control method implementations
** =======================================================
*/

/*
** Clear all gray lists.
** Called when entering sweep phase or restarting collection.
*/
void global_State::clearGrayLists() {
  *getGrayPtr() = *getGrayAgainPtr() = nullptr;
  *getWeakPtr() = *getAllWeakPtr() = *getEphemeronPtr() = nullptr;
}


/*
** Set the "time" to wait before starting a new incremental cycle.
** Cycle will start when memory usage hits (marked * pause / 100).
*/
void global_State::setPause() {
  l_mem threshold = applygcparam(this, PAUSE, getGCMarked());
  l_mem debt = threshold - getTotalBytes();
  if (debt < 0) debt = 0;
  luaE_setdebt(this, debt);
}


/*
** Set debt for the next minor collection in generational mode.
** Collection triggers when memory grows genminormul% relative to base.
*/
void global_State::setMinorDebt() {
  luaE_setdebt(this, applygcparam(this, MINORMUL, getGCMajorMinor()));
}


/*
** Check whether to shift from minor to major collection.
** Shifts if accumulated old bytes exceeds minormajor% of lived bytes.
*/
int global_State::checkMinorMajor() {
  l_mem limit = applygcparam(this, MINORMAJOR, getGCMajorMinor());
  if (limit == 0)
    return 0;  /* special case: 'minormajor' 0 stops major collections */
  return (getGCMarked() >= limit);
}


/*
** Correct all gray lists for generational mode.
** Coalesces them into 'grayagain' list.
*/
void global_State::correctGrayLists() {
  GCObject **list = correctgraylist(getGrayAgainPtr());
  *list = getWeak(); setWeak(nullptr);
  list = correctgraylist(list);
  *list = getAllWeak(); setAllWeak(nullptr);
  list = correctgraylist(list);
  *list = getEphemeron(); setEphemeron(nullptr);
  correctgraylist(list);
}

/* }====================================================== */


/*
** GCObject method implementations
*/
void GCObject::fix(lua_State* L) const {  /* const - only modifies mutable GC fields */
  global_State *g = G(L);
  lua_assert(g->getAllGC() == this);  /* object must be 1st in 'allgc' list! */
  set2gray(this);  /* they will be gray forever */
  setage(this, GCAge::Old);  /* and old forever */
  g->setAllGC(getNext());  /* remove object from 'allgc' list */
  setNext(g->getFixedGC());  /* link it to 'fixedgc' list */
  g->setFixedGC(this);
}

void GCObject::checkFinalizer(lua_State* L, Table* mt) {
  global_State *g = G(L);
  if (tofinalize(this) ||                 /* obj. is already marked... */
      gfasttm(g, mt, TMS::TM_GC) == nullptr ||    /* or has no finalizer... */
      (g->getGCStp() & GCSTPCLS))                   /* or closing state? */
    return;  /* nothing to be done */
  else {  /* move 'this' to 'finobj' list */
    GCObject **p;
    if (g->isSweepPhase()) {
      makewhite(g, this);  /* "sweep" object 'this' */
      if (g->getSweepGC() == &this->next)  /* should not remove 'sweepgc' object */
        g->setSweepGC(GCSweeping::sweeptolive(L, g->getSweepGC()));  /* change 'sweepgc' */
    }
    else
      correctpointers(*g, this);
    /* search for pointer pointing to 'this' */
    for (p = g->getAllGCPtr(); *p != this; p = (*p)->getNextPtr()) { /* empty */ }
    *p = getNext();  /* remove 'this' from 'allgc' list */
    setNext(g->getFinObj());  /* link it in 'finobj' list */
    g->setFinObj(this);
    setMarkedBit(FINALIZEDBIT);  /* mark it as such */
  }
}


