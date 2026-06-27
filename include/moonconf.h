/*
** $Id: luaconf.h $
** Configuration file for Lua
** See Copyright Notice in lua.h
*/


#ifndef moonconf_h
#define moonconf_h

#include <limits.h>
#include <stddef.h>


/*
** ===================================================================
** General Configuration File for Lua
**
** Some definitions here can be changed externally, through the compiler
** (e.g., with '-D' options): They are commented out or protected
** by '#if !defined' guards. However, several other definitions
** should be changed directly here, either because they affect the
** Lua ABI (by making the changes here, you ensure that all software
** connected to Lua, such as C libraries, will be compiled with the same
** configuration); or because they are seldom changed.
**
** Search for "@@" to find all configurable definitions.
** ===================================================================
*/


/*
** {====================================================================
** System Configuration: macros to adapt (if needed) Lua to some
** particular platform, for instance restricting it to C89.
** =====================================================================
*/

/*
@@ MOON_USE_C89 controls the use of non-ISO-C89 features.
** Define it if you want Lua to avoid the use of a few C99 features
** or Windows-specific features on Windows.
*/
/* #define MOON_USE_C89 */


/*
** By default, Lua on Windows use (some) specific Windows features
*/
#if !defined(MOON_USE_C89) && defined(_WIN32) && !defined(_WIN32_WCE)
#define MOON_USE_WINDOWS  /* enable goodies for regular Windows */
#endif


#if defined(MOON_USE_WINDOWS)
#define MOON_DL_DLL	/* enable support for DLL */
#define MOON_USE_C89	/* broadly, Windows is C89 */
#endif


/*
** When POSIX DLL ('MOON_USE_DLOPEN') is enabled, the Lua stand-alone
** application will try to dynamically link a 'readline' facility
** for its REPL.  In that case, MOON_READLINELIB is the name of the
** library it will look for those facilities.  If lua.c cannot open
** the specified library, it will generate a warning and then run
** without 'readline'.  If that macro is not defined, lua.c will not
** use 'readline'.
*/
#if defined(MOON_USE_LINUX)
#define MOON_USE_POSIX
#define MOON_USE_DLOPEN		/* needs an extra library: -ldl */
#define MOON_READLINELIB		"libreadline.so"
#endif


#if defined(MOON_USE_MACOSX)
#define MOON_USE_POSIX
#define MOON_USE_DLOPEN		/* macOS does not need -ldl */
#define MOON_READLINELIB		"libedit.dylib"
#endif


#if defined(MOON_USE_IOS)
#define MOON_USE_POSIX
#define MOON_USE_DLOPEN
#endif


#if defined(MOON_USE_C89) && defined(MOON_USE_POSIX)
#error "POSIX is not compatible with C89"
#endif


/*
@@ MOONI_IS32INT is true iff 'int' has (at least) 32 bits.
*/
#define MOONI_IS32INT	((UINT_MAX >> 30) >= 3)

/* }================================================================== */



/*
** {==================================================================
** Configuration for Number types. These options should not be
** set externally, because any other code connected to Lua must
** use the same configuration.
** ===================================================================
*/

/*
@@ MOON_INT_TYPE defines the type for Lua integers.
@@ MOON_FLOAT_TYPE defines the type for Lua floats.
** Lua should work fine with any mix of these options supported
** by your C compiler. The usual configurations are 64-bit integers
** and 'double' (the default), 32-bit integers and 'float' (for
** restricted platforms), and 'long'/'double' (for C compilers not
** compliant with C99, which may not have support for 'long long').
*/

/* predefined options for MOON_INT_TYPE */
#define MOON_INT_INT		1
#define MOON_INT_LONG		2
#define MOON_INT_LONGLONG	3

/* predefined options for MOON_FLOAT_TYPE */
#define MOON_FLOAT_FLOAT		1
#define MOON_FLOAT_DOUBLE	2
#define MOON_FLOAT_LONGDOUBLE	3


/* Default configuration ('long long' and 'double', for 64-bit Lua) */
#define MOON_INT_DEFAULT		MOON_INT_LONGLONG
#define MOON_FLOAT_DEFAULT	MOON_FLOAT_DOUBLE


/*
@@ MOON_32BITS enables Lua with 32-bit integers and 32-bit floats.
*/
/* #define MOON_32BITS */


/*
@@ MOON_C89_NUMBERS ensures that Lua uses the largest types available for
** C89 ('long' and 'double'); Windows always has '__int64', so it does
** not need to use this case.
*/
#if defined(MOON_USE_C89) && !defined(MOON_USE_WINDOWS)
#define MOON_C89_NUMBERS		1
#else
#define MOON_C89_NUMBERS		0
#endif


#if defined(MOON_32BITS)	/* { */
/*
** 32-bit integers and 'float'
*/
#if MOONI_IS32INT  /* use 'int' if big enough */
#define MOON_INT_TYPE	MOON_INT_INT
#else  /* otherwise use 'long' */
#define MOON_INT_TYPE	MOON_INT_LONG
#endif
#define MOON_FLOAT_TYPE	MOON_FLOAT_FLOAT

#elif MOON_C89_NUMBERS	/* }{ */
/*
** largest types available for C89 ('long' and 'double')
*/
#define MOON_INT_TYPE	MOON_INT_LONG
#define MOON_FLOAT_TYPE	MOON_FLOAT_DOUBLE

#else		/* }{ */
/* use defaults */

#define MOON_INT_TYPE	MOON_INT_DEFAULT
#define MOON_FLOAT_TYPE	MOON_FLOAT_DEFAULT

#endif				/* } */


/* }================================================================== */



/*
** {==================================================================
** Configuration for Paths.
** ===================================================================
*/

/*
** MOON_PATH_SEP is the character that separates templates in a path.
** MOON_PATH_MARK is the string that marks the substitution points in a
** template.
** MOON_EXEC_DIR in a Windows path is replaced by the executable's
** directory.
*/
#define MOON_PATH_SEP            ";"
#define MOON_PATH_MARK           "?"
#define MOON_EXEC_DIR            "!"


/*
@@ MOON_PATH_DEFAULT is the default path that Lua uses to look for
** Lua libraries.
@@ MOON_CPATH_DEFAULT is the default path that Lua uses to look for
** C libraries.
** CHANGE them if your machine has a non-conventional directory
** hierarchy or if you want to install your libraries in
** non-conventional directories.
*/

#define MOON_VDIR	MOON_VERSION_MAJOR "." MOON_VERSION_MINOR
#if defined(_WIN32)	/* { */
/*
** In Windows, any exclamation mark ('!') in the path is replaced by the
** path of the directory of the executable file of the current process.
*/
#define MOON_LDIR	"!\\lua\\"
#define MOON_CDIR	"!\\"
#define MOON_SHRDIR	"!\\..\\share\\lua\\" MOON_VDIR "\\"

#if !defined(MOON_PATH_DEFAULT)
#define MOON_PATH_DEFAULT  \
		MOON_LDIR"?.mn;"  MOON_LDIR"?\\init.mn;" \
		MOON_CDIR"?.mn;"  MOON_CDIR"?\\init.mn;" \
		MOON_SHRDIR"?.mn;" MOON_SHRDIR"?\\init.mn;" \
		".\\?.mn;" ".\\?\\init.mn"
#endif

#if !defined(MOON_CPATH_DEFAULT)
#define MOON_CPATH_DEFAULT \
		MOON_CDIR"?.dll;" \
		MOON_CDIR"..\\lib\\lua\\" MOON_VDIR "\\?.dll;" \
		MOON_CDIR"loadall.dll;" ".\\?.dll"
#endif

#else			/* }{ */

#define MOON_ROOT	"/usr/local/"
#define MOON_LDIR	MOON_ROOT "share/lua/" MOON_VDIR "/"
#define MOON_CDIR	MOON_ROOT "lib/lua/" MOON_VDIR "/"

#if !defined(MOON_PATH_DEFAULT)
#define MOON_PATH_DEFAULT  \
		MOON_LDIR"?.mn;"  MOON_LDIR"?/init.mn;" \
		MOON_CDIR"?.mn;"  MOON_CDIR"?/init.mn;" \
		"./?.mn;" "./?/init.mn"
#endif

#if !defined(MOON_CPATH_DEFAULT)
#define MOON_CPATH_DEFAULT \
		MOON_CDIR"?.so;" MOON_CDIR"loadall.so;" "./?.so"
#endif

#endif			/* } */


/*
@@ MOON_DIRSEP is the directory separator (for submodules).
** CHANGE it if your machine does not use "/" as the directory separator
** and is not Windows. (On Windows Lua automatically uses "\".)
*/
#if !defined(MOON_DIRSEP)

#if defined(_WIN32)
#define MOON_DIRSEP	"\\"
#else
#define MOON_DIRSEP	"/"
#endif

#endif


/*
** MOON_IGMARK is a mark to ignore all after it when building the
** module name (e.g., used to build the moonopen_ function name).
** Typically, the suffix after the mark is the module version,
** as in "mod-v1.2.so".
*/
#define MOON_IGMARK		"-"

/* }================================================================== */


/*
** {==================================================================
** Marks for exported symbols in the C code
** ===================================================================
*/

/*
@@ MOON_API is a mark for all core API functions.
@@ MOONLIB_API is a mark for all auxiliary library functions.
@@ MOONMOD_API is a mark for all standard library opening functions.
** CHANGE them if you need to define those functions in some special way.
** For instance, if you want to create one Windows DLL with the core and
** the libraries, you may want to use the following definition (define
** MOON_BUILD_AS_DLL to get it).
*/
#if defined(MOON_BUILD_AS_DLL)	/* { */

#if defined(MOON_CORE) || defined(MOON_LIB)	/* { */
#define MOON_API __declspec(dllexport)
#else						/* }{ */
#define MOON_API __declspec(dllimport)
#endif						/* } */

#else				/* }{ */

#define MOON_API		extern

#endif				/* } */


/*
** More often than not the libs go together with the core.
*/
#define MOONLIB_API	MOON_API

#if defined(__cplusplus)
/* Lua uses the "C name" when calling open functions */
#define MOONMOD_API	extern "C"
#else
#define MOONMOD_API	MOON_API
#endif

/* }================================================================== */


/*
** {==================================================================
** Compatibility with previous versions
** ===================================================================
*/

/*
@@ MOON_COMPAT_GLOBAL avoids 'global' being a reserved word
*/
#define MOON_COMPAT_GLOBAL


/*
@@ MOON_COMPAT_MATHLIB controls the presence of several deprecated
** functions in the mathematical library.
** (These functions were already officially removed in 5.3;
** nevertheless they are still available here.)
*/
/* #define MOON_COMPAT_MATHLIB */


/*
@@ The following macros supply trivial compatibility for some
** changes in the API. The macros themselves document how to
** change your code to avoid using them.
** (Once more, these macros were officially removed in 5.3, but they are
** still available here.)
*/
#define moon_strlen(L,i)		moon_rawlen(L, (i))

#define moon_objlen(L,i)		moon_rawlen(L, (i))

#define moon_equal(L,idx1,idx2)		moon_compare(L,(idx1),(idx2),MOON_OPEQ)
#define moon_lessthan(L,idx1,idx2)	moon_compare(L,(idx1),(idx2),MOON_OPLT)

/* }================================================================== */



/*
** {==================================================================
** Configuration for Numbers (low-level part).
** Change these definitions if no predefined MOON_FLOAT_* / MOON_INT_*
** satisfy your needs.
** ===================================================================
*/

/*
@@ MOONI_UACNUMBER is the result of a 'default argument promotion'
@@ over a floating number.
@@ l_floatatt(x) corrects float attribute 'x' to the proper float type
** by prefixing it with one of FLT/DBL/LDBL.
@@ MOON_NUMBER_FRMLEN is the length modifier for writing floats.
@@ MOON_NUMBER_FMT is the format for writing floats with the maximum
** number of digits that respects tostring(tonumber(numeral)) == numeral.
** (That would be floor(log10(2^n)), where n is the number of bits in
** the float mantissa.)
@@ MOON_NUMBER_FMT_N is the format for writing floats with the minimum
** number of digits that ensures tonumber(tostring(number)) == number.
** (That would be MOON_NUMBER_FMT+2.)
@@ l_mathop allows the addition of an 'l' or 'f' to all math operations.
@@ l_floor takes the floor of a float.
@@ moon_str2number converts a decimal numeral to a number.
*/


/* The following definition is good for most cases here */

#define l_floor(x)		(l_mathop(floor)(x))


/* now the variable definitions */

#if MOON_FLOAT_TYPE == MOON_FLOAT_FLOAT		/* { single float */

#define MOON_NUMBER	float

#define l_floatatt(n)		(FLT_##n)

#define MOONI_UACNUMBER	double

#define MOON_NUMBER_FRMLEN	""
#define MOON_NUMBER_FMT		"%.7g"
#define MOON_NUMBER_FMT_N	"%.9g"

#define l_mathop(op)		op##f

#define moon_str2number(s,p)	strtof((s), (p))


#elif MOON_FLOAT_TYPE == MOON_FLOAT_LONGDOUBLE	/* }{ long double */

#define MOON_NUMBER	long double

#define l_floatatt(n)		(LDBL_##n)

#define MOONI_UACNUMBER	long double

#define MOON_NUMBER_FRMLEN	"L"
#define MOON_NUMBER_FMT		"%.19Lg"
#define MOON_NUMBER_FMT_N	"%.21Lg"

#define l_mathop(op)		op##l

#define moon_str2number(s,p)	strtold((s), (p))

#elif MOON_FLOAT_TYPE == MOON_FLOAT_DOUBLE	/* }{ double */

#define MOON_NUMBER	double

#define l_floatatt(n)		(DBL_##n)

#define MOONI_UACNUMBER	double

#define MOON_NUMBER_FRMLEN	""
#define MOON_NUMBER_FMT		"%.15g"
#define MOON_NUMBER_FMT_N	"%.17g"

#define l_mathop(op)		op

#define moon_str2number(s,p)	strtod((s), (p))

#else						/* }{ */

#error "numeric float type not defined"

#endif					/* } */



/*
@@ MOON_UNSIGNED is the unsigned version of MOON_INTEGER.
@@ MOONI_UACINT is the result of a 'default argument promotion'
@@ over a MOON_INTEGER.
@@ MOON_INTEGER_FRMLEN is the length modifier for reading/writing integers.
@@ MOON_INTEGER_FMT is the format for writing integers.
@@ MOON_MAXINTEGER is the maximum value for a MOON_INTEGER.
@@ MOON_MININTEGER is the minimum value for a MOON_INTEGER.
@@ MOON_MAXUNSIGNED is the maximum value for a MOON_UNSIGNED.
@@ moon_integer2str converts an integer to a string.
*/


/* The following definitions are good for most cases here */

#define MOON_INTEGER_FMT		"%" MOON_INTEGER_FRMLEN "d"

#define MOONI_UACINT		MOON_INTEGER

#define moon_integer2str(s,sz,n)  \
	l_sprintf((s), sz, MOON_INTEGER_FMT, (MOONI_UACINT)(n))

/*
** use MOONI_UACINT here to avoid problems with promotions (which
** can turn a comparison between unsigneds into a signed comparison)
*/
#define MOON_UNSIGNED		unsigned MOONI_UACINT


/* now the variable definitions */

#if MOON_INT_TYPE == MOON_INT_INT		/* { int */

#define MOON_INTEGER		int
#define MOON_INTEGER_FRMLEN	""

#define MOON_MAXINTEGER		INT_MAX
#define MOON_MININTEGER		INT_MIN

#define MOON_MAXUNSIGNED		UINT_MAX

#elif MOON_INT_TYPE == MOON_INT_LONG	/* }{ long */

#define MOON_INTEGER		long
#define MOON_INTEGER_FRMLEN	"l"

#define MOON_MAXINTEGER		LONG_MAX
#define MOON_MININTEGER		LONG_MIN

#define MOON_MAXUNSIGNED		ULONG_MAX

#elif MOON_INT_TYPE == MOON_INT_LONGLONG	/* }{ long long */

/* use presence of macro LLONG_MAX as proxy for C99 compliance */
#if defined(LLONG_MAX)		/* { */
/* use ISO C99 stuff */

#define MOON_INTEGER		long long
#define MOON_INTEGER_FRMLEN	"ll"

#define MOON_MAXINTEGER		LLONG_MAX
#define MOON_MININTEGER		LLONG_MIN

#define MOON_MAXUNSIGNED		ULLONG_MAX

#elif defined(MOON_USE_WINDOWS) /* }{ */
/* in Windows, can use specific Windows types */

#define MOON_INTEGER		__int64
#define MOON_INTEGER_FRMLEN	"I64"

#define MOON_MAXINTEGER		_I64_MAX
#define MOON_MININTEGER		_I64_MIN

#define MOON_MAXUNSIGNED		_UI64_MAX

#else				/* }{ */

#error "Compiler does not support 'long long'. Use option '-DLUA_32BITS' \
  or '-DLUA_C89_NUMBERS' (see file 'luaconf.h' for details)"

#endif				/* } */

#else				/* }{ */

#error "numeric integer type not defined"

#endif				/* } */

/* }================================================================== */


/*
** {==================================================================
** Dependencies with C99 and other C details
** ===================================================================
*/

/*
@@ l_sprintf is equivalent to 'snprintf' or 'sprintf' in C89.
** (All uses in Lua have only one format item.)
*/
#if !defined(MOON_USE_C89)
#define l_sprintf(s,sz,f,i)	snprintf(s,sz,f,i)
#else
#define l_sprintf(s,sz,f,i)	((void)(sz), sprintf(s,f,i))
#endif


/*
@@ moon_strx2number converts a hexadecimal numeral to a number.
** In C99, 'strtod' does that conversion. Otherwise, you can
** leave 'moon_strx2number' undefined and Lua will provide its own
** implementation.
*/
#if !defined(MOON_USE_C89)
#define moon_strx2number(s,p)		moon_str2number(s,p)
#endif


/*
@@ moon_pointer2str converts a pointer to a readable string in a
** non-specified way.
*/
#define moon_pointer2str(buff,sz,p)	l_sprintf(buff,sz,"%p",p)


/*
@@ moon_number2strx converts a float to a hexadecimal numeral.
** In C99, 'sprintf' (with format specifiers '%a'/'%A') does that.
** Otherwise, you can leave 'moon_number2strx' undefined and Lua will
** provide its own implementation.
*/
#if !defined(MOON_USE_C89)
#define moon_number2strx(L,b,sz,f,n)  \
	((void)L, l_sprintf(b,sz,f,(MOONI_UACNUMBER)(n)))
#endif


/*
** 'strtof' and 'opf' variants for math functions are not valid in
** C89. Otherwise, the macro 'HUGE_VALF' is a good proxy for testing the
** availability of these variants. ('math.h' is already included in
** all files that use these macros.)
*/
#if defined(MOON_USE_C89) || (defined(HUGE_VAL) && !defined(HUGE_VALF))
#undef l_mathop  /* variants not available */
#undef moon_str2number
#define l_mathop(op)		(moon_Number)op  /* no variant */
#define moon_str2number(s,p)	((moon_Number)strtod((s), (p)))
#endif


/*
@@ MOON_KCONTEXT is the type of the context ('ctx') for continuation
** functions.  It must be a numerical type; Lua will use 'intptr_t' if
** available, otherwise it will use 'ptrdiff_t' (the nearest thing to
** 'intptr_t' in C89)
*/
#define MOON_KCONTEXT	ptrdiff_t

#if !defined(MOON_USE_C89) && defined(__STDC_VERSION__) && \
    __STDC_VERSION__ >= 199901L
#include <stdint.h>
#if defined(INTPTR_MAX)  /* even in C99 this type is optional */
#undef MOON_KCONTEXT
#define MOON_KCONTEXT	intptr_t
#endif
#endif


/*
@@ moon_getlocaledecpoint gets the locale "radix character" (decimal point).
** Change that if you do not want to use C locales. (Code using this
** macro must include the header 'locale.h'.)
*/
#if !defined(moon_getlocaledecpoint)
#define moon_getlocaledecpoint()		(localeconv()->decimal_point[0])
#endif


/*
** macros to improve jump prediction, used mostly for error handling
** and debug facilities. (Some macros in the Lua API use these macros.
** Define MOON_NOBUILTIN if you do not want '__builtin_expect' in your
** code.)
*/
#if !defined(mooni_likely)

#if defined(__GNUC__) && !defined(MOON_NOBUILTIN)
#define mooni_likely(x)		(__builtin_expect(((x) != 0), 1))
#define mooni_unlikely(x)	(__builtin_expect(((x) != 0), 0))
#else
#define mooni_likely(x)		(x)
#define mooni_unlikely(x)	(x)
#endif

#endif



/* }================================================================== */


/*
** {==================================================================
** Language Variations
** =====================================================================
*/

/*
@@ MOON_NOCVTN2S/MOON_NOCVTS2N control how Lua performs some
** coercions. Define MOON_NOCVTN2S to turn off automatic coercion from
** numbers to strings. Define MOON_NOCVTS2N to turn off automatic
** coercion from strings to numbers.
*/
/* #define MOON_NOCVTN2S */
/* #define MOON_NOCVTS2N */


/*
@@ MOON_USE_APICHECK turns on several consistency checks on the C API.
** Define it as a help when debugging C code.
*/
/* #define MOON_USE_APICHECK */

/* }================================================================== */


/*
** {==================================================================
** Macros that affect the API and must be stable (that is, must be the
** same when you compile Lua and when you compile code that links to
** Lua).
** =====================================================================
*/

/*
@@ MOON_EXTRASPACE defines the size of a raw memory area associated with
** a Lua state with very fast access.
** CHANGE it if you need a different size.
*/
#define MOON_EXTRASPACE		(sizeof(void *))


/*
@@ MOON_IDSIZE gives the maximum size for the description of the source
** of a function in debug information.
** CHANGE it if you want a different size.
*/
#define MOON_IDSIZE	60


/*
@@ MOONL_BUFFERSIZE is the initial buffer size used by the lauxlib
** buffer system.
*/
#define MOONL_BUFFERSIZE   ((int)(16 * sizeof(void*) * sizeof(moon_Number)))


/*
@@ MOONI_MAXALIGN defines fields that, when used in a union, ensure
** maximum alignment for the other items in that union.
*/
#define MOONI_MAXALIGN  moon_Number n; double u; void *s; moon_Integer i; long l

/* }================================================================== */





/* =================================================================== */

/*
** Local configuration. You can use this space to add your redefinitions
** without modifying the main part of the file.
*/



#endif

