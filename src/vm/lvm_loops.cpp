/*
** For-loop operations for Lua VM
** See Copyright Notice in lua.h
*/

#define MOON_CORE

#include "lprefix.h"

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lobject.h"
#include "lstate.h"
#include "lvirtualmachine.h"
#include "lvm.h"


/*
** Try to convert a 'for' limit to an integer, preserving the semantics
** of the loop. Return true if the loop must not run; otherwise, '*p'
** gets the integer limit.
** (The following explanation assumes a positive step; it is valid for
** negative steps mutatis mutandis.)
** If the limit is an integer or can be converted to an integer,
** rounding down, that is the limit.
** Otherwise, check whether the limit can be converted to a float. If
** the float is too large, clip it to MOON_MAXINTEGER.  If the float
** is too negative, the loop should not run, because any initial
** integer value is greater than such limit; so, the function returns
** true to signal that. (For this latter case, no integer limit would be
** correct; even a limit of MOON_MININTEGER would run the loop once for
** an initial value equal to MOON_MININTEGER.)
*/
int moon_State::forLimit(moon_Integer init, const TValue *lim,
                        moon_Integer *p, moon_Integer step) {
  if (!getVM().tointeger(lim, p, (step < 0 ? F2Imod::F2Iceil : F2Imod::F2Ifloor))) {
    // not coercible to in integer
    moon_Number flim;  // try to convert to float
    if (!tonumber(lim, &flim))  // cannot convert to float?
      moonG_forerror(this, lim, "limit");
    // else 'flim' is a float out of integer bounds
    if (mooni_numlt(0, flim)) {  // if it is positive, it is too large
      if (step < 0) return 1;  // initial value must be less than it
      *p = MOON_MAXINTEGER;  /* truncate */
    }
    else {  // it is less than min integer
      if (step > 0) return 1;  // initial value must be greater than it
      *p = MOON_MININTEGER;  /* truncate */
    }
  }
  return (step > 0 ? init > *p : init < *p);  // not to run?
}


/*
** Prepare a numerical for loop (opcode OP_FORPREP).
** Before execution, stack is as follows:
**   ra     : initial value
**   ra + 1 : limit
**   ra + 2 : step
** Return true to skip the loop. Otherwise,
** after preparation, stack will be as follows:
**   ra     : loop counter (integer loops) or limit (float loops)
**   ra + 1 : step
**   ra + 2 : control variable
*/
int moon_State::forPrep(StkId ra) {
  auto *pinit = s2v(ra);
  auto *plimit = s2v(ra + 1);
  auto *pstep = s2v(ra + 2);
  if (ttisinteger(pinit) && ttisinteger(pstep)) {  // integer loop?
    auto init = ivalue(pinit);
    auto step = ivalue(pstep);
    moon_Integer limit;
    if (step == 0)
      moonG_runerror(this, "'for' step is zero");
    if (this->forLimit(init, plimit, &limit, step))
      return 1;  // skip the loop
    else {  // prepare loop counter
      moon_Unsigned count;
      if (step > 0) {  // ascending loop?
        count = l_castS2U(limit) - l_castS2U(init);
        if (step != 1)  // avoid division in the too common case
          count /= l_castS2U(step);
      }
      else {  // step < 0; descending loop
        count = l_castS2U(init) - l_castS2U(limit);
        // Handle MOON_MININTEGER edge case explicitly
        if (l_unlikely(step == MOON_MININTEGER)) {
          // For step == MOON_MININTEGER, count should be divided by max value
          count /= l_castS2U(MOON_MAXINTEGER) + 1u;
        }
        else {
          // 'step+1' avoids negating 'mininteger' in normal case
          count /= l_castS2U(-(step + 1)) + 1u;
        }
      }
      // use 'changeInt' for places that for sure had integers
      s2v(ra)->changeInt(l_castU2S(count));  // change init to count
      s2v(ra + 1)->setInt(step);  // change limit to step
      s2v(ra + 2)->changeInt(init);  // change step to init
    }
  }
  else {  // try making all values floats
    moon_Number init, limit, step;
    if (l_unlikely(!tonumber(plimit, &limit)))
      moonG_forerror(this, plimit, "limit");
    if (l_unlikely(!tonumber(pstep, &step)))
      moonG_forerror(this, pstep, "step");
    if (l_unlikely(!tonumber(pinit, &init)))
      moonG_forerror(this, pinit, "initial value");
    if (step == 0)
      moonG_runerror(this, "'for' step is zero");
    if (mooni_numlt(0, step) ? mooni_numlt(limit, init)
                            : mooni_numlt(init, limit))
      return 1;  // skip the loop
    else {
      // make sure all values are floats
      s2v(ra)->setFloat(limit);
      s2v(ra + 1)->setFloat(step);
      s2v(ra + 2)->setFloat(init);  // control variable
    }
  }
  return 0;
}


/*
** Execute a step of a float numerical for loop, returning
** true iff the loop must continue. (The integer case is
** written online with opcode OP_FORLOOP, for performance.)
*/
int moon_State::floatForLoop(StkId ra) {
  auto step = fltvalue(s2v(ra + 1));
  auto limit = fltvalue(s2v(ra));
  auto idx = fltvalue(s2v(ra + 2));  // control variable
  idx = mooni_numadd(this, idx, step);  // increment index
  if (mooni_numlt(0, step) ? mooni_numle(idx, limit)
                          : mooni_numle(limit, idx)) {
    s2v(ra + 2)->changeFloat(idx);  // update control variable
    return 1;  // jump back
  }
  else
    return 0;  // finish the loop
}
