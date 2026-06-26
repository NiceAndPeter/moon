/*
** Garbage Collector - Marking Module
** See Copyright Notice in lua.h
*/

#define LUA_CORE

#include "lprefix.h"

#include <cstring>

#include "gc_marking.h"
#include "gc_weak.h"
#include "../lgc.h"
#include "../../core/ldo.h"
#include "../../objects/lfunc.h"
#include "../../objects/lstring.h"
#include "../../objects/ltable.h"
#include "../../core/ltm.h"

/*
** GC Marking Module Implementation
**
** This module contains all the mark-phase logic for Lua's tri-color
** incremental garbage collector. The marking phase identifies reachable
** objects by traversing the object graph from roots.
**
** ORGANIZATION:
** - Helper functions (objsize, gnodelast, getgclist, linkgclist, genlink)
** - Type-specific traversal functions (one per GC-able type)
** - Core marking functions (reallymarkobject, propagatemark, propagateall)
** - Utility functions (markmt, markbeingfnz, remarkupvals, cleargraylists)
*/


/*
** =======================================================
** Helper Functions
** =======================================================
*/

// Note: maskcolors, makewhite, set2gray, set2black are now in lgc.h

// Note: clearkey is now in GCCore module
#include "gc_core.h"
static inline void clearkey(Node* n) { GCCore::clearkey(n); }

/*
** Get last node in hash array (one past the end)
*/
static inline Node* gnodelast(Table* h) noexcept {
    return gnode(h, h->nodeSize());
}

// Note: objsize is now in GCCore module
static inline l_mem objsize(GCObject* o) { return GCCore::objsize(o); }

// Note: getgclist is now in GCCore module
static inline GCObject** getgclist(GCObject* o) { return GCCore::getgclist(o); }

// Note: linkgclist_ is now in GCCore module
static inline void linkgclist_(GCObject* o, GCObject** pnext, GCObject** list) {
    GCCore::linkgclist_(o, pnext, list);
}

template<typename T>
inline void linkobjgclist(T* o, GCObject*& p) {
	linkgclist_(obj2gco(o), getgclist(o), &p);
}

// Specialized versions for encapsulated types
static inline void linkgclistTable(Table* h, GCObject*& p) {
    linkgclist_(obj2gco(h), h->getGclistPtr(), &p);
}

static inline void linkgclistThread(lua_State* th, GCObject*& p) {
    linkgclist_(obj2gco(th), th->getGclistPtr(), &p);
}

// Note: gcvalueN is now in lgc.h

// Access to collectable objects in table array part
inline GCObject* gcvalarr(Table* t, unsigned int i) noexcept {
	return iscollectable(*(t)->getArrayTag(i)) ? (t)->getArrayVal(i)->gc : nullptr;
}


/*
** =======================================================
** Type-Specific Traversal Functions
** =======================================================
*/

// Functions getmode, traverseweakvalue, traverseephemeron are in lgc.cpp

/*
** Traverse a table (delegates to weak or strong traversal)
** Returns approximate cost in work units
*/
l_mem GCMarking::traversetable(GlobalState& g, Table* h) {
    markobjectN(g, h->getMetatable());
    switch (GCWeak::getmode(g, h)) {
        case 0:  // not weak
            traversestrongtable(g, h);
            break;
        case 1:  // weak values
            traverseweakvalue(g, h);
            break;
        case 2:  // weak keys (ephemeron)
            GCWeak::traverseephemeron(g, h, 0);
            break;
        case 3:  // all weak; nothing to traverse
            if (g.getGCState() == GCState::Propagate)
                linkgclistTable(h, *g.getGrayAgainPtr());
            else
                linkgclistTable(h, *g.getAllWeakPtr());
            break;
    }
    return static_cast<l_mem>(1 + 2 * h->nodeSize() + h->arraySize());
}

/*
** Traverse a userdata object
*/
l_mem GCMarking::traverseudata(GlobalState& g, Udata* u) {
    markobjectN(g, u->getMetatable());
    for (int i = 0; i < u->getNumUserValues(); i++)
        markvalue(g, &u->getUserValue(i)->value);
    genlink(g, obj2gco(u));
    return 1 + u->getNumUserValues();
}

/*
** Traverse a prototype (function template)
*/
l_mem GCMarking::traverseproto(GlobalState& g, Proto* f) {
    markobjectN(g, f->getSource());
    // Use std::span and range-based for loops
    for (auto& constant : f->getConstantsSpan())
        markvalue(g, &constant);
    for (const auto& upval : f->getUpvaluesSpan())
        markobjectN(g, upval.getName());
    for (Proto* nested : f->getProtosSpan())
        markobjectN(g, nested);
    for (const auto& locvar : f->getDebugInfo().getLocVarsSpan())
        markobjectN(g, locvar.getVarName());
    return 1 + f->getConstantsSize() + f->getUpvaluesSize() +
           f->getProtosSize() + f->getLocVarsSize();
}

/*
** Traverse a C closure
*/
l_mem GCMarking::traverseCclosure(GlobalState& g, CClosure* cl) {
    for (int i = 0; i < cl->getNumUpvalues(); i++)
        markvalue(g, cl->getUpvalue(i));
    return 1 + cl->getNumUpvalues();
}

/*
** Traverse a Lua closure
*/
l_mem GCMarking::traverseLclosure(GlobalState& g, LClosure* cl) {
    markobjectN(g, cl->getProto());
    for (int i = 0; i < cl->getNumUpvalues(); i++) {
        UpVal* upvalue = cl->getUpval(i);
        markobjectN(g, upvalue);
    }
    return 1 + cl->getNumUpvalues();
}

/*
** Traverse a thread
*/
l_mem GCMarking::traversethread(GlobalState& g, lua_State* th) {
    UpVal* upvalue;
    StkId o = th->getStack().p;
    if (isold(th) || g.getGCState() == GCState::Propagate)
        linkgclistThread(th, *g.getGrayAgainPtr());
    if (o == nullptr)
        return 0;  // stack not completely built yet
    lua_assert(g.getGCState() == GCState::Atomic ||
               th->getOpenUpval() == nullptr || th->isInTwups());
    for (; o < th->getTop().p; o++)
        markvalue(g, s2v(o));
    for (upvalue = th->getOpenUpval(); upvalue != nullptr; upvalue = upvalue->getOpenNext())
        markobject(g, upvalue);
    if (g.getGCState() == GCState::Atomic) {
        if (!g.getGCEmergency())
            th->shrinkStack();
        for (o = th->getTop().p; o < th->getStackLast().p + EXTRA_STACK; o++)
            setnilvalue(s2v(o));
        if (!th->isInTwups() && th->getOpenUpval() != nullptr) {
            th->setTwups(g.getTwups());
            g.setTwups(th);
        }
    }
    return 1 + (th->getTop().p - th->getStack().p);
}


/*
** =======================================================
** Core Marking Functions
** =======================================================
*/

/*
** Mark an object as reachable
** This is the entry point for marking - called when we discover a white object
*/
void GCMarking::reallymarkobject(GlobalState& g, GCObject* o) {
    g.setGCMarked(g.getGCMarked() + objsize(o));
    switch (static_cast<int>(o->getType())) {
        case static_cast<int>(ctb(LuaT::SHRSTR)):
        case static_cast<int>(ctb(LuaT::LNGSTR)): {
            set2black(o);  // strings have no children
            break;
        }
        case static_cast<int>(ctb(LuaT::UPVAL)): {
            UpVal* upvalue = gco2upv(o);
            if (upvalue->isOpen())
                set2gray(upvalue);  // open upvalues kept gray
            else
                set2black(upvalue);  // closed upvalues visited here
            markvalue(g, upvalue->getVP());
            break;
        }
        case static_cast<int>(ctb(LuaT::USERDATA)): {
            Udata* u = gco2u(o);
            if (u->getNumUserValues() == 0) {
                markobjectN(g, u->getMetatable());
                set2black(u);
                break;
            }
            // else fall through to add to gray list
        }  // FALLTHROUGH
        case static_cast<int>(ctb(LuaT::LCL)):
        case static_cast<int>(ctb(LuaT::CCL)):
        case static_cast<int>(ctb(LuaT::TABLE)):
        case static_cast<int>(ctb(LuaT::THREAD)):
        case static_cast<int>(ctb(LuaT::PROTO)): {
            linkobjgclist(o, *g.getGrayPtr());  // to be visited later
            break;
        }
        default:
            lua_assert(0);
            break;
    }
}

/*
** Process one gray object - traverse its children and mark it black
** Returns the traversal cost (work units)
*/
l_mem GCMarking::propagatemark(GlobalState& g) {
    GCObject* o = g.getGray();
    nw2black(o);
    g.setGray(*getgclist(o));  // remove from 'gray' list
    switch (static_cast<int>(o->getType())) {
        case static_cast<int>(ctb(LuaT::TABLE)):
            return traversetable(g, gco2t(o));
        case static_cast<int>(ctb(LuaT::USERDATA)):
            return traverseudata(g, gco2u(o));
        case static_cast<int>(ctb(LuaT::LCL)):
            return traverseLclosure(g, gco2lcl(o));
        case static_cast<int>(ctb(LuaT::CCL)):
            return traverseCclosure(g, gco2ccl(o));
        case static_cast<int>(ctb(LuaT::PROTO)):
            return traverseproto(g, gco2p(o));
        case static_cast<int>(ctb(LuaT::THREAD)):
            return traversethread(g, gco2th(o));
        default:
            lua_assert(0);
            return 0;
    }
}

/*
** Process all gray objects (used in atomic phase)
*/
void GCMarking::propagateall(GlobalState& g) {
    while (g.getGray())
        propagatemark(g);
}


/*
** =======================================================
** Utility Marking Functions
** =======================================================
*/

/*
** Mark metamethods for basic types
*/
void GCMarking::markmt(GlobalState& g) {
    for (int i = 0; i < LUA_NUMTYPES; i++)
        markobjectN(g, g.getMetatable(i));
}

/*
** Mark all objects in tobefnz list (being finalized)
*/
void GCMarking::markbeingfnz(GlobalState& g) {
    GCObject* o;
    for (o = g.getToBeFnz(); o != nullptr; o = o->getNext())
        markobject(g, o);
}

/*
** Remark upvalues for unmarked threads
** Simulates a barrier between each open upvalue and its value
*/
void GCMarking::remarkupvals(GlobalState& g) {
    lua_State* thread;
    lua_State** p = g.getTwupsPtr();
    while ((thread = *p) != nullptr) {
        if (!iswhite(thread) && thread->getOpenUpval() != nullptr)
            p = thread->getTwupsPtr();
        else {
            UpVal* upvalue;
            lua_assert(!isold(thread) || thread->getOpenUpval() == nullptr);
            *p = thread->getTwups();
            thread->setTwups(thread);  // mark out of list
            for (upvalue = thread->getOpenUpval(); upvalue != nullptr; upvalue = upvalue->getOpenNext()) {
                lua_assert(getage(upvalue) <= getage(thread));
                if (!iswhite(upvalue)) {
                    lua_assert(upvalue->isOpen() && isgray(upvalue));
                    markvalue(g, upvalue->getVP());
                }
            }
        }
    }
}

/*
** Mark root set and reset all gray lists to start a new collection.
** Initializes GCmarked to count total live bytes during cycle.
*/
void GCMarking::restartcollection(GlobalState& g) {
    g.clearGrayLists();  // Use the new method
    g.setGCMarked(0);
    markobject(g, mainthread(&g));
    markvalue(g, g.getRegistry());
    markmt(g);
    markbeingfnz(g);  // mark any finalizing object left from previous cycle
}

/*
** Mark black 'OLD1' objects when starting a new young collection.
** Gray objects are already in gray lists for atomic phase.
*/
void GCMarking::markold(GlobalState& g, GCObject* from, GCObject* to) {
    GCObject* p;
    for (p = from; p != to; p = p->getNext()) {
        if (getage(p) == GCAge::Old1) {
            lua_assert(!iswhite(p));
            setage(p, GCAge::Old);  // now they are old
            if (isblack(p))
                reallymarkobject(g, p);
        }
    }
}

/*
** Link object for generational mode post-processing.
** TOUCHED1 objects go to grayagain, TOUCHED2 advance to OLD.
*/
void GCMarking::genlink(GlobalState& g, GCObject* o) {
    lua_assert(isblack(o));
    if (getage(o) == GCAge::Touched1) {  // touched in this cycle?
        linkobjgclist(o, *g.getGrayAgainPtr());  // link it back in 'grayagain'
    }  // everything else does not need to be linked back
    else if (getage(o) == GCAge::Touched2)
        setage(o, GCAge::Old);  // advance age
}

/*
** Traverse array part of a table, marking collectable values.
** Returns 1 if any white objects were marked, 0 otherwise.
*/
int GCMarking::traversearray(GlobalState& g, Table* h) {
    unsigned asize = h->arraySize();
    int marked = 0;  // true if some object is marked in this traversal
    for (unsigned i = 0; i < asize; i++) {
        GCObject* o = gcvalarr(h, i);
        if (o != nullptr && iswhite(o)) {
            marked = 1;
            reallymarkobject(g, o);
        }
    }
    return marked;
}

/*
** Traverse a strong (non-weak) table.
** Marks all keys and values, then calls genlink for generational mode.
*/
void GCMarking::traversestrongtable(GlobalState& g, Table* h) {
    Node* n;
    Node* limit = gnodelast(h);
    traversearray(g, h);
    for (n = gnode(h, 0); n < limit; n++) {  // traverse hash part
        if (isempty(gval(n)))  // entry is empty?
            clearkey(n);  // clear its key
        else {
            lua_assert(!n->isKeyNil());
            markkey(g, n);
            markvalue(g, gval(n));
        }
    }
    genlink(g, obj2gco(h));
}

/*
** Clear all gray lists (called when entering sweep phase)
*/
void GCMarking::cleargraylists(GlobalState& g) {
    *g.getGrayPtr() = *g.getGrayAgainPtr() = nullptr;
    *g.getWeakPtr() = *g.getAllWeakPtr() = *g.getEphemeronPtr() = nullptr;
}
