/*
** Garbage Collector - Finalizer Module
** See Copyright Notice in lua.h
*/

#define LUA_CORE

#include "lprefix.h"

#include <cstring>

#include "gc_finalizer.h"
#include "../lgc.h"
#include "../../core/ldo.h"
#include "../../core/ltm.h"
#include "../../objects/lstring.h"

/*
** GC Finalizer Module Implementation
**
** This module contains all the finalization logic for Lua's garbage collector.
** Finalization allows objects to execute cleanup code (__gc metamethods) before
** being collected.
**
** ORGANIZATION:
** - Utility functions (checkSizes, findlast, checkpointer, correctpointers)
** - Core finalization (udata2finalize, dothecall, GCTM)
** - Finalization control (separatetobefnz, callallpendingfinalizers)
*/

// Note: maskcolors and color manipulation functions are now in lgc.h


/*
** =======================================================
** Utility Functions
** =======================================================
*/

/*
** If possible, shrink string table.
** Called during finalization to optimize memory usage.
*/
void GCFinalizer::checkSizes(lua_State* L, GlobalState& g) {
    if (!g.getGCEmergency()) {
        if (g.getStringTable()->getNumElements() < g.getStringTable()->getSize() / 4)
            TString::resize(L, g.getStringTable()->getSize() / 2);
    }
}


/*
** Find last 'next' field in list 'p' (to add elements at its end).
*/
GCObject** GCFinalizer::findlast(GCObject** p) {
    while (*p != nullptr)
        p = (*p)->getNextPtr();
    return p;
}


/*
** If pointer 'p' points to 'o', move it to the next element.
*/
void GCFinalizer::checkpointer(GCObject** p, GCObject* o) {
    if (o == *p)
        *p = o->getNext();
}


/*
** Correct pointers to objects inside 'allgc' list when
** object 'o' is being removed from the list.
*/
void GCFinalizer::correctpointers(GlobalState& g, GCObject* o) {
    checkpointer(g.getSurvivalPtr(), o);
    checkpointer(g.getOld1Ptr(), o);
    checkpointer(g.getReallyOldPtr(), o);
    checkpointer(g.getFirstOld1Ptr(), o);
}


/*
** =======================================================
** Core Finalization Functions
** =======================================================
*/

/*
** Get the next udata to be finalized from the 'tobefnz' list, and
** link it back into the 'allgc' list.
*/
GCObject* GCFinalizer::udata2finalize(GlobalState& g) {
    GCObject* o = g.getToBeFnz();  // get first element
    lua_assert(tofinalize(o));
    g.setToBeFnz(o->getNext());  // remove it from 'tobefnz' list
    o->setNext(g.getAllGC());  // return it to 'allgc' list
    g.setAllGC(o);
    o->clearMarkedBit(FINALIZEDBIT);  // object is "normal" again
    if (g.isSweepPhase())
        makewhite(&g, o);  // "sweep" object
    else if (getage(o) == GCAge::Old1)
        g.setFirstOld1(o);  // it is the first OLD1 object in the list
    return o;
}


/*
** Helper function for calling finalizer.
** Calls function at stack[top-2] with argument at stack[top-1].
*/
void GCFinalizer::dothecall(lua_State* L, void* ud) {
    UNUSED(ud);
    L->callNoYield(L->getTop().p - 2, 0);
}


/*
** Execute a single finalizer (__gc metamethod).
**
** FINALIZATION PROCESS:
** 1. Get next object from tobefnz list (objects pending finalization)
** 2. Look up its __gc metamethod
** 3. Call the metamethod in protected mode
** 4. Handle any errors by issuing a warning
**
** CRITICAL INVARIANTS DURING FINALIZATION:
** - Disable GC during __gc execution (GCSTPGC flag)
**   Rationale: __gc can allocate, but we can't collect during finalization
**   because it could trigger nested finalizers, leading to reentrancy issues
**
** - Disable debug hooks (setAllowHook(0))
**   Rationale: Debug hooks during __gc could interfere with finalization
**
** - Mark call frame with CIST_FIN flag
**   Rationale: Allows error handling to know we're in a finalizer
**
** ERROR HANDLING:
** Errors in __gc are non-fatal. We issue a warning but continue execution.
** This prevents a badly written __gc from crashing the entire program.
**
** RESURRECTION:
** If __gc stores the object in a global variable or other reachable location,
** the object is "resurrected" and won't be collected. It will be finalized
** again in the next GC cycle if it becomes unreachable again.
*/
void GCFinalizer::GCTM(lua_State* L) {
    GlobalState* g = G(L);
    const TValue* metamethod;
    TValue v;
    lua_assert(!g->getGCEmergency());
    setgcovalue(L, &v, udata2finalize(*g));
    metamethod = luaT_gettmbyobj(L, &v, TMS::TM_GC);

    if (!notm(metamethod)) {  // is there a finalizer?
        TStatus status;
        lu_byte oldah = L->getAllowHook();
        lu_byte oldgcstp = g->getGCStp();
        g->setGCStp(oldgcstp | GCSTPGC);  // avoid GC steps
        L->setAllowHook(0);  // stop debug hooks during GC metamethod
        L->getStackSubsystem().setSlot(L->getTop().p, metamethod);  // push finalizer...
        L->getStackSubsystem().push();
        L->getStackSubsystem().setSlot(L->getTop().p, &v);  // ... and its argument
        L->getStackSubsystem().push();
        L->getCI()->setCallStatus(L->getCI()->getCallStatus() | CIST_FIN);  // will run a finalizer
        status = L->pCall(dothecall, nullptr, L->saveStack(L->getTop().p - 2), 0);
        L->getCI()->setCallStatus(L->getCI()->getCallStatus() & ~CIST_FIN);  // not running a finalizer anymore
        L->setAllowHook(oldah);  // restore hooks
        g->setGCStp(oldgcstp);  // restore state

        if (l_unlikely(status != LUA_OK)) {  // error while running __gc?
            luaE_warnerror(L, "__gc");
            L->getStackSubsystem().pop();  // pops error object
        }
    }
}


/*
** =======================================================
** Finalization Control Functions
** =======================================================
*/

/*
** Move all unreachable objects (or 'all' objects) that need
** finalization from list 'finobj' to list 'tobefnz' (to be finalized).
** (Note that objects after 'finobjold1' cannot be white, so they
** don't need to be traversed. In incremental mode, 'finobjold1' is nullptr,
** so the whole list is traversed.)
*/
void GCFinalizer::separatetobefnz(GlobalState& g, int all) {
    GCObject* curr;
    GCObject** p = g.getFinObjPtr();
    GCObject** lastnext = findlast(g.getToBeFnzPtr());

    while ((curr = *p) != g.getFinObjOld1()) {  // traverse all finalizable objects
        lua_assert(tofinalize(curr));
        if (!(iswhite(curr) || all))  // not being collected?
            p = curr->getNextPtr();  // don't bother with it
        else {
            if (curr == g.getFinObjSur())  // removing 'finobjsur'?
                g.setFinObjSur(curr->getNext());  // correct it
            *p = curr->getNext();  /* remove 'curr' from 'finobj' list */
            curr->setNext(*lastnext);  // link at the end of 'tobefnz' list
            *lastnext = curr;
            lastnext = curr->getNextPtr();
        }
    }
}


/*
** Call all pending finalizers.
** Processes entire tobefnz list until empty.
*/
void GCFinalizer::callallpendingfinalizers(lua_State* L) {
    GlobalState* g = G(L);
    while (g->getToBeFnz())
        GCTM(L);
}
