/*
** Garbage Collector - Marking Module
** See Copyright Notice in lua.h
*/

#ifndef gc_marking_h
#define gc_marking_h

#include "../../core/lstate.h"
#include "../lgc.h"
#include "../../objects/lobject.h"

/*
** GCMarking - Encapsulates all garbage collector marking logic
**
** This module handles the marking phase of the tri-color garbage collector.
** Marking identifies all reachable objects by traversing the object graph
** starting from root objects (globals, registry, main thread, etc.).
**
** KEY CONCEPTS:
** - reallymarkobject(): Marks a single object (entry point for marking)
** - propagatemark(): Processes one gray object, marking its children
** - propagateall(): Processes all gray objects until convergence
** - traverse*(): Type-specific traversal functions for different object types
**
** GRAY LIST MANAGEMENT:
** Objects are placed in gray lists when marked. The propagate functions
** remove objects from gray lists, traverse their children, and mark them black.
**
** INCREMENTAL MARKING:
** The marking can be done incrementally - propagatemark() processes one
** gray object at a time, allowing the collector to interleave with program execution.
*/
class GCMarking {
public:
    /*
    ** Mark an object as reachable.
    ** Called when we discover a white object during marking.
    ** Updates GCmarked counter and adds object to appropriate gray list.
    */
    static void reallymarkobject(GlobalState& g, GCObject* o);

    /*
    ** Process one gray object: traverse its children and mark it black.
    ** Returns the traversal cost (approximate number of bytes/fields visited).
    ** This is the core incremental marking operation.
    */
    static l_mem propagatemark(GlobalState& g);

    /*
    ** Process all gray objects until none remain.
    ** This runs marking to completion (used in atomic phase).
    */
    static void propagateall(GlobalState& g);

    /*
    ** Mark metamethods for basic types.
    ** Called during atomic phase to ensure metatables are reachable.
    */
    static void markmt(GlobalState& g);

    /*
    ** Mark all objects in the tobefnz list (being finalized).
    ** Called during atomic phase to keep finalizable objects alive.
    */
    static void markbeingfnz(GlobalState& g);

    /*
    ** Remark open upvalues for unmarked threads.
    ** Simulates a barrier between each open upvalue and its value.
    ** Also cleans up the twups list (threads with upvalues).
    */
    static void remarkupvals(GlobalState& g);

    /*
    ** Clear all gray lists (called when entering sweep phase).
    */
    static void cleargraylists(GlobalState& g);

    /*
    ** Mark root set and reset all gray lists to start a new collection.
    ** Initializes GCmarked to count total live bytes during cycle.
    */
    static void restartcollection(GlobalState& g);

    /*
    ** Mark black 'OLD1' objects when starting a new young collection.
    ** Gray objects are already in gray lists for atomic phase.
    */
    static void markold(GlobalState& g, GCObject* from, GCObject* to);

    /*
    ** Link object for generational mode post-processing.
    ** TOUCHED1 objects go to grayagain, TOUCHED2 advance to OLD.
    */
    static void genlink(GlobalState& g, GCObject* o);

    /*
    ** Traverse array part of a table, marking collectable values.
    ** Returns 1 if any white objects were marked, 0 otherwise.
    */
    static int traversearray(GlobalState& g, Table* h);

    /*
    ** Traverse a strong (non-weak) table.
    ** Marks all keys and values, then calls genlink for generational mode.
    */
    static void traversestrongtable(GlobalState& g, Table* h);

private:
    /*
    ** Type-specific traversal functions.
    ** Each function marks the object's children and returns traversal cost.
    */
    static l_mem traversetable(GlobalState& g, Table* h);
    static l_mem traverseudata(GlobalState& g, Udata* u);
    static l_mem traverseproto(GlobalState& g, Proto* f);
    static l_mem traverseCclosure(GlobalState& g, CClosure* cl);
    static l_mem traverseLclosure(GlobalState& g, LClosure* cl);
    static l_mem traversethread(GlobalState& g, lua_State* th);
};

/*
** Inline marking helper functions
** These replace the old macros for type-safe marking operations
*/

/* Mark a value if it's a white collectable object */
inline void markvalue(GlobalState& g, const TValue* o) {
    checkliveness(mainthread(&g), o);
    if (iscollectable(o) && iswhite(gcvalue(o))) {
        GCMarking::reallymarkobject(g, gcvalue(o));
    }
}

/* Mark a table node's key if it's white */
inline void markkey(GlobalState& g, const Node* n) {
    if (n->isKeyCollectable() && iswhite(n->getKeyGC())) {
        GCMarking::reallymarkobject(g, n->getKeyGC());
    }
}

/* Mark an object if it's white */
template<typename T>
inline void markobject(GlobalState& g, const T* t) {
    if (iswhite(t)) {
        GCMarking::reallymarkobject(g, obj2gco(t));
    }
}

/* Mark an object that can be nullptr */
template<typename T>
inline void markobjectN(GlobalState& g, const T* t) {
    if (t) {
        markobject(g, t);
    }
}

#endif /* gc_marking_h */
