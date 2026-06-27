/*
** Comparison operations for Lua VM
** See Copyright Notice in lua.h
*/

#define MOON_CORE

#include "mprefix.h"

#include <cstring>

#include "moon.h"

#include "mdebug.h"
#include "mdo.h"
#include "mobject.h"
#include "mstate.h"
#include "mstring.h"
#include "mtable.h"
#include "mtm.h"
#include "mvm.h"
#include "mvirtualmachine.h"


/*
** Compare two strings 'ts1' x 'ts2', returning an integer less-equal-
** -greater than zero if 'ts1' is less-equal-greater than 'ts2'.
** The code is a little tricky because it allows '\0' in the strings
** and it uses 'strcoll' (to respect locales) for each segment
** of the strings. Note that segments can compare equal but still
** have different lengths.
*/
[[nodiscard]] int l_strcmp (const TString *ts1, const TString *ts2) {
  size_t rl1;  // real length
  auto *s1 = getStringWithLength(ts1, rl1);
  size_t rl2;
  auto *s2 = getStringWithLength(ts2, rl2);
  for (;;) {  // for each segment
    auto temp = strcoll(s1, s2);
    if (temp != 0)  // not equal?
      return temp;  // done
    else {  // strings are equal up to a '\0'
      auto zl1 = strlen(s1);  // index of first '\0' in 's1'
      auto zl2 = strlen(s2);  // index of first '\0' in 's2'
      if (zl2 == rl2)  // 's2' is finished?
        return (zl1 == rl1) ? 0 : 1;  // check 's1'
      else if (zl1 == rl1)  // 's1' is finished?
        return -1;  // 's1' is less than 's2' ('s2' is not finished)
      // both strings longer than 'zl'; go on comparing after the '\0'
      zl1++; zl2++;
      s1 += zl1; rl1 -= zl1; s2 += zl2; rl2 -= zl2;
    }
  }
}


/*
** Check whether integer 'i' is less than float 'f'. If 'i' has an
** exact representation as a float ('l_intfitsf'), compare numbers as
** floats. Otherwise, use the equivalence 'i < f <=> i < ceil(f)'.
** If 'ceil(f)' is out of integer range, either 'f' is greater than
** all integers or less than all integers.
** (The test with 'l_intfitsf' is only for performance; the else
** case is correct for all values, but it is slow due to the conversion
** from float to int.)
** When 'f' is NaN, comparisons must result in false.
**
** DESIGN RATIONALE: Lua supports both integer and float types, requiring
** careful mixed-type comparisons. Direct float conversion can lose precision
** for large integers (> 2^53 on typical platforms). Using ceiling/floor
** functions and integer comparison preserves exact semantics.
**
** Example: For a 64-bit integer 2^60, comparing as floats would round it,
** potentially giving incorrect results. Instead, we compute ceil(f) as an
** integer and compare in the integer domain where no precision is lost.
*/
[[nodiscard]] int LTintfloat (moon_Integer i, moon_Number f) {
  if (l_intfitsf(i))
    return mooni_numlt(cast_num(i), f);  // compare them as floats
  else {  // i < f <=> i < ceil(f)
    moon_Integer fi;
    if (VirtualMachine::flttointeger(f, &fi, F2Imod::F2Iceil))  // fi = ceil(f)
      return i < fi;  // compare them as integers
    else  // 'f' is either greater or less than all integers
      return f > 0;  // greater?
  }
}


/*
** Check whether integer 'i' is less than or equal to float 'f'.
** See comments on previous function.
*/
[[nodiscard]] int LEintfloat (moon_Integer i, moon_Number f) {
  if (l_intfitsf(i))
    return mooni_numle(cast_num(i), f);  // compare them as floats
  else {  // i <= f <=> i <= floor(f)
    moon_Integer fi;
    if (VirtualMachine::flttointeger(f, &fi, F2Imod::F2Ifloor))  // fi = floor(f)
      return i <= fi;  // compare them as integers
    else  // 'f' is either greater or less than all integers
      return f > 0;  // greater?
  }
}


/*
** Check whether float 'f' is less than integer 'i'.
** See comments on previous function.
*/
[[nodiscard]] int LTfloatint (moon_Number f, moon_Integer i) {
  if (l_intfitsf(i))
    return mooni_numlt(f, cast_num(i));  // compare them as floats
  else {  // f < i <=> floor(f) < i
    moon_Integer fi;
    if (VirtualMachine::flttointeger(f, &fi, F2Imod::F2Ifloor))  // fi = floor(f)
      return fi < i;  // compare them as integers
    else  // 'f' is either greater or less than all integers
      return f < 0;  // less?
  }
}


/*
** Check whether float 'f' is less than or equal to integer 'i'.
** See comments on previous function.
*/
[[nodiscard]] int LEfloatint (moon_Number f, moon_Integer i) {
  if (l_intfitsf(i))
    return mooni_numle(f, cast_num(i));  // compare them as floats
  else {  // f <= i <=> ceil(f) <= i
    moon_Integer fi;
    if (VirtualMachine::flttointeger(f, &fi, F2Imod::F2Iceil))  // fi = ceil(f)
      return fi <= i;  // compare them as integers
    else  // 'f' is either greater or less than all integers
      return f < 0;  // less?
  }
}


/*
** return 'l < r' for non-numbers.
*/
int moon_State::lessThanOthers(const TValue *l, const TValue *r) {
  moon_assert(!ttisnumber(l) || !ttisnumber(r));
  if (ttisstring(l) && ttisstring(r))  // both are strings?
    return *tsvalue(l) < *tsvalue(r);  // Use TString operator<
  else
    return moonT_callorderTM(this, l, r, TMS::TM_LT);
}


/*
** return 'l <= r' for non-numbers.
*/
int moon_State::lessEqualOthers(const TValue *l, const TValue *r) {
  moon_assert(!ttisnumber(l) || !ttisnumber(r));
  if (ttisstring(l) && ttisstring(r))  // both are strings?
    return *tsvalue(l) <= *tsvalue(r);  // Use TString operator<=
  else
    return moonT_callorderTM(this, l, r, TMS::TM_LE);
}


// moonV_lessequal, moonV_equalobj removed - use VirtualMachine methods directly
