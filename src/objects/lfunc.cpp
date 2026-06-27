/*
** Auxiliary functions to manipulate prototypes and closures
** See Copyright Notice in lua.h
*/

#define MOON_CORE

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
CClosure* CClosure::create(moon_State* L, int nupvals) {
  size_t extra = (nupvals > 0) ? (static_cast<size_t>(nupvals) - 1) * sizeof(TValue) : 0;
  CClosure* c = new (L, ctb(MoonT::CCL), extra) CClosure(nupvals);
  return c;
}


// Constructor
LClosure::LClosure(int nupvals)
  : numberOfUpvalues(cast_byte(nupvals)), gclist(nullptr), p(nullptr) {
  // Initialize upvals array to nullptr
  std::fill_n(upvals, nupvals, nullptr);
}

// Factory method
LClosure* LClosure::create(moon_State* L, int nupvals) {
  // Calculate total size using the same formula as sizeLclosure macro
  // size = offsetof(LClosure, upvals) + sizeof(UpVal*) * nupvals
  // But operator new receives sizeof(LClosure) which includes one UpVal*
  // So extra = sizeLclosure(nupvals) - sizeof(LClosure)
  size_t total_size = sizeLclosure(nupvals);
  size_t extra = total_size - sizeof(LClosure);
  LClosure* c = new (L, ctb(MoonT::LCL), extra) LClosure(nupvals);
  return c;
}


/*
** fill a closure with new closed upvalues
*/
void LClosure::initUpvals(moon_State* L) {
  for (int i = 0; i < numberOfUpvalues; i++) {
    // Use placement new - calls constructor (initializes to closed nil upvalue)
    UpVal *upvalue = new (L, ctb(MoonT::UPVAL)) UpVal();
    upvalue->setVP(upvalue->getValueSlot());  // make it closed
    // Constructor already sets value to nil, but keeping setnilvalue for clarity
    setnilvalue(upvalue->getVP());
    upvals[i] = upvalue;
    moonC_objbarrier(L, this, upvalue);
  }
}


/*
** Create a new upvalue at the given level, and link it to the list of
** open upvalues of 'L' after entry 'prev'.
**/
static UpVal *newupval (moon_State *L, StkId level, UpVal **prev) {
  // Use placement new - calls constructor
  UpVal *upvalue = new (L, ctb(MoonT::UPVAL)) UpVal();
  UpVal *next = *prev;
  upvalue->setVP(s2v(level));  // current value lives in the stack
  upvalue->setOpenNext(next);  // link it to list of open upvalues
  upvalue->setOpenPrevious(prev);
  if (next)
    next->setOpenPrevious(upvalue->getOpenNextPtr());  // link next's previous to our next field
  *prev = upvalue;
  if (!L->isInTwups()) {  // thread not in list of threads with upvalues?
    L->setTwups(G(L)->getTwups());  // link it to the list
    G(L)->setTwups(L);
  }
  return upvalue;
}


/*
** Find and reuse, or create if it does not exist, an upvalue
** at the given level.
*/
UpVal *moonF_findupval (moon_State *L, StkId level) {
  UpVal **pp = L->getOpenUpvalPtr();
  UpVal *p;
  moon_assert(L->isInTwups() || L->getOpenUpval() == nullptr);
  while ((p = *pp) != nullptr && p->getLevel() >= level) {  // search for it
    moon_assert(!isdead(G(L), p));
    if (p->getLevel() == level)  // corresponding upvalue?
      return p;  // return it
    pp = p->getOpenNextPtr();
  }
  // not found: create a new upvalue after 'pp'
  return newupval(L, level, pp);
}


/*
** Call closing method for object 'obj' with error object 'err'. The
** boolean 'yy' controls whether the call is yieldable.
** (This function assumes EXTRA_STACK.)
*/
static void callclosemethod (moon_State *L, TValue *obj, TValue *err, int yy) {
  StkId top = L->getTop().p;
  StkId func = top;
  const TValue *metamethod = moonT_gettmbyobj(L, obj, TMS::TM_CLOSE);
  L->getStackSubsystem().setSlot(top++, metamethod);  // will call metamethod...
  L->getStackSubsystem().setSlot(top++, obj);  // with 'self' as the 1st argument
  if (err != nullptr)  // if there was an error...
    L->getStackSubsystem().setSlot(top++, err);  // then error object will be 2nd argument
  L->getStackSubsystem().setTopPtr(top);  // add function and arguments
  if (yy)
    L->call( func, 0);
  else
    L->callNoYield( func, 0);
}


/*
** Check whether object at given level has a close metamethod and raise
** an error if not.
*/
static void checkclosemth (moon_State *L, StkId level) {
  const TValue *metamethod = moonT_gettmbyobj(L, s2v(level), TMS::TM_CLOSE);
  if (ttisnil(metamethod)) {  // no metamethod?
    int idx = cast_int(level - L->getCI()->funcRef().p);  // variable index
    const char *vname = moonG_findlocal(L, L->getCI(), idx, nullptr);
    if (vname == nullptr) vname = "?";
    moonG_runerror(L, "variable '%s' got a non-closable value", vname);
  }
}


/*
** Prepare and call a closing method.
** If status is CLOSEKTOP, the call to the closing method will be pushed
** at the top of the stack. Otherwise, values can be pushed right after
** the 'level' of the upvalue being closed, as everything after that
** won't be used again.
*/
static void prepcallclosemth (moon_State *L, StkId level, TStatus status,
                                            int yy) {
  TValue *upvalue = s2v(level);  // value being closed
  TValue *errobj;
  switch (status) {
    case MOON_OK:
      L->getStackSubsystem().setTopPtr(level + 1);  // call will be at this level
      // FALLTHROUGH
    case CLOSEKTOP:  // don't need to change top
      errobj = nullptr;  // no error object
      break;
    default:  // 'moonD_seterrorobj' will set top to level + 2
      errobj = s2v(level + 1);  // error object goes after 'upvalue'
      L->setErrorObj( status, level + 1);  // set error object
      break;
  }
  callclosemethod(L, upvalue, errobj, yy);
}


// Maximum value for deltas in 'tbclist'
inline constexpr unsigned short MAXDELTA = USHRT_MAX;


/*
** Insert a variable in the list of to-be-closed variables.
*/
void moonF_newtbcupval (moon_State *L, StkId level) {
  moon_assert(level > L->getTbclist().p);
  if (l_isfalse(s2v(level)))
    return;  // false doesn't need to be closed
  checkclosemth(L, level);  // value must have a close method
  while (cast_uint(level - L->getTbclist().p) > MAXDELTA) {
    L->getTbclist().p += MAXDELTA;  // create a dummy node at maximum delta
    L->getTbclist().p->tbclist.delta = 0;
  }
  level->tbclist.delta = static_cast<unsigned short>(level - L->getTbclist().p);
  L->getTbclist().p = level;
}


void UpVal::unlink() {
  moon_assert(this->isOpen());
  *getOpenPrevious() = getOpenNext();
  if (getOpenNext())
    getOpenNext()->setOpenPrevious(getOpenPrevious());
}

void moonF_unlinkupval (UpVal *upvalue) {
  upvalue->unlink();
}


/*
** Close all upvalues up to the given stack level.
*/
void moonF_closeupval (moon_State *L, StkId level) {
  UpVal *upvalue;
  while ((upvalue = L->getOpenUpval()) != nullptr && upvalue->getLevel() >= level) {
    TValue *slot = upvalue->getValueSlot();  // new position for value
    moon_assert(upvalue->getLevel() < L->getTop().p);
    moonF_unlinkupval(upvalue);  // remove upvalue from 'openupval' list
    *slot = *upvalue->getVP();  /* move value to upvalue slot */
    upvalue->setVP(slot);  // now current value lives here
    if (!iswhite(upvalue)) {  // neither white nor dead?
      nw2black(upvalue);  // closed upvalues cannot be gray
      moonC_barrier(L, upvalue, slot);
    }
  }
}


/*
** Remove first element from the tbclist plus its dummy nodes.
*/
static void poptbclist (moon_State *L) {
  StkId tbc = L->getTbclist().p;
  moon_assert(tbc->tbclist.delta > 0);  // first element cannot be dummy
  tbc -= tbc->tbclist.delta;
  while (tbc > L->getStack().p && tbc->tbclist.delta == 0)
    tbc -= MAXDELTA;  // remove dummy nodes
  L->getTbclist().p = tbc;
}


/*
** Close all upvalues and to-be-closed variables up to the given stack
** level. Return restored 'level'.
*/
StkId moonF_close (moon_State *L, StkId level, TStatus status, int yy) {
  ptrdiff_t levelrel = L->saveStack(level);
  moonF_closeupval(L, level);  // first, close the upvalues
  while (L->getTbclist().p >= level) {  // traverse tbc's down to that level
    StkId tbc = L->getTbclist().p;  // get variable index
    poptbclist(L);  // remove it from list
    prepcallclosemth(L, tbc, status, yy);  // close variable
    level = L->restoreStack(levelrel);
  }
  return level;
}


Proto *moonF_newproto (moon_State *L) {
  // Use placement new - calls constructor which initializes all fields to safe defaults
  Proto *f = new (L, ctb(MoonT::PROTO)) Proto();
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

lu_mem moonF_protosize (Proto *p) {
  return p->memorySize();
}


void Proto::free(moon_State* L) {
  if (!(getFlag() & PF_FIXED)) {
    moonM_freearray(L, getCode(), cast_sizet(getCodeSize()));
    moonM_freearray(L, getLineInfo(), cast_sizet(getLineInfoSize()));
    moonM_freearray(L, getAbsLineInfo(), cast_sizet(getAbsLineInfoSize()));
  }
  moonM_freearray(L, getProtos(), cast_sizet(getProtosSize()));
  moonM_freearray(L, getConstants(), cast_sizet(getConstantsSize()));
  moonM_freearray(L, getLocVars(), cast_sizet(getLocVarsSize()));
  moonM_freearray(L, getUpvalues(), cast_sizet(getUpvaluesSize()));
  moonM_free(L, this);
}

void moonF_freeproto (moon_State *L, Proto *f) {
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
    if (pc < locvar.getEndPC()) {  // is variable active?
      local_number--;
      if (local_number == 0)
        return getStringContents(locvar.getVarName());
    }
  }
  return nullptr;  // not found
}

const char *moonF_getlocalname (const Proto *f, int local_number, int pc) {
  return f->getLocalName(local_number, pc);
}

