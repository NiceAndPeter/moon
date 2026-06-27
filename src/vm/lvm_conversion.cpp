/*
** Type conversion operations for Lua VM
** See Copyright Notice in lua.h
*/

#define MOON_CORE

#include "lprefix.h"

#include <cmath>

#include "lua.h"

#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "lvm.h"
#include "lvirtualmachine.h"


/*
** Try to convert a value from string to a number value.
** If the value is not a string or is a string not representing
** a valid numeral (or if coercions from strings to numbers
** are disabled via macro 'cvt2num'), do not modify 'result'
** and return 0.
*/
static int l_strton (const TValue *obj, TValue *result) {
  moon_assert(obj != result);
  if (!cvt2num(obj))  // is object not a string?
    return 0;
  else {
    TString *st = tsvalue(obj);
    size_t stlen;
    const char *s = getStringWithLength(st, stlen);
    return (moonO_str2num(s, result) == stlen + 1);
  }
}


/*
** Try to convert a value to a float. The float case is already handled
** by the inline 'tonumber' function.
*/
static int tonumber_ (const TValue *obj, moon_Number *n) {
  TValue v;
  if (ttisinteger(obj)) {
    *n = cast_num(ivalue(obj));
    return 1;
  }
  else if (l_strton(obj, &v)) {  // string coercible to number?
    *n = nvalue(&v);  /* convert result of 'moonO_str2num' to a float */
    return 1;
  }
  else
    return 0;  // conversion failed
}


// moonV_flttointeger removed - use VirtualMachine::flttointeger() directly


/*
** try to convert a value to an integer, rounding according to 'mode',
** without string coercion.
** ("Fast track" handled by inline 'tointegerns' function.)
*/
static int tointegerns_ (const TValue *obj, moon_Integer *p, F2Imod mode) {
  if (ttisfloat(obj))
    return VirtualMachine::flttointeger(fltvalue(obj), p, mode);
  else if (ttisinteger(obj)) {
    *p = ivalue(obj);
    return 1;
  }
  else
    return 0;
}


/*
** try to convert a value to an integer.
*/
static int tointeger_ (const TValue *obj, moon_Integer *p, F2Imod mode) {
  TValue v;
  if (l_strton(obj, &v))  // does 'obj' point to a numerical string?
    obj = &v;  // change it to point to its corresponding number
  return tointegerns_(obj, p, mode);
}


/*
** TValue conversion methods
*/
int TValue::toNumber(moon_Number* n) const {
  return tonumber_(this, n);
}

int TValue::toInteger(moon_Integer* p, F2Imod mode) const {
  return tointeger_(this, p, mode);
}

int TValue::toIntegerNoString(moon_Integer* p, F2Imod mode) const {
  return tointegerns_(this, p, mode);
}
