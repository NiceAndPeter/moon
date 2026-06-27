#include "lua.h"
#include "lauxlib.h"

static int id (moon_State *L) {
  return moon_gettop(L);
}


static const struct moonL_Reg funcs[] = {
  {"id", id},
  {NULL, NULL}
};


MOONMOD_API int moonopen_lib2 (moon_State *L) {
  moon_settop(L, 2);
  moon_setglobal(L, "y");  /* y gets 2nd parameter */
  moon_setglobal(L, "x");  /* x gets 1st parameter */
  moonL_newlib(L, funcs);
  return 1;
}


