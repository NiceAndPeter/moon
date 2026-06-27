/*
** Garbage Collector
** See Copyright Notice in lua.h
*/

#ifndef lgc_h
#define lgc_h


#include <cstddef>


#include "mobject.h"
#include "mstate.h"

/*
** TRI-COLOR MARKING INVARIANT
**
** Collectable objects may have one of three colors: white, which means
** the object is not marked; gray, which means the object is marked, but
** its references may be not marked; and black, which means that the
** object and all its references are marked.
**
** The main invariant of the garbage collector, while marking objects, is that
** a black object can never point to a white one. Moreover, any gray object must
** be in a "gray list" (gray, grayagain, weak, allweak, ephemeron) so that it
** can be visited again before finishing the collection cycle. (Open upvalues are
** an exception to this rule, as they are attached to a corresponding thread.)
** These lists have no meaning when the invariant is not being enforced (e.g., sweep phase).
**
** INVARIANT ENFORCEMENT:
** When the program modifies a black object to point to a white object during marking,
** we must restore the invariant using a write barrier. Two strategies:
**
** 1. Forward barrier (barrier-forward): Mark the white object gray
**    - Used when the white object is likely to survive (e.g., table keys)
**    - Prevents marking the same object multiple times
**
** 2. Backward barrier (barrier-back): Mark the black object gray again
**    - Used when the black object is likely to be modified again (e.g., table values)
**    - Avoids marking many objects when writing to frequently-modified tables
**
** INCREMENTAL COLLECTION:
** The tri-color scheme allows the GC to run incrementally. Each GC "step" processes
** some gray objects (marking their children). The program can run between steps.
** Write barriers ensure correctness even though the program modifies objects during GC.
**
** PHASES:
** - Propagate: Process gray list, marking reachable objects
** - Atomic: Final marking pass (runs without interruption)
** - Sweep: Free white objects (those that stayed white are unreachable)
**
** See lgc.cpp for detailed implementation of the marking algorithm.
*/


/*
** macro to tell when main invariant (white objects cannot point to black
** ones) must be kept. During a collection, the sweep phase may break
** the invariant, as objects turned white may point to still-black
** objects. The invariant is restored when sweep ends and all objects
** are white again.
*/

inline bool GlobalState::keepInvariant() const noexcept {
	return getGCState() <= GCState::Atomic;
}

// Check if GC is in sweep phase
inline bool GlobalState::isSweepPhase() const noexcept {
	return GCState::SweepAllGC <= getGCState() && getGCState() <= GCState::SweepEnd;
}


/*
** some useful bit tricks
*/

// Bit mask generation
constexpr int bitmask(int b) noexcept {
    return (1 << b);
}

constexpr lu_byte bit2mask(int b1, int b2) noexcept {
    return cast_byte(bitmask(b1) | bitmask(b2));
}

// Bit testing
constexpr lu_byte testbits(lu_byte x, lu_byte m) noexcept {
    return (x & m);
}

constexpr bool testbit(lu_byte x, int b) noexcept {
    return (testbits(x, cast_byte(bitmask(b))) != 0);
}

// Bit manipulation is now handled through GCObject methods like setMarked()


/*
** Layout for bit use in 'marked' field. First three bits are
** used for object "age" in generational mode. Last bit is used
** by tests.
*/
constexpr int WHITE0BIT = 3;  // object is white (type 0)
constexpr int WHITE1BIT = 4;  // object is white (type 1)
constexpr int BLACKBIT = 5;  // object is black
constexpr int FINALIZEDBIT = 6;  // object has been marked for finalization
constexpr int TESTBIT = 7;

constexpr lu_byte WHITEBITS = bit2mask(WHITE0BIT, WHITE1BIT);

// object age in generational mode
enum class GCAge : lu_byte {
	New       = 0,  // created in current cycle
	Survival  = 1,  // created in previous cycle
	Old0      = 2,  // marked old by frw. barrier in this cycle
	Old1      = 3,  // first full cycle as old
	Old       = 4,  // really old object (not to be visited)
	Touched1  = 5,  // old object touched this cycle
	Touched2  = 6  // old object touched in previous cycle
};

constexpr lu_byte AGEBITS = 7;  // all age bits (111)

// GCObject color and age inline method implementations

inline bool GCObject::isWhite() const noexcept {
  return testbits(marked, WHITEBITS);
}

inline bool GCObject::isBlack() const noexcept {
  return testbit(marked, BLACKBIT);
}

inline bool GCObject::isGray() const noexcept {
  return !testbits(marked, bitmask(BLACKBIT) | WHITEBITS);
}

inline GCAge GCObject::getAge() const noexcept {
  return static_cast<GCAge>(marked & AGEBITS);
}

inline void GCObject::setAge(GCAge age) const noexcept {  // const - marked is mutable
  marked = cast_byte((marked & (~AGEBITS)) | static_cast<lu_byte>(age));
}

inline bool GCObject::isOld() const noexcept {
  return getAge() > GCAge::Survival;
}

template<typename Derived>
inline void GCBase<Derived>::setAge(GCAge age) const noexcept {  // const - marked is mutable
  marked = cast_byte((marked & (~AGEBITS)) | static_cast<lu_byte>(age));
}

template<typename Derived>
inline bool GCBase<Derived>::isOld() const noexcept {
  return getAge() > GCAge::Survival;
}

// Wrapper functions for backward compatibility
// Accept any GC-managed type pointer (uses reinterpret_cast like original macros)
template<typename T>
inline bool iswhite(const T* x) noexcept {
  return reinterpret_cast<const GCObject*>(x)->isWhite();
}

template<typename T>
inline bool isblack(const T* x) noexcept {
  return reinterpret_cast<const GCObject*>(x)->isBlack();
}

template<typename T>
inline bool isgray(const T* x) noexcept {
  return reinterpret_cast<const GCObject*>(x)->isGray();
}

template<typename T>
inline GCAge getage(const T* o) noexcept {
  return reinterpret_cast<const GCObject*>(o)->getAge();
}

template<typename T>
inline void setage(T* o, lu_byte a) noexcept {
  reinterpret_cast<const GCObject*>(o)->setAge(static_cast<GCAge>(a));
}

template<typename T>
inline void setage(T* o, GCAge a) noexcept {
  reinterpret_cast<const GCObject*>(o)->setAge(a);
}

template<typename T>
inline bool isold(const T* o) noexcept {
  return reinterpret_cast<const GCObject*>(o)->isOld();
}

inline bool tofinalize(const GCObject* x) noexcept {
	return testbit(x->getMarked(), FINALIZEDBIT);
}

// Get the "other" white color (for dead object detection)
inline lu_byte otherwhite(const GlobalState* g) noexcept {
	return g->getCurrentWhite() ^ WHITEBITS;
}

// Check if marked value is dead given other-white bits
constexpr bool isdeadm(lu_byte ow, lu_byte m) noexcept {
	return (m & ow) != 0;
}

// Check if a GC object is dead
inline bool isdead(const GlobalState* g, const GCObject* v) noexcept {
	return isdeadm(otherwhite(g), v->getMarked());
}

// Template version for any GC-able type (Table, TString, UpVal, etc.)
template<typename T>
inline bool isdead(const GlobalState* g, const T* v) noexcept {
	return isdeadm(otherwhite(g), reinterpret_cast<const GCObject*>(v)->getMarked());
}

/*
** Check liveness of a value being manipulated by the program.
** Any value being manipulated by the program either is non-collectable,
** or the collectable object has the right tag and it is not dead.
** The option 'L == nullptr' allows this function to be used where L is not available.
*/
inline void checkliveness(moon_State* L, const TValue* obj) noexcept {
	(void)L;
	moon_longassert(!iscollectable(obj) ||
		(righttt(obj) && (L == nullptr || !isdead(G(L), gcvalue(obj)))));
}

inline void changewhite(GCObject* x) noexcept {
	x->setMarked(x->getMarked() ^ WHITEBITS);
}

inline void nw2black(GCObject* x) noexcept {
	x->setMarkedBit(BLACKBIT);
}

inline lu_byte GlobalState::getWhite() const noexcept {
	return cast_byte(getCurrentWhite() & WHITEBITS);
}

// Note: G_NEW, G_SURVIVAL, G_OLD*, G_TOUCHED*, AGEBITS moved above for inline functions
// Note: getage, setage, isold are now inline functions defined above


/*
** In generational mode, objects are created 'new'. After surviving one
** cycle, they become 'survival'. Both 'new' and 'survival' can point
** to any other object, as they are traversed at the end of the cycle.
** We call them both 'young' objects.
** If a survival object survives another cycle, it becomes 'old1'.
** 'old1' objects can still point to survival objects (but not to
** new objects), so they still must be traversed. After another cycle
** (that, being old, 'old1' objects will "survive" no matter what)
** finally the 'old1' object becomes really 'old', and then they
** are no more traversed.
**
** To keep its invariants, the generational mode uses the same barriers
** also used by the incremental mode. If a young object is caught in a
** forward barrier, it cannot become old immediately, because it can
** still point to other young objects. Instead, it becomes 'old0',
** which in the next cycle becomes 'old1'. So, 'old0' objects is
** old but can point to new and survival objects; 'old1' is old
** but cannot point to new objects; and 'old' cannot point to any
** young object.
**
** If any old object ('old0', 'old1', 'old') is caught in a back
** barrier, it becomes 'touched1' and goes into a gray list, to be
** visited at the end of the cycle.  There it evolves to 'touched2',
** which can point to survivals but not to new objects. In yet another
** cycle then it becomes 'old' again.
**
** The generational mode must also control the colors of objects,
** because of the barriers.  While the mutator is running, young objects
** are kept white. 'old', 'old1', and 'touched2' objects are kept black,
** as they cannot point to new objects; exceptions are threads and open
** upvalues, which age to 'old1' and 'old' but are kept gray. 'old0'
** objects may be gray or black, as in the incremental mode. 'touched1'
** objects are kept gray, as they must be visited again at the end of
** the cycle.
*/


/*
** {======================================================
** Default Values for GC parameters
** =======================================================
*/

/*
** Minor collections will shift to major ones after MOONI_MINORMAJOR%
** bytes become old.
*/
inline constexpr int MOONI_MINORMAJOR = 70;

/*
** Major collections will shift to minor ones after a collection
** collects at least MOONI_MAJORMINOR% of the new bytes.
*/
inline constexpr int MOONI_MAJORMINOR = 50;

/*
** A young (minor) collection will run after creating MOONI_GENMINORMUL%
** new bytes.
*/
inline constexpr int MOONI_GENMINORMUL = 20;


// incremental

// Number of bytes must be MOONI_GCPAUSE% before starting new cycle
inline constexpr int MOONI_GCPAUSE = 250;

/*
** Step multiplier: The collector handles MOONI_GCMUL% work units for
** each new allocated word. (Each "work unit" corresponds roughly to
** sweeping one object or traversing one slot.)
*/
inline constexpr int MOONI_GCMUL = 200;

// How many bytes to allocate before next GC step
inline constexpr size_t MOONI_GCSTEPSIZE = (200 * sizeof(Table));


// These macros must remain as macros due to token pasting (##)
// They expand parameter names like STEPMUL to MOON_GCPSTEPMUL at compile time
#define setgcparam(g,p,v)  ((g)->setGCParam(MOON_GCP##p, moonO_codeparam(v)))
#define applygcparam(g,p,x)  moonO_applyparam((g)->getGCParam(MOON_GCP##p), x)

// }======================================================


/*
** Control when GC is running:
*/
inline constexpr lu_byte GCSTPUSR = 1;  // bit true when GC stopped by user
inline constexpr lu_byte GCSTPGC  = 2;  // bit true when GC stopped by itself
inline constexpr lu_byte GCSTPCLS = 4;  // bit true when closing Lua state


/*
** Does one step of collection when debt becomes zero. 'pre'/'pos'
** allows some adjustments to be done only when needed. macro
** 'condchangemem' is used only for heavy tests (forcing a full
** GC cycle on every opportunity)
*/


// Forward declarations needed by template functions
MOONI_FUNC void moonC_step (moon_State& L);
MOONI_FUNC void moonC_fullgc (moon_State& L, int isemergency);

#if !defined(HARDMEMTESTS)
template<typename PreFunc, typename PostFunc>
inline void condchangemem([[maybe_unused]] moon_State* L,
                          [[maybe_unused]] PreFunc pre,
                          [[maybe_unused]] PostFunc post,
                          [[maybe_unused]] int emg) {
	// Empty in normal builds
}
#else
template<typename PreFunc, typename PostFunc>
inline void condchangemem(moon_State* L, PreFunc pre, PostFunc post, int emg) {
	if (G(L)->isGCRunning()) {
		pre();
		moonC_fullgc(*L, emg);
		post();
	}
}
#endif

template<typename PreFunc, typename PostFunc>
inline void moonC_condGC(moon_State* L, PreFunc pre, PostFunc post) {
	if (G(L)->getGCDebt() <= 0) {
		pre();
		moonC_step(*L);
		post();
	}
	condchangemem(L, pre, post, 0);
}

// more often than not, 'pre'/'pos' are empty
inline void moonC_checkGC(moon_State* L) {
	moonC_condGC(L, [](){}, [](){});
}


// Forward declarations for barrier implementation functions
MOONI_FUNC void moonC_barrier_ (moon_State& L, GCObject *o, GCObject *v);
MOONI_FUNC void moonC_barrierback_ (moon_State& L, GCObject *o);

/*
** Write barrier for object-to-object references.
** If 'p' (parent) is black and 'o' (object) is white, mark 'o' gray.
*/
inline void moonC_objbarrier(moon_State* L, GCObject* p, GCObject* o) noexcept {
	if (isblack(p) && iswhite(o))
		moonC_barrier_(*L, obj2gco(p), obj2gco(o));
}

/*
** Write barrier for TValue references.
** If 'v' is collectable, apply object barrier.
*/
inline void moonC_barrier(moon_State* L, GCObject* p, const TValue* v) noexcept {
	if (iscollectable(v))
		moonC_objbarrier(L, p, gcvalue(v));
}

/*
** Backward write barrier for generational GC.
** If 'p' is black and 'o' is white, mark 'p' as gray (move backward).
*/
inline void moonC_objbarrierback(moon_State* L, GCObject* p, GCObject* o) noexcept {
	if (isblack(p) && iswhite(o))
		moonC_barrierback_(*L, p);
}

/*
** Backward write barrier for TValue references.
** If 'v' is collectable, apply backward barrier.
*/
inline void moonC_barrierback(moon_State* L, GCObject* p, const TValue* v) noexcept {
	if (iscollectable(v))
		moonC_objbarrierback(L, p, gcvalue(v));
}

// Use GCObject::fix() method instead of moonC_fix
MOONI_FUNC void moonC_freeallobjects (moon_State& L);

// ARC (automatic reference counting) reclamation engine — moon fork, Phase 1.
// Deferred model: moonC_incref/moonC_decref adjust an object's refcount without
// needing a moon_State (decref queues zero-count objects); moonC_drain(L)
// reclaims the queue at a safe point, recursively releasing children. Cycles
// never reach zero and so leak by design. moonC_release(L,o) is decref+drain for
// convenience. The deinit counter is a debug aid for tests.
inline void moonC_incref (GCObject *o) noexcept { o->retain(); }
MOONI_FUNC void moonC_decref (GCObject *o) noexcept;
MOONI_FUNC void moonC_drain (moon_State& L);
MOONI_FUNC void moonC_release (moon_State& L, GCObject *o);
inline void moonC_retain (GCObject *o) noexcept { o->retain(); }  // alias of incref
MOONI_FUNC unsigned long long moonC_deinitcount() noexcept;
MOONI_FUNC void moonC_resetdeinitcount() noexcept;
// moonC_step and moonC_fullgc declared earlier for template functions
MOONI_FUNC void moonC_runtilstate (moon_State& L, GCState state, int fast);
MOONI_FUNC void propagateall (GlobalState& g);  // used by GCCollector
[[nodiscard]] MOONI_FUNC GCObject *moonC_newobj (moon_State& L, MoonT tt, size_t sz);
[[nodiscard]] MOONI_FUNC GCObject *moonC_newobjdt (moon_State& L, MoonT tt, size_t sz,
                                                 size_t offset);
// moonC_barrier_ and moonC_barrierback_ declared above before inline barrier functions
// Use GCObject::checkFinalizer() method instead of moonC_checkfinalizer
MOONI_FUNC void moonC_changemode (moon_State& L, GCKind newmode);

// Weak table functions
[[nodiscard]] MOONI_FUNC int getmode (GlobalState *g, Table *h);
MOONI_FUNC void traverseweakvalue (GlobalState& g, Table *h);
[[nodiscard]] MOONI_FUNC int traverseephemeron (GlobalState *g, Table *h, int inv);

// Sweeping helper
MOONI_FUNC void freeobj (moon_State& L, GCObject *o);


/*
** {==================================================================
** Placement new operator implementations for GC-allocated objects
** ===================================================================
*/

// CClosure placement new operator
inline void* CClosure::operator new(size_t size, moon_State* L, MoonT tt, size_t extra) {
  return moonC_newobj(*L, tt, size + extra);
}

// LClosure placement new operator
inline void* LClosure::operator new(size_t size, moon_State* L, MoonT tt, size_t extra) {
  return moonC_newobj(*L, tt, size + extra);
}

// Udata placement new operator
inline void* Udata::operator new(size_t size, moon_State* L, MoonT tt, size_t extra) {
  return moonC_newobj(*L, tt, size + extra);
}

// TString placement new operator
inline void* TString::operator new(size_t size, moon_State* L, MoonT tt, size_t extra) {
  return moonC_newobj(*L, tt, size + extra);
}

// Proto placement new operator
inline void* Proto::operator new(size_t size, moon_State* L, MoonT tt) {
  return moonC_newobj(*L, tt, size);
}

// UpVal placement new operator
inline void* UpVal::operator new(size_t size, moon_State* L, MoonT tt) {
  return moonC_newobj(*L, tt, size);
}

// Table placement new operator
inline void* Table::operator new(size_t size, moon_State* L, MoonT tt) {
  return moonC_newobj(*L, tt, size);
}

// }==================================================================


/*
** {==================================================================
** TValue assignment inline functions
** Defined here (not in lobject.h) because they need:
**   - G() from lstate.h
**   - isdead() from lgc.h
** ===================================================================
*/

/*
** TValue assignment operator (simple copy, no GC barriers)
** Use this for stack-to-stack assignments or when you know barriers aren't needed.
*/
inline TValue& TValue::operator=(const TValue& other) noexcept {
	value_ = other.value_;
	tt_ = other.tt_;
	return *this;
}

// }==================================================================


/*
** {==================================================================
** GC color manipulation inline functions
** ===================================================================
*/

// mask with all color bits
constexpr int maskcolors = (bitmask(BLACKBIT) | WHITEBITS);

// mask with all GC bits
constexpr int maskgcbits = (maskcolors | AGEBITS);

/*
** Make an object white (candidate for collection).
** Erases color bits and sets the current white bit (which alternates each cycle).
*/
inline void makewhite(GlobalState* g, GCObject* x) noexcept {
    x->setMarked(cast_byte((x->getMarked() & ~maskcolors) | g->getWhite()));
}

/*
** Make an object gray (in work queue).
** Clears all color bits, resulting in gray (neither white nor black).
** Gray objects are linked into gray lists for incremental processing.
*/
inline void set2gray(const GCObject* x) noexcept {  // const - marked is mutable
    x->clearMarkedBits(maskcolors);
}

/*
** Make an object black (fully processed).
** Sets black bit and clears white bits. Black objects have no more work to do
** in this GC cycle unless the program creates new references to white objects.
*/
inline void set2black(GCObject* x) noexcept {
    x->setMarked(cast_byte((x->getMarked() & ~WHITEBITS) | bitmask(BLACKBIT)));
}

// Check if a TValue contains a white collectable object
inline bool valiswhite(const TValue* x) noexcept {
    return iscollectable(x) && iswhite(gcvalue(x));
}

// Check if a table node's key is white
inline bool keyiswhite(const Node* n) noexcept {
    return n->isKeyCollectable() && iswhite(n->getKeyGC());
}

/*
** Protected access to objects in values (returns nullptr if not collectable)
*/
inline GCObject* gcvalueN(const TValue* o) noexcept {
    return iscollectable(o) ? gcvalue(o) : nullptr;
}

// }==================================================================


#endif
