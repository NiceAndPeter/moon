/*
** Lexical Analyzer
** See Copyright Notice in lua.h
*/

#define LUA_CORE

#include "lprefix.h"

#include <span>

#include <clocale>
#include <cstring>

#include "lua.h"

#include "lctype.h"
#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "llex.h"
#include "lobject.h"
#include "lparser.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "lzio.h"

// minimum size for string buffer
#if !defined(LUA_MINBUFFER)
#define LUA_MINBUFFER   32
#endif

// ORDER RESERVED
static const char *const luaX_tokens [] = {
    "and", "break", "do", "else", "elseif",
    "end", "false", "for", "function", "global", "goto", "if",
    "in", "local", "nil", "not", "or", "repeat",
    "return", "then", "true", "until", "while",
    "//", "..", "...", "==", ">=", "<=", "~=",
    "<<", ">>", "::", "<eof>",
    "<number>", "<integer>", "<name>", "<string>"
};

void LexState::save(int c) {
  Mbuffer *b = getBuffer();
  if (luaZ_bufflen(b) + 1 > luaZ_sizebuffer(b)) {
    size_t newsize = luaZ_sizebuffer(b);  /* get old size */;
    if (newsize >= (MAX_SIZE/3 * 2))  // larger than MAX_SIZE/1.5 ?
      lexError("lexical element too long", 0);
    newsize += (newsize >> 1);  // new size is 1.5 times the old one
    luaZ_resizebuffer(getLuaState(), b, newsize);
  }
  size_t len = luaZ_bufflen(b);
  b->buffer[len] = cast_char(c);
  b->n++;
}

void LexState::saveAndNext() {
  save(getCurrentChar());
  next();
}

void luaX_init (lua_State *L) 
{
  TString *envName = luaS_newliteral(L, LUA_ENV);
  envName->fix(L);
  lu_byte tokenIndex = 0;
  for (auto const& token : std::span(luaX_tokens)) 
  {
    auto ts = TString::create(L, token);
    ts->fix(L);  
    ts->setExtra(++tokenIndex);  
  }
}

const char *LexState::tokenToStr(int token) {
  if (token < FIRST_RESERVED) {  // single-byte symbols?
    if (lisprint(token))
      return luaO_pushfstring(getLuaState(), "'%c'", token);
    else  // control character
      return luaO_pushfstring(getLuaState(), "'<\\%d>'", token);
  }
  else {
    const char *s = luaX_tokens[token - FIRST_RESERVED];
    if (token < static_cast<int>(RESERVED::TK_EOS))  // fixed format (symbols and reserved words)?
      return luaO_pushfstring(getLuaState(), "'%s'", s);
    else  // names, strings, and numerals
      return s;
  }
}

const char* LexState::txtToken(int token) {
  switch (token) {
    case static_cast<int>(RESERVED::TK_NAME): case static_cast<int>(RESERVED::TK_STRING):
    case static_cast<int>(RESERVED::TK_FLT): case static_cast<int>(RESERVED::TK_INT):
      save('\0');
      return luaO_pushfstring(getLuaState(), "'%s'", luaZ_buffer(getBuffer()));
    default:
      return tokenToStr(token);
  }
}

l_noret LexState::lexError(const char *msg, int token) {
  msg = luaG_addinfo(getLuaState(), msg, getSource(), getLineNumber());
  if (token)
    luaO_pushfstring(getLuaState(), "%s near %s", msg, txtToken(token));
  getLuaState()->doThrow(LUA_ERRSYNTAX);
}

l_noret LexState::syntaxError(const char *msg) {
  lexError(msg, getCurrentToken().token);
}

/*
** Anchors a string in scanner's table so that it will not be collected
** until the end of the compilation; by that time it should be anchored
** somewhere. It also internalizes long strings, ensuring there is only
** one copy of each unique string.
*/
TString* LexState::anchorStr(TString *ts) {
  lua_State *luaState = getLuaState();
  TValue oldts;
  LuaT tag = getTable()->getStr(ts, &oldts);
  if (!tagisempty(tag))  // string already present?
    return tsvalue(&oldts);  // use stored value
  else {  // create a new entry
    TValue *stv = s2v(luaState->getTop().p);  // reserve stack space for string
    luaState->getStackSubsystem().push();
    setsvalue(luaState, stv, ts);  // push (anchor) the string on the stack
    getTable()->set(luaState, stv, stv);  // t[string] = string
    // table is not a metatable, so it does not need to invalidate cache
    luaC_checkGC(luaState);
    luaState->getStackSubsystem().pop();  // remove string from stack
    return ts;
  }
}

/*
** Creates a new string and anchors it in scanner's table.
*/
TString *LexState::newString(const char *str, size_t l) {
  return anchorStr(TString::create(getLuaState(), str, l));
}

/*
** increment line number and skips newline sequence (any of
** \n, \r, \n\r, or \r\n)
*/
void LexState::incLineNumber() {
  int old = getCurrentChar();
  lua_assert(currIsNewline());
  next();  // skip '\n' or '\r'
  if (currIsNewline() && getCurrentChar() != old)
    next();  // skip '\n\r' or '\r\n'
  if (++getLineNumberRef() >= std::numeric_limits<int>::max())
    lexError("chunk has too many lines", 0);
}

void LexState::setInput(lua_State *state, ZIO *zio, TString *src, int firstchar) {
  getCurrentTokenRef().token = 0;
  setLuaState(state);
  setCurrent(firstchar);
  getLookaheadRef().token = static_cast<int>(RESERVED::TK_EOS);  // no look-ahead token
  setZIO(zio);
  setLineNumber(1);
  setLastLine(1);
  setSource(src);
  /* all three strings here ("_ENV", "break", "global") were fixed,
     so they cannot be collected */
  setEnvName(luaS_newliteral(state, LUA_ENV));  // get env string
  setBreakName(luaS_newliteral(state, "break"));  // get "break" string
#if defined(LUA_COMPAT_GLOBAL)
  // compatibility mode: "global" is not a reserved word
  setGlobalName(luaS_newliteral(state, "global"));  // get "global" string
  getGlobalName()->setExtra(0);  // mark it as not reserved
#endif
  luaZ_resizebuffer(getLuaState(), getBuffer(), LUA_MINBUFFER);  // initialize buffer
}

/*
** =======================================================
** LEXICAL ANALYZER
** =======================================================
*/

int LexState::checkNext1(int c) {
  if (getCurrentChar() == c) {
    next();
    return 1;
  }
  else return 0;
}


/*
** Check whether current char is in set 'set' (with two chars) and
** saves it
*/
int LexState::checkNext2(const char *set) {
  lua_assert(set[2] == '\0');
  if (getCurrentChar() == set[0] || getCurrentChar() == set[1]) {
    saveAndNext();
    return 1;
  }
  else return 0;
}


// LUA_NUMBER
/*
** This function is quite liberal in what it accepts, as 'luaO_str2num'
** will reject ill-formed numerals. Roughly, it accepts the following
** pattern:
**
**   %d(%x|%.|([Ee][+-]?))* | 0[Xx](%x|%.|([Pp][+-]?))*
**
** The only tricky part is to accept [+-] only after a valid exponent
** mark, to avoid reading '3-4' or '0xe+1' as a single number.
**
** The caller might have already read an initial dot.
*/
int LexState::readNumeral(SemInfo *seminfo) {
  TValue obj;
  const char *expo = "Ee";
  int first = getCurrentChar();
  lua_assert(lisdigit(getCurrentChar()));
  saveAndNext();
  if (first == '0' && checkNext2("xX"))  // hexadecimal?
    expo = "Pp";
  for (;;) {
    if (checkNext2(expo))  // exponent mark?
      checkNext2("-+");  // optional exponent sign
    else if (lisxdigit(getCurrentChar()) || getCurrentChar() == '.')  // '%x|%.'
      saveAndNext();
    else break;
  }
  if (lislalpha(getCurrentChar()))  // is numeral touching a letter?
    saveAndNext();  // force an error
  save('\0');
  if (luaO_str2num(luaZ_buffer(getBuffer()), &obj) == 0)  // format error?
    lexError("malformed number", static_cast<int>(RESERVED::TK_FLT));
  if (ttisinteger(&obj)) {
    seminfo->i = ivalue(&obj);
    return static_cast<int>(RESERVED::TK_INT);
  }
  else {
    lua_assert(ttisfloat(&obj));
    seminfo->r = fltvalue(&obj);
    return static_cast<int>(RESERVED::TK_FLT);
  }
}


/*
** read a sequence '[=*[' or ']=*]', leaving the last bracket. If
** sequence is well formed, return its number of '='s + 2; otherwise,
** return 1 if it is a single bracket (no '='s and no 2nd bracket);
** otherwise (an unfinished '[==...') return 0.
*/
size_t LexState::skipSep() {
  size_t count = 0;
  int s = getCurrentChar();
  lua_assert(s == '[' || s == ']');
  saveAndNext();
  while (getCurrentChar() == '=') {
    saveAndNext();
    count++;
  }
  return (getCurrentChar() == s) ? count + 2
         : (count == 0) ? 1
         : 0;
}


void LexState::readLongString(SemInfo *seminfo, size_t sep) {
  int line = getLineNumber();  // initial line (for error message)
  saveAndNext();  // skip 2nd '['
  if (currIsNewline())  // string starts with a newline?
    incLineNumber();  // skip it
  for (;;) {
    switch (getCurrentChar()) {
      case EOZ: {  // error
        const char *what = (seminfo ? "string" : "comment");
        const char *msg = luaO_pushfstring(getLuaState(),
                     "unfinished long %s (starting at line %d)", what, line);
        lexError(msg, static_cast<int>(RESERVED::TK_EOS));
        break;  // to avoid warnings
      }
      case ']': {
        if (skipSep() == sep) {
          saveAndNext();  // skip 2nd ']'
          goto endloop;
        }
        break;
      }
      case '\n': case '\r': {
        save('\n');
        incLineNumber();
        if (!seminfo) luaZ_resetbuffer(getBuffer());  // avoid wasting space
        break;
      }
      default: {
        if (seminfo) saveAndNext();
        else next();
      }
    }
  } endloop:
  if (seminfo)
    seminfo->ts = newString(luaZ_buffer(getBuffer()) + sep,
                                luaZ_bufflen(getBuffer()) - 2 * sep);
}


void LexState::escCheck(int c, const char *msg) {
  if (!c) {
    if (getCurrentChar() != EOZ)
      saveAndNext();  // add current to buffer for error message
    lexError(msg, static_cast<int>(RESERVED::TK_STRING));
  }
}


int LexState::getHexa() {
  saveAndNext();
  escCheck(lisxdigit(getCurrentChar()), "hexadecimal digit expected");
  return luaO_hexavalue(getCurrentChar());
}


int LexState::readHexaEsc() {
  int r = getHexa();
  r = (r << 4) + getHexa();
  luaZ_buffremove(getBuffer(), 2);  // remove saved chars from buffer
  return r;
}


/*
** When reading a UTF-8 escape sequence, save everything to the buffer
** for error reporting in case of errors; 'i' counts the number of
** saved characters, so that they can be removed if case of success.
*/
l_uint32 LexState::readUtf8Esc() {
  l_uint32 r;
  int i = 4;  // number of chars to be removed: start with #"\u{X"
  saveAndNext();  // skip 'u'
  escCheck(getCurrentChar() == '{', "missing '{'");
  r = cast_uint(getHexa());  // must have at least one digit
  while (cast_void(saveAndNext()), lisxdigit(getCurrentChar())) {
    i++;
    escCheck(r <= (0x7FFFFFFFu >> 4), "UTF-8 value too large");
    r = (r << 4) + luaO_hexavalue(getCurrentChar());
  }
  escCheck(getCurrentChar() == '}', "missing '}'");
  next();  // skip '}'
  luaZ_buffremove(getBuffer(), i);  // remove saved chars from buffer
  return r;
}


void LexState::utf8Esc() {
  char utf8buffer[UTF8BUFFSZ];
  int n = luaO_utf8esc(utf8buffer, readUtf8Esc());
  for (; n > 0; n--)  // add 'utf8buffer' to string
    save(utf8buffer[UTF8BUFFSZ - n]);
}


int LexState::readDecEsc() {
  int i;
  int r = 0;  // result accumulator
  for (i = 0; i < 3 && lisdigit(getCurrentChar()); i++) {  // read up to 3 digits
    r = 10*r + getCurrentChar() - '0';
    saveAndNext();
  }
  escCheck(r <= UCHAR_MAX, "decimal escape too large");
  luaZ_buffremove(getBuffer(), i);  // remove read digits from buffer
  return r;
}


void LexState::readString(int del, SemInfo *seminfo) {
  saveAndNext();  // keep delimiter (for error messages)
  while (getCurrentChar() != del) {
    switch (getCurrentChar()) {
      case EOZ:
        lexError("unfinished string", static_cast<int>(RESERVED::TK_EOS));
        break;  // to avoid warnings
      case '\n':
      case '\r':
        lexError("unfinished string", static_cast<int>(RESERVED::TK_STRING));
        break;  // to avoid warnings
      case '\\': {  // escape sequences
        int c;  // final character to be saved
        saveAndNext();  // keep '\\' for error messages
        switch (getCurrentChar()) {
          case 'a': c = '\a'; goto read_save;
          case 'b': c = '\b'; goto read_save;
          case 'f': c = '\f'; goto read_save;
          case 'n': c = '\n'; goto read_save;
          case 'r': c = '\r'; goto read_save;
          case 't': c = '\t'; goto read_save;
          case 'v': c = '\v'; goto read_save;
          case 'x': c = readHexaEsc(); goto read_save;
          case 'u': utf8Esc();  goto no_save;
          case '\n': case '\r':
            incLineNumber(); c = '\n'; goto only_save;
          case '\\': case '\"': case '\'':
            c = getCurrentChar(); goto read_save;
          case EOZ: goto no_save;  // will raise an error next loop
          case 'z': {  // zap following span of spaces
            luaZ_buffremove(getBuffer(), 1);  // remove '\\'
            next();  // skip the 'z'
            while (lisspace(getCurrentChar())) {
              if (currIsNewline()) incLineNumber();
              else next();
            }
            goto no_save;
          }
          default: {
            escCheck(lisdigit(getCurrentChar()), "invalid escape sequence");
            c = readDecEsc();  // digital escape '\ddd'
            goto only_save;
          }
        }
       read_save:
         next();
         // go through
       only_save:
         luaZ_buffremove(getBuffer(), 1);  // remove '\\'
         save(c);
         // go through
       no_save: break;
      }
      default:
        saveAndNext();
    }
  }
  saveAndNext();  // skip delimiter
  seminfo->ts = newString(luaZ_buffer(getBuffer()) + 1,
                              luaZ_bufflen(getBuffer()) - 2);
}


int LexState::lex(SemInfo *seminfo) {
  luaZ_resetbuffer(getBuffer());
  for (;;) {
    switch (getCurrentChar()) {
      case '\n': case '\r': {  // line breaks
        incLineNumber();
        break;
      }
      case ' ': case '\f': case '\t': case '\v': {  // spaces
        next();
        break;
      }
      case '-': {  // '-' or '--' (comment)
        next();
        if (getCurrentChar() != '-') return '-';
        // else is a comment
        next();
        if (getCurrentChar() == '[') {  // long comment?
          size_t sep = skipSep();
          luaZ_resetbuffer(getBuffer());  // 'skip_sep' may dirty the buffer
          if (sep >= 2) {
            readLongString(nullptr, sep);  // skip long comment
            luaZ_resetbuffer(getBuffer());  // previous call may dirty the buff.
            break;
          }
        }
        // else short comment
        while (!currIsNewline() && getCurrentChar() != EOZ)
          next();  // skip until end of line (or end of file)
        break;
      }
      case '[': {  // long string or simply '['
        size_t sep = skipSep();
        if (sep >= 2) {
          readLongString(seminfo, sep);
          return static_cast<int>(RESERVED::TK_STRING);
        }
        else if (sep == 0)  // '[=...' missing second bracket?
          lexError("invalid long string delimiter", static_cast<int>(RESERVED::TK_STRING));
        return '[';
      }
      case '=': {
        next();
        if (checkNext1('=')) return static_cast<int>(RESERVED::TK_EQ);  // '=='
        else return '=';
      }
      case '<': {
        next();
        if (checkNext1('=')) return static_cast<int>(RESERVED::TK_LE);  // '<='
        else if (checkNext1('<')) return static_cast<int>(RESERVED::TK_SHL);  // '<<'
        else return '<';
      }
      case '>': {
        next();
        if (checkNext1('=')) return static_cast<int>(RESERVED::TK_GE);  // '>='
        else if (checkNext1('>')) return static_cast<int>(RESERVED::TK_SHR);  // '>>'
        else return '>';
      }
      case '/': {
        next();
        if (checkNext1('/')) return static_cast<int>(RESERVED::TK_IDIV);  // '//'
        else return '/';
      }
      case '~': {
        next();
        if (checkNext1('=')) return static_cast<int>(RESERVED::TK_NE);  // '~='
        else return '~';
      }
      case ':': {
        next();
        if (checkNext1(':')) return static_cast<int>(RESERVED::TK_DBCOLON);  // '::'
        else return ':';
      }
      case '"': case '\'': {  /* short literal strings */
        readString(getCurrentChar(), seminfo);
        return static_cast<int>(RESERVED::TK_STRING);
      }
      case '.': {  // '.', '..', '...', or number
        saveAndNext();
        if (checkNext1('.')) {
          if (checkNext1('.'))
            return static_cast<int>(RESERVED::TK_DOTS);  // '...'
          else return static_cast<int>(RESERVED::TK_CONCAT);  // '..'
        }
        else if (!lisdigit(getCurrentChar())) return '.';
        else return readNumeral(seminfo);
      }
      case '0': case '1': case '2': case '3': case '4':
      case '5': case '6': case '7': case '8': case '9': {
        return readNumeral(seminfo);
      }
      case EOZ: {
        return static_cast<int>(RESERVED::TK_EOS);
      }
      default: {
        if (lislalpha(getCurrentChar())) {  // identifier or reserved word?
          TString *ts;
          do {
            saveAndNext();
          } while (lislalnum(getCurrentChar()));
          // find or create string
          ts = TString::create(getLuaState(), luaZ_buffer(getBuffer()),
                                   luaZ_bufflen(getBuffer()));
          if (isreserved(ts))  // reserved word?
            return ts->getExtra() - 1 + FIRST_RESERVED;
          else {
            seminfo->ts = anchorStr(ts);
            return static_cast<int>(RESERVED::TK_NAME);
          }
        }
        else {  // single-char tokens ('+', '*', '%', '{', '}', ...)
          int c = getCurrentChar();
          next();
          return c;
        }
      }
    }
  }
}


void LexState::nextToken() {
  setLastLine(getLineNumber());
  if (getLookahead().token != static_cast<int>(RESERVED::TK_EOS)) {  // is there a look-ahead token?
    getCurrentTokenRef() = getLookahead();  // use this one
    getLookaheadRef().token = static_cast<int>(RESERVED::TK_EOS);  // and discharge it
  }
  else
    getCurrentTokenRef().token = lex(&getCurrentTokenRef().seminfo);  // read next token
}


int LexState::lookaheadToken() {
  lua_assert(getLookahead().token == static_cast<int>(RESERVED::TK_EOS));
  getLookaheadRef().token = lex(&getLookaheadRef().seminfo);
  return getLookahead().token;
}

