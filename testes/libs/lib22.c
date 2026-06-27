/* implementation for lib2-v2 */

#include <string.h>

#include "lua.h"
#include "lauxlib.h"

static int id (moon_State *L) {
  moon_pushboolean(L, 1);
  moon_insert(L, 1);
  return moon_gettop(L);
}


struct STR {
  void *ud;
  moon_Alloc allocf;
};


static void *t_freestr (void *ud, void *ptr, size_t osize, size_t nsize) {
  struct STR *blk = (struct STR*)ptr - 1;
  blk->allocf(blk->ud, blk, sizeof(struct STR) + osize, 0);
  return NULL;
}


static int newstr (moon_State *L) {
  size_t len;
  const char *str = moonL_checklstring(L, 1, &len);
  void *ud;
  moon_Alloc allocf = moon_getallocf(L, &ud);
  struct STR *blk = (struct STR*)allocf(ud, NULL, 0,
                                        len + 1 + sizeof(struct STR));
  if (blk == NULL) {  /* allocation error? */
    moon_pushliteral(L, "not enough memory");
    moon_error(L);  /* raise a memory error */
  }
  blk->ud = ud;  blk->allocf = allocf;
  memcpy(blk + 1, str, len + 1);
  moon_pushexternalstring(L, (char *)(blk + 1), len, t_freestr, L);
  return 1;
}


/*
** Create an external string and keep it in the registry, so that it
** will test that the library code is still available (to deallocate
** this string) when closing the state.
*/
static void initstr (moon_State *L) {
  moon_pushcfunction(L, newstr);
  moon_pushstring(L,
     "012345678901234567890123456789012345678901234567890123456789");
  moon_call(L, 1, 1);  /* call newstr("0123...") */
  moonL_ref(L, MOON_REGISTRYINDEX);  /* keep string in the registry */
}


static const struct moonL_Reg funcs[] = {
  {"id", id},
  {"newstr", newstr},
  {NULL, NULL}
};


MOONMOD_API int moonopen_lib2 (moon_State *L) {
  moon_settop(L, 2);
  moon_setglobal(L, "y");  /* y gets 2nd parameter */
  moon_setglobal(L, "x");  /* x gets 1st parameter */
  initstr(L);
  moonL_newlib(L, funcs);
  return 1;
}


