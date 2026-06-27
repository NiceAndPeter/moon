#include "lua.h"


int moonopen_lib2 (moon_State *L);

MOONMOD_API int moonopen_lib21 (moon_State *L) {
  return moonopen_lib2(L);
}


