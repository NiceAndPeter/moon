/*
** Some generic functions over Lua objects
** See Copyright Notice in lua.h
*/

#define MOON_CORE

#include "mprefix.h"


#include <algorithm>
#include <cfloat>
#include <clocale>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "moon.h"

#include "mctype.h"
#include "mdebug.h"
#include "mdo.h"
#include "mmem.h"
#include "mobject.h"
#include "mstate.h"
#include "mstring.h"
#include "mvirtualmachine.h"
#include "mvm.h"


/*
** Computes ceil(log2(x)), which is the smallest integer n such that
** x <= (1 << n).
** Precondition: x > 0 (ceil(log2(0)) is undefined)
*/
lu_byte moonO_ceillog2 (unsigned int x) {
  moon_assert(x > 0);  // ceil(log2(0)) is undefined
  static const lu_byte log_2[256] = {  // log_2[i - 1] = ceil(log2(i))
    0,1,2,2,3,3,3,3,4,4,4,4,4,4,4,4,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
    6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8
  };
  int l = 0;
  x--;
  while (x >= 256) { l += 8; x >>= 8; }
  return cast_byte(l + log_2[x]);
}

/*
** Encodes 'p'% as a floating-point byte, represented as (eeeexxxx).
** The exponent is represented using excess-7. Mimicking IEEE 754, the
** representation normalizes the number when possible, assuming an extra
** 1 before the mantissa (xxxx) and adding one to the exponent (eeee)
** to signal that. So, the real value is (1xxxx) * 2^(eeee - 7 - 1) if
** eeee != 0, and (xxxx) * 2^-7 otherwise (subnormal numbers).
*/
lu_byte moonO_codeparam (unsigned int p) {
  if (p >= (static_cast<lu_mem>(0x1F) << (0xF - 7 - 1)) * 100u)  // overflow?
    return 0xFF;  // return maximum value
  else {
    p = (static_cast<l_uint32>(p) * 128 + 99) / 100;  // round up the division
    if (p < 0x10) {  // subnormal number?
      // exponent bits are already zero; nothing else to do
      return cast_byte(p);
    }
    else {  // p >= 0x10 implies ceil(log2(p + 1)) >= 5
      // preserve 5 bits in 'p'
      unsigned log = moonO_ceillog2(p + 1) - 5u;
      return cast_byte(((p >> log) - 0x10) | ((log + 1) << 4));
    }
  }
}


/*
** Computes 'p' times 'x', where 'p' is a floating-point byte. Roughly,
** we have to multiply 'x' by the mantissa and then shift accordingly to
** the exponent.  If the exponent is positive, both the multiplication
** and the shift increase 'x', so we have to care only about overflows.
** For negative exponents, however, multiplying before the shift keeps
** more significant bits, as long as the multiplication does not
** overflow, so we check which order is best.
*/
l_mem moonO_applyparam (lu_byte p, l_mem x) {
  int m = p & 0xF;  // mantissa
  int e = (p >> 4);  // exponent
  if (e > 0) {  // normalized?
    e--;  // correct exponent
    m += 0x10;  // correct mantissa; maximum value is 0x1F
  }
  e -= 7;  // correct excess-7
  if (e >= 0) {
    if (x < (MAX_LMEM / 0x1F) >> e)  // no overflow?
      return (x * m) << e;  // order doesn't matter here
    else  // real overflow
      return MAX_LMEM;
  }
  else {  // negative exponent
    e = -e;
    if (x < MAX_LMEM / 0x1F)  // multiplication cannot overflow?
      return (x * m) >> e;  // multiplying first gives more precision
    else if ((x >> e) <  MAX_LMEM / 0x1F)  // cannot overflow after shift?
      return (x >> e) * m;
    else  // real overflow
      return MAX_LMEM;
  }
}


static moon_Integer intarith (moon_State *L, int op, moon_Integer v1,
                                                   moon_Integer v2) {
  switch (op) {
    case MOON_OPADD: return intop(+, v1, v2);
    case MOON_OPSUB:return intop(-, v1, v2);
    case MOON_OPMUL:return intop(*, v1, v2);
    case MOON_OPMOD: return L->getVM().mod(v1, v2);
    case MOON_OPIDIV: return L->getVM().idiv(v1, v2);
    case MOON_OPBAND: return intop(&, v1, v2);
    case MOON_OPBOR: return intop(|, v1, v2);
    case MOON_OPBXOR: return intop(^, v1, v2);
    case MOON_OPSHL: return VirtualMachine::shiftl(v1, v2);
    case MOON_OPSHR: return VirtualMachine::shiftr(v1, v2);
    case MOON_OPUNM: return intop(-, 0, v1);
    case MOON_OPBNOT: return intop(^, l_castU2S(~l_castS2U(0)), v1);
    default: moon_assert(0); return 0;
  }
}


static moon_Number numarith (moon_State *L, int op, moon_Number v1,
                                                  moon_Number v2) {
  switch (op) {
    case MOON_OPADD: return mooni_numadd(L, v1, v2);
    case MOON_OPSUB: return mooni_numsub(L, v1, v2);
    case MOON_OPMUL: return mooni_nummul(L, v1, v2);
    case MOON_OPDIV: return mooni_numdiv(L, v1, v2);
    case MOON_OPPOW: return mooni_numpow(L, v1, v2);
    case MOON_OPIDIV: return mooni_numidiv(L, v1, v2);
    case MOON_OPUNM: return mooni_numunm(L, v1);
    case MOON_OPMOD: return L->getVM().modf(v1, v2);
    default: moon_assert(0); return 0;
  }
}


int moonO_rawarith (moon_State *L, int op, const TValue *p1, const TValue *p2,
                   TValue *res) {
  switch (op) {
    case MOON_OPBAND: case MOON_OPBOR: case MOON_OPBXOR:
    case MOON_OPSHL: case MOON_OPSHR:
    case MOON_OPBNOT: {  // operate only on integers
      moon_Integer i1; moon_Integer i2;
      if (tointegerns(p1, &i1) && tointegerns(p2, &i2)) {
        res->setInt(intarith(L, op, i1, i2));
        return 1;
      }
      else return 0;  // fail
    }
    case MOON_OPDIV: case MOON_OPPOW: {  // operate only on floats
      moon_Number n1; moon_Number n2;
      if (tonumberns(p1, n1) && tonumberns(p2, n2)) {
        res->setFloat(numarith(L, op, n1, n2));
        return 1;
      }
      else return 0;  // fail
    }
    default: {  // other operations
      moon_Number n1; moon_Number n2;
      if (ttisinteger(p1) && ttisinteger(p2)) {
        res->setInt(intarith(L, op, ivalue(p1), ivalue(p2)));
        return 1;
      }
      else if (tonumberns(p1, n1) && tonumberns(p2, n2)) {
        res->setFloat(numarith(L, op, n1, n2));
        return 1;
      }
      else return 0;  // fail
    }
  }
}


void moonO_arith (moon_State *L, int op, const TValue *p1, const TValue *p2,
                 StkId res) {
  if (!moonO_rawarith(L, op, p1, p2, s2v(res))) {
    // could not perform raw operation; try metamethod
    moonT_trybinTM(L, p1, p2, res, static_cast<TMS>((op - MOON_OPADD) + static_cast<int>(TMS::TM_ADD)));
  }
}


lu_byte moonO_hexavalue (int c) {
  moon_assert(lisxdigit(c));
  if (lisdigit(c)) return cast_byte(c - '0');
  else return cast_byte((ltolower(c) - 'a') + 10);
}


static bool isneg (const char **s) {
  if (**s == '-') { (*s)++; return true; }
  else if (**s == '+') (*s)++;
  return false;
}



/*
** {==================================================================
** Lua's implementation for 'moon_strx2number'
** ===================================================================
*/

#if !defined(moon_strx2number)

/* maximum number of significant digits to read (to avoid overflows
   even with single floats) */
inline constexpr int MAXSIGDIG = 30;

/*
** convert a hexadecimal numeric string to a number, following
** C99 specification for 'strtod'
*/
static moon_Number moon_strx2number (const char *s, char **endptr) {
  moon_Number r = l_mathop(0.0);  // result (accumulator)
  int sigdig = 0;  // number of significant digits
  int nosigdig = 0;  // number of non-significant digits
  int e = 0;  // exponent correction
  int hasdot = 0;  // true after seen a dot
  *endptr = cast_charp(s);  /* nothing is valid yet */
  while (lisspace(cast_uchar(*s))) s++;  // skip initial spaces
  int neg = isneg(&s);  // check sign
  if (!(*s == '0' && (*(s + 1) == 'x' || *(s + 1) == 'X')))  // check '0x'
    return l_mathop(0.0);  // invalid format (no '0x')
  int dot = moon_getlocaledecpoint();
  for (s += 2; ; s++) {  // skip '0x' and read numeral
    if (*s == dot) {
      if (hasdot) break;  // second dot? stop loop
      else hasdot = 1;
    }
    else if (lisxdigit(cast_uchar(*s))) {
      if (sigdig == 0 && *s == '0')  // non-significant digit (zero)?
        nosigdig++;
      else if (++sigdig <= MAXSIGDIG)  // can read it without overflow?
          r = (r * l_mathop(16.0)) + moonO_hexavalue(*s);
      else e++;  // too many digits; ignore, but still count for exponent
      if (hasdot) e--;  // decimal digit? correct exponent
    }
    else break;  // neither a dot nor a digit
  }
  if (nosigdig + sigdig == 0)  // no digits?
    return l_mathop(0.0);  // invalid format
  *endptr = cast_charp(s);  /* valid up to here */
  e *= 4;  // each digit multiplies/divides value by 2^4
  if (*s == 'p' || *s == 'P') {  // exponent part?
    int exp1 = 0;  // exponent value
    s++;  // skip 'p'
    int neg1 = isneg(&s);  // exponent sign
    if (!lisdigit(cast_uchar(*s)))
      return l_mathop(0.0);  // invalid; must have at least one digit
    while (lisdigit(cast_uchar(*s)))  // read exponent
      exp1 = exp1 * 10 + *(s++) - '0';
    if (neg1) exp1 = -exp1;
    e += exp1;
    *endptr = cast_charp(s);  /* valid up to here */
  }
  if (neg) r = -r;
  return l_mathop(ldexp)(r, e);
}

#endif
// }======================================================


// maximum length of a numeral to be converted to a number
#if !defined (L_MAXLENNUM)
#define L_MAXLENNUM	200
#endif

/*
** Convert string 's' to a Lua number (put in 'result'). Return nullptr on
** fail or the address of the ending '\0' on success. ('mode' == 'x')
** means a hexadecimal numeral.
*/
static const char *l_str2dloc (const char *s, moon_Number *result, int mode) {
  char *endptr;
  *result = (mode == 'x') ? moon_strx2number(s, &endptr)  /* try to convert */
                          : moon_str2number(s, &endptr);
  if (endptr == s) return nullptr;  // nothing recognized?
  while (lisspace(cast_uchar(*endptr))) endptr++;  // skip trailing spaces
  return (*endptr == '\0') ? endptr : nullptr;  // OK iff no trailing chars
}


/*
** Convert string 's' to a Lua number (put in 'result') handling the
** current locale.
** This function accepts both the current locale or a dot as the radix
** mark. If the conversion fails, it may mean number has a dot but
** locale accepts something else. In that case, the code copies 's'
** to a buffer (because 's' is read-only), changes the dot to the
** current locale radix mark, and tries to convert again.
** The variable 'mode' checks for special characters in the string:
** - 'n' means 'inf' or 'nan' (which should be rejected)
** - 'x' means a hexadecimal numeral
** - '.' just optimizes the search for the common case (no special chars)
*/
static const char *l_str2d (const char *s, moon_Number *result) {
  const char *endptr;
  const char *pmode = strpbrk(s, ".xXnN");  // look for special chars
  int mode = pmode ? ltolower(cast_uchar(*pmode)) : 0;
  if (mode == 'n')  // reject 'inf' and 'nan'
    return nullptr;
  endptr = l_str2dloc(s, result, mode);  // try to convert
  if (endptr == nullptr) {  // failed? may be a different locale
    char buff[L_MAXLENNUM + 1];
    const char *pdot = strchr(s, '.');
    if (pdot == nullptr || strlen(s) > L_MAXLENNUM)
      return nullptr;  // string too long or no dot; fail
    strcpy(buff, s);  // copy string to buffer
    buff[pdot - s] = moon_getlocaledecpoint();  // correct decimal point
    endptr = l_str2dloc(buff, result, mode);  // try again
    if (endptr != nullptr)
      endptr = s + (endptr - buff);  // make relative to 's'
  }
  return endptr;
}


inline constexpr moon_Unsigned MAXBY10 = static_cast<moon_Unsigned>(MOON_MAXINTEGER / 10);
inline constexpr int MAXLASTD = cast_int(MOON_MAXINTEGER % 10);

static const char *l_str2int (const char *s, moon_Integer *result) {
  moon_Unsigned a = 0;
  int empty = 1;
  while (lisspace(cast_uchar(*s))) s++;  // skip initial spaces
  int neg = isneg(&s);
  if (s[0] == '0' &&
      (s[1] == 'x' || s[1] == 'X')) {  // hex?
    s += 2;  // skip '0x'
    for (; lisxdigit(cast_uchar(*s)); s++) {
      a = a * 16 + moonO_hexavalue(*s);
      empty = 0;
    }
  }
  else {  // decimal
    for (; lisdigit(cast_uchar(*s)); s++) {
      int d = *s - '0';
      if (a >= MAXBY10 && (a > MAXBY10 || d > MAXLASTD + neg))  // overflow?
        return nullptr;  // do not accept it (as integer)
      a = a * 10 + cast_uint(d);
      empty = 0;
    }
  }
  while (lisspace(cast_uchar(*s))) s++;  // skip trailing spaces
  if (empty || *s != '\0') return nullptr;  // something wrong in the numeral
  else {
    *result = l_castU2S((neg) ? 0u - a : a);
    return s;
  }
}


size_t moonO_str2num (const char *s, TValue *o) {
  moon_Integer i; moon_Number n;
  const char *e;
  if ((e = l_str2int(s, &i)) != nullptr) {  // try as an integer
    o->setInt(i);
  }
  else if ((e = l_str2d(s, &n)) != nullptr) {  // else try as a float
    o->setFloat(n);
  }
  else
    return 0;  // conversion failed
  return ct_diff2sz(e - s) + 1;  // success; return string size
}


int moonO_utf8esc (char *buff, l_uint32 x) {
  int n = 1;  // number of bytes put in buffer (backwards)
  moon_assert(x <= 0x7FFFFFFFu);
  if (x < 0x80)  // ASCII?
    buff[UTF8BUFFSZ - 1] = cast_char(x);
  else {  // need continuation bytes
    unsigned int mfb = 0x3f;  // maximum that fits in first byte
    do {  // add continuation bytes
      buff[UTF8BUFFSZ - (n++)] = cast_char(0x80 | (x & 0x3f));
      x >>= 6;  // remove added bits
      mfb >>= 1;  // now there is one less bit available in first byte
    } while (x > mfb);  // still needs continuation byte?
    buff[UTF8BUFFSZ - n] = cast_char((~mfb << 1) | x);  // add first byte
  }
  return n;
}


/*
** The size of the buffer for the conversion of a number to a string
** 'MOON_N2SBUFFSZ' must be enough to accommodate both MOON_INTEGER_FMT
** and MOON_NUMBER_FMT.  For a long long int, this is 19 digits plus a
** sign and a final '\0', adding to 21. For a long double, it can go to
** a sign, the dot, an exponent letter, an exponent sign, 4 exponent
** digits, the final '\0', plus the significant digits, which are
** approximately the *_DIG attribute.
*/
#if MOON_N2SBUFFSZ < (20 + l_floatatt(DIG))
#error "invalid value for MOON_N2SBUFFSZ"
#endif


/*
** Convert a float to a string, adding it to a buffer. First try with
** a not too large number of digits, to avoid noise (for instance,
** 1.1 going to "1.1000000000000001"). If that lose precision, so
** that reading the result back gives a different number, then do the
** conversion again with extra precision. Moreover, if the numeral looks
** like an integer (without a decimal point or an exponent), add ".0" to
** its end.
*/
static int tostringbuffFloat (moon_Number n, char *buff) {
  // first conversion
  int len = l_sprintf(buff, MOON_N2SBUFFSZ, MOON_NUMBER_FMT,
                            (MOONI_UACNUMBER)n);
  moon_Number check = moon_str2number(buff, nullptr);  // read it back
  if (check != n) {  // not enough precision?
    // convert again with more precision
    len = l_sprintf(buff, MOON_N2SBUFFSZ, MOON_NUMBER_FMT_N,
                          (MOONI_UACNUMBER)n);
  }
  // looks like an integer?
  if (buff[strspn(buff, "-0123456789")] == '\0') {
    buff[len++] = moon_getlocaledecpoint();
    buff[len++] = '0';  // adds '.0' to result
  }
  return len;
}


/*
** Convert a number object to a string, adding it to a buffer.
*/
unsigned moonO_tostringbuff (const TValue *obj, char *buff) {
  moon_assert(ttisnumber(obj));
  int len = ttisinteger(obj)
              ? moon_integer2str(buff, MOON_N2SBUFFSZ, ivalue(obj))
              : tostringbuffFloat(fltvalue(obj), buff);
  moon_assert(len < MOON_N2SBUFFSZ);
  return cast_uint(len);
}


/*
** Convert a number object to a Lua string, replacing the value at 'obj'
*/
void moonO_tostring (moon_State *L, TValue *obj) {
  char buff[MOON_N2SBUFFSZ];
  unsigned len = moonO_tostringbuff(obj, buff);
  setsvalue(L, obj, TString::create(L, buff, len));
}




/*
** {==================================================================
** 'moonO_pushvfstring'
** ===================================================================
*/

/*
** Size for buffer space used by 'moonO_pushvfstring'. It should be
** (MOON_IDSIZE + MOON_N2SBUFFSZ) + a minimal space for basic messages,
** so that 'moonG_addinfo' can work directly on the static buffer.
*/
inline constexpr unsigned int BUFVFS = cast_uint(MOON_IDSIZE + MOON_N2SBUFFSZ + 95);

/*
** Buffer used by 'moonO_pushvfstring'. 'err' signals an error while
** building result (memory error [1] or buffer overflow [2]).
*/
class BuffFS {
public:
  moon_State *L;
  char *b;
  size_t buffsize;
  size_t blen;  // length of string in 'buff'
  int err;
  char space[BUFVFS];  // initial buffer

  // Constructor
  explicit BuffFS(moon_State *L_arg) noexcept
    : L(L_arg), b(space), buffsize(sizeof(space)), blen(0), err(0) {}
};


/*
** Push final result from 'moonO_pushvfstring'. This function may raise
** errors explicitly or through memory errors, so it must run protected.
*/
static void pushbuff (moon_State *L, void *ud) {
  BuffFS *buff = static_cast<BuffFS*>(ud);
  switch (buff->err) {
    case 1:  // memory error
      L->doThrow( MOON_ERRMEM);
      break;
    case 2:  // length overflow: Add "..." at the end of result
      if (buff->buffsize - buff->blen < 3)
        strcpy(buff->b + buff->blen - 3, "...");  // 'blen' must be > 3
      else {  // there is enough space left for the "..."
        strcpy(buff->b + buff->blen, "...");
        buff->blen += 3;
      }
      // FALLTHROUGH
    default: {  // no errors, but it can raise one creating the new string
      TString *tstring = TString::create(L, buff->b, buff->blen);
      setsvalue2s(L, L->getTop().p, tstring);
      L->getStackSubsystem().push();
    }
  }
}


static const char *clearbuff (BuffFS *buff) {
  moon_State *L = buff->L;
  const char *res;
  if (L->rawRunProtected( pushbuff, buff) != MOON_OK)  // errors?
    res = nullptr;  // error message is on the top of the stack
  else
    res = getStringContents(tsvalue(s2v(L->getTop().p - 1)));
  if (buff->b != buff->space)  // using dynamic buffer?
    moonM_freearray(L, buff->b, buff->buffsize);  // free it
  return res;
}


static void addstr2buff (BuffFS *buff, std::span<const char> str) {
  size_t slen = str.size();
  size_t left = buff->buffsize - buff->blen;  // space left in the buffer
  if (buff->err)  // do nothing else after an error
    return;
  if (slen > left) {  // new string doesn't fit into current buffer?
    if (slen > ((MAX_SIZE/2) - buff->blen)) {  // overflow?
      memcpy(buff->b + buff->blen, str.data(), left);  // copy what it can
      buff->blen = buff->buffsize;
      buff->err = 2;  // doesn't add anything else
      return;
    }
    else {
      size_t newsize = buff->buffsize + slen;  // limited to MAX_SIZE/2
      char *newb =
        (buff->b == buff->space)  // still using static space?
        ? moonM_reallocvector<char>(buff->L, nullptr, 0, newsize)
        : moonM_reallocvector<char>(buff->L, buff->b, buff->buffsize, newsize);
      if (newb == nullptr) {  // allocation error?
        buff->err = 1;  // signal a memory error
        return;
      }
      if (buff->b == buff->space)  // new buffer (not reallocated)?
        memcpy(newb, buff->b, buff->blen);  // copy previous content
      buff->b = newb;  // set new (larger) buffer...
      buff->buffsize = newsize;  // ...and its new size
    }
  }
  memcpy(buff->b + buff->blen, str.data(), slen);  // copy new content
  buff->blen += slen;
}


/*
** Add a numeral to the buffer.
*/
static void addnum2buff (BuffFS *buff, TValue *num) {
  char numbuff[MOON_N2SBUFFSZ];
  unsigned len = moonO_tostringbuff(num, numbuff);
  addstr2buff(buff, std::span(numbuff, len));
}


/*
** this function handles only '%d', '%c', '%f', '%p', '%s', and '%%'
   conventional formats, plus Lua-specific '%I' and '%U'
*/
const char *moonO_pushvfstring (moon_State *L, const char *fmt, va_list argp) {
  const char *e;  // points to next '%'
  BuffFS buff(L);  // holds last part of the result
  while ((e = strchr(fmt, '%')) != nullptr) {
    addstr2buff(&buff, std::span(fmt, ct_diff2sz(e - fmt)));  // add 'fmt' up to '%'
    switch (*(e + 1)) {  // conversion specifier
      case 's': {  // zero-terminated string
        const char *s = va_arg(argp, char *);
        if (s == nullptr) s = "(null)";
        addstr2buff(&buff, std::span(s, strlen(s)));
        break;
      }
      case 'c': {  // an 'int' as a character
        char c = cast_char(va_arg(argp, int));
        addstr2buff(&buff, std::span(&c, 1));
        break;
      }
      case 'd': {  // an 'int'
        TValue num;
        num.setInt(va_arg(argp, int));
        addnum2buff(&buff, &num);
        break;
      }
      case 'I': {  // a 'moon_Integer'
        TValue num;
        num.setInt(cast_Integer(va_arg(argp, l_uacInt)));
        addnum2buff(&buff, &num);
        break;
      }
      case 'f': {  // a 'moon_Number'
        TValue num;
        num.setFloat(cast_num(va_arg(argp, l_uacNumber)));
        addnum2buff(&buff, &num);
        break;
      }
      case 'p': {  // a pointer
        char bf[MOON_N2SBUFFSZ];  // enough space for '%p'
        void *p = va_arg(argp, void *);
        int len = moon_pointer2str(bf, MOON_N2SBUFFSZ, p);
        addstr2buff(&buff, std::span(bf, cast_uint(len)));
        break;
      }
      case 'U': {  // an 'unsigned long' as a UTF-8 sequence
        char bf[UTF8BUFFSZ];
        unsigned long arg = va_arg(argp, unsigned long);
        int len = moonO_utf8esc(bf, static_cast<l_uint32>(arg));
        addstr2buff(&buff, std::span(bf + UTF8BUFFSZ - len, cast_uint(len)));
        break;
      }
      case '%': {
        addstr2buff(&buff, std::span("%", 1));
        break;
      }
      default: {
        addstr2buff(&buff, std::span(e, 2));  // keep unknown format in the result
        break;
      }
    }
    fmt = e + 2;  // skip '%' and the specifier
  }
  addstr2buff(&buff, std::span(fmt, strlen(fmt)));  // rest of 'fmt'
  return clearbuff(&buff);  // empty buffer into a new string
}


const char *moonO_pushfstring (moon_State *L, const char *fmt, ...) {
  const char *msg;
  va_list argp;
  va_start(argp, fmt);
  msg = moonO_pushvfstring(L, fmt, argp);
  va_end(argp);
  if (msg == nullptr)  // error?
    L->doThrow( MOON_ERRMEM);
  return msg;
}

// }==================================================================


#define RETS	"..."
#define PRE	"[string \""
#define POS	"\"]"

inline void addstr(std::span<char>& dest, std::span<const char> src) noexcept {
	std::copy(src.begin(), src.end(), dest.begin());
	dest = dest.subspan(src.size());
}

// std::span-based chunk ID formatting
void moonO_chunkid (std::span<char> out, std::span<const char> source) {
  size_t bufflen = out.size();  // free space in buffer
  size_t srclen = source.size();
  if (!source.empty() && source[0] == '=') {  // 'literal' source
    if (srclen <= bufflen) {  // small enough?
      std::copy_n(source.data() + 1, srclen, out.data());
    }
    else {  // truncate it
      addstr(out, source.subspan(1, bufflen - 1));
      out[0] = '\0';
    }
  }
  else if (!source.empty() && source[0] == '@') {  // file name
    if (srclen <= bufflen) {  // small enough?
      std::copy_n(source.data() + 1, srclen, out.data());
    }
    else {  // add '...' before rest of name
      addstr(out, std::span(RETS, LL(RETS)));
      bufflen -= LL(RETS);
      std::copy_n(source.data() + 1 + srclen - bufflen, bufflen, out.data());
    }
  }
  else {  // string; format as [string "source"]
    const char *nl = strchr(source.data(), '\n');  // find first new line (if any)
    addstr(out, std::span(PRE, LL(PRE)));  // add prefix
    bufflen -= LL(PRE RETS POS) + 1;  // save space for prefix+suffix+'\0'
    if (srclen < bufflen && nl == nullptr) {  // small one-line source?
      addstr(out, source);  // keep it
    }
    else {
      if (nl != nullptr)
        srclen = ct_diff2sz(nl - source.data());  // stop at first newline
      if (srclen > bufflen) srclen = bufflen;
      addstr(out, source.subspan(0, srclen));
      addstr(out, std::span(RETS, LL(RETS)));
    }
    std::copy_n(POS, LL(POS) + 1, out.data());
  }
}

