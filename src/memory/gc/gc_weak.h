/*
** Garbage Collector - Weak Table Module
** See Copyright Notice in lua.h
*/

#ifndef gc_weak_h
#define gc_weak_h

#include "../../core/lstate.h"
#include "../lgc.h"
#include "../../objects/lobject.h"
#include "../../objects/ltable.h"

/*
** GCWeak - Encapsulates all garbage collector weak table logic
**
** This module handles weak tables in the garbage collector. Weak tables
** allow keys or values to be collected even if they're referenced in the
** table, enabling caches and other memory-sensitive data structures.
**
** KEY CONCEPTS:
** - Weak values: Table values can be collected
** - Weak keys (ephemerons): Table keys can be collected
** - Weak keys + values: Both can be collected
** - Ephemeron convergence: Iterative marking for ephemeron tables
**
** WEAK TABLE TYPES:
** 1. Weak values: {__mode = "v"} - values don't prevent collection
** 2. Weak keys: {__mode = "k"} - keys don't prevent collection (ephemerons)
** 3. Weak both: {__mode = "kv"} - neither keys nor values prevent collection
**
** TRAVERSAL:
** - traverseweakvalue(): Traverse weak-value table, mark keys only
** - traverseephemeron(): Traverse ephemeron table with special logic
** - convergeephemerons(): Iteratively mark ephemerons until convergence
**
** CLEARING:
** - clearbykeys(): Remove entries with unmarked keys
** - clearbyvalues(): Remove entries with unmarked values
*/
class GCWeak {
public:
    /*
    ** Get weak mode of table from its metatable's __mode field.
    ** Returns: (result & 1) iff weak values; (result & 2) iff weak keys
    */
    static int getmode(GlobalState& g, Table* h);

    /*
    ** Traverse a table with weak values.
    ** Marks keys only; values may be collected.
    ** Links table to appropriate gray list.
    */
    static void traverseweakvalue(GlobalState& g, Table* h);

    /*
    ** Traverse an ephemeron table (weak keys).
    ** Marks values only if their keys are marked.
    ** 'inv': traverse in reverse order (for convergence optimization)
    ** Returns: true if any object was marked during traversal
    */
    static int traverseephemeron(GlobalState& g, Table* h, int inv);

    /*
    ** Traverse all ephemeron tables, propagating marks from keys to values.
    ** Repeats until convergence (no more marks propagate).
    ** 'dir' alternates direction to optimize convergence on chains.
    */
    static void convergeephemerons(GlobalState& g);

    /*
    ** Clear entries with unmarked keys from all weak tables in list 'l'.
    ** Called in atomic phase after marking completes.
    */
    static void clearbykeys(GlobalState& g, GCObject* l);

    /*
    ** Clear entries with unmarked values from all weak tables in list 'l'
    ** up to element 'f'.
    ** Called in atomic phase after marking completes.
    */
    static void clearbyvalues(GlobalState& g, GCObject* l, GCObject* f);

private:
    /*
    ** Helper: link object to appropriate gray list based on generational mode.
    ** Handles Touched1/Touched2 ages for generational collector.
    */
    static void genlink(GlobalState& g, GCObject* o);
};

#endif  // gc_weak_h
