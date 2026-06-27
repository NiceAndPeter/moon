/*
** Lua Parser - Label and Goto Management
** See Copyright Notice in lua.h
*/

#define MOON_CORE

#include "mprefix.h"


#include <cstring>

#include "moon.h"

#include "mdebug.h"
#include "mdo.h"
#include "mfunc.h"
#include "mlex.h"
#include "mmem.h"
#include "mobject.h"
#include "mopcodes.h"
#include "mparser.h"
#include "mstate.h"
#include "mstring.h"
#include "mtable.h"


/* because all strings are unified by the scanner, the parser
   can use pointer equality for string equality */
inline bool eqstr(const TString& a, const TString& b) noexcept {
	return (&a) == (&b);
}


/*
** nodes for block list (list of active blocks)
*/
typedef struct BlockCnt {
  struct BlockCnt *previous;  // chain
  int firstlabel;  // index of first label in this block
  int firstgoto;  // index of first pending goto in this block
  short numberOfActiveVariables;  // number of active declarations at block entry
  lu_byte upval;  // true if some variable in the block is an upvalue
  lu_byte isloop;  // 1 if 'block' is a loop; 2 if it has pending breaks
  lu_byte insidetbc;  // true if inside the scope of a to-be-closed var.
} BlockCnt;


/*
** Generates an error that a goto jumps into the scope of some
** variable declaration.
*/
l_noret LexState::jumpscopeerror(FuncState *funcState, Labeldesc *gt) {
  TString *tsname = funcState->getlocalvardesc(gt->numberOfActiveVariables)->vd.name;
  const char *varname = (tsname != nullptr) ? getStringContents(tsname) : "*";
  semerror("<goto %s> at line %d jumps into the scope of '%s'",
           getStringContents(gt->name), gt->line, varname);  // raise the error
}


/*
** Closes the goto at index 'g' to given 'label' and removes it
** from the list of pending gotos.
** If it jumps into the scope of some variable, raises an error.
** The goto needs a CLOSE if it jumps out of a block with upvalues,
** or out of the scope of some variable and the block has upvalues
** (signaled by parameter 'bup').
*/
void LexState::closegoto(FuncState *funcState, int g, Labeldesc *label, int bup) {
  int i;
  Labellist *gl = &getDyndata()->gt;  // list of gotos
  Labeldesc *gt = &(*gl)[g];  // goto to be resolved
  moon_assert(eqstr(*gt->name, *label->name));
  if (l_unlikely(gt->numberOfActiveVariables < label->numberOfActiveVariables))  // enter some scope?
    jumpscopeerror(funcState, gt);
  if (gt->close ||
      (label->numberOfActiveVariables < gt->numberOfActiveVariables && bup)) {  // needs close?
    lu_byte stklevel = funcState->reglevel(label->numberOfActiveVariables);
    // move jump to CLOSE position
    funcState->getProto().getCode()[gt->pc + 1] = funcState->getProto().getCode()[gt->pc];
    // put CLOSE instruction at original position
    funcState->getProto().getCode()[gt->pc] = CREATE_ABCk(OP_CLOSE, stklevel, 0, 0, 0);
    gt->pc++;  // must point to jump instruction
  }
  funcState->patchlist(gt->pc, label->pc);  // goto jumps to label
  for (i = g; i < gl->getN() - 1; i++)  // remove goto from pending list
    (*gl)[i] = (*gl)[i + 1];
  gl->setN(gl->getN() - 1);
}


/*
** Search for an active label with the given name, starting at
** index 'ilb' (so that it can search for all labels in current block
** or all labels in current function).
*/
Labeldesc *LexState::findlabel(TString* name, int ilb) {
  Dyndata *dynData = getDyndata();
  for (; ilb < dynData->label.getN(); ilb++) {
    Labeldesc *lb = &dynData->label[ilb];
    if (eqstr(*lb->name, *name))  // correct label?
      return lb;
  }
  return nullptr;  // label not found
}


/*
** Adds a new label/goto in the corresponding list.
*/
int LexState::newlabelentry(FuncState *funcState, Labellist *l, TString* name, int line, int pc) {
  int n = l->getN();
  Labeldesc* desc = l->allocateNew();  // MoonVector automatically grows
  desc->name = name;
  desc->line = line;
  desc->numberOfActiveVariables = funcState->getNumActiveVars();
  desc->close = 0;
  desc->pc = pc;
  return n;
}


/*
** Create a new label with the given 'name' at the given 'line'.
** 'last' tells whether label is the last non-op statement in its
** block. Solves all pending gotos to this new label and adds
** a close instruction if necessary.
** Returns true iff it added a close instruction.
*/
void LexState::createlabel(FuncState *funcState, TString *name, int line, int last) {
  // FuncState passed as parameter
  Labellist *ll = &getDyndata()->label;
  int l = newlabelentry(funcState, ll, name, line, funcState->getlabel());
  if (last) {  // label is last no-op statement in the block?
    // assume that locals are already out of scope
    (*ll)[l].numberOfActiveVariables = funcState->getBlock()->numberOfActiveVariables;
  }
}


/*
** generates an error for an undefined 'goto'.
*/
l_noret LexState::undefgoto([[maybe_unused]] FuncState *funcState, Labeldesc *gt) {
  // breaks are checked when created, cannot be undefined
  moon_assert(!eqstr(*gt->name, *getBreakName()));
  semerror("no visible label '%s' for <goto> at line %d",
           getStringContents(gt->name), gt->line);
}
