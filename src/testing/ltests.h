/*
** Internal Header for Debugging of the Lua Implementation
** See Copyright Notice in lua.h
*/

#ifndef ltests_h
#define ltests_h


#include <cstdio>
#include <cstdlib>

// test Lua with compatibility code
#define MOON_COMPAT_MATHLIB
#undef MOON_COMPAT_GLOBAL


#define MOON_DEBUG


// turn on assertions (unless explicitly disabled via MOON_TESTS_NO_ASSERT)
#ifndef MOON_TESTS_NO_ASSERT
#ifndef MOONI_ASSERT
#define MOONI_ASSERT
#endif
#endif


// to avoid warnings, and to make sure value is really unused
#define UNUSED(x)       (x=0, (void)(x))


// test for sizes in 'l_sprintf' (make sure whole buffer is available)
#undef l_sprintf
#if !defined(MOON_USE_C89)
#define l_sprintf(s,sz,f,i)	(memset(s,0xAB,sz), snprintf(s,sz,f,i))
#else
#define l_sprintf(s,sz,f,i)	(memset(s,0xAB,sz), sprintf(s,f,i))
#endif


// get a chance to test code without jump tables
#define MOON_USE_JUMPTABLE	0


// use 32-bit integers in random generator
#define MOON_RAND32


// test stack reallocation without strict address use
#define MOONI_STRICT_ADDRESS	0


// memory-allocator control variables
typedef struct Memcontrol {
  int failnext;
  unsigned long numblocks;
  unsigned long total;
  unsigned long maxmem;
  unsigned long memlimit;
  unsigned long countlimit;
  unsigned long objcount[MOON_NUMTYPES];
} Memcontrol;

MOON_API Memcontrol l_memcontrol;


#define mooni_tracegc(L,f)		mooni_tracegctest(L, f)
extern void mooni_tracegctest (moon_State *L, int first);


/*
** generic variable for debug tricks
*/
extern void *l_Trick;


/*
** Function to traverse and check all memory used by Lua
*/
extern int moon_checkmemory (moon_State *L);

/*
** Function to print an object GC-friendly
*/
class GCObject;
extern void moon_printobj (moon_State *L, class GCObject *o);


/*
** Function to print a value
*/
class TValue;
extern void moon_printvalue (class TValue *v);

/*
** Function to print the stack
*/
extern void moon_printstack (moon_State *L);
extern int moon_printallstack (moon_State *L);


// test for lock/unlock

struct L_EXTRA { int lock; int *plock; };
#undef MOON_EXTRASPACE
#define MOON_EXTRASPACE	sizeof(struct L_EXTRA)
#define getlock(l)	cast(struct L_EXTRA*, moon_getextraspace(l))
#define mooni_userstateopen(l)  \
	(getlock(l)->lock = 0, getlock(l)->plock = &(getlock(l)->lock))
#define mooni_userstateclose(l)  \
  moon_assert(getlock(l)->lock == 1 && getlock(l)->plock == &(getlock(l)->lock))
#define mooni_userstatethread(l,l1) \
  moon_assert(getlock(l1)->plock == getlock(l)->plock)
#define mooni_userstatefree(l,l1) \
  moon_assert(getlock(l)->plock == getlock(l1)->plock)
#define moon_lock(l)     moon_assert((*getlock(l)->plock)++ == 0)
#define moon_unlock(l)   moon_assert(--(*getlock(l)->plock) == 0)



MOON_API int moonB_opentests (moon_State *L);

MOON_API void *debug_realloc (void *ud, void *block,
                             size_t osize, size_t nsize);


#define moonL_newstate()  \
	moon_newstate(debug_realloc, &l_memcontrol, moonL_makeseed(nullptr))
#define mooni_openlibs(L)  \
  {  moonL_openlibs(L); \
     moonL_requiref(L, "T", moonB_opentests, 1); \
     moon_pop(L, 1); }




// change some sizes to give some bugs a chance

#undef MOONL_BUFFERSIZE
#define MOONL_BUFFERSIZE		23
#define MINSTRTABSIZE		2
#define MAXIWTHABS		3

#define STRCACHE_N	23
#define STRCACHE_M	5


/*
** This one is not compatible with tests for opcode optimizations,
** as it blocks some optimizations
#define MAXINDEXRK	0
*/


/*
** Reduce maximum stack size to make stack-overflow tests run faster.
** (But value is still large enough to overflow smaller integers.)
*/
#define MOONI_MAXSTACK   68000


// test mode uses more stack space
#undef MOONI_MAXCCALLS
#define MOONI_MAXCCALLS	180


// force Lua to use its own implementations
#undef moon_strx2number
#undef moon_number2strx


#endif

