/*
** Garbage Collector - Main Orchestration Module
** See Copyright Notice in lua.h
*/

#define MOON_CORE

#include "mprefix.h"

#include "gc_collector.h"
#include "gc_core.h"
#include "gc_marking.h"
#include "gc_sweeping.h"
#include "gc_finalizer.h"
#include "gc_weak.h"
#include "../mgc.h"
#include "../../core/mstate.h"
#include "../../objects/mstring.h"

/*
** Maximum number of elements to sweep in each single step.
*/
#define GCSWEEPMAX	20

/*
** Cost (in work units) of running one finalizer.
*/
#define CWUFIN	10


// Note: propagateall and moonC_runtilstate are declared in lgc.h


/*
** ATOMIC PHASE
** Completes marking in one indivisible step, handles weak tables,
** separates finalizable objects, and flips white color.
*/
void GCCollector::atomic(moon_State* L) {
  GlobalState* g = G(L);
  GCObject *origweak, *origall;
  GCObject *grayagain = g->getGrayAgain();  // save original list
  g->setGrayAgain(nullptr);
  moon_assert(g->getEphemeron() == nullptr && g->getWeak() == nullptr);
  moon_assert(!iswhite(mainthread(g)));
  g->setGCState(GCState::Atomic);
  markobject(*g, L);  // mark running thread
  // registry and global metatables may be changed by API
  markvalue(*g, g->getRegistry());
  GCMarking::markmt(*g);  // mark global metatables
  propagateall(*g);  // empties 'gray' list
  // remark occasional upvalues of (maybe) dead threads
  GCMarking::remarkupvals(*g);
  propagateall(*g);  // propagate changes
  g->setGray(grayagain);
  propagateall(*g);  // traverse 'grayagain' list
  GCWeak::convergeephemerons(*g);
  // at this point, all strongly accessible objects are marked.
  // Clear values from weak tables, before checking finalizers
  GCWeak::clearbyvalues(*g, g->getWeak(), nullptr);
  GCWeak::clearbyvalues(*g, g->getAllWeak(), nullptr);
  origweak = g->getWeak(); origall = g->getAllWeak();
  GCFinalizer::separatetobefnz(*g, 0);  // separate objects to be finalized
  GCMarking::markbeingfnz(*g);  // mark objects that will be finalized
  propagateall(*g);  // remark, to propagate 'resurrection'
  GCWeak::convergeephemerons(*g);
  // at this point, all resurrected objects are marked.
  // remove dead objects from weak tables
  GCWeak::clearbykeys(*g, g->getEphemeron());  // clear keys from all ephemeron
  GCWeak::clearbykeys(*g, g->getAllWeak());  // clear keys from all 'allweak'
  // clear values from resurrected weak tables
  GCWeak::clearbyvalues(*g, g->getWeak(), origweak);
  GCWeak::clearbyvalues(*g, g->getAllWeak(), origall);
  TString::clearCache(g);
  g->setCurrentWhite(cast_byte(otherwhite(g)));  // flip current white
  moon_assert(g->getGray() == nullptr);
}


/*
** FINISH YOUNG COLLECTION
** Completes a young-generation collection.
*/
void GCCollector::finishgencycle(moon_State* L, GlobalState* g) {
  g->correctGrayLists();
  GCFinalizer::checkSizes(L, *g);
  g->setGCState(GCState::Propagate);  // skip restart
  if (!g->getGCEmergency())
    GCFinalizer::callallpendingfinalizers(L);
}


/*
** TRANSITION: MINOR TO INCREMENTAL
** Shifts from minor collection to major collections.
** Starts in sweep-all state to clear all objects (mostly black in gen mode).
*/
void GCCollector::minor2inc(moon_State* L, GlobalState* g, GCKind kind) {
  g->setGCMajorMinor(g->getGCMarked());  // number of live bytes
  g->setGCKind(kind);
  g->setReallyOld(nullptr); g->setOld1(nullptr); g->setSurvival(nullptr);
  g->setFinObjROld(nullptr); g->setFinObjOld1(nullptr); g->setFinObjSur(nullptr);
  GCSweeping::entersweep(L);  // continue as an incremental cycle
  // set a debt equal to the step size
  moonE_setdebt(g, applygcparam(g, STEPSIZE, 100));
}


/*
** CHECK MINOR-TO-MAJOR TRANSITION
** Decide whether to shift from minor to major mode based on accumulated old bytes.
*/
int GCCollector::checkmajorminor(moon_State* L, GlobalState* g) {
  if (g->getGCKind() == GCKind::GenerationalMajor) {  // generational mode?
    l_mem numbytes = g->getTotalBytes();
    l_mem addedbytes = numbytes - g->getGCMajorMinor();
    l_mem limit = applygcparam(g, MAJORMINOR, addedbytes);
    l_mem tobecollected = numbytes - g->getGCMarked();
    if (tobecollected > limit) {
      atomic2gen(L, g);  // return to generational mode
      g->setMinorDebt();
      return 1;  // exit incremental collection
    }
  }
  g->setGCMajorMinor(g->getGCMarked());  // prepare for next collection
  return 0;  // stay doing incremental collections
}


/*
** YOUNG COLLECTION
** Performs a minor collection in generational mode.
*/
void GCCollector::youngcollection(moon_State* L, GlobalState* g) {
  l_mem addedold1 = 0;
  l_mem marked = g->getGCMarked();  // preserve 'g->getGCMarked()'
  GCObject **psurvival;  // to point to first non-dead survival object
  GCObject *dummy;  // dummy out parameter to 'sweepgen'
  moon_assert(g->getGCState() == GCState::Propagate);
  if (g->getFirstOld1()) {  // are there regular OLD1 objects?
    GCMarking::markold(*g, g->getFirstOld1(), g->getReallyOld());  // mark them
    g->setFirstOld1(nullptr);  // no more OLD1 objects (for now)
  }
  GCMarking::markold(*g, g->getFinObj(), g->getFinObjROld());
  GCMarking::markold(*g, g->getToBeFnz(), nullptr);

  atomic(L);  // will lose 'g->marked'

  // sweep nursery and get a pointer to its last live element
  g->setGCState(GCState::SweepAllGC);
  psurvival = GCSweeping::sweepgen(L, *g, g->getAllGCPtr(), g->getSurvival(), g->getFirstOld1Ptr(), &addedold1);
  // sweep 'survival'
  GCSweeping::sweepgen(L, *g, psurvival, g->getOld1(), g->getFirstOld1Ptr(), &addedold1);
  g->setReallyOld(g->getOld1());
  g->setOld1(*psurvival);  // 'survival' survivals are old now
  g->setSurvival(g->getAllGC());  // all news are survivals

  // repeat for 'finobj' lists
  dummy = nullptr;  // no 'firstold1' optimization for 'finobj' lists
  psurvival = GCSweeping::sweepgen(L, *g, g->getFinObjPtr(), g->getFinObjSur(), &dummy, &addedold1);
  // sweep 'survival'
  GCSweeping::sweepgen(L, *g, psurvival, g->getFinObjOld1(), &dummy, &addedold1);
  g->setFinObjROld(g->getFinObjOld1());
  g->setFinObjOld1(*psurvival);  // 'survival' survivals are old now
  g->setFinObjSur(g->getFinObj());  // all news are survivals

  GCSweeping::sweepgen(L, *g, g->getToBeFnzPtr(), nullptr, &dummy, &addedold1);

  // keep total number of added old1 bytes
  g->setGCMarked(marked + addedold1);

  // decide whether to shift to major mode
  if (g->checkMinorMajor()) {
    minor2inc(L, g, GCKind::GenerationalMajor);  // go to major mode
    g->setGCMarked(0);  // avoid pause in first major cycle (see 'setpause')
  }
  else
    finishgencycle(L, g);  // still in minor mode; finish it
}


/*
** TRANSITION: ATOMIC TO GENERATIONAL
** Clears gray lists, sweeps all to old, sets up generational sublists.
*/
void GCCollector::atomic2gen(moon_State* L, GlobalState* g) {
  g->clearGrayLists();
  // sweep all elements making them old
  g->setGCState(GCState::SweepAllGC);
  GCSweeping::sweep2old(L, g->getAllGCPtr());
  // everything alive now is old
  GCObject *allgc = g->getAllGC();
  g->setReallyOld(allgc); g->setOld1(allgc); g->setSurvival(allgc);
  g->setFirstOld1(nullptr);  // there are no OLD1 objects anywhere

  // repeat for 'finobj' lists
  GCSweeping::sweep2old(L, g->getFinObjPtr());
  GCObject *finobj = g->getFinObj();
  g->setFinObjROld(finobj); g->setFinObjOld1(finobj); g->setFinObjSur(finobj);

  GCSweeping::sweep2old(L, g->getToBeFnzPtr());

  g->setGCKind(GCKind::GenerationalMinor);
  g->setGCMajorMinor(g->getGCMarked());  // "base" for number of bytes
  g->setGCMarked(0);  // to count the number of added old1 bytes
  finishgencycle(L, g);
}


/*
** ENTER GENERATIONAL MODE
** Runs to end of atomic cycle, converts all objects to old.
*/
void GCCollector::entergen(moon_State* L, GlobalState* g) {
  moonC_runtilstate(*L, GCState::Pause, 1);  // prepare to start a new cycle
  moonC_runtilstate(*L, GCState::Propagate, 1);  // start new cycle
  atomic(L);  // propagates all and then do the atomic stuff
  atomic2gen(L, g);
  g->setMinorDebt();  // set debt assuming next cycle will be minor
}


/*
** FULL GENERATIONAL COLLECTION
** Temporarily switches to incremental for full sweep, then returns to gen mode.
*/
void GCCollector::fullgen(moon_State* L, GlobalState* g) {
  minor2inc(L, g, GCKind::Incremental);
  entergen(L, g);
}


/*
** FULL INCREMENTAL COLLECTION
** Performs a complete GC cycle in incremental mode.
*/
void GCCollector::fullinc(moon_State* L, GlobalState* g) {
  if (g->keepInvariant())  // black objects?
    GCSweeping::entersweep(L);  // sweep everything to turn them back to white
  // finish any pending sweep phase to start a new cycle
  moonC_runtilstate(*L, GCState::Pause, 1);
  moonC_runtilstate(*L, GCState::CallFin, 1);  // run up to finalizers
  moonC_runtilstate(*L, GCState::Pause, 1);  // finish collection
  g->setPause();
}


/*
** SINGLE STEP
** Performs one incremental GC step.
** Returns work done or special value indicating state change.
*/
l_mem GCCollector::singlestep(moon_State* L, int fast) {
  GlobalState* g = G(L);
  l_mem stepresult;
  moon_assert(!g->getGCStopEm());  // collector is not reentrant
  g->setGCStopEm(1);  // no emergency collections while collecting
  switch (g->getGCState()) {
    case GCState::Pause: {
      GCMarking::restartcollection(*g);
      g->setGCState(GCState::Propagate);
      stepresult = 1;
      break;
    }
    case GCState::Propagate: {
      if (fast || g->getGray() == nullptr) {
        g->setGCState(GCState::EnterAtomic);  // finish propagate phase
        stepresult = 1;
      }
      else
        stepresult = GCMarking::propagatemark(*g);  // traverse one gray object
      break;
    }
    case GCState::EnterAtomic: {
      atomic(L);
      if (checkmajorminor(L, g))
        stepresult = STEP_2_MINOR;
      else {
        GCSweeping::entersweep(L);
        stepresult = ATOMIC_STEP;
      }
      break;
    }
    case GCState::SweepAllGC: {  // sweep "regular" objects
      GCSweeping::sweepstep(L, *g, GCState::SweepFinObj, g->getFinObjPtr(), fast);
      stepresult = GCSWEEPMAX;
      break;
    }
    case GCState::SweepFinObj: {  // sweep objects with finalizers
      GCSweeping::sweepstep(L, *g, GCState::SweepToBeFnz, g->getToBeFnzPtr(), fast);
      stepresult = GCSWEEPMAX;
      break;
    }
    case GCState::SweepToBeFnz: {  // sweep objects to be finalized
      GCSweeping::sweepstep(L, *g, GCState::SweepEnd, nullptr, fast);
      stepresult = GCSWEEPMAX;
      break;
    }
    case GCState::SweepEnd: {  // finish sweeps
      GCFinalizer::checkSizes(L, *g);
      g->setGCState(GCState::CallFin);
      stepresult = GCSWEEPMAX;
      break;
    }
    case GCState::CallFin: {  // call finalizers
      if (g->getToBeFnz() && !g->getGCEmergency()) {
        g->setGCStopEm(0);  // ok collections during finalizers
        GCFinalizer::GCTM(L);  // call one finalizer
        stepresult = CWUFIN;
      }
      else {  // emergency mode or no more finalizers
        g->setGCState(GCState::Pause);  // finish collection
        stepresult = STEP_2_PAUSE;
      }
      break;
    }
    default: moon_assert(0); return 0;
  }
  g->setGCStopEm(0);
  return stepresult;
}


/*
** INCREMENTAL STEP
** Performs a basic incremental step with work calculation.
*/
void GCCollector::incstep(moon_State* L, GlobalState* g) {
  l_mem stepsize = applygcparam(g, STEPSIZE, 100);
  l_mem work2do = applygcparam(g, STEPMUL, stepsize / cast_int(sizeof(void*)));
  l_mem stres;
  int fast = (work2do == 0);  // special case: do a full collection
  do {  // repeat until enough work
    stres = singlestep(L, fast);  // perform one single step
    if (stres == STEP_2_MINOR)  // returned to minor collections?
      return;  // nothing else to be done here
    else if (stres == STEP_2_PAUSE || (stres == ATOMIC_STEP && !fast))
      break;  // end of cycle or atomic
    else
      work2do -= stres;
  } while (fast || work2do > 0);
  if (g->getGCState() == GCState::Pause)
    g->setPause();  // pause until next cycle
  else
    moonE_setdebt(g, stepsize);
}
