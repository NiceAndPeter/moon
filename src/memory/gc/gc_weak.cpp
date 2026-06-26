/*
** Garbage Collector - Weak Table Module
** See Copyright Notice in lua.h
*/

#define LUA_CORE

#include "lprefix.h"

#include <cstring>

#include "gc_weak.h"
#include "../lgc.h"
#include "gc_marking.h"
#include "../../core/ltm.h"
#include "../../objects/lstring.h"
#include "../../objects/ltable.h"

/*
** GC Weak Table Module Implementation
**
** This module contains all the weak table logic for Lua's garbage collector.
** Weak tables allow keys or values to be collected even if they're referenced
** in the table, enabling caches and ephemeron tables.
**
** ORGANIZATION:
** - Helper functions (genlink)
** - Mode detection (getmode)
** - Traversal (traverseweakvalue, traverseephemeron)
** - Convergence (convergeephemerons)
** - Clearing (clearbykeys, clearbyvalues)
*/

/* Mask with all color bits */
#define maskcolors (bitmask(BLACKBIT) | WHITEBITS)

/* Access to collectable objects in array part of tables */
inline GCObject* gcvalarr(Table* t, unsigned int i) noexcept {
	return iscollectable(*(t)->getArrayTag(i)) ? (t)->getArrayVal(i)->gc : nullptr;
}

/* Note: gcvalueN and valiswhite are now in lgc.h */
/* Note: markkey and markvalue are defined in gc_marking.h */
#include "gc_core.h"  /* For utility functions */

/*
** Barrier for weak tables. Strings behave as 'values', so are never removed.
** For other objects: if really collected, cannot keep them; for objects
** being finalized, keep them in keys, but not in values.
*/
static bool iscleared(GlobalState& g, const GCObject* o) {
    if (o == nullptr) return false;  /* non-collectable value */
    else if (novariant(o->getType()) == LUA_TSTRING) {
        markobject(g, o);  /* strings are 'values', so are never weak */
        return false;
    }
    else return iswhite(o);
}

/* Note: clearkey is now in GCCore module */
static inline void clearkey(Node* n) { GCCore::clearkey(n); }

/*
** Get last node in hash array (one past the end)
*/
static inline Node* gnodelast(Table* h) noexcept {
    return gnode(h, h->nodeSize());
}

/*
** Get pointer to gclist field for different object types
** (just forward to GCCore implementation)
*/
static inline GCObject** getgclist(GCObject* o) {
    return GCCore::getgclist(o);
}

/*
** Link object into a GC list and make it gray
*/
static void linkgclist_(GCObject* o, GCObject** pnext, GCObject** list) {
    lua_assert(!isgray(o));
    *pnext = *list;
    *list = o;
    o->clearMarkedBits(maskcolors);  /* set2gray */
}

template<typename T>
inline void linkobjgclist(T* o, GCObject*& p) {
	linkgclist_(obj2gco(o), getgclist(o), &p);
}

/* Link Table into GC list */
static inline void linkgclistTable(Table* h, GCObject*& p) {
    linkgclist_(obj2gco(h), h->getGclistPtr(), &p);
}


/*
** =======================================================
** Helper Functions
** =======================================================
*/

/*
** Link object to appropriate gray list based on generational mode.
** Handles Touched1/Touched2 ages for generational collector.
*/
void GCWeak::genlink(GlobalState& g, GCObject* o) {
    lua_assert(isblack(o));
    if (getage(o) == GCAge::Touched1) {  /* touched in this cycle? */
        linkobjgclist(o, *g.getGrayAgainPtr());  /* link it back in 'grayagain' */
    }  /* everything else does not need to be linked back */
    else if (getage(o) == GCAge::Touched2)
        setage(o, GCAge::Old);  /* advance age */
}


/*
** =======================================================
** Mode Detection
** =======================================================
*/

/*
** Get weak mode of table from its metatable's __mode field.
** Returns: (result & 1) iff weak values; (result & 2) iff weak keys
*/
int GCWeak::getmode(GlobalState& g, Table* h) {
    const TValue* mode = gfasttm(&g, h->getMetatable(), TMS::TM_MODE);
    if (mode == nullptr || !ttisshrstring(mode))
        return 0;  /* ignore non-(short)string modes */
    else {
        const char* smode = getShortStringContents(tsvalue(mode));
        const char* weakkey = strchr(smode, 'k');
        const char* weakvalue = strchr(smode, 'v');
        return ((weakkey != nullptr) << 1) | (weakvalue != nullptr);
    }
}


/*
** =======================================================
** Weak Table Traversal
** =======================================================
*/

/*
** Traverse array part of a table.
** Returns true if any object was marked during traversal.
*/
static int traversearray(GlobalState& g, Table* h) {
    unsigned asize = h->arraySize();
    int marked = 0;  /* true if some object is marked in this traversal */
    unsigned i;
    for (i = 0; i < asize; i++) {
        GCObject* o = gcvalarr(h, i);
        if (o != nullptr && iswhite(o)) {
            marked = 1;
            GCMarking::reallymarkobject(g, o);
        }
    }
    return marked;
}


/*
** Traverse a table with weak values and link it to proper list.
** During propagate phase, keep it in 'grayagain' list, to be revisited
** in the atomic phase. In the atomic phase, if table has any white value,
** put it in 'weak' list, to be cleared; otherwise, call 'genlink' to
** check table age in generational mode.
*/
void GCWeak::traverseweakvalue(GlobalState& g, Table* h) {
    Node* n;
    Node* limit = gnodelast(h);
    /* if there is array part, assume it may have white values
       (it is not worth traversing it now just to check) */
    int hasclears = (h->arraySize() > 0);

    for (n = gnode(h, 0); n < limit; n++) {  /* traverse hash part */
        if (isempty(gval(n)))  /* entry is empty? */
            clearkey(n);  /* clear its key */
        else {
            lua_assert(!n->isKeyNil());
            markkey(g, n);
            if (!hasclears && iscleared(g, gcvalueN(gval(n))))  /* a white value? */
                hasclears = 1;  /* table will have to be cleared */
        }
    }

    if (g.getGCState() == GCState::Propagate)
        linkgclistTable(h, *g.getGrayAgainPtr());  /* must retraverse it in atomic phase */
    else if (hasclears)
        linkgclistTable(h, *g.getWeakPtr());  /* has to be cleared later */
    else
        genlink(g, obj2gco(h));
}


/*
** Traverse an ephemeron table and link it to proper list. Returns true
** iff any object was marked during this traversal (which implies that
** convergence has to continue). During propagation phase, keep table
** in 'grayagain' list, to be visited again in the atomic phase. In
** the atomic phase, if table has any white->white entry, it has to
** be revisited during ephemeron convergence (as that key may turn
** black). Otherwise, if it has any white key, table has to be cleared
** (in the atomic phase). In generational mode, some tables must be kept
** in some gray list for post-processing; this is done by 'genlink'.
*/
int GCWeak::traverseephemeron(GlobalState& g, Table* h, int inv) {
    int hasclears = 0;  /* true if table has white keys */
    int hasww = 0;  /* true if table has entry "white-key -> white-value" */
    unsigned int i;
    unsigned int nsize = h->nodeSize();
    int marked = traversearray(g, h);  /* traverse array part */

    /* traverse hash part; if 'inv', traverse descending
       (see 'convergeephemerons') */
    for (i = 0; i < nsize; i++) {
        Node* n = inv ? gnode(h, nsize - 1 - i) : gnode(h, i);
        if (isempty(gval(n)))  /* entry is empty? */
            clearkey(n);  /* clear its key */
        else if (iscleared(g, n->getKeyGCOrNull())) {  /* key is not marked (yet)? */
            hasclears = 1;  /* table must be cleared */
            if (valiswhite(gval(n)))  /* value not marked yet? */
                hasww = 1;  /* white-white entry */
        }
        else if (valiswhite(gval(n))) {  /* value not marked yet? */
            marked = 1;
            markvalue(g, gval(n));  /* mark it now */
        }
    }

    /* link table into proper list */
    if (g.getGCState() == GCState::Propagate)
        linkgclistTable(h, *g.getGrayAgainPtr());  /* must retraverse it in atomic phase */
    else if (hasww)  /* table has white->white entries? */
        linkgclistTable(h, *g.getEphemeronPtr());  /* have to propagate again */
    else if (hasclears)  /* table has white keys? */
        linkgclistTable(h, *g.getAllWeakPtr());  /* may have to clean white keys */
    else
        genlink(g, obj2gco(h));  /* check whether collector still needs to see it */

    return marked;
}


/*
** =======================================================
** Ephemeron Convergence
** =======================================================
*/

/*
** Traverse all ephemeron tables propagating marks from keys to values.
** Repeat until it converges, that is, nothing new is marked. 'dir'
** inverts the direction of the traversals, trying to speed up
** convergence on chains in the same table.
*/
void GCWeak::convergeephemerons(GlobalState& g) {
    int changed;
    int dir = 0;
    do {
        GCObject* w;
        GCObject* next = g.getEphemeron();  /* get ephemeron list */
        g.setEphemeron(nullptr);  /* tables may return to this list when traversed */
        changed = 0;
        while ((w = next) != nullptr) {  /* for each ephemeron table */
            Table* h = gco2t(w);
            next = h->getGclist();  /* list is rebuilt during loop */
            nw2black(h);  /* out of the list (for now) */
            if (traverseephemeron(g, h, dir)) {  /* marked some value? */
                GCMarking::propagateall(g);  /* propagate changes */
                changed = 1;  /* will have to revisit all ephemeron tables */
            }
        }
        dir = !dir;  /* invert direction next time */
    } while (changed);  /* repeat until no more changes */
}


/*
** =======================================================
** Weak Table Clearing
** =======================================================
*/

/*
** Clear entries with unmarked keys from all weak tables in list 'l'.
** Called in atomic phase after marking completes.
*/
void GCWeak::clearbykeys(GlobalState& g, GCObject* l) {
    for (; l; l = gco2t(l)->getGclist()) {
        Table* h = gco2t(l);
        Node* limit = gnodelast(h);
        Node* n;
        for (n = gnode(h, 0); n < limit; n++) {
            if (iscleared(g, n->getKeyGCOrNull()))  /* unmarked key? */
                setempty(gval(n));  /* remove entry */
            if (isempty(gval(n)))  /* is entry empty? */
                clearkey(n);  /* clear its key */
        }
    }
}


/*
** Clear entries with unmarked values from all weak tables in list 'l'
** up to element 'f'.
** Called in atomic phase after marking completes.
*/
void GCWeak::clearbyvalues(GlobalState& g, GCObject* l, GCObject* f) {
    for (; l != f; l = gco2t(l)->getGclist()) {
        Table* h = gco2t(l);
        Node* n;
        Node* limit = gnodelast(h);
        unsigned int i;
        unsigned int asize = h->arraySize();

        for (i = 0; i < asize; i++) {
            GCObject* o = gcvalarr(h, i);
            if (iscleared(g, o))  /* value was collected? */
                *h->getArrayTag(i) = LuaT::EMPTY;  /* remove entry */
        }

        for (n = gnode(h, 0); n < limit; n++) {
            if (iscleared(g, gcvalueN(gval(n))))  /* unmarked value? */
                setempty(gval(n));  /* remove entry */
            if (isempty(gval(n)))  /* is entry empty? */
                clearkey(n);  /* clear its key */
        }
    }
}
