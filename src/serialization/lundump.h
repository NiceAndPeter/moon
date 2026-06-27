/*
** load precompiled Lua chunks
** See Copyright Notice in lua.h
*/

#ifndef lundump_h
#define lundump_h

#include <climits>

#include "llimits.h"
#include "lobject.h"
#include "lzio.h"


// data to catch conversion errors
#define MOONC_DATA	"\x19\x93\r\n\x1a\n"

#define MOONC_INT	-0x5678
#define MOONC_INST	0x12345678
#define MOONC_NUM	cast_num(-370.5)

/*
** Encode major-minor version in one byte, one nibble for each
*/
#define MOONC_VERSION	(MOON_VERSION_MAJOR_N*16+MOON_VERSION_MINOR_N)

#define MOONC_FORMAT	0  // this is the official format


// load one chunk; from lundump.c
MOONI_FUNC LClosure* moonU_undump (moon_State* L, ZIO* Z, const char* name,
                                               int fixed);

// dump one chunk; from ldump.c
MOONI_FUNC int moonU_dump (moon_State* L, const Proto* f, moon_Writer w,
                         void* data, int strip);

#endif
