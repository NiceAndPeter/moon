/*
** Garbage Collector - Core Utilities Module
** See Copyright Notice in lua.h
*/

#ifndef gc_core_h
#define gc_core_h

#include "../../core/lstate.h"
#include "../lgc.h"
#include "../../objects/lobject.h"

/*
** GCCore - Core garbage collector utility functions
**
** This module contains fundamental GC utility functions used across
** the garbage collector implementation:
**
** - objsize(): Calculate memory size of GC objects
** - getgclist(): Get pointer to object's gclist field
** - linkgclist_(): Link an object into a GC list
** - clearkey(): Clear dead keys from table nodes
** - freeupval(): Free an upvalue object
**
** These utilities are used by marking, sweeping, and finalization modules.
*/
class GCCore {
public:
    /*
    ** Calculate the memory size of a GC object.
    ** Used for GC accounting and statistics.
    ** Returns size in bytes (l_mem).
    */
    static l_mem objsize(GCObject* o);

    /*
    ** Get pointer to the gclist field of an object.
    ** Different object types store gclist in different locations.
    ** Returns pointer to gclist field, or nullptr for invalid types.
    */
    static GCObject** getgclist(GCObject* o);

    /*
    ** Link a GC object into a gray list.
    ** Sets the object to gray and adds it to the specified list.
    **
    ** Parameters:
    ** - o: Object to link (will be set to gray)
    ** - pnext: Pointer to object's gclist field
    ** - list: Pointer to head of list to link into
    */
    static void linkgclist_(GCObject* o, GCObject** pnext, GCObject** list);

    /*
    ** Clear dead keys from empty table nodes.
    ** If a node is empty, marks its key as dead for collection.
    ** This allows key collection while preserving table structure.
    **
    ** Parameters:
    ** - n: Table node to check and potentially clear
    */
    static void clearkey(Node* n);

    /*
    ** Free an upvalue object.
    ** Handles unlinking open upvalues and calling destructor.
    **
    ** Parameters:
    ** - L: Lua state
    ** - upvalue: Upvalue to free
    */
    static void freeupval(moon_State* L, UpVal* upvalue);
};

#endif  // gc_core_h
