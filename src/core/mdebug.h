/*
** Auxiliary functions from Debug Interface module
** See Copyright Notice in lua.h
*/

#ifndef ldebug_h
#define ldebug_h


#include "mstate.h"


/*
** mark for entries in 'lineinfo' array that has absolute information in
** 'abslineinfo' array
*/
inline constexpr int ABSLINEINFO = (-0x80);


/*
** MAXimum number of successive Instructions WiTHout ABSolute line
** information. (A power of two allows fast divisions.)
*/
#if !defined(MAXIWTHABS)
inline constexpr int MAXIWTHABS = 128;
#endif


MOONI_FUNC int moonG_getfuncline (const Proto *f, int pc);
MOONI_FUNC const char *moonG_findlocal (moon_State *L, CallInfo *callInfo, int n,
                                                    StkId *pos);
MOONI_FUNC l_noret moonG_typeerror (moon_State *L, const TValue *o,
                                                const char *opname);
MOONI_FUNC l_noret moonG_callerror (moon_State *L, const TValue *o);
MOONI_FUNC l_noret moonG_forerror (moon_State *L, const TValue *o,
                                               const char *what);
MOONI_FUNC l_noret moonG_concaterror (moon_State *L, const TValue *p1,
                                                  const TValue *p2);
MOONI_FUNC l_noret moonG_opinterror (moon_State *L, const TValue *p1,
                                                 const TValue *p2,
                                                 const char *msg);
MOONI_FUNC l_noret moonG_tointerror (moon_State *L, const TValue *p1,
                                                 const TValue *p2);
MOONI_FUNC l_noret moonG_ordererror (moon_State *L, const TValue *p1,
                                                 const TValue *p2);
MOONI_FUNC l_noret moonG_runerror (moon_State *L, const char *fmt, ...);
MOONI_FUNC const char *moonG_addinfo (moon_State *L, const char *msg,
                                                  TString *src, int line);
MOONI_FUNC l_noret moonG_errormsg (moon_State *L);
MOONI_FUNC int moonG_traceexec (moon_State *L, const Instruction *pc);
MOONI_FUNC int moonG_tracecall (moon_State *L);


#endif
