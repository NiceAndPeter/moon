/*
** Garbage Collector - Core Utilities Module
** See Copyright Notice in lua.h
*/

#define LUA_CORE

#include "lprefix.h"

#include "gc_core.h"
#include "../lgc.h"
#include "../../objects/lobject.h"
#include "../../objects/ltable.h"
#include "../../objects/lstring.h"
#include "../../objects/lfunc.h"
#include "../../core/lstate.h"
#include "../lmem.h"

/*
** Calculate the memory size of a GC object.
** Returns the size in bytes for GC accounting purposes.
*/
l_mem GCCore::objsize(GCObject* o) {
    lu_mem res;
    switch (static_cast<int>(o->getType())) {
        case static_cast<int>(ctb(LuaT::TABLE)): {
            res = gco2t(o)->size();
            break;
        }
        case static_cast<int>(ctb(LuaT::LCL)): {
            LClosure* cl = gco2lcl(o);
            res = sizeLclosure(cl->getNumUpvalues());
            break;
        }
        case static_cast<int>(ctb(LuaT::CCL)): {
            CClosure* cl = gco2ccl(o);
            res = sizeCclosure(cl->getNumUpvalues());
            break;
        }
        case static_cast<int>(ctb(LuaT::USERDATA)): {
            Udata* u = gco2u(o);
            res = sizeudata(u->getNumUserValues(), u->getLen());
            break;
        }
        case static_cast<int>(ctb(LuaT::PROTO)): {
            res = gco2p(o)->memorySize();
            break;
        }
        case static_cast<int>(ctb(LuaT::THREAD)): {
            res = luaE_threadsize(gco2th(o));
            break;
        }
        case static_cast<int>(ctb(LuaT::SHRSTR)): {
            TString* tstring = gco2ts(o);
            res = sizestrshr(cast_uint(tstring->getShrlen()));
            break;
        }
        case static_cast<int>(ctb(LuaT::LNGSTR)): {
            TString* tstring = gco2ts(o);
            res = TString::calculateLongStringSize(tstring->getLnglen(), tstring->getShrlen());
            break;
        }
        case static_cast<int>(ctb(LuaT::UPVAL)): {
            res = sizeof(UpVal);
            break;
        }
        default: res = 0; lua_assert(0);
    }
    return static_cast<l_mem>(res);
}


/*
** Get pointer to the gclist field of a GC object.
** Different object types store this field in different locations.
*/
GCObject** GCCore::getgclist(GCObject* o) {
    switch (static_cast<int>(o->getType())) {
        case static_cast<int>(ctb(LuaT::TABLE)): return gco2t(o)->getGclistPtr();
        case static_cast<int>(ctb(LuaT::LCL)): return gco2lcl(o)->getGclistPtr();
        case static_cast<int>(ctb(LuaT::CCL)): return gco2ccl(o)->getGclistPtr();
        case static_cast<int>(ctb(LuaT::THREAD)): return gco2th(o)->getGclistPtr();
        case static_cast<int>(ctb(LuaT::PROTO)): return gco2p(o)->getGclistPtr();
        case static_cast<int>(ctb(LuaT::USERDATA)): {
            Udata* u = gco2u(o);
            lua_assert(u->getNumUserValues() > 0);
            return u->getGclistPtr();
        }
        case static_cast<int>(ctb(LuaT::UPVAL)):
            // UpVals use the base GCObject 'next' field for gray list linkage
            return o->getNextPtr();
        case static_cast<int>(ctb(LuaT::SHRSTR)):
        case static_cast<int>(ctb(LuaT::LNGSTR)):
            /* Strings are marked black directly and should never be in gray list.
             * However, with LTO, we've seen strings passed to this function.
             * Use the 'next' field (from GCObject base) as a fallback. */
            return o->getNextPtr();
        default:
            /* Fallback: use base GCObject 'next' field for unhandled/unknown types.
             * With LTO, we've seen invalid type values (e.g., 0xab), possibly due to
             * aggressive optimizations or memory reordering. Using the base 'next'
             * field is safe and prevents crashes. */
            return o->getNextPtr();
    }
}


/*
** Link a GC object into a gray list.
** The object is set to gray and added to the specified list.
*/
void GCCore::linkgclist_(GCObject* o, GCObject** pnext, GCObject** list) {
    lua_assert(!isgray(o));  // cannot be in a gray list
    *pnext = *list;
    *list = o;
    set2gray(o);  // now it is
}


/*
** Clear dead keys from empty table nodes.
** If entry is empty, mark its key as dead. This allows the collection
** of the key, but keeps its entry in the table (its removal could break
** a chain and could break a table traversal). Other places never manipulate
** dead keys, because the associated empty value is enough to signal that
** the entry is logically empty.
*/
void GCCore::clearkey(Node* n) {
    lua_assert(isempty(gval(n)));
    if (n->isKeyCollectable())
        n->setKeyDead();  // unused key; remove it
}


/*
** Free an upvalue object.
** Unlinks open upvalues and calls destructor before freeing.
*/
void GCCore::freeupval(lua_State* L, UpVal* upvalue) {
    if (upvalue->isOpen())
        luaF_unlinkupval(upvalue);
    upvalue->~UpVal();  // Call destructor
    luaM_free(L, upvalue);
}
