#include "lua.h"

/* function from lib1.c */
MOONMOD_API int lib1_export (moon_State *L);

MOONMOD_API int moonopen_lib11 (moon_State *L) {
  return lib1_export(L);
}


