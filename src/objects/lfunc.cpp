/*
** Auxiliary functions to manipulate prototypes and closures
** See Copyright Notice in lua.h
*/

#define LUA_CORE

#include "lprefix.h"


#include <algorithm>
#include <cstddef>

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"



// Constructor
CClosure::CClosure(int nupvals)
  : numberOfUpvalues(cast_byte(nupvals)), gclist(nullptr), f(nullptr) {
  // upvalue array initialized by caller if needed
}

// Factory method
CClosure* CClosure::create(lua_State* L, int nupvals) {
  size_t extra = (nupvals > 0) ? (static_cast<size_t>(nupvals) - 1) * sizeof(TValue) : 0;
  CClosure* c = new (L, ctb(LuaT::CCL), extra) CClosure(nupvals);
  return c;
}


// Constructor
LClosure::LClosure(int nupvals)
  : numberOfUpvalues(cast_byte(nupvals)), gclist(nullptr), p(nullptr) {
  // Initialize upvals array to nullptr
  std::fill_n(upvals, nupvals, nullptr);
}

// Factory method
LClosure* LClosure::create(lua_State* L, int nupvals) {
  // Calculate total size using the same formula as sizeLclosure macro
  // size = offsetof(LClosure, upvals) + sizeof(UpVal*) * nupvals
  // But operator new receives sizeof(LClosure) which includes one UpVal*
  // So extra = sizeLclosure(nupvals) - sizeof(LClosure)
  size_t total_size = sizeLclosure(nupvals);
  size_t extra = total_size - sizeof(LClosure);
  LClosure* c = new (L, ctb(LuaT::LCL), extra) LClosure(nupvals);
  return c;
}


/*
** fill a closure with new closed upvalues
*/
void LClosure::initUpvals(lua_State* L) {
  int i;
  for (i = 0; i < numberOfUpvalues; i++) {
    // Use placement new - calls constructor (initializes to closed nil upvalue)
    UpVal *uv = new (L, ctb(LuaT::UPVAL)) UpVal();
    uv->setVP(uv->getValueSlot());  /* make it closed */
    // Constructor already sets value to nil, but keeping setnilvalue for clarity
    setnilvalue(uv->getVP());
    upvals[i] = uv;
    luaC_objbarrier(L, this, uv);
  }
}


/*
** Create a new upvalue at the given level, and link it to the list of
** open upvalues of 'L' after entry 'prev'.
**/
static UpVal *newupval (lua_State *L, StkId level, UpVal **prev) {
  // Use placement new - calls constructor
  UpVal *uv = new (L, ctb(LuaT::UPVAL)) UpVal();
  UpVal *next = *prev;
  uv->setVP(s2v(level));  /* current value lives in the stack */
  uv->setOpenNext(next);  /* link it to list of open upvalues */
  uv->setOpenPrevious(prev);
  if (next)
    next->setOpenPrevious(uv->getOpenNextPtr());  /* link next's previous to our next field */
  *prev = uv;
  if (!L->isInTwups()) {  /* thread not in list of threads with upvalues? */
    L->setTwups(G(L)->getTwups());  /* link it to the list */
    G(L)->setTwups(L);
  }
  return uv;
}


/*
** Find and reuse, or create if it does not exist, an upvalue
** at the given level.
*/
UpVal *luaF_findupval (lua_State *L, StkId level) {
  UpVal **pp = L->getOpenUpvalPtr();
  UpVal *p;
  lua_assert(L->isInTwups() || L->getOpenUpval() == nullptr);
  while ((p = *pp) != nullptr && p->getLevel() >= level) {  /* search for it */
    lua_assert(!isdead(G(L), p));
    if (p->getLevel() == level)  /* corresponding upvalue? */
      return p;  /* return it */
    pp = p->getOpenNextPtr();
  }
  /* not found: create a new upvalue after 'pp' */
  return newupval(L, level, pp);
}


/*
** Call closing method for object 'obj' with error object 'err'. The
** boolean 'yy' controls whether the call is yieldable.
** (This function assumes EXTRA_STACK.)
*/
static void callclosemethod (lua_State *L, TValue *obj, TValue *err, int yy) {
  StkId top = L->getTop().p;
  StkId func = top;
  const TValue *metamethod = luaT_gettmbyobj(L, obj, TMS::TM_CLOSE);
  L->getStackSubsystem().setSlot(top++, metamethod);  /* will call metamethod... */
  L->getStackSubsystem().setSlot(top++, obj);  /* with 'self' as the 1st argument */
  if (err != nullptr)  /* if there was an error... */
    L->getStackSubsystem().setSlot(top++, err);  /* then error object will be 2nd argument */
  L->getStackSubsystem().setTopPtr(top);  /* add function and arguments */
  if (yy)
    L->call( func, 0);
  else
    L->callNoYield( func, 0);
}


/*
** Check whether object at given level has a close metamethod and raise
** an error if not.
*/
static void checkclosemth (lua_State *L, StkId level) {
  const TValue *metamethod = luaT_gettmbyobj(L, s2v(level), TMS::TM_CLOSE);
  if (ttisnil(metamethod)) {  /* no metamethod? */
    int idx = cast_int(level - L->getCI()->funcRef().p);  /* variable index */
    const char *vname = luaG_findlocal(L, L->getCI(), idx, nullptr);
    if (vname == nullptr) vname = "?";
    luaG_runerror(L, "variable '%s' got a non-closable value", vname);
  }
}


/*
** Prepare and call a closing method.
** If status is CLOSEKTOP, the call to the closing method will be pushed
** at the top of the stack. Otherwise, values can be pushed right after
** the 'level' of the upvalue being closed, as everything after that
** won't be used again.
*/
static void prepcallclosemth (lua_State *L, StkId level, TStatus status,
                                            int yy) {
  TValue *uv = s2v(level);  /* value being closed */
  TValue *errobj;
  switch (status) {
    case LUA_OK:
      L->getStackSubsystem().setTopPtr(level + 1);  /* call will be at this level */
      /* FALLTHROUGH */
    case CLOSEKTOP:  /* don't need to change top */
      errobj = nullptr;  /* no error object */
      break;
    default:  /* 'luaD_seterrorobj' will set top to level + 2 */
      errobj = s2v(level + 1);  /* error object goes after 'uv' */
      L->setErrorObj( status, level + 1);  /* set error object */
      break;
  }
  callclosemethod(L, uv, errobj, yy);
}


/* Maximum value for deltas in 'tbclist' */
inline constexpr unsigned short MAXDELTA = USHRT_MAX;


/*
** Insert a variable in the list of to-be-closed variables.
*/
void luaF_newtbcupval (lua_State *L, StkId level) {
  lua_assert(level > L->getTbclist().p);
  if (l_isfalse(s2v(level)))
    return;  /* false doesn't need to be closed */
  checkclosemth(L, level);  /* value must have a close method */
  while (cast_uint(level - L->getTbclist().p) > MAXDELTA) {
    L->getTbclist().p += MAXDELTA;  /* create a dummy node at maximum delta */
    L->getTbclist().p->tbclist.delta = 0;
  }
  level->tbclist.delta = static_cast<unsigned short>(level - L->getTbclist().p);
  L->getTbclist().p = level;
}


void UpVal::unlink() {
  lua_assert(this->isOpen());
  *getOpenPrevious() = getOpenNext();
  if (getOpenNext())
    getOpenNext()->setOpenPrevious(getOpenPrevious());
}

void luaF_unlinkupval (UpVal *uv) {
  uv->unlink();
}


/*
** Close all upvalues up to the given stack level.
*/
void luaF_closeupval (lua_State *L, StkId level) {
  UpVal *uv;
  while ((uv = L->getOpenUpval()) != nullptr && uv->getLevel() >= level) {
    TValue *slot = uv->getValueSlot();  /* new position for value */
    lua_assert(uv->getLevel() < L->getTop().p);
    luaF_unlinkupval(uv);  /* remove upvalue from 'openupval' list */
    *slot = *uv->getVP();  /* move value to upvalue slot */
    uv->setVP(slot);  /* now current value lives here */
    if (!iswhite(uv)) {  /* neither white nor dead? */
      nw2black(uv);  /* closed upvalues cannot be gray */
      luaC_barrier(L, uv, slot);
    }
  }
}


/*
** Remove first element from the tbclist plus its dummy nodes.
*/
static void poptbclist (lua_State *L) {
  StkId tbc = L->getTbclist().p;
  lua_assert(tbc->tbclist.delta > 0);  /* first element cannot be dummy */
  tbc -= tbc->tbclist.delta;
  while (tbc > L->getStack().p && tbc->tbclist.delta == 0)
    tbc -= MAXDELTA;  /* remove dummy nodes */
  L->getTbclist().p = tbc;
}


/*
** Close all upvalues and to-be-closed variables up to the given stack
** level. Return restored 'level'.
*/
StkId luaF_close (lua_State *L, StkId level, TStatus status, int yy) {
  ptrdiff_t levelrel = L->saveStack(level);
  luaF_closeupval(L, level);  /* first, close the upvalues */
  while (L->getTbclist().p >= level) {  /* traverse tbc's down to that level */
    StkId tbc = L->getTbclist().p;  /* get variable index */
    poptbclist(L);  /* remove it from list */
    prepcallclosemth(L, tbc, status, yy);  /* close variable */
    level = L->restoreStack(levelrel);
  }
  return level;
}


Proto *luaF_newproto (lua_State *L) {
  // Use placement new - calls constructor which initializes all fields to safe defaults
  Proto *f = new (L, ctb(LuaT::PROTO)) Proto();
  // Constructor handles all initialization
  return f;
}


lu_mem Proto::memorySize() const {
  lu_mem sz = static_cast<lu_mem>(sizeof(Proto))
            + cast_uint(getProtosSize()) * sizeof(Proto*)
            + cast_uint(getConstantsSize()) * sizeof(TValue)
            + cast_uint(getLocVarsSize()) * sizeof(LocVar)
            + cast_uint(getUpvaluesSize()) * sizeof(Upvaldesc);
  if (!(getFlag() & PF_FIXED)) {
    sz += cast_uint(getCodeSize()) * sizeof(Instruction);
    sz += cast_uint(getLineInfoSize()) * sizeof(lu_byte);
    sz += cast_uint(getAbsLineInfoSize()) * sizeof(AbsLineInfo);
  }
  return sz;
}

lu_mem luaF_protosize (Proto *p) {
  return p->memorySize();
}


void Proto::free(lua_State* L) {
  if (!(getFlag() & PF_FIXED)) {
    luaM_freearray(L, getCode(), cast_sizet(getCodeSize()));
    luaM_freearray(L, getLineInfo(), cast_sizet(getLineInfoSize()));
    luaM_freearray(L, getAbsLineInfo(), cast_sizet(getAbsLineInfoSize()));
  }
  luaM_freearray(L, getProtos(), cast_sizet(getProtosSize()));
  luaM_freearray(L, getConstants(), cast_sizet(getConstantsSize()));
  luaM_freearray(L, getLocVars(), cast_sizet(getLocVarsSize()));
  luaM_freearray(L, getUpvalues(), cast_sizet(getUpvaluesSize()));
  luaM_free(L, this);
}

void luaF_freeproto (lua_State *L, Proto *f) {
  f->free(L);
}


/*
** Look for n-th local variable at line 'line' in function 'func'.
** Returns nullptr if not found.
*/
const char* Proto::getLocalName(int local_number, int pc) const {
  auto locVarsSpan = getDebugInfo().getLocVarsSpan();
  for (const auto& locvar : locVarsSpan) {
    if (locvar.getStartPC() > pc)
      break;
    if (pc < locvar.getEndPC()) {  /* is variable active? */
      local_number--;
      if (local_number == 0)
        return getStringContents(locvar.getVarName());
    }
  }
  return nullptr;  /* not found */
}

const char *luaF_getlocalname (const Proto *f, int local_number, int pc) {
  return f->getLocalName(local_number, pc);
}

