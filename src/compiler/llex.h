/*
** Lexical Analyzer
** See Copyright Notice in lua.h
*/

#ifndef llex_h
#define llex_h

#include <climits>

#include "lobject.h"
#include "lzio.h"

// Forward declarations
class ExpDesc;
struct Labeldesc;
class Labellist;
struct ConsControl;
struct LHS_assign;
struct BlockCnt;

/*
** grep "ORDER OPR" if you change these enums  (ORDER OP)
*/
enum class BinOpr {
  // arithmetic operators
  OPR_ADD, OPR_SUB, OPR_MUL, OPR_MOD, OPR_POW,
  OPR_DIV, OPR_IDIV,
  // bitwise operators
  OPR_BAND, OPR_BOR, OPR_BXOR,
  OPR_SHL, OPR_SHR,
  // string operator
  OPR_CONCAT,
  // comparison operators
  OPR_EQ, OPR_LT, OPR_LE,
  OPR_NE, OPR_GT, OPR_GE,
  // logical operators
  OPR_AND, OPR_OR,
  OPR_NOBINOPR
};

enum class UnOpr { OPR_MINUS, OPR_BNOT, OPR_NOT, OPR_LEN, OPR_NOUNOPR };

/*
** Single-char tokens (terminal symbols) are represented by their own
** numeric code. Other tokens start at the following value.
*/
inline constexpr int FIRST_RESERVED = UCHAR_MAX + 1;


#if !defined(MOON_ENV)
#define MOON_ENV		"_ENV"
#endif


/*
* WARNING: if you change the order of this enumeration,
* grep "ORDER RESERVED"
*/
enum class RESERVED {
  // terminal symbols denoted by reserved words
  TK_AND = FIRST_RESERVED, TK_BREAK,
  TK_DO, TK_ELSE, TK_ELSEIF, TK_END, TK_FALSE, TK_FOR, TK_FUNCTION,
  TK_GLOBAL, TK_GOTO, TK_IF, TK_IN, TK_LOCAL, TK_NIL, TK_NOT, TK_OR,
  TK_REPEAT, TK_RETURN, TK_THEN, TK_TRUE, TK_UNTIL, TK_WHILE,
  // other terminal symbols
  TK_IDIV, TK_CONCAT, TK_DOTS, TK_EQ, TK_GE, TK_LE, TK_NE,
  TK_SHL, TK_SHR,
  TK_DBCOLON, TK_EOS,
  TK_FLT, TK_INT, TK_NAME, TK_STRING
};

// number of reserved words
inline constexpr int NUM_RESERVED = (cast_int(static_cast<int>(RESERVED::TK_WHILE)-FIRST_RESERVED + 1));


typedef union {
  moon_Number r;
  moon_Integer i;
  TString *tstring;
} SemInfo;  // semantics information


typedef struct Token {
  int token;
  SemInfo seminfo;
} Token;


// Subsystem for input character stream handling
class InputScanner {
private:
  int current;  // current character (charint)
  int linenumber;  // input line counter
  int lastline;  // line of last token 'consumed'
  ZIO *z;  // input stream
  TString *source;  // current source name

public:
  // Accessors
  int getCurrent() const noexcept { return current; }
  int getLineNumber() const noexcept { return linenumber; }
  int getLastLine() const noexcept { return lastline; }
  ZIO* getZIO() const noexcept { return z; }
  TString* getSource() const noexcept { return source; }

  void setCurrent(int c) noexcept { current = c; }
  void setLineNumber(int line) noexcept { linenumber = line; }
  void setLastLine(int line) noexcept { lastline = line; }
  void setZIO(ZIO* zio) noexcept { z = zio; }
  void setSource(TString* src) noexcept { source = src; }

  int& getLineNumberRef() noexcept { return linenumber; }

  // Operations
  void next() noexcept { current = zgetc(z); }
  bool currIsNewline() const noexcept { return current == '\n' || current == '\r'; }
};

// Subsystem for token state management
class TokenState {
private:
  Token current;  // current token
  Token lookahead;  // look ahead token

public:
  // Accessors
  const Token& getCurrent() const noexcept { return current; }
  Token& getCurrentRef() noexcept { return current; }
  const Token& getLookahead() const noexcept { return lookahead; }
  Token& getLookaheadRef() noexcept { return lookahead; }
};

// Subsystem for string interning and buffer management
class StringInterner {
private:
  Mbuffer *buff;  // buffer for tokens
  Table *h;  // to avoid collection/reuse strings
  TString *environmentName;  // environment variable name
  TString *breakLabelName;  // "break" name (used as a label)
  TString *globalKeywordName;  // "global" name (when not a reserved word)

public:
  // Accessors
  Mbuffer* getBuffer() const noexcept { return buff; }
  Table* getTable() const noexcept { return h; }
  TString* getEnvName() const noexcept { return environmentName; }
  TString* getBreakName() const noexcept { return breakLabelName; }
  TString* getGlobalName() const noexcept { return globalKeywordName; }

  void setBuffer(Mbuffer* b) noexcept { buff = b; }
  void setTable(Table* table) noexcept { h = table; }
  void setEnvName(TString* env) noexcept { environmentName = env; }
  void setBreakName(TString* brk) noexcept { breakLabelName = brk; }
  void setGlobalName(TString* gbl) noexcept { globalKeywordName = gbl; }
};

/* Lexical state - focused on tokenization only
** Parser-specific fields and methods moved to Parser class */
class LexState {
public:
  // Direct subsystem access for performance
  // These subsystems are frequently accessed in hot paths, so we make them
  // public to eliminate indirection overhead from delegating accessor methods
  InputScanner scanner;
  TokenState tokens;
  StringInterner strings;

private:
  // Shared state (lexer + parser)
  struct moon_State *L;
  class Dyndata *dyd;  // dynamic structures shared by lexer and parser

public:
  // Accessors delegating to subsystems

  // InputScanner accessors
  int getCurrentChar() const noexcept { return scanner.getCurrent(); }
  int getLineNumber() const noexcept { return scanner.getLineNumber(); }
  int getLastLine() const noexcept { return scanner.getLastLine(); }
  ZIO* getZIO() const noexcept { return scanner.getZIO(); }
  TString* getSource() const noexcept { return scanner.getSource(); }

  void setCurrent(int c) noexcept { scanner.setCurrent(c); }
  void setLineNumber(int line) noexcept { scanner.setLineNumber(line); }
  void setLastLine(int line) noexcept { scanner.setLastLine(line); }
  void setZIO(ZIO* zio) noexcept { scanner.setZIO(zio); }
  void setSource(TString* src) noexcept { scanner.setSource(src); }

  int& getLineNumberRef() noexcept { return scanner.getLineNumberRef(); }
  void next() noexcept { scanner.next(); }
  bool currIsNewline() const noexcept { return scanner.currIsNewline(); }

  // TokenState accessors
  const Token& getCurrentToken() const noexcept { return tokens.getCurrent(); }
  Token& getCurrentTokenRef() noexcept { return tokens.getCurrentRef(); }
  const Token& getLookahead() const noexcept { return tokens.getLookahead(); }
  Token& getLookaheadRef() noexcept { return tokens.getLookaheadRef(); }

  // Hot-path token accessors - frequently used in parser
  // Direct access to token value without going through full getCurrentToken() call
  inline int getToken() const noexcept { return tokens.getCurrent().token; }
  inline void setToken(int tok) noexcept { tokens.getCurrentRef().token = tok; }
  inline SemInfo& getSemInfo() noexcept { return tokens.getCurrentRef().seminfo; }
  inline const SemInfo& getSemInfo() const noexcept { return tokens.getCurrent().seminfo; }

  // StringInterner accessors
  Mbuffer* getBuffer() const noexcept { return strings.getBuffer(); }
  Table* getTable() const noexcept { return strings.getTable(); }
  TString* getEnvName() const noexcept { return strings.getEnvName(); }
  TString* getBreakName() const noexcept { return strings.getBreakName(); }
  TString* getGlobalName() const noexcept { return strings.getGlobalName(); }

  void setBuffer(Mbuffer* b) noexcept { strings.setBuffer(b); }
  void setTable(Table* table) noexcept { strings.setTable(table); }
  void setEnvName(TString* env) noexcept { strings.setEnvName(env); }
  void setBreakName(TString* brk) noexcept { strings.setBreakName(brk); }
  void setGlobalName(TString* gbl) noexcept { strings.setGlobalName(gbl); }

  // Shared state accessors
  struct moon_State* getLuaState() const noexcept { return L; }
  void setLuaState(struct moon_State* state) noexcept { L = state; }
  class Dyndata* getDyndata() const noexcept { return dyd; }
  void setDyndata(class Dyndata* d) noexcept { dyd = d; }

  // Lexer method declarations (implemented in llex.cpp)
  void saveAndNext();
  void setInput(moon_State *state, ZIO *zio, TString *src, int firstchar);
  TString *newString(const char *str, size_t l);
  void nextToken();
  int lookaheadToken();
  l_noret syntaxError(const char *s);
  l_noret semerror(const char *fmt, ...);
  const char *tokenToStr(int token);

  // Parser utility methods (used by FuncState, kept in LexState for access)
  Labeldesc *findlabel(TString *name, int ilb);
  int newlabelentry(class FuncState *funcState, Labellist *l, TString *name, int line, int pc);
  void closegoto(class FuncState *funcState, int g, Labeldesc *label, int bup);
  l_noret jumpscopeerror(class FuncState *funcState, Labeldesc *gt);
  void createlabel(class FuncState *funcState, TString *name, int line, int last);
  l_noret undefgoto(class FuncState *funcState, Labeldesc *gt);

private:
  // Lexer helper methods (converted from static functions)
  // Batch 1: Trivial functions
  void save(int c);
  void incLineNumber();
  int checkNext1(int c);
  int checkNext2(const char *set);
  void escCheck(int c, const char *msg);

  // Batch 2: Simple functions
  int getHexa();
  int readHexaEsc();
  int readDecEsc();
  const char* txtToken(int token);
  size_t skipSep();
  TString* anchorStr(TString *tstring);

  // Batch 3: Medium functions
  l_noret lexError(const char *msg, int token);
  l_uint32 readUtf8Esc();
  void utf8Esc();
  int readNumeral(SemInfo *seminfo);

  // Batch 4: Complex functions
  void readLongString(SemInfo *seminfo, size_t sep);
  void readString(int del, SemInfo *seminfo);
  int readName(SemInfo *seminfo);  // identifier or reserved word
  int lex(SemInfo *seminfo);
};


MOONI_FUNC void moonX_init (moon_State *L);


#endif
