#include "astutil.h"
#include "bb.h"
#include "expr.h"
#include "optimizations.h"
#include "stmt.h"
#include "view.h"

//#define DEBUG_CP

bool isCandidateForCopyPropagation(FnSymbol* fn, VarSymbol* var) {
  return
    var != fn->getReturnSymbol() &&
    !is_complex_type(var->type) &&
    var->type->refType &&
    !var->isConcurrent;
}

bool invalidateCopies(SymExpr* se, Vec<SymExpr*>& defSet) {
  if (defSet.set_in(se))
    return true;
  if (CallExpr* parent = dynamic_cast<CallExpr*>(se->parentExpr)) {
    if (parent->isPrimitive(PRIMITIVE_SET_REF))
      return true;
    if (isRecordType(se->var->type)) {
      if (parent->isPrimitive(PRIMITIVE_GET_MEMBER))
        return true;
      if (parent->isPrimitive(PRIMITIVE_SET_MEMBER) && parent->get(1) == se)
        return true;
    }
  }
  return false;
}

typedef Map<Symbol*,Symbol*> AvailableMap;
typedef MapElem<Symbol*,Symbol*> AvailableMapElem;

static void
localCopyPropagationCore(BasicBlock* bb,
                         AvailableMap& available,
                         Vec<SymExpr*>& useSet,
                         Vec<SymExpr*>& defSet) {

  forv_Vec(Expr, expr, bb->exprs) {

    Vec<BaseAST*> asts;
    collect_asts(&asts, expr);

    //
    // replace uses with available copies
    //
    forv_Vec(BaseAST, ast, asts) {
      if (SymExpr* se = dynamic_cast<SymExpr*>(ast)) {
        if (useSet.set_in(se)) {
          if (Symbol* sym = available.get(se->var)) {
            if (!invalidateCopies(se, defSet))
              se->var = sym;
          }
        }
      }
    }

    //
    // invalidate available copies based on defs
    //  also if a reference to a variable is taken since the reference
    //  can be assigned
    //
    forv_Vec(BaseAST, ast, asts) {
      if (SymExpr* se = dynamic_cast<SymExpr*>(ast)) {
        if (invalidateCopies(se, defSet)) {
          form_Map(AvailableMapElem, pair, available) {
            if (pair->key == se->var || pair->value == se->var)
              available.put(pair->key, NULL);
          }
        }
      }
    }

    //
    // insert pairs into available copies map
    //
    if (CallExpr* call = dynamic_cast<CallExpr*>(expr))
      if (call->isPrimitive(PRIMITIVE_MOVE))
        if (SymExpr* lhs = dynamic_cast<SymExpr*>(call->get(1)))
          if (SymExpr* rhs = dynamic_cast<SymExpr*>(call->get(2)))
            if (lhs->var != rhs->var &&
                defSet.set_in(lhs) &&
                (useSet.set_in(rhs) || rhs->var->isConst() || rhs->var->isImmediate()))
              available.put(lhs->var, rhs->var);
  }
}


//
// Apply local copy propagation to basic blocks of function
//
void localCopyPropagation(FnSymbol* fn) {
  buildBasicBlocks(fn);
  compute_sym_uses(fn);

  //
  // locals: a vector of local variables in function fn that are
  // candidates for copy propagation
  //
  Vec<Symbol*> locals;
  forv_Vec(BasicBlock, bb, *fn->basicBlocks) {
    forv_Vec(Expr, expr, bb->exprs) {
      if (DefExpr* def = dynamic_cast<DefExpr*>(expr))
        if (VarSymbol* var = dynamic_cast<VarSymbol*>(def->sym))
          if (isCandidateForCopyPropagation(fn, var))
            locals.add(def->sym);
    }
  }

  //
  // useSet: a set of SymExprs that are uses of local variables
  // defSet: a set of SymExprs that are defs of local variables
  //
  Vec<SymExpr*> useSet;
  Vec<SymExpr*> defSet;
  forv_Vec(Symbol, local, locals) {
    forv_Vec(SymExpr, se, local->defs) {
      defSet.set_add(se);
    } 
    forv_Vec(SymExpr, se, local->uses) {
      useSet.set_add(se);
    }
  }

  forv_Vec(BasicBlock, bb, *fn->basicBlocks) {
    AvailableMap available;
    localCopyPropagationCore(bb, available, useSet, defSet);
  }
}


#ifdef DEBUG_CP
static void
debug_flow_print_set(Vec<Vec<bool>*>& sets) {
  int i = 0;
  forv_Vec(Vec<bool>, set, sets) {
    printf("%d: ", i);
    for (int j = 0; j < set->n; j++)
      printf("%d", set->v[j] ? 1 : 0);
    printf("\n");
    i++;
  }
  printf("\n");
}
#endif


//
// Apply global copy propagation to basic blocks of function
//
void globalCopyPropagation(FnSymbol* fn) {
  buildBasicBlocks(fn);

  // global copy propagation will have no effect
  if (fn->basicBlocks->n <= 1)
    return;

  compute_sym_uses(fn);

  //
  // locals: a vector of local variables in function fn that are
  // candidates for copy propagation
  //
  Vec<Symbol*> locals;
  forv_Vec(BasicBlock, bb, *fn->basicBlocks) {
    forv_Vec(Expr, expr, bb->exprs) {
      if (DefExpr* def = dynamic_cast<DefExpr*>(expr))
        if (VarSymbol* var = dynamic_cast<VarSymbol*>(def->sym))
          if (isCandidateForCopyPropagation(fn, var))
            locals.add(def->sym);
    }
  }

  //
  // useSet: a set of SymExprs that are uses of local variables
  // defSet: a set of SymExprs that are defs of local variables
  //
  Vec<SymExpr*> useSet;
  Vec<SymExpr*> defSet;
  forv_Vec(Symbol, local, locals) {
    forv_Vec(SymExpr, se, local->defs) {
      defSet.set_add(se);
    } 
    forv_Vec(SymExpr, se, local->uses) {
      useSet.set_add(se);
    }
  }

  //
  // _LHS: left-hand side of copy
  // _RHS: right-hand side of copy
  // _N: basic block locations; basic block i contains the copies from
  //     _N(i-1) to _N(i)
  //
  // Compute sparse versions of these vectors; convert to dense and
  // drop _; these are sparse to zero out invalidated copies
  //
  Vec<int> _N;
  Vec<SymExpr*> _LHS;
  Vec<SymExpr*> _RHS;
  int start = 0;
  forv_Vec(BasicBlock, bb, *fn->basicBlocks) {
    forv_Vec(Expr, expr, bb->exprs) {

      Vec<BaseAST*> asts;
      collect_asts(&asts, expr);

      //
      // invalidate available copies based on defs
      //
      forv_Vec(BaseAST, ast, asts) {
        if (SymExpr* se = dynamic_cast<SymExpr*>(ast)) {
          if (invalidateCopies(se, defSet)) {
            for (int i = start; i < _LHS.n; i++) {
              if (_LHS.v[i]) {
                if (_LHS.v[i]->var == se->var || _RHS.v[i]->var == se->var) {
                  _LHS.v[i] = 0;
                  _RHS.v[i] = 0;
                }
              }
            }
          }
        }
      }

      //
      // insert pairs into available copies map
      //
      if (CallExpr* call = dynamic_cast<CallExpr*>(expr))
        if (call->isPrimitive(PRIMITIVE_MOVE))
          if (SymExpr* lhs = dynamic_cast<SymExpr*>(call->get(1)))
            if (SymExpr* rhs = dynamic_cast<SymExpr*>(call->get(2)))
              if (lhs->var != rhs->var &&
                  defSet.set_in(lhs) &&
                  (useSet.set_in(rhs) || rhs->var->isConst() || rhs->var->isImmediate())) {
                _LHS.add(lhs);
                _RHS.add(rhs);
              }
    }
    _N.add(_LHS.n);
    start = _LHS.n;
  }

#ifdef DEBUG_CP
  printf("\n");
  list_view(fn);

  printBasicBlocks(fn);
#endif

  //
  // Turn sparse _LHS, _RHS, _N into dense LHS, RHS, N
  //
  Vec<int> N;
  Vec<SymExpr*> LHS;
  Vec<SymExpr*> RHS;

  int j = 0;
  int i = 0;
  forv_Vec(BasicBlock, bb, *fn->basicBlocks) {
#ifdef DEBUG_CP
    printf("%d:\n", i);
#endif
    while (j < _N.v[i]) {
      if (_LHS.v[j]) {
#ifdef DEBUG_CP
        list_view(_LHS.v[j]->parentExpr);
#endif
        LHS.add(_LHS.v[j]);
        RHS.add(_RHS.v[j]);
      }
      j++;
    }
    N.add(LHS.n);
    i++;
  }
#ifdef DEBUG_CP
  printf("\n");
#endif

  Vec<Vec<bool>*> COPY;
  Vec<Vec<bool>*> KILL;
  Vec<Vec<bool>*> IN;
  Vec<Vec<bool>*> OUT;
  j = 0;
  i = 0;
  forv_Vec(BasicBlock, bb, *fn->basicBlocks) {
    Vec<bool>* copy = new Vec<bool>();
    Vec<bool>* kill = new Vec<bool>();
    Vec<bool>* in = new Vec<bool>();
    Vec<bool>* out = new Vec<bool>();
    for (int k = 0; k < N.v[fn->basicBlocks->n-1]; k++) {
      copy->add(false);
      kill->add(false);
      in->add(false);
      out->add(false);
    }
    while (j < N.v[i]) {
      copy->v[j] = true;
      j++;
    }
    COPY.add(copy);
    KILL.add(kill);
    IN.add(in);
    OUT.add(out);
    i++;
  }

#ifdef DEBUG_CP
  printf("COPY:\n"); debug_flow_print_set(COPY);
#endif

  //
  // compute kill set
  //
  i = 0;
  start = 0;
  forv_Vec(BasicBlock, bb, *fn->basicBlocks) {
    int stop = N.v[i];
    forv_Vec(Expr, expr, bb->exprs) {

      Vec<BaseAST*> asts;
      collect_asts(&asts, expr);

      //
      // invalidate available copies based on defs
      //
      forv_Vec(BaseAST, ast, asts) {
        if (SymExpr* se = dynamic_cast<SymExpr*>(ast)) {
          if (invalidateCopies(se, defSet)) {
            for (int j = 0; j < start; j++) {
              if (LHS.v[j]->var == se->var || RHS.v[j]->var == se->var) {
                KILL.v[i]->v[j] = true;
              }
            }
            for (int j = stop; j < LHS.n; j++) {
              if (LHS.v[j]->var == se->var || RHS.v[j]->var == se->var) {
                KILL.v[i]->v[j] = true;
              }
            }
          }
        }
      }
    }
    start = stop;
    i++;
  }

#ifdef DEBUG_CP
  printf("KILL:\n"); debug_flow_print_set(KILL);
#endif

  // initialize IN set
  for (int i = 1; i < fn->basicBlocks->n; i++) {
    for (int j = 0; j < LHS.n; j++) {
      IN.v[i]->v[j] = true;
    }
  }

#ifdef DEBUG_CP
  printf("IN:\n"); debug_flow_print_set(IN);
#endif

  forwardFlowAnalysis(fn, COPY, KILL, IN, OUT, true);


  for (int i = 0; i < fn->basicBlocks->n; i++) {
    BasicBlock* bb = fn->basicBlocks->v[i];
    AvailableMap available;
    bool proceed = false;
    for (int j = 0; j < LHS.n; j++) {
      if (IN.v[i]->v[j]) {
        available.put(LHS.v[j]->var, RHS.v[j]->var);
        proceed = true;
      }
    }
    if (proceed)
      localCopyPropagationCore(bb, available, useSet, defSet);
  }

  forv_Vec(Vec<bool>, copy, COPY)
    delete copy;

  forv_Vec(Vec<bool>, kill, KILL)
    delete kill;

  forv_Vec(Vec<bool>, in, IN)
    delete in;

  forv_Vec(Vec<bool>, out, OUT)
    delete out;
}


void singleAssignmentRefPropagation() {
  compute_sym_uses();

  forv_Vec(BaseAST, ast, gAsts) {
    if (VarSymbol* var = dynamic_cast<VarSymbol*>(ast)) {
      if (isReference(var->type) && var->defs.n == 1) {
        if (CallExpr* move = dynamic_cast<CallExpr*>(var->defs.v[0]->parentExpr)) {
          if (move->isPrimitive(PRIMITIVE_MOVE)) {
            if (SymExpr* rhs = dynamic_cast<SymExpr*>(move->get(2))) {
              if (isReference(rhs->var->type)) {
                forv_Vec(SymExpr, se, var->uses) {
                  SymExpr* rhsCopy = rhs->copy();
                  se->replace(rhsCopy);
                  rhsCopy->var->uses.add(rhsCopy);
                }
                var->defPoint->remove();
                move->remove();
              }
            }
          }
        }
      }
    }
  }

  compute_sym_uses();
  forv_Vec(BaseAST, ast, gAsts) {
    if (VarSymbol* var = dynamic_cast<VarSymbol*>(ast)) {
      if (isReference(var->type) && var->defs.n == 1) {
        if (CallExpr* move = dynamic_cast<CallExpr*>(var->defs.v[0]->parentExpr)) {
          if (move->isPrimitive(PRIMITIVE_MOVE)) {
            if (CallExpr* rhs = dynamic_cast<CallExpr*>(move->get(2))) {
              if (rhs->isPrimitive(PRIMITIVE_SET_REF)) {
                bool stillAlive = false;
                forv_Vec(SymExpr, se, var->uses) {
                  CallExpr* parent = dynamic_cast<CallExpr*>(se->parentExpr);
                  if (parent && parent->isPrimitive(PRIMITIVE_GET_REF)) {
                    parent->replace(rhs->get(1)->copy());
                  } else if (parent &&
                             (parent->isPrimitive(PRIMITIVE_GET_MEMBER_VALUE) ||
                              parent->isPrimitive(PRIMITIVE_GET_MEMBER))) {
                    parent->get(1)->replace(rhs->get(1)->copy());
                  } else if (parent && parent->isPrimitive(PRIMITIVE_MOVE)) {
                    parent->get(2)->replace(rhs->copy());
                  } else
                    stillAlive = true;
                }
                if (!stillAlive) {
                  var->defPoint->remove();
                  var->defs.v[0]->getStmtExpr()->remove();
                }
              } else if (rhs->isPrimitive(PRIMITIVE_GET_MEMBER)) {
                bool stillAlive = false;
                forv_Vec(SymExpr, se, var->uses) {
                  CallExpr* parent = dynamic_cast<CallExpr*>(se->parentExpr);
                  if (parent && parent->isPrimitive(PRIMITIVE_GET_REF)) {
                    parent->replace(new CallExpr(PRIMITIVE_GET_MEMBER_VALUE,
                                                 rhs->get(1)->copy(),
                                                 rhs->get(2)->copy()));
                  } else if (parent && parent->isPrimitive(PRIMITIVE_MOVE)) {
                    parent->get(2)->replace(rhs->copy());
                  } else
                    stillAlive = true;
                }
                if (!stillAlive) {
                  var->defPoint->remove();
                  var->defs.v[0]->getStmtExpr()->remove();
                }
              }
            }
          }
        }
      }
    }
  }
}


void copyPropagation(void) {
  if (fBaseline)
    return;
  forv_Vec(FnSymbol, fn, gFns) {
    if (!fNoCopyPropagation)
      localCopyPropagation(fn);
    deadVariableElimination(fn);
    deadExpressionElimination(fn);
    if (!fNoCopyPropagation && !fNoFlowAnalysis) {
      globalCopyPropagation(fn);
      deadVariableElimination(fn);
      deadExpressionElimination(fn);
    }
  }
}


void refPropagation() {
  if (fBaseline)
    return;
  singleAssignmentRefPropagation();
  forv_Vec(FnSymbol, fn, gFns) {
    deadVariableElimination(fn);
    deadExpressionElimination(fn);
  }
}
