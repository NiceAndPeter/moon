/*
** Lua stand-alone interpreter
** See Copyright Notice in lua.h
*/

#include "mprefix.h"


#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <csignal>

#include "moon.h"

#include "mauxlib.h"
#include "moonlib.h"
#include "mlimits.h"


#if !defined(MOON_PROGNAME)
#define MOON_PROGNAME		"lua"
#endif

#if !defined(MOON_INIT_VAR)
#define MOON_INIT_VAR		"MOON_INIT"
#endif

#define MOON_INITVARVERSION	MOON_INIT_VAR MOON_VERSUFFIX


static moon_State *globalL = nullptr;

static const char *progname = MOON_PROGNAME;


#if defined(MOON_USE_POSIX)  // {

/*
** Use 'sigaction' when available.
*/
static void setsignal (int sig, void (*handler)(int)) {
  struct sigaction sa;
  sa.sa_handler = handler;
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);  // do not mask any signal
  sigaction(sig, &sa, nullptr);
}

#else  // }{

#define setsignal            signal

#endif  // }


/*
** Hook set by signal function to stop the interpreter.
*/
static void lstop (moon_State *L, moon_Debug *ar) {
  (void)ar;  // unused arg.
  moon_sethook(L, nullptr, 0, 0);  // reset hook
  moonL_error(L, "interrupted!");
}


/*
** Function to be called at a C signal. Because a C signal cannot
** just change a Lua state (as there is no proper synchronization),
** this function only sets a hook that, when called, will stop the
** interpreter.
*/
static void laction (int i) {
  int flag = MOON_MASKCALL | MOON_MASKRET | MOON_MASKLINE | MOON_MASKCOUNT;
  setsignal(i, SIG_DFL);  // if another SIGINT happens, terminate process
  moon_sethook(globalL, lstop, flag, 1);
}


static void print_usage (const char *badoption) {
  moon_writestringerror("%s: ", progname);
  if (badoption[1] == 'e' || badoption[1] == 'l')
    moon_writestringerror("'%s' needs argument\n", badoption);
  else
    moon_writestringerror("unrecognized option '%s'\n", badoption);
  moon_writestringerror(
  "usage: %s [options] [script [args]]\n"
  "Available options are:\n"
  "  -e stat   execute string 'stat'\n"
  "  -i        enter interactive mode after executing 'script'\n"
  "  -l mod    require library 'mod' into global 'mod'\n"
  "  -l g=mod  require library 'mod' into global 'g'\n"
  "  -v        show version information\n"
  "  -E        ignore environment variables\n"
  "  -W        turn warnings on\n"
  "  --        stop handling options\n"
  "  -         stop handling options and execute stdin\n"
  ,
  progname);
}


/*
** Prints an error message, adding the program name in front of it
** (if present)
*/
static void l_message (const char *pname, const char *msg) {
  if (pname) moon_writestringerror("%s: ", pname);
  moon_writestringerror("%s\n", msg);
}


/*
** Check whether 'status' is not OK and, if so, prints the error
** message on the top of the stack.
*/
static int report (moon_State *L, int status) {
  if (status != MOON_OK) {
    const char *msg = moon_tostring(L, -1);
    if (msg == nullptr)
      msg = "(error message not a string)";
    l_message(progname, msg);
    moon_pop(L, 1);  // remove message
  }
  return status;
}


/*
** Message handler used to run all chunks
*/
static int msghandler (moon_State *L) {
  const char *msg = moon_tostring(L, 1);
  if (msg == nullptr) {  // is error object not a string?
    if (moonL_callmeta(L, 1, "__tostring") &&  // does it have a metamethod
        moon_type(L, -1) == MOON_TSTRING)  // that produces a string?
      return 1;  // that is the message
    else
      msg = moon_pushfstring(L, "(error object is a %s value)",
                               moonL_typename(L, 1));
  }
  moonL_traceback(L, L, msg, 1);  // append a standard traceback
  return 1;  // return the traceback
}


/*
** Interface to 'moon_pcall', which sets appropriate message function
** and C-signal handler. Used to run all chunks.
*/
static int docall (moon_State *L, int narg, int nres) {
  int base = moon_gettop(L) - narg;  // function index
  moon_pushcfunction(L, msghandler);  // push message handler
  moon_insert(L, base);  // put it under function and args
  globalL = L;  // to be available to 'laction'
  setsignal(SIGINT, laction);  // set C-signal handler
  const int status = moon_pcall(L, narg, nres, base);
  setsignal(SIGINT, SIG_DFL);  // reset C-signal handler
  moon_remove(L, base);  // remove message handler from the stack
  return status;
}


static void print_version (void) {
  moon_writestring(MOON_COPYRIGHT, strlen(MOON_COPYRIGHT));
  moon_writeline();
}


/*
** Create the 'arg' table, which stores all arguments from the
** command line ('argv'). It should be aligned so that, at index 0,
** it has 'argv[script]', which is the script name. The arguments
** to the script (everything after 'script') go to positive indices;
** other arguments (before the script name) go to negative indices.
** If there is no script name, assume interpreter's name as base.
** (If there is no interpreter's name either, 'script' is -1, so
** table sizes are zero.)
*/
static void createargtable (moon_State *L, char **argv, int argc, int script) {
  int i, narg;
  narg = argc - (script + 1);  // number of positive indices
  moon_createtable(L, narg, script + 1);
  for (i = 0; i < argc; i++) {
    moon_pushstring(L, argv[i]);
    moon_rawseti(L, -2, i - script);
  }
  moon_setglobal(L, "arg");
}


static int dochunk (moon_State *L, int status) {
  if (status == MOON_OK) status = docall(L, 0, 0);
  return report(L, status);
}


static int dofile (moon_State *L, const char *name) {
  return dochunk(L, moonL_loadfile(L, name));
}


static int dostring (moon_State *L, const char *s, const char *name) {
  return dochunk(L, moonL_loadbuffer(L, s, strlen(s), name));
}


/*
** Receives 'globname[=modname]' and runs 'globname = require(modname)'.
** If there is no explicit modname and globname contains a '-', cut
** the suffix after '-' (the "version") to make the global name.
*/
static int dolibrary (moon_State *L, char *globname) {
  int status;
  char *suffix = nullptr;
  char *modname = strchr(globname, '=');
  if (modname == nullptr) {  // no explicit name?
    modname = globname;  // module name is equal to global name
    suffix = strchr(modname, *MOON_IGMARK);  // look for a suffix mark
  }
  else {
    *modname = '\0';  /* global name ends here */
    modname++;  // module name starts after the '='
  }
  moon_getglobal(L, "require");
  moon_pushstring(L, modname);
  status = docall(L, 1, 1);  // call 'require(modname)'
  if (status == MOON_OK) {
    if (suffix != nullptr)  // is there a suffix mark?
      *suffix = '\0';  /* remove suffix from global name */
    moon_setglobal(L, globname);  // globname = require(modname)
  }
  return report(L, status);
}


/*
** Push on the stack the contents of table 'arg' from 1 to #arg
*/
static int pushargs (moon_State *L) {
  int i, n;
  if (moon_getglobal(L, "arg") != MOON_TTABLE)
    moonL_error(L, "'arg' is not a table");
  n = (int)moonL_len(L, -1);
  moonL_checkstack(L, n + 3, "too many arguments to script");
  for (i = 1; i <= n; i++)
    moon_rawgeti(L, -i, i);
  moon_remove(L, -i);  // remove table from the stack
  return n;
}


static int handle_script (moon_State *L, char **argv) {
  const char *fname = argv[0];
  if (strcmp(fname, "-") == 0 && strcmp(argv[-1], "--") != 0)
    fname = nullptr;  // stdin
  int status = moonL_loadfile(L, fname);
  if (status == MOON_OK) {
    int n = pushargs(L);  // push arguments to script
    status = docall(L, n, MOON_MULTRET);
  }
  return report(L, status);
}


// bits of various argument indicators in 'args'
#define has_error	1  // bad option
#define has_i		2  // -i
#define has_v		4  // -v
#define has_e		8  // -e
#define has_E		16  // -E


/*
** Traverses all arguments from 'argv', returning a mask with those
** needed before running any Lua code or an error code if it finds any
** invalid argument. In case of error, 'first' is the index of the bad
** argument.  Otherwise, 'first' is -1 if there is no program name,
** 0 if there is no script name, or the index of the script name.
*/
static int collectargs (char **argv, int *first) {
  int args = 0;
  int i;
  if (argv[0] != nullptr) {  // is there a program name?
    if (argv[0][0])  // not empty?
      progname = argv[0];  // save it
  }
  else {  // no program name
    *first = -1;
    return 0;
  }
  for (i = 1; argv[i] != nullptr; i++) {  // handle arguments
    *first = i;
    if (argv[i][0] != '-')  // not an option?
        return args;  // stop handling options
    switch (argv[i][1]) {  // else check option
      case '-':  // '--'
        if (argv[i][2] != '\0')  // extra characters after '--'?
          return has_error;  // invalid option
        // if there is a script name, it comes after '--'
        *first = (argv[i + 1] != nullptr) ? i + 1 : 0;
        return args;
      case '\0':  // '-'
        return args;  // script "name" is '-'
      case 'E':
        if (argv[i][2] != '\0')  // extra characters?
          return has_error;  // invalid option
        args |= has_E;
        break;
      case 'W':
        if (argv[i][2] != '\0')  // extra characters?
          return has_error;  // invalid option
        break;
      case 'i':
        args |= has_i;  /* (-i implies -v) *//* FALLTHROUGH */
      case 'v':
        if (argv[i][2] != '\0')  // extra characters?
          return has_error;  // invalid option
        args |= has_v;
        break;
      case 'e':
        args |= has_e;  // FALLTHROUGH
      case 'l':  // both options need an argument
        if (argv[i][2] == '\0') {  // no concatenated argument?
          i++;  // try next 'argv'
          if (argv[i] == nullptr || argv[i][0] == '-')
            return has_error;  // no next argument or it is another option
        }
        break;
      default:  // invalid option
        return has_error;
    }
  }
  *first = 0;  /* no script name */
  return args;
}


/*
** Processes options 'e' and 'l', which involve running Lua code, and
** 'W', which also affects the state.
** Returns 0 if some code raises an error.
*/
static int runargs (moon_State *L, char **argv, int n) {
  for (int i = 1; i < n; i++) {
    int option = argv[i][1];
    moon_assert(argv[i][0] == '-');  // already checked
    switch (option) {
      case 'e':  case 'l': {
        char *extra = argv[i] + 2;  // both options need an argument
        if (*extra == '\0') extra = argv[++i];
        moon_assert(extra != nullptr);
        const int status = (option == 'e')
                 ? dostring(L, extra, "=(command line)")
                 : dolibrary(L, extra);
        if (status != MOON_OK) return 0;
        break;
      }
      case 'W':
        moon_warning(L, "@on", 0);  // warnings on
        break;
    }
  }
  return 1;
}


static int handle_luainit (moon_State *L) {
  const char *name = "=" MOON_INITVARVERSION;
  const char *init = getenv(name + 1);
  if (init == nullptr) {
    name = "=" MOON_INIT_VAR;
    init = getenv(name + 1);  // try alternative name
  }
  if (init == nullptr) return MOON_OK;
  else if (init[0] == '@')
    return dofile(L, init+1);
  else
    return dostring(L, init, name);
}


/*
** {==================================================================
** Read-Eval-Print Loop (REPL)
** ===================================================================
*/

#if !defined(MOON_PROMPT)
#define MOON_PROMPT		"> "
#define MOON_PROMPT2		">> "
#endif

#if !defined(MOON_MAXINPUT)
#define MOON_MAXINPUT		512
#endif


/*
** moon_stdin_is_tty detects whether the standard input is a 'tty' (that
** is, whether we're running lua interactively).
*/
#if !defined(moon_stdin_is_tty)  // {

#if defined(MOON_USE_POSIX)  // {

#include <unistd.h>
#define moon_stdin_is_tty()	isatty(0)

#elif defined(MOON_USE_WINDOWS)  // }{

#include <io.h>
#include <windows.h>

#define moon_stdin_is_tty()	_isatty(_fileno(stdin))

#else  // }{

// ISO C definition
#define moon_stdin_is_tty()	1  // assume stdin is a tty

#endif  // }

#endif  // }


/*
** * moon_initreadline initializes the readline system.
** * moon_readline defines how to show a prompt and then read a line from
**   the standard input.
** * moon_saveline defines how to "save" a read line in a "history".
** * moon_freeline defines how to free a line read by moon_readline.
*/

#if !defined(moon_readline)  // {
// Otherwise, all previously listed functions should be defined.

#if defined(MOON_USE_READLINE)  // {
// Lua will be linked with '-lreadline'

#include <readline/readline.h>
#include <readline/history.h>

#define moon_initreadline(L)	((void)L, rl_readline_name="lua")
#define moon_readline(buff,prompt)	((void)buff, readline(prompt))
#define moon_saveline(line)	add_history(line)
#define moon_freeline(line)	free(line)

#else  // }{
// use dynamically loaded readline (or nothing)

// pointer to 'readline' function (if any)
typedef char *(*l_readlineT) (const char *prompt);
static l_readlineT l_readline = nullptr;

// pointer to 'add_history' function (if any)
typedef void (*l_addhistT) (const char *string);
static l_addhistT l_addhist = nullptr;


static char *moon_readline (char *buff, const char *prompt) {
  if (l_readline != nullptr)  // is there a 'readline'?
    return (*l_readline)(prompt);  // use it
  else {  // emulate 'readline' over 'buff'
    fputs(prompt, stdout);
    fflush(stdout);  // show prompt
    return fgets(buff, MOON_MAXINPUT, stdin);  // read line
  }
}


static void moon_saveline (const char *line) {
  if (l_addhist != nullptr)  // is there an 'add_history'?
    (*l_addhist)(line);  // use it
  // else nothing to be done
}


static void moon_freeline (char *line) {
  if (l_readline != nullptr)  // is there a 'readline'?
    free(line);  // free line created by it
  // else 'moon_readline' used an automatic buffer; nothing to free
}


#if defined(MOON_USE_DLOPEN) && defined(MOON_READLINELIB)  // {
// try to load 'readline' dynamically

#include <dlfcn.h>

static void moon_initreadline (moon_State *L) {
  void *lib = dlopen(MOON_READLINELIB, RTLD_NOW | RTLD_LOCAL);
  if (lib == nullptr)
    moon_warning(L, "library '" MOON_READLINELIB "' not found", 0);
  else {
    const char **name = static_cast<const char**>(dlsym(lib, "rl_readline_name"));
    if (name != nullptr)
      *name = "lua";
    l_readline = reinterpret_cast<l_readlineT>(cast_func(dlsym(lib, "readline")));
    l_addhist = reinterpret_cast<l_addhistT>(cast_func(dlsym(lib, "add_history")));
    if (l_readline == nullptr)
      moon_warning(L, "unable to load 'readline'", 0);
  }
}

#else  // }{
// no dlopen or MOON_READLINELIB undefined

// Leave pointers with nullptr
#define moon_initreadline(L)	((void)L)

#endif  // }

#endif  // }

#endif  // }


/*
** Return the string to be used as a prompt by the interpreter. Leave
** the string (or nil, if using the default value) on the stack, to keep
** it anchored.
*/
static const char *get_prompt (moon_State *L, int firstline) {
  if (moon_getglobal(L, firstline ? "_PROMPT" : "_PROMPT2") == MOON_TNIL)
    return (firstline ? MOON_PROMPT : MOON_PROMPT2);  // use the default
  else {  // apply 'tostring' over the value
    const char *p = moonL_tolstring(L, -1, nullptr);
    moon_remove(L, -2);  // remove original value
    return p;
  }
}

// mark in error messages for incomplete statements
#define EOFMARK		"<eof>"
#define marklen		(sizeof(EOFMARK)/sizeof(char) - 1)


/*
** Check whether 'status' signals a syntax error and the error
** message at the top of the stack ends with the above mark for
** incomplete statements.
*/
static int incomplete (moon_State *L, int status) {
  if (status == MOON_ERRSYNTAX) {
    size_t lmsg;
    const char *msg = moon_tolstring(L, -1, &lmsg);
    if (lmsg >= marklen && strcmp(msg + lmsg - marklen, EOFMARK) == 0)
      return 1;
  }
  return 0;  // else...
}


/*
** Prompt the user, read a line, and push it into the Lua stack.
*/
static int pushline (moon_State *L, int firstline) {
  char buffer[MOON_MAXINPUT];
  size_t l;
  const char *prmt = get_prompt(L, firstline);
  char *b = moon_readline(buffer, prmt);
  moon_pop(L, 1);  // remove prompt
  if (b == nullptr)
    return 0;  // no input
  l = strlen(b);
  if (l > 0 && b[l-1] == '\n')  // line ends with newline?
    b[--l] = '\0';  // remove it
  moon_pushlstring(L, b, l);
  moon_freeline(b);
  return 1;
}


/*
** Try to compile line on the stack as 'return <line>;'; on return, stack
** has either compiled chunk or original line (if compilation failed).
*/
static int addreturn (moon_State *L) {
  const char *line = moon_tostring(L, -1);  // original line
  const char *retline = moon_pushfstring(L, "return %s;", line);
  int status = moonL_loadbuffer(L, retline, strlen(retline), "=stdin");
  if (status == MOON_OK)
    moon_remove(L, -2);  // remove modified line
  else
    moon_pop(L, 2);  // pop result from 'moonL_loadbuffer' and modified line
  return status;
}


static void checklocal (const char *line) {
  static const size_t szloc = sizeof("local") - 1;
  static const char space[] = " \t";
  line += strspn(line, space);  // skip spaces
  if (strncmp(line, "local", szloc) == 0 &&  // "local"?
      strchr(space, *(line + szloc)) != nullptr) {  // followed by a space?
    moon_writestringerror("%s\n",
      "warning: locals do not survive across lines in interactive mode");
  }
}


/*
** Read multiple lines until a complete Lua statement or an error not
** for an incomplete statement. Start with first line already read in
** the stack.
*/
static int multiline (moon_State *L) {
  size_t len;
  const char *line = moon_tolstring(L, 1, &len);  // get first line
  checklocal(line);
  for (;;) {  // repeat until gets a complete statement
    int status = moonL_loadbuffer(L, line, len, "=stdin");  // try it
    if (!incomplete(L, status) || !pushline(L, 0))
      return status;  // should not or cannot try to add continuation line
    moon_remove(L, -2);  // remove error message (from incomplete line)
    moon_pushliteral(L, "\n");  // add newline...
    moon_insert(L, -2);  // ...between the two lines
    moon_concat(L, 3);  // join them
    line = moon_tolstring(L, 1, &len);  // get what is has
  }
}


/*
** Read a line and try to load (compile) it first as an expression (by
** adding "return " in front of it) and second as a statement. Return
** the final status of load/call with the resulting function (if any)
** in the top of the stack.
*/
static int loadline (moon_State *L) {
  const char *line;
  int status;
  moon_settop(L, 0);
  if (!pushline(L, 1))
    return -1;  // no input
  if ((status = addreturn(L)) != MOON_OK)  // 'return ...' did not work?
    status = multiline(L);  // try as command, maybe with continuation lines
  line = moon_tostring(L, 1);
  if (line[0] != '\0')  // non empty?
    moon_saveline(line);  // keep history
  moon_remove(L, 1);  // remove line from the stack
  moon_assert(moon_gettop(L) == 1);
  return status;
}


/*
** Prints (calling the Lua 'print' function) any values on the stack
*/
static void l_print (moon_State *L) {
  int n = moon_gettop(L);
  if (n > 0) {  // any result to be printed?
    moonL_checkstack(L, MOON_MINSTACK, "too many results to print");
    moon_getglobal(L, "print");
    moon_insert(L, 1);
    if (moon_pcall(L, n, 0, 0) != MOON_OK)
      l_message(progname, moon_pushfstring(L, "error calling 'print' (%s)",
                                             moon_tostring(L, -1)));
  }
}


/*
** Do the REPL: repeatedly read (load) a line, evaluate (call) it, and
** print any results.
*/
static void doREPL (moon_State *L) {
  int status;
  const char *oldprogname = progname;
  progname = nullptr;  // no 'progname' on errors in interactive mode
  moon_initreadline(L);
  while ((status = loadline(L)) != -1) {
    if (status == MOON_OK)
      status = docall(L, 0, MOON_MULTRET);
    if (status == MOON_OK) l_print(L);
    else report(L, status);
  }
  moon_settop(L, 0);  // clear stack
  moon_writeline();
  progname = oldprogname;
}

// }==================================================================

#if !defined(mooni_openlibs)
#define mooni_openlibs(L)	moonL_openselectedlibs(L, ~0, 0)
#endif


/*
** Main body of stand-alone interpreter (to be called in protected mode).
** Reads the options and handles them all.
*/
static int pmain (moon_State *L) {
  int argc = (int)moon_tointeger(L, 1);
  char **argv = static_cast<char **>(moon_touserdata(L, 2));
  int script;
  int args = collectargs(argv, &script);
  int optlim = (script > 0) ? script : argc;  // first argv not an option
  moonL_checkversion(L);  // check that interpreter has correct version
  if (args == has_error) {  // bad arg?
    print_usage(argv[script]);  // 'script' has index of bad arg.
    return 0;
  }
  if (args & has_v)  // option '-v'?
    print_version();
  if (args & has_E) {  // option '-E'?
    moon_pushboolean(L, 1);  // signal for libraries to ignore env. vars.
    moon_setfield(L, MOON_REGISTRYINDEX, "MOON_NOENV");
  }
  mooni_openlibs(L);  // open standard libraries
  createargtable(L, argv, argc, script);  // create table 'arg'
  moon_gc(L, MOON_GCRESTART);  // start GC...
  moon_gc(L, MOON_GCGEN);  // ...in generational mode
  if (!(args & has_E)) {  // no option '-E'?
    if (handle_luainit(L) != MOON_OK)  // run MOON_INIT
      return 0;  // error running MOON_INIT
  }
  if (!runargs(L, argv, optlim))  // execute arguments -e and -l
    return 0;  // something failed
  if (script > 0) {  // execute main script (if there is one)
    if (handle_script(L, argv + script) != MOON_OK)
      return 0;  // interrupt in case of error
  }
  if (args & has_i)  // -i option?
    doREPL(L);  // do read-eval-print loop
  else if (script < 1 && !(args & (has_e | has_v))) {  // no active option?
    if (moon_stdin_is_tty()) {  // running in interactive mode?
      print_version();
      doREPL(L);  // do read-eval-print loop
    }
    else
      dofile(L, nullptr);  // executes stdin as a file
  }
  moon_pushboolean(L, 1);  // signal no errors
  return 1;
}


int main (int argc, char **argv) {
  int status, result;
  moon_State *L = moonL_newstate();  // create state
  if (L == nullptr) {
    l_message(argv[0], "cannot create state: not enough memory");
    return EXIT_FAILURE;
  }
  moon_gc(L, MOON_GCSTOP);  // stop GC while building state
  moon_pushcfunction(L, &pmain);  // to call 'pmain' in protected mode
  moon_pushinteger(L, argc);  // 1st argument
  moon_pushlightuserdata(L, argv);  // 2nd argument
  status = moon_pcall(L, 2, 1, 0);  // do the call
  result = moon_toboolean(L, -1);  // get result
  report(L, status);
  moon_close(L);
  return (result && status == MOON_OK) ? EXIT_SUCCESS : EXIT_FAILURE;
}

