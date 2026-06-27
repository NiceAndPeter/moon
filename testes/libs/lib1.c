#include "moon.h"
#include "mauxlib.h"

static int id (moon_State *L) {
  return moon_gettop(L);
}


static const struct moonL_Reg funcs[] = {
  {"id", id},
  {NULL, NULL}
};


/* function used by lib11.c */
MOONMOD_API int lib1_export (moon_State *L) {
  moon_pushstring(L, "exported");
  return 1;
}


MOONMOD_API int onefunction (moon_State *L) {
  moonL_checkversion(L);
  moon_settop(L, 2);
  moon_pushvalue(L, 1);
  return 2;
}


MOONMOD_API int anotherfunc (moon_State *L) {
  moonL_checkversion(L);
  moon_pushfstring(L, "%d%%%d\n", (int)moon_tointeger(L, 1),
                                 (int)moon_tointeger(L, 2));
  return 1;
} 


MOONMOD_API int moonopen_lib1_sub (moon_State *L) {
  moon_setglobal(L, "y");  /* 2nd arg: extra value (file name) */
  moon_setglobal(L, "x");  /* 1st arg: module name */
  moonL_newlib(L, funcs);
  return 1;
}

