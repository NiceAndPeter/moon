/*
** Garbage Collector - Sweeping Module
** See Copyright Notice in lua.h
*/

#ifndef gc_sweeping_h
#define gc_sweeping_h

#include "../../core/lstate.h"
#include "../lgc.h"
#include "../../objects/lobject.h"

/*
** GCSweeping - Encapsulates all garbage collector sweeping logic
**
** This module handles the sweep phase of the tri-color garbage collector.
** Sweeping removes dead objects (white objects after marking completes)
** and prepares surviving objects for the next collection cycle.
**
** KEY CONCEPTS:
** - sweeplist(): Sweeps a list of objects, freeing dead ones
** - sweeptolive(): Sweeps until finding a live object
** - sweep2old(): Sweeps and ages objects for generational GC
** - sweepgen(): Generational sweep with age management
**
** INCREMENTAL SWEEPING:
** The sweeping can be done incrementally - sweeplist() processes a limited
** number of objects at a time, allowing the collector to interleave with
** program execution.
**
** GENERATIONAL MODE:
** In generational mode, sweeping also manages object ages (New, Survival,
** Old0, Old1, Old, Touched1, Touched2) to optimize collection of young objects.
*/
class GCSweeping {
public:
    /*
    ** Sweep a list of objects, removing dead ones.
    ** 'countin' limits how many objects to sweep (for incremental collection).
    ** Returns pointer to where sweeping stopped (nullptr if list exhausted).
    */
    static GCObject** sweeplist(lua_State* L, GCObject** p, l_mem countin);

    /*
    ** Sweep a list until finding a live object (or end of list).
    ** Returns pointer to first live object (or nullptr).
    */
    static GCObject** sweeptolive(lua_State* L, GCObject** p);

    /*
    ** Sweep for generational mode transition (atomic2gen).
    ** All surviving objects become old. Dead objects are freed.
    */
    static void sweep2old(lua_State* L, GCObject** p);

    /*
    ** Sweep for generational mode.
    ** Advances ages of surviving objects and removes dead ones.
    ** Returns pointer to where sweeping stopped.
    */
    static GCObject** sweepgen(lua_State* L, global_State& g, GCObject** p,
                               GCObject* limit, GCObject** pfirstold1,
                               l_mem* paddedold);

    /*
    ** Enter the sweep phase.
    ** Sets up sweep state and finds first live object.
    */
    static void entersweep(lua_State* L);

    /*
    ** Perform one step of sweeping.
    ** Advances to 'nextstate' when current sweep completes.
    */
    static void sweepstep(lua_State* L, global_State& g,
                          GCState nextstate, GCObject** nextlist, int fast);

    /*
    ** Delete all objects in list until 'limit' (not including limit).
    ** Used for cleanup and shutdown.
    */
    static void deletelist(lua_State* L, GCObject* p, GCObject* limit);
};

#endif /* gc_sweeping_h */
