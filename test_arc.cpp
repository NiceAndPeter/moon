// ARC reclamation engine test — moon fork, Phase 1.
//
// Exercises moonC_retain / moonC_release directly on real object graphs, with
// explicit ownership modelling (retain on store, release on drop), since the
// VM/stack is not yet ARC-instrumented. Verifies:
//   * an acyclic graph is reclaimed deterministically (whole subtree frees), and
//   * a reference cycle leaks (no free) without crashing.

#include <cassert>
#include <cstdio>

#include "moon.h"
#include "mauxlib.h"

#include "mstate.h"
#include "mobject.h"
#include "mtable.h"
#include "mstring.h"
#include "mgc.h"

static int failures = 0;
static void expect(bool cond, const char* what) {
  if (cond) { std::printf("  ok: %s\n", what); }
  else      { std::printf("  FAIL: %s\n", what); ++failures; }
}

// Store value 'v' into table 't' at integer key 'k'. Table::setInt now performs
// ARC accounting itself (retains the stored value), so no manual retain here.
static void storeInt(moon_State* L, Table* t, moon_Integer k, const TValue* v) {
  TValue tmp; tmp = *v;
  t->setInt(L, k, &tmp);
}

int main() {
  moon_State* L = moonL_newstate();
  if (L == nullptr) { std::printf("could not create state\n"); return 1; }

  // ---- Acyclic graph: root -> {childTable, string}; releasing root frees all 3.
  {
    unsigned long long before = moonC_deinitcount();

    Table* root = Table::create(L);              // refcount 1 (birth)
    Table* child = Table::create(L);             // refcount 1 (birth)
    TString* str = TString::create(L, "arc-leaf-unique-xyz");  // refcount 1 (birth)

    TValue v;
    sethvalue(L, &v, child); storeInt(L, root, 1, &v);  // setInt retains child -> rc 2
    setsvalue(L, &v, str);   storeInt(L, root, 2, &v);  // setInt retains str   -> rc 2

    // Drop the birth references of the children — root is now their sole owner.
    moonC_release(*L, obj2gco(child));           // child rc 1
    moonC_release(*L, obj2gco(str));             // str   rc 1

    // Drop root's last reference: frees root, then cascades to child and str.
    moonC_release(*L, obj2gco(root));

    unsigned long long freed = moonC_deinitcount() - before;
    std::printf("acyclic: reclaimed %llu objects (expected 3)\n", freed);
    expect(freed == 3, "acyclic graph fully reclaimed (root + child + leaf)");
  }

  // ---- Cycle: x <-> y. Dropping both birth refs leaves a self-sustaining
  //      cycle (each still has refcount 1 from the other) -> leaks, no crash.
  {
    unsigned long long before = moonC_deinitcount();

    Table* x = Table::create(L);                 // refcount 1
    Table* y = Table::create(L);                 // refcount 1

    TValue v;
    sethvalue(L, &v, y); storeInt(L, x, 1, &v);  // setInt retains y -> rc 2
    sethvalue(L, &v, x); storeInt(L, y, 1, &v);  // setInt retains x -> rc 2

    // Drop both birth references. Now x and y reference only each other.
    moonC_release(*L, obj2gco(x));               // x rc 1 (held by y)
    moonC_release(*L, obj2gco(y));               // y rc 1 (held by x)

    unsigned long long freed = moonC_deinitcount() - before;
    std::printf("cyclic: reclaimed %llu objects (expected 0 — cycle leaks)\n", freed);
    expect(freed == 0, "reference cycle leaks (not reclaimed) and does not crash");
  }

  moon_close(L);

  if (failures == 0) std::printf("ARC engine test: ALL OK\n");
  else               std::printf("ARC engine test: %d FAILURE(S)\n", failures);
  return failures == 0 ? 0 : 1;
}
