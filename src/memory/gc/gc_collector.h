/*
** Garbage Collector - Main Orchestration Module
** See Copyright Notice in lua.h
*/

#ifndef gc_collector_h
#define gc_collector_h

#include "../../core/lstate.h"
#include "../lgc.h"

/*
** GCCollector - Main GC orchestration and control
**
** This module handles the high-level orchestration of garbage collection
** phases and the coordination between incremental and generational modes.
**
** KEY RESPONSIBILITIES:
** - Atomic phase coordination (atomic)
** - Incremental step execution (singlestep, incstep)
** - Generational collection management (youngcollection, entergen, atomic2gen)
** - Mode transitions (minor2inc, fullinc, fullgen)
** - Collection completion (finishgencycle, checkmajorminor)
**
** INCREMENTAL VS GENERATIONAL:
** - Incremental: Interleaves GC work with program execution in small steps
** - Generational: Exploits object lifetime patterns (most objects die young)
**
** GC PHASES (Incremental):
** 1. Pause: Idle, waiting for next cycle
** 2. Propagate: Mark gray objects incrementally
** 3. Atomic: Complete marking, handle weak tables, start sweep
** 4. Sweep*: Free dead objects incrementally
** 5. CallFin: Run finalizers
**
** This module contains the main control logic that drives the GC through
** these phases and handles transitions between collection modes.
*/
class GCCollector {
public:
    /*
    ** ATOMIC PHASE
    ** Completes the marking phase in one indivisible step:
    ** - Propagates all remaining gray objects
    ** - Handles weak tables (ephemerons)
    ** - Separates finalizable objects
    ** - Flips white color for next cycle
    */
    static void atomic(moon_State* L);

    /*
    ** INCREMENTAL STEPPING
    ** Performs one small GC step to interleave with program execution.
    ** Returns: work done in units, or special values for state changes.
    ** - fast=true: skip propagation, do everything in atomic phase
    */
    static l_mem singlestep(moon_State* L, int fast);

    /*
    ** INCREMENTAL STEP WITH WORK CALCULATION
    ** Performs a basic incremental step with automatic work sizing.
    ** Loops until sufficient work is done or cycle completes.
    */
    static void incstep(moon_State* L, GlobalState* g);

    /*
    ** GENERATIONAL YOUNG COLLECTION
    ** Performs a minor collection in generational mode:
    ** - Mark OLD1 objects
    ** - Run atomic phase
    ** - Sweep young generation
    ** - Decide whether to shift to major mode
    */
    static void youngcollection(moon_State* L, GlobalState* g);

    /*
    ** TRANSITION: ATOMIC TO GENERATIONAL
    ** Converts from incremental mode after atomic phase.
    ** Sweeps all objects to old, sets up generational sublists.
    */
    static void atomic2gen(moon_State* L, GlobalState* g);

    /*
    ** ENTER GENERATIONAL MODE
    ** Transitions from incremental to generational mode:
    ** - Completes current cycle to atomic phase
    ** - Converts all objects to old
    ** - Sets up generational parameters
    */
    static void entergen(moon_State* L, GlobalState* g);

    /*
    ** TRANSITION: MINOR TO INCREMENTAL
    ** Shifts from minor (generational) to major (incremental) collection.
    ** Sets up for sweep-all phase to handle black objects.
    */
    static void minor2inc(moon_State* L, GlobalState* g, GCKind kind);

    /*
    ** FULL INCREMENTAL COLLECTION
    ** Performs a complete GC cycle in incremental mode.
    ** Used for explicit collection requests.
    */
    static void fullinc(moon_State* L, GlobalState* g);

    /*
    ** FULL GENERATIONAL COLLECTION
    ** Performs a complete collection in generational mode.
    ** Temporarily switches to incremental for full sweep.
    */
    static void fullgen(moon_State* L, GlobalState* g);

    /*
    ** FINISH YOUNG COLLECTION
    ** Completes a young-generation collection:
    ** - Corrects gray lists
    ** - Checks sizes
    ** - Runs pending finalizers
    */
    static void finishgencycle(moon_State* L, GlobalState* g);

    /*
    ** CHECK MAJOR-TO-MINOR TRANSITION
    ** After atomic phase in major mode, check if can return to minor mode.
    ** Returns 1 if transitioned back to minor, 0 if staying in major.
    */
    static int checkmajorminor(moon_State* L, GlobalState* g);

    // Special return values for singlestep()
    static constexpr l_mem STEP_2_PAUSE = -3;  // finished collection; entered pause state
    static constexpr l_mem ATOMIC_STEP = -2;  // atomic step
    static constexpr l_mem STEP_2_MINOR = -1;  // moved to minor collections
};

#endif  // gc_collector_h
