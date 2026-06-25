/*
** Garbage Collector - Finalizer Module
** See Copyright Notice in lua.h
*/

#ifndef gc_finalizer_h
#define gc_finalizer_h

#include "../../core/lstate.h"
#include "../lgc.h"
#include "../../objects/lobject.h"

/*
** GCFinalizer - Encapsulates all garbage collector finalization logic
**
** This module handles the finalization phase of the garbage collector.
** Finalization executes __gc metamethods on objects before they are
** collected, allowing proper cleanup of resources.
**
** KEY CONCEPTS:
** - separatetobefnz(): Moves unreachable finalizable objects to tobefnz list
** - GCTM(): Executes a single __gc metamethod in protected mode
** - callallpendingfinalizers(): Runs all pending finalizers
** - udata2finalize(): Gets next object to finalize
**
** FINALIZATION INVARIANTS:
** - GC is disabled during __gc execution (prevents reentrancy)
** - Debug hooks are disabled during __gc (prevents interference)
** - Call frames are marked with CIST_FIN flag
** - Errors in __gc are non-fatal (issue warning, continue)
**
** RESURRECTION:
** If __gc stores an object in a reachable location, the object is
** "resurrected" and won't be collected. It will be finalized again
** in the next cycle if it becomes unreachable again.
*/
class GCFinalizer {
public:
    /*
    ** Shrink string table if it's too sparse.
    ** Called during finalization phase to optimize memory.
    */
    static void checkSizes(lua_State* L, global_State& g);

    /*
    ** Move all unreachable finalizable objects to the tobefnz list.
    ** If 'all' is true, moves all finalizable objects regardless of color.
    */
    static void separatetobefnz(global_State& g, int all);

    /*
    ** Execute a single finalizer (__gc metamethod).
    ** Gets next object from tobefnz, calls its __gc in protected mode.
    */
    static void GCTM(lua_State* L);

    /*
    ** Call all pending finalizers.
    ** Processes entire tobefnz list until empty.
    */
    static void callallpendingfinalizers(lua_State* L);

    /*
    ** Correct pointers when removing object 'o' from allgc list.
    ** Updates survival, old1, reallyold, firstold1 pointers if needed.
    */
    static void correctpointers(global_State& g, GCObject* o);

private:
    /*
    ** Get next object to finalize from tobefnz list.
    ** Removes from tobefnz, adds back to allgc, clears FINALIZEDBIT.
    */
    static GCObject* udata2finalize(global_State& g);

    /*
    ** Helper: find last 'next' field in list (to add elements at end).
    */
    static GCObject** findlast(GCObject** p);

    /*
    ** Helper: if pointer 'p' points to 'o', move it to next element.
    */
    static void checkpointer(GCObject** p, GCObject* o);

    /*
    ** Helper: wrapper for calling finalizer function.
    */
    static void dothecall(lua_State* L, void* ud);
};

#endif /* gc_finalizer_h */
