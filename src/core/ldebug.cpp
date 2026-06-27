/*
** Debug Interface
** See Copyright Notice in lua.h
*/

#define MOON_CORE

#include "lprefix.h"


#include <algorithm>
#include <cstdarg>
#include <cstddef>
#include <cstring>

#include "lua.h"

#include "lapi.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "llex.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lvm.h"



// Both CClosure and LClosure have tt at same offset (from GCBase)
inline bool MoonClosure(const Closure* f) noexcept {
    return f != nullptr && f->c.getType() == ctb(MoonT::LCL);
}

static const char strlocal[] = "local";
static const char strupval[] = "upvalue";

static const char *funcnamefromcall (moon_State *L, CallInfo *callInfo,
                                                   const char **name);


static int currentpc (CallInfo *callInfo) {
  moon_assert(callInfo->isLua());
  return callInfo->getFunc()->getProto()->getPCRelative(callInfo->getSavedPC());
}


/*
** Get a "base line" to find the line corresponding to an instruction.
** Base lines are regularly placed at MAXIWTHABS intervals, so usually
** an integer division gets the right place. When the source file has
** large sequences of empty/comment lines, it may need extra entries,
** so the original estimate needs a correction.
** If the original estimate is -1, the initial 'if' ensures that the
** 'while' will run at least once.
** The assertion that the estimate is a lower bound for the correct base
** is valid as long as the debug info has been generated with the same
** value for MAXIWTHABS or smaller. (Previous releases use a little
** smaller value.)
*/
// Use span accessors
static int getbaseline (const Proto *f, int pc, int *basepc) {
  auto absLineInfoSpan = f->getDebugInfo().getAbsLineInfoSpan();
  if (absLineInfoSpan.empty() || pc < absLineInfoSpan[0].getPC()) {
    *basepc = -1;  /* start from the beginning */
    return f->getLineDefined();
  }
  else {
    // Binary search for the last AbsLineInfo with PC <= pc
    // std::upper_bound finds first element with PC > pc, so we go back one
    auto it = std::upper_bound(absLineInfoSpan.begin(), absLineInfoSpan.end(), pc,
                               [](int target_pc, const AbsLineInfo& info) {
                                 return target_pc < info.getPC();
                               });
    moon_assert(it != absLineInfoSpan.begin());  // we know there's at least one element with PC <= pc
    --it;  // go back to last element with PC <= pc
    *basepc = it->getPC();
    return it->getLine();
  }
}


/*
** Get the line corresponding to instruction 'pc' in function 'f';
** first gets a base line and from there does the increments until
** the desired instruction.
*/
// Use span accessors
int moonG_getfuncline (const Proto *f, int pc) {
  auto lineInfoSpan = f->getDebugInfo().getLineInfoSpan();
  if (lineInfoSpan.empty())  // no debug information?
    return -1;
  else {
    int basepc;
    int baseline = getbaseline(f, pc, &basepc);
    // Walk from basepc+1 to pc (inclusive), accumulating line deltas
    for (size_t i = static_cast<size_t>(basepc + 1); i <= static_cast<size_t>(pc); i++) {
      moon_assert(lineInfoSpan[i] != ABSLINEINFO);
      baseline += lineInfoSpan[i];  // correct line
    }
    return baseline;
  }
}


static int getcurrentline (CallInfo *callInfo) {
  return moonG_getfuncline(callInfo->getFunc()->getProto(), currentpc(callInfo));
}


/*
** Set 'trap' for all active Lua frames.
** This function can be called during a signal, under "reasonable"
** assumptions. A new 'callInfo' is completely linked in the list before it
** becomes part of the "active" list, and we assume that pointers are
** atomic; see comment in next function.
** (A compiler doing interprocedural optimizations could, theoretically,
** reorder memory writes in such a way that the list could be
** temporarily broken while inserting a new element. We simply assume it
** has no good reasons to do that.)
*/
static void settraps (CallInfo *callInfo) {
  for (; callInfo != nullptr; callInfo = callInfo->getPrevious())
    if (callInfo->isLua())
      callInfo->getTrap() = 1;
}


/*
** This function can be called during a signal, under "reasonable"
** assumptions.
** Fields 'basehookcount' and 'hookcount' (set by 'resethookcount')
** are for debug only, and it is no problem if they get arbitrary
** values (causes at most one wrong hook call). 'hookmask' is an atomic
** value. We assume that pointers are atomic too (e.g., gcc ensures that
** for all platforms where it runs). Moreover, 'hook' is always checked
** before being called (see 'moonD_hook').
*/
MOON_API void moon_sethook (moon_State *L, moon_Hook func, int mask, int count) {
  if (func == nullptr || mask == 0) {  // turn off hooks?
    mask = 0;
    func = nullptr;
  }
  L->setHook(func);
  L->setBaseHookCount(count);
  L->resetHookCount();
  L->setHookMask(cast_byte(mask));
  if (mask)
    settraps(L->getCI());  // to trace inside 'moonV_execute'
}


MOON_API moon_Hook moon_gethook (moon_State *L) {
  return L->getHook();
}


MOON_API int moon_gethookmask (moon_State *L) {
  return L->getHookMask();
}


MOON_API int moon_gethookcount (moon_State *L) {
  return L->getBaseHookCount();
}


MOON_API int moon_getstack (moon_State *L, int level, moon_Debug *ar) {
  int status;
  if (level < 0) return 0;  // invalid (negative) level
  moon_lock(L);
  CallInfo *callInfo;
  for (callInfo = L->getCI(); level > 0 && callInfo != L->getBaseCI(); callInfo = callInfo->getPrevious())
    level--;
  if (level == 0 && callInfo != L->getBaseCI()) {  // level found?
    status = 1;
    ar->i_ci = callInfo;
  }
  else status = 0;  // no such level
  moon_unlock(L);
  return status;
}


static const char *upvalname (const Proto *p, int upvalue) {
  TString *s = check_exp(upvalue < p->getUpvaluesSize(), p->getUpvalues()[upvalue].getName());
  if (s == nullptr) return "?";
  else return getStringContents(s);
}


static const char *findvararg (CallInfo *callInfo, int n, StkId *pos) {
  if (clLvalue(s2v(callInfo->funcRef().p))->getProto()->getFlag() & PF_ISVARARG) {
    int nextra = callInfo->getExtraArgs();
    if (n >= -nextra) {  // 'n' is negative
      *pos = callInfo->funcRef().p - nextra - (n + 1);
      return "(vararg)";  // generic name for any vararg
    }
  }
  return nullptr;  // no such vararg
}


// moon_State method
const char *moon_State::findLocal(CallInfo *ci_arg, int n, StkId *pos) {
  StkId base = ci_arg->funcRef().p + 1;
  const char *name = nullptr;
  if (ci_arg->isLua()) {
    if (n < 0)  // access to vararg values?
      return findvararg(ci_arg, n, pos);
    else
      name = ci_arg->getFunc()->getProto()->getLocalName(n, currentpc(ci_arg));  }
  if (name == nullptr) {  // no 'standard' name?
    StkId limit = (ci_arg == getCI()) ? getTop().p : ci_arg->getNext()->funcRef().p;
    if (limit - base >= n && n > 0) {  // is 'n' inside 'callInfo' stack?
      // generic name for any valid slot
      name = ci_arg->isLua() ? "(temporary)" : "(C temporary)";
    }
    else
      return nullptr;  // no name
  }
  if (pos)
    *pos = base + (n - 1);
  return name;
}

const char *moonG_findlocal (moon_State *L, CallInfo *callInfo, int n, StkId *pos) {
  return L->findLocal(callInfo, n, pos);
}


MOON_API const char *moon_getlocal (moon_State *L, const moon_Debug *ar, int n) {
  const char *name;
  moon_lock(L);
  if (ar == nullptr) {  // information about non-active function?
    if (!isLfunction(s2v(L->getTop().p - 1)))  // not a Lua function?
      name = nullptr;
    else  // consider live variables at function start (parameters)
      name = clLvalue(s2v(L->getTop().p - 1))->getProto()->getLocalName(n, 0);  }
  else {  // active function; get information through 'ar'
    StkId pos = nullptr;  // to avoid warnings
    name = moonG_findlocal(L, ar->i_ci, n, &pos);
    if (name) {
      *s2v(L->getTop().p) = *s2v(pos);  /* use operator= */
      api_incr_top(L);
    }
  }
  moon_unlock(L);
  return name;
}


MOON_API const char *moon_setlocal (moon_State *L, const moon_Debug *ar, int n) {
  StkId pos = nullptr;  // to avoid warnings
  moon_lock(L);
  const char *name = moonG_findlocal(L, ar->i_ci, n, &pos);
  if (name) {
    api_checkpop(L, 1);
    *s2v(pos) = *s2v(L->getTop().p - 1);  /* use operator= */
    L->getStackSubsystem().pop();  // pop value
  }
  moon_unlock(L);
  return name;
}


static void funcinfo (moon_Debug *ar, Closure *cl) {
  if (!MoonClosure(cl)) {
    ar->source = "=[C]";
    ar->srclen = LL("=[C]");
    ar->linedefined = -1;
    ar->lastlinedefined = -1;
    ar->what = "C";
  }
  else {
    const Proto *p = reinterpret_cast<LClosure*>(cl)->getProto();
    if (p->getSource()) {
      ar->source = getStringWithLength(p->getSource(), ar->srclen);
    }
    else {
      ar->source = "=?";
      ar->srclen = LL("=?");
    }
    ar->linedefined = p->getLineDefined();
    ar->lastlinedefined = p->getLastLineDefined();
    ar->what = (ar->linedefined == 0) ? "main" : "Lua";
  }
  moonO_chunkid(ar->short_src, ar->source, ar->srclen);
}


// Use span accessors
static int nextline (const Proto *p, int currentline, size_t pc) {
  auto lineInfoSpan = p->getDebugInfo().getLineInfoSpan();
  if (lineInfoSpan[pc] != ABSLINEINFO)
    return currentline + lineInfoSpan[pc];
  else
    return moonG_getfuncline(p, static_cast<int>(pc));
}


static void collectvalidlines (moon_State *L, Closure *f) {
  if (!MoonClosure(f)) {
    setnilvalue(s2v(L->getTop().p));
    api_incr_top(L);
  }
  else {
    const Proto *p = reinterpret_cast<LClosure*>(f)->getProto();
    int currentline = p->getLineDefined();
    Table *t = Table::create(L);  // new table to store active lines
    sethvalue2s(L, L->getTop().p, t);  // push it on stack
    api_incr_top(L);
    auto lineInfoSpan = p->getDebugInfo().getLineInfoSpan();
    if (!lineInfoSpan.empty()) {  // proto with debug information?
      size_t i;
      TValue v;
      setbtvalue(&v);  // boolean 'true' to be the value of all indices
      if (!(p->getFlag() & PF_ISVARARG))  // regular function?
        i = 0;  // consider all instructions
      else {  // vararg function
        auto codeSpan = p->getCodeSpan();
        moon_assert(InstructionView(codeSpan[0]).opcode() == OP_VARARGPREP);
        currentline = nextline(p, currentline, 0);
        i = 1;  // skip first instruction (OP_VARARGPREP)
      }
      for (; i < lineInfoSpan.size(); i++) {  // for each instruction
        currentline = nextline(p, currentline, i);  // get its line
        t->setInt(L, currentline, &v);  // table[line] = true
      }
    }
  }
}


static const char *getfuncname (moon_State *L, CallInfo *callInfo, const char **name) {
  // calling function is a known function?
  if (callInfo != nullptr && !(callInfo->getCallStatus() & CIST_TAIL))
    return funcnamefromcall(L, callInfo->getPrevious(), name);
  else return nullptr;  // no way to find a name
}


static int auxgetinfo (moon_State *L, const char *what, moon_Debug *ar,
                       Closure *f, CallInfo *callInfo) {
  int status = 1;
  for (; *what; what++) {
    switch (*what) {
      case 'S': {
        funcinfo(ar, f);
        break;
      }
      case 'l': {
        ar->currentline = (callInfo && callInfo->isLua()) ? getcurrentline(callInfo) : -1;
        break;
      }
      case 'u': {
        ar->nups = (f == nullptr) ? 0 : f->c.getNumUpvalues();
        if (!MoonClosure(f)) {
          ar->isvararg = 1;
          ar->nparams = 0;
        }
        else {
          LClosure* lf = reinterpret_cast<LClosure*>(f);
          ar->isvararg = (lf->getProto()->getFlag() & PF_ISVARARG) ? 1 : 0;
          ar->nparams = lf->getProto()->getNumParams();
        }
        break;
      }
      case 't': {
        if (callInfo != nullptr) {
          ar->istailcall = !!(callInfo->getCallStatus() & CIST_TAIL);
          ar->extraargs =
                   cast_uchar((callInfo->getCallStatus() & MAX_CCMT) >> CIST_CCMT);
        }
        else {
          ar->istailcall = 0;
          ar->extraargs = 0;
        }
        break;
      }
      case 'n': {
        ar->namewhat = getfuncname(L, callInfo, &ar->name);
        if (ar->namewhat == nullptr) {
          ar->namewhat = "";  // not found
          ar->name = nullptr;
        }
        break;
      }
      case 'r': {
        if (callInfo == nullptr || !(callInfo->getCallStatus() & CIST_HOOKED))
          ar->ftransfer = ar->ntransfer = 0;
        else {
          ar->ftransfer = L->getTransferInfo().ftransfer;
          ar->ntransfer = L->getTransferInfo().ntransfer;
        }
        break;
      }
      case 'L':
      case 'f':  // handled by moon_getinfo
        break;
      default: status = 0;  // invalid option
    }
  }
  return status;
}


MOON_API int moon_getinfo (moon_State *L, const char *what, moon_Debug *ar) {
  moon_lock(L);
  CallInfo *callInfo;
  TValue *func;
  if (*what == '>') {
    callInfo = nullptr;
    func = s2v(L->getTop().p - 1);
    api_check(L, ttisfunction(func), "function expected");
    what++;  // skip the '>'
    L->getStackSubsystem().pop();  // pop function
  }
  else {
    callInfo = ar->i_ci;
    func = s2v(callInfo->funcRef().p);
    moon_assert(ttisfunction(func));
  }
  Closure *cl = ttisclosure(func) ? clvalue(func) : nullptr;
  int status = auxgetinfo(L, what, ar, cl, callInfo);
  if (strchr(what, 'f')) {
    L->getStackSubsystem().setSlot(L->getTop().p, func);
    api_incr_top(L);
  }
  if (strchr(what, 'L'))
    collectvalidlines(L, cl);
  moon_unlock(L);
  return status;
}


/*
** {======================================================
** Symbolic Execution
** =======================================================
*/


static int filterpc (int pc, int jmptarget) {
  if (pc < jmptarget)  // is code conditional (inside a jump)?
    return -1;  // cannot know who sets that register
  else return pc;  // current position sets that register
}


/*
** Try to find last instruction before 'lastpc' that modified register 'reg'.
*/
static int findsetreg (const Proto *p, int lastpc, int reg) {
  int setreg = -1;  // keep last instruction that changed 'reg'
  int jmptarget = 0;  // any code before this address is conditional
  if (InstructionView(p->getCode()[lastpc]).testMMMode())
    lastpc--;  // previous instruction was not actually executed
  for (int pc = 0; pc < lastpc; pc++) {
    Instruction i = p->getCode()[pc];
    InstructionView view(i);
    OpCode op = static_cast<OpCode>(view.opcode());
    int a = view.a();
    int change;  // true if current instruction changed 'reg'
    switch (op) {
      case OP_LOADNIL: {  // set registers from 'a' to 'a+b'
        int b = view.b();
        change = (a <= reg && reg <= a + b);
        break;
      }
      case OP_TFORCALL: {  // affect all regs above its base
        change = (reg >= a + 2);
        break;
      }
      case OP_CALL:
      case OP_TAILCALL: {  // affect all registers above base
        change = (reg >= a);
        break;
      }
      case OP_JMP: {  // doesn't change registers, but changes 'jmptarget'
        int b = view.sj();
        int dest = pc + 1 + b;
        // jump does not skip 'lastpc' and is larger than current one?
        if (dest <= lastpc && dest > jmptarget)
          jmptarget = dest;  // update 'jmptarget'
        change = 0;
        break;
      }
      default:  // any instruction that sets A
        change = (view.testAMode() && reg == a);
        break;
    }
    if (change)
      setreg = filterpc(pc, jmptarget);
  }
  return setreg;
}


/*
** Find a "name" for the constant 'c'.
*/
static const char *kname (const Proto *p, int index, const char **name) {
  TValue *kvalue = &p->getConstants()[index];
  if (ttisstring(kvalue)) {
    *name = getStringContents(tsvalue(kvalue));
    return "constant";
  }
  else {
    *name = "?";
    return nullptr;
  }
}


static const char *basicgetobjname (const Proto *p, int *ppc, int reg,
                                    const char **name) {
  int pc = *ppc;
  *name = p->getLocalName(reg + 1, pc);  if (*name)  /* is a local? */
    return strlocal;
  // else try symbolic execution
  *ppc = pc = findsetreg(p, pc, reg);
  if (pc != -1) {  // could find instruction?
    Instruction i = p->getCode()[pc];
    OpCode op = static_cast<OpCode>(InstructionView(i).opcode());
    switch (op) {
      case OP_MOVE: {
        int b = InstructionView(i).b();  // move from 'b' to 'a'
        if (b < InstructionView(i).a())
          return basicgetobjname(p, ppc, b, name);  // get name for 'b'
        break;
      }
      case OP_GETUPVAL: {
        *name = upvalname(p, InstructionView(i).b());
        return strupval;
      }
      case OP_LOADK: return kname(p, InstructionView(i).bx(), name);
      case OP_LOADKX: return kname(p, InstructionView(p->getCode()[pc + 1]).ax(), name);
      default: break;
    }
  }
  return nullptr;  // could not find reasonable name
}


/*
** Find a "name" for the register 'c'.
*/
static void rname (const Proto *p, int pc, int c, const char **name) {
  const char *what = basicgetobjname(p, &pc, c, name);  // search for 'c'
  if (!(what && *what == 'c'))  // did not find a constant name?
    *name = "?";
}


/*
** Check whether table being indexed by instruction 'i' is the
** environment '_ENV'
*/
static const char *isEnv (const Proto *p, int pc, Instruction i, int isup) {
  int t = InstructionView(i).b();  // table index
  const char *name;  // name of indexed variable
  if (isup) {  // is 't' an upvalue?
    name = upvalname(p, t);
  }
  else {  // 't' is a register
    const char *what = basicgetobjname(p, &pc, t, &name);
    /* 'name' must be the name of a local variable (at the current
       level or an upvalue) */
    if (what != strlocal && what != strupval)
      name = nullptr;  // cannot be the variable _ENV
  }
  return (name && strcmp(name, MOON_ENV) == 0) ? "global" : "field";
}


/*
** Extend 'basicgetobjname' to handle table accesses
*/
static const char *getobjname (const Proto *p, int lastpc, int reg,
                               const char **name) {
  const char *kind = basicgetobjname(p, &lastpc, reg, name);
  if (kind != nullptr)
    return kind;
  else if (lastpc != -1) {  // could find instruction?
    Instruction i = p->getCode()[lastpc];
    OpCode op = static_cast<OpCode>(InstructionView(i).opcode());
    switch (op) {
      case OP_GETTABUP: {
        int k = InstructionView(i).c();  // key index
        kname(p, k, name);
        return isEnv(p, lastpc, i, 1);
      }
      case OP_GETTABLE: {
        int k = InstructionView(i).c();  // key index
        rname(p, lastpc, k, name);
        return isEnv(p, lastpc, i, 0);
      }
      case OP_GETI: {
        *name = "integer index";
        return "field";
      }
      case OP_GETFIELD: {
        int k = InstructionView(i).c();  // key index
        kname(p, k, name);
        return isEnv(p, lastpc, i, 0);
      }
      case OP_SELF: {
        int k = InstructionView(i).c();  // key index
        kname(p, k, name);
        return "method";
      }
      default: break;  // go through to return nullptr
    }
  }
  return nullptr;  // could not find reasonable name
}


/*
** Try to find a name for a function based on the code that called it.
** (Only works when function was called by a Lua function.)
** Returns what the name is (e.g., "for iterator", "method",
** "metamethod") and sets '*name' to point to the name.
*/
static const char *funcnamefromcode (moon_State *L, const Proto *p,
                                     int pc, const char **name) {
  TMS metamethodEvent = (TMS)0;  // (initial value avoids warnings)
  Instruction i = p->getCode()[pc];  // calling instruction
  switch (InstructionView(i).opcode()) {
    case OP_CALL:
    case OP_TAILCALL:
      return getobjname(p, pc, InstructionView(i).a(), name);  // get function name
    case OP_TFORCALL: {  // for iterator
      *name = "for iterator";
       return "for iterator";
    }
    // other instructions can do calls through metamethods
    case OP_SELF: case OP_GETTABUP: case OP_GETTABLE:
    case OP_GETI: case OP_GETFIELD:
      metamethodEvent = TMS::TM_INDEX;
      break;
    case OP_SETTABUP: case OP_SETTABLE: case OP_SETI: case OP_SETFIELD:
      metamethodEvent = TMS::TM_NEWINDEX;
      break;
    case OP_MMBIN: case OP_MMBINI: case OP_MMBINK: {
      metamethodEvent = static_cast<TMS>(InstructionView(i).c());
      break;
    }
    case OP_UNM: metamethodEvent = TMS::TM_UNM; break;
    case OP_BNOT: metamethodEvent = TMS::TM_BNOT; break;
    case OP_LEN: metamethodEvent = TMS::TM_LEN; break;
    case OP_CONCAT: metamethodEvent = TMS::TM_CONCAT; break;
    case OP_EQ: metamethodEvent = TMS::TM_EQ; break;
    // no cases for OP_EQI and OP_EQK, as they don't call metamethods
    case OP_LT: case OP_LTI: case OP_GTI: metamethodEvent = TMS::TM_LT; break;
    case OP_LE: case OP_LEI: case OP_GEI: metamethodEvent = TMS::TM_LE; break;
    case OP_CLOSE: case OP_RETURN: metamethodEvent = TMS::TM_CLOSE; break;
    default:
      return nullptr;  // cannot find a reasonable name
  }
  *name = getShortStringContents(G(L)->getTMName(static_cast<int>(metamethodEvent))) + 2;
  return "metamethod";
}


/*
** Try to find a name for a function based on how it was called.
*/
static const char *funcnamefromcall (moon_State *L, CallInfo *callInfo,
                                                   const char **name) {
  if (callInfo->getCallStatus() & CIST_HOOKED) {  // was it called inside a hook?
    *name = "?";
    return "hook";
  }
  else if (callInfo->getCallStatus() & CIST_FIN) {  // was it called as a finalizer?
    *name = "__gc";
    return "metamethod";  // report it as such
  }
  else if (callInfo->isLua())
    return funcnamefromcode(L, callInfo->getFunc()->getProto(), currentpc(callInfo), name);
  else
    return nullptr;
}

// }======================================================



/*
** Check whether pointer 'o' points to some value in the stack frame of
** the current function and, if so, returns its index.  Because 'o' may
** not point to a value in this stack, we cannot compare it with the
** region boundaries (undefined behavior in ISO C).
*/
static int instack (CallInfo *callInfo, const TValue *o) {
  StkId base = callInfo->funcRef().p + 1;
  StkId end = callInfo->topRef().p;
  auto it = std::find_if(base, end, [o](const StackValue& sv) {
    return o == s2v(&sv);
  });
  return (it != end) ? static_cast<int>(it - base) : -1;  // return position or -1 if not found
}


/*
** Checks whether value 'o' came from an upvalue. (That can only happen
** with instructions OP_GETTABUP/OP_SETTABUP, which operate directly on
** upvalues.)
*/
static const char *getupvalname (CallInfo *callInfo, const TValue *o,
                                 const char **name) {
  LClosure *c = callInfo->getFunc();
  for (int i = 0; i < c->getNumUpvalues(); i++) {
    if (c->getUpval(i)->getVP() == o) {
      *name = upvalname(c->getProto(), i);
      return strupval;
    }
  }
  return nullptr;
}


static const char *formatvarinfo (moon_State *L, const char *kind,
                                                const char *name) {
  if (kind == nullptr)
    return "";  // no information
  else
    return moonO_pushfstring(L, " (%s '%s')", kind, name);
}

/*
** Build a string with a "description" for the value 'o', such as
** "variable 'x'" or "upvalue 'y'".
*/
static const char *varinfo (moon_State *L, const TValue *o) {
  CallInfo *callInfo = L->getCI();
  const char *name = nullptr;  // to avoid warnings
  const char *kind = nullptr;
  if (callInfo->isLua()) {
    kind = getupvalname(callInfo, o, &name);  // check whether 'o' is an upvalue
    if (!kind) {  // not an upvalue?
      int reg = instack(callInfo, o);  // try a register
      if (reg >= 0)  // is 'o' a register?
        kind = getobjname(callInfo->getFunc()->getProto(), currentpc(callInfo), reg, &name);
    }
  }
  return formatvarinfo(L, kind, name);
}


/*
** Raise a type error
*/
static l_noret typeerror (moon_State *L, const TValue *o, const char *op,
                          const char *extra) {
  const char *t = moonT_objtypename(L, o);
  moonG_runerror(L, "attempt to %s a %s value%s", op, t, extra);
}


/*
** Raise a type error with "standard" information about the faulty
** object 'o' (using 'varinfo').
*/
// moon_State method
l_noret moon_State::typeError(const TValue *o, const char *op) {
  typeerror(this, o, op, varinfo(this, o));
}

l_noret moonG_typeerror (moon_State *L, const TValue *o, const char *op) {
  L->typeError(o, op);
}


/*
** Raise an error for calling a non-callable object. Try to find a name
** for the object based on how it was called ('funcnamefromcall'); if it
** cannot get a name there, try 'varinfo'.
*/
// moon_State method
l_noret moon_State::callError(const TValue *o) {
  const char *name = nullptr;  // to avoid warnings
  const char *kind = funcnamefromcall(this, callInfo, &name);
  const char *extra = kind ? formatvarinfo(this, kind, name) : varinfo(this, o);
  typeerror(this, o, "call", extra);
}

l_noret moonG_callerror (moon_State *L, const TValue *o) {
  L->callError(o);
}


// moon_State method
l_noret moon_State::forError(const TValue *o, const char *what) {
  runError("bad 'for' %s (number expected, got %s)",
           what, moonT_objtypename(this, o));
}

l_noret moonG_forerror (moon_State *L, const TValue *o, const char *what) {
  L->forError(o, what);
}


// moon_State method
l_noret moon_State::concatError(const TValue *p1, const TValue *p2) {
  if (ttisstring(p1) || cvt2str(p1)) p1 = p2;
  typeError(p1, "concatenate");
}

l_noret moonG_concaterror (moon_State *L, const TValue *p1, const TValue *p2) {
  L->concatError(p1, p2);
}


// moon_State method
l_noret moon_State::opinterError(const TValue *p1, const TValue *p2, const char *msg) {
  if (!ttisnumber(p1))  // first operand is wrong?
    p2 = p1;  // now second is wrong
  typeError(p2, msg);
}

l_noret moonG_opinterror (moon_State *L, const TValue *p1,
                         const TValue *p2, const char *msg) {
  L->opinterError(p1, p2, msg);
}


/*
** Error when both values are convertible to numbers, but not to integers
*/
// moon_State method
l_noret moon_State::toIntError(const TValue *p1, const TValue *p2) {
  moon_Integer temp;
  if (!tointegerns(p1, &temp))
    p2 = p1;
  runError("number%s has no integer representation", varinfo(this, p2));
}

l_noret moonG_tointerror (moon_State *L, const TValue *p1, const TValue *p2) {
  L->toIntError(p1, p2);
}


// moon_State method
l_noret moon_State::orderError(const TValue *p1, const TValue *p2) {
  const char *t1 = moonT_objtypename(this, p1);
  const char *t2 = moonT_objtypename(this, p2);
  if (strcmp(t1, t2) == 0)
    runError("attempt to compare two %s values", t1);
  else
    runError("attempt to compare %s with %s", t1, t2);
}

l_noret moonG_ordererror (moon_State *L, const TValue *p1, const TValue *p2) {
  L->orderError(p1, p2);
}


// add src:line information to 'msg'
// moon_State method
const char *moon_State::addInfo(const char *msg, TString *src, int line) {
  if (src == nullptr)  // no debug information?
    return moonO_pushfstring(this, "?:?: %s", msg);
  else {
    char buff[MOON_IDSIZE];
    size_t idlen;
    const char *id = getStringWithLength(src, idlen);
    moonO_chunkid(buff, id, idlen);
    return moonO_pushfstring(this, "%s:%d: %s", buff, line, msg);
  }
}

const char *moonG_addinfo (moon_State *L, const char *msg, TString *src,
                                        int line) {
  return L->addInfo(msg, src, line);
}


// moon_State method
l_noret moon_State::errorMsg() {
  if (getErrFunc() != 0) {  // is there an error handling function?
    StkId errfunc_ptr = this->restoreStack(getErrFunc());
    moon_assert(ttisfunction(s2v(errfunc_ptr)));
    *s2v(getTop().p) = *s2v(getTop().p - 1);  /* move argument - use operator= */
    *s2v(getTop().p - 1) = *s2v(errfunc_ptr);  /* push function - use operator= */
    getStackSubsystem().push();  // assume EXTRA_STACK
    callNoYield(getTop().p - 2, 1);  // call it
  }
  if (ttisnil(s2v(getTop().p - 1))) {  // error object is nil?
    // change it to a proper message
    setsvalue2s(this, getTop().p - 1, TString::create(this, "<no error object>", 17));
  }
  doThrow(MOON_ERRRUN);
}

l_noret moonG_errormsg (moon_State *L) {
  L->errorMsg();
}


// moon_State method
l_noret moon_State::runError(const char *fmt, ...) {
  const char *msg;
  va_list argp;
  moonC_checkGC(this);  // error message uses memory
  pushvfstring(this, argp, fmt, msg);
  if (callInfo->isLua()) {  // Lua function?
    // add source:line information
    addInfo(msg, callInfo->getFunc()->getProto()->getSource(), getcurrentline(callInfo));
    *s2v(getTop().p - 2) = *s2v(getTop().p - 1);  /* remove 'msg' - use operator= */
    getStackSubsystem().pop();
  }
  errorMsg();
}

l_noret moonG_runerror (moon_State *L, const char *fmt, ...) {
  const char *msg;
  va_list argp;
  moonC_checkGC(L);  // error message uses memory
  pushvfstring(L, argp, fmt, msg);
  if (L->getCI()->isLua()) {  // Lua function?
    // add source:line information
    L->addInfo(msg, L->getCI()->getFunc()->getProto()->getSource(), getcurrentline(L->getCI()));
    *s2v(L->getTop().p - 2) = *s2v(L->getTop().p - 1);  /* remove 'msg' - use operator= */
    L->getStackSubsystem().pop();
  }
  L->errorMsg();
}


/*
** Check whether new instruction 'newpc' is in a different line from
** previous instruction 'oldpc'. More often than not, 'newpc' is only
** one or a few instructions after 'oldpc' (it must be after, see
** caller), so try to avoid calling 'moonG_getfuncline'. If they are
** too far apart, there is a good chance of a ABSLINEINFO in the way,
** so it goes directly to 'moonG_getfuncline'.
*/
// Use span accessors
static int changedline (const Proto *p, int oldpc, int newpc) {
  auto lineInfoSpan = p->getDebugInfo().getLineInfoSpan();
  if (lineInfoSpan.empty())  // no debug information?
    return 0;
  if (newpc - oldpc < MAXIWTHABS / 2) {  // not too far apart?
    int delta = 0;  // line difference
    size_t pc = static_cast<size_t>(oldpc);
    for (;;) {
      ++pc;
      const int lineinfo = lineInfoSpan[pc];
      if (lineinfo == ABSLINEINFO)
        break;  // cannot compute delta; fall through
      delta += lineinfo;
      if (static_cast<int>(pc) == newpc)
        return (delta != 0);  // delta computed successfully
    }
  }
  /* either instructions are too far apart or there is an absolute line
     info in the way; compute line difference explicitly */
  return (moonG_getfuncline(p, oldpc) != moonG_getfuncline(p, newpc));
}


/*
** Traces Lua calls. If code is running the first instruction of a function,
** and function is not vararg, and it is not coming from an yield,
** calls 'moonD_hookcall'. (Vararg functions will call 'moonD_hookcall'
** after adjusting its variable arguments; otherwise, they could call
** a line/count hook before the call hook. Functions coming from
** an yield already called 'moonD_hookcall' before yielding.)
*/
// moon_State method
int moon_State::traceCall() {
  CallInfo *ci_local = callInfo;
  Proto *p = ci_local->getFunc()->getProto();
  ci_local->getTrap() = 1;  // ensure hooks will be checked
  if (ci_local->getSavedPC() == p->getCode()) {  // first instruction (not resuming)?
    if (p->getFlag() & PF_ISVARARG)
      return 0;  // hooks will start at VARARGPREP instruction
    else if (!(ci_local->callStatusRef() & CIST_HOOKYIELD))  // not yielded?
      hookCall(ci_local);  // check 'call' hook
  }
  return 1;  // keep 'trap' on
}

int moonG_tracecall (moon_State *L) {
  return L->traceCall();
}


/*
** Traces the execution of a Lua function. Called before the execution
** of each opcode, when debug is on. 'L->oldpc' stores the last
** instruction traced, to detect line changes. When entering a new
** function, 'npci' will be zero and will test as a new line whatever
** the value of 'oldpc'.  Some exceptional conditions may return to
** a function without setting 'oldpc'. In that case, 'oldpc' may be
** invalid; if so, use zero as a valid value. (A wrong but valid 'oldpc'
** at most causes an extra call to a line hook.)
** This function is not "Protected" when called, so it should correct
** 'L->getTop().p' before calling anything that can run the GC.
*/
// moon_State method
int moon_State::traceExec(const Instruction *pc) {
  CallInfo *ci_local = callInfo;
  lu_byte mask = cast_byte(getHookMask());
  const Proto *p = ci_local->getFunc()->getProto();
  if (!(mask & (MOON_MASKLINE | MOON_MASKCOUNT))) {  // no hooks?
    ci_local->getTrap() = 0;  // don't need to stop again
    return 0;  // turn off 'trap'
  }
  pc++;  // reference is always next instruction
  ci_local->setSavedPC(pc);  // save 'pc'
  int counthook = (mask & MOON_MASKCOUNT) && (--getHookCountRef() == 0);
  if (counthook)
    this->resetHookCount();  // reset count
  else if (!(mask & MOON_MASKLINE))
    return 1;  // no line hook and count != 0; nothing to be done now
  if (ci_local->callStatusRef() & CIST_HOOKYIELD) {  // hook yielded last time?
    ci_local->callStatusRef() &= ~CIST_HOOKYIELD;  // erase mark
    return 1;  // do not call hook again (VM yielded, so it did not move)
  }
  if (!moonP_isIT(*(ci_local->getSavedPC() - 1)))  // top not being used?
    getStackSubsystem().setTopPtr(ci_local->topRef().p);  // correct top
  if (counthook)
    callHook(MOON_HOOKCOUNT, -1, 0, 0);  // call count hook
  if (mask & MOON_MASKLINE) {
    // 'oldpc' may be invalid; use zero in this case
    int oldpc_val = (getOldPC() < p->getCodeSize()) ? getOldPC() : 0;
    int npci = p->getPCRelative(pc);
    if (npci <= oldpc_val ||  // call hook when jump back (loop),
        changedline(p, oldpc_val, npci)) {  // or when enter new line
      int newline = moonG_getfuncline(p, npci);
      callHook(MOON_HOOKLINE, newline, 0, 0);  // call line hook
    }
    setOldPC(npci);  // 'pc' of last call to line hook
  }
  if (getStatus() == MOON_YIELD) {  // did hook yield?
    if (counthook)
      setHookCount(1);  // undo decrement to zero
    ci_local->callStatusRef() |= CIST_HOOKYIELD;  // mark that it yielded
    doThrow(MOON_YIELD);
  }
  return 1;  // keep 'trap' on
}

int moonG_traceexec (moon_State *L, const Instruction *pc) {
  return L->traceExec(pc);
}

