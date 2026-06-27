/*
** Garbage Collector - Sweeping Module
** See Copyright Notice in lua.h
*/

#define MOON_CORE

#include "mprefix.h"

#include <cstring>

#include "gc_sweeping.h"
#include "../mgc.h"
#include "../../core/mdo.h"
#include "../../objects/mtable.h"
#include "../../objects/mfunc.h"
#include "../../objects/mstring.h"
#include "gc_marking.h"

/*
** GC Sweeping Module Implementation
**
** This module contains all the sweep-phase logic for Lua's tri-color
** incremental garbage collector. The sweeping phase removes dead objects
** (white objects after marking completes) and prepares surviving objects
** for the next collection cycle.
**
** ORGANIZATION:
** - Core sweeping functions (sweeplist, sweeptolive)
** - Generational sweeping (sweep2old, sweepgen)
** - Sweep control (entersweep, sweepstep)
** - Utility functions (deletelist)
*/

// How many objects to sweep in one step (incremental sweep limit)
#define GCSWEEPMAX	20

// Note: maskcolors, maskgcbits, and color manipulation functions are now in lgc.h
#include "gc_core.h"  // For utility functions

// Note: objsize is now in GCCore module
static inline l_mem objsize(GCObject* o) { return GCCore::objsize(o); }

/*
** Link object into a GC list and make it gray
*/
static void linkgclist_(GCObject* o, GCObject** pnext, GCObject** list) {
    moon_assert(!isgray(o));  // cannot be in a gray list
    *pnext = *list;
    *list = o;
    set2gray(o);  // now it is
}

// Link moon_State into GC list
static inline void linkgclistThread(moon_State* th, GCObject*& p) {
    linkgclist_(obj2gco(th), th->getGclistPtr(), &p);
}


/*
** =======================================================
** Core Sweeping Functions
** =======================================================
*/

/*
** Sweep a list of GC objects.
** Removes dead objects (white objects after marking) and prepares
** surviving objects for next cycle (resets to current white and age New).
**
** 'p': pointer to pointer to start of list
** 'countin': maximum number of objects to sweep (for incremental collection)
**
** Returns: pointer to where sweeping stopped (nullptr if list exhausted)
*/
GCObject** GCSweeping::sweeplist(moon_State* L, GCObject** p, l_mem countin) {
    GlobalState* g = G(L);
    lu_byte ow = otherwhite(g);
    lu_byte white = g->getWhite();  // current white

    while (*p != nullptr && countin-- > 0) {
        GCObject* curr = *p;
        lu_byte marked = curr->getMarked();

        if (isdeadm(ow, marked)) {  // is 'curr' dead?
            *p = curr->getNext();  /* remove 'curr' from list */
            freeobj(*L, curr);  // erase 'curr'
        }
        else {  // change mark to 'white' and age to 'new'
            curr->setMarked(cast_byte((marked & ~maskgcbits) |
                                       white |
                                       static_cast<lu_byte>(GCAge::New)));
            p = curr->getNextPtr();  // go to next element
        }
    }

    return (*p == nullptr) ? nullptr : p;
}


/*
** Sweep a list until finding a live object (or end of list).
** Used to find the starting point for continued sweeping.
*/
GCObject** GCSweeping::sweeptolive(moon_State* L, GCObject** p) {
    GCObject** old = p;
    do {
        p = sweeplist(L, p, 1);
    } while (p == old);
    return p;
}


/*
** =======================================================
** Generational Sweeping Functions
** =======================================================
*/

/*
** Sweep for generational mode transition (atomic2gen).
** All surviving objects become old. Dead objects are freed.
** This is called when transitioning from incremental to generational mode.
*/
void GCSweeping::sweep2old(moon_State* L, GCObject** p) {
    GCObject* curr;
    GlobalState* g = G(L);

    while ((curr = *p) != nullptr) {
        if (iswhite(curr)) {  // is 'curr' dead?
            moon_assert(isdead(g, curr));
            *p = curr->getNext();  /* remove 'curr' from list */
            freeobj(*L, curr);  // erase 'curr'
        }
        else {  // all surviving objects become old
            setage(curr, GCAge::Old);

            if (curr->getType() == ctb(MoonT::THREAD)) {  // threads must be watched
                moon_State* th = gco2th(curr);
                linkgclistThread(th, *g->getGrayAgainPtr());  // insert into 'grayagain' list
            }
            else if (curr->getType() == ctb(MoonT::UPVAL) && gco2upv(curr)->isOpen())
                set2gray(curr);  // open upvalues are always gray
            else  // everything else is black
                nw2black(curr);

            p = curr->getNextPtr();  // go to next element
        }
    }
}


/*
** Sweep for generational mode.
** Delete dead objects. (Because the collection is not incremental, there
** are no "new white" objects during the sweep. So, any white object must
** be dead.) For non-dead objects, advance their ages and clear the color
** of new objects. (Old objects keep their colors.)
**
** The ages of GCAge::Touched1 and GCAge::Touched2 objects cannot be advanced
** here, because these old-generation objects are usually not swept here.
** They will all be advanced in 'correctgraylist'. That function will also
** remove objects turned white here from any gray list.
*/
GCObject** GCSweeping::sweepgen(moon_State* L, GlobalState& g, GCObject** p,
                                 GCObject* limit, GCObject** pfirstold1,
                                 l_mem* paddedold) {
    static const GCAge nextage[] = {
        GCAge::Survival,  // from GCAge::New
        GCAge::Old1,  // from GCAge::Survival
        GCAge::Old1,  // from GCAge::Old0
        GCAge::Old,  // from GCAge::Old1
        GCAge::Old,  // from GCAge::Old (do not change)
        GCAge::Touched1,  // from GCAge::Touched1 (do not change)
        GCAge::Touched2  // from GCAge::Touched2 (do not change)
    };

    l_mem addedold = 0;
    int white = g.getWhite();
    GCObject* curr;

    while ((curr = *p) != limit) {
        if (iswhite(curr)) {  // is 'curr' dead?
            moon_assert(!isold(curr) && isdead(&g, curr));
            *p = curr->getNext();  /* remove 'curr' from list */
            freeobj(*L, curr);  // erase 'curr'
        }
        else {  // correct mark and age
            GCAge age = getage(curr);
            if (age == GCAge::New) {  // new objects go back to white
                int marked = curr->getMarked() & ~maskgcbits;  // erase GC bits
                curr->setMarked(cast_byte(marked | static_cast<lu_byte>(GCAge::Survival) | white));
            }
            else {  // all other objects will be old, and so keep their color
                moon_assert(age != GCAge::Old1);  // advanced in 'markold'
                setage(curr, nextage[static_cast<size_t>(age)]);
                if (getage(curr) == GCAge::Old1) {
                    addedold += objsize(curr);  // bytes becoming old
                    if (*pfirstold1 == nullptr)
                        *pfirstold1 = curr;  /* first OLD1 object in the list */
                }
            }
            p = curr->getNextPtr();  // go to next element
        }
    }

    *paddedold += addedold;
    return p;
}


/*
** =======================================================
** Sweep Control Functions
** =======================================================
*/

/*
** Enter the sweep phase.
** Sets up sweep state and finds first live object to start sweeping from.
*/
void GCSweeping::entersweep(moon_State* L) {
    GlobalState* g = G(L);
    g->setGCState(GCState::SweepAllGC);
    moon_assert(g->getSweepGC() == nullptr);
    g->setSweepGC(sweeptolive(L, g->getAllGCPtr()));
}


/*
** Perform one step of sweeping.
** Sweeps up to GCSWEEPMAX objects (or all remaining if 'fast' is true).
** When current sweep completes, advances to 'nextstate' and sets up 'nextlist'.
*/
void GCSweeping::sweepstep(moon_State* L, GlobalState& g,
                           GCState nextstate, GCObject** nextlist, int fast) {
    if (g.getSweepGC())
        g.setSweepGC(sweeplist(L, g.getSweepGC(), fast ? MAX_LMEM : GCSWEEPMAX));
    else {  // enter next state
        g.setGCState(nextstate);
        g.setSweepGC(nextlist);
    }
}


/*
** =======================================================
** Utility Functions
** =======================================================
*/

/*
** Delete all objects in list 'p' until (but not including) object 'limit'.
** Used for cleanup and shutdown operations.
*/
void GCSweeping::deletelist(moon_State* L, GCObject* p, GCObject* limit) {
    while (p != limit) {
        GCObject* next = p->getNext();
        freeobj(*L, p);
        p = next;
    }
}
