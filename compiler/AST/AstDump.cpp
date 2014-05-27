#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

#include "AstDump.h"

#include "driver.h"
#include "expr.h"
#include "stmt.h"
#include "stringutil.h"
#include "symbol.h"


AstDump::AstDump() {
  mName      =     0;
  mPath      =     0;
  mFP        =     0;
  mIndent    =     0;
  mNeedSpace = false;
}

AstDump::~AstDump() {
  close();
}

void AstDump::view(const char* passName, int passNum) {
  forv_Vec(ModuleSymbol, module, allModules) {
    if (log_module[0] == '\0' || strcmp(log_module, module->name) == 0) {
      AstDump logger;

      if (logger.open(module, passName, passNum) == true) {
        module->accept(&logger);
        logger.close();
      }
    }
  }
}

bool AstDump::open(const ModuleSymbol* module, const char* passName, int passNum) {
  char numBuf[4];

  snprintf(numBuf, 4, "%02d", passNum);

  mName      = astr(module->name, "_", numBuf, passName, ".ast");
  mPath      = astr(log_dir, mName);
  mFP        = fopen(mPath, "w");
  mIndent    = 0;
  mNeedSpace = false;

  if (mFP != 0) {
    fprintf(mFP, "AST dump for %s after pass %s.\n", module->name, passName);
  }

  return (mFP != 0) ? true : false;
}

bool AstDump::close() {
  bool retval = false;
  
  if (mFP != 0 && fclose(mFP) == 0) {
    mFP    =    0;
    retval = true;
  }

  return retval;
}

//
// ArgSymbol
//
bool AstDump::visitEnter(ArgSymbol* node) {
  return false;
}

//
// CallExpr
//
bool AstDump::visitEnter(CallExpr* node) {
  if (isBlockStmt(node->parentExpr)) {
    newline();
  }

  if (FnSymbol* fn = node->isResolved()) {
    if (fn->hasFlag(FLAG_BEGIN_BLOCK))
      write("begin");
    else if (fn->hasFlag(FLAG_ON_BLOCK))
      write("on");
  }

  if (fLogIds) {
    fprintf(mFP, "(%d ", node->id);
    mNeedSpace = false;
  } else {
    write(true, "(", false);
  }

  if (!node->primitive) {
    write("call");
  } else {
    if (node->isPrimitive(PRIM_RETURN))
      write("return");

    else if (node->isPrimitive(PRIM_YIELD))
      write("yield");

    else {
      if (mNeedSpace)
        fputc(' ', mFP);

      fprintf(mFP, "'%s'", node->primitive->name);

      mNeedSpace = true;
    }
  }

  if (node->partialTag)
    write("(partial)");

  return true;
}

void AstDump::visitExit(CallExpr* node) {
  write(false, ")", true);
}


//
// DefExpr
//
bool AstDump::visitEnter(DefExpr* node) {
  Symbol* sym    = node->sym;
  bool    retval = true;

  if (isModuleSymbol(sym)) {
    retval = false;

  } else {
    if (isBlockStmt(node->parentExpr)) {
      newline();
    }

    if (FnSymbol* fn = toFnSymbol(sym)) {
      writeFnSymbol(fn);

    } else if (isTypeSymbol(sym)) {
      if (toAggregateType(sym->type)) {
        if (sym->hasFlag(FLAG_SYNC))
          write("sync");

        if (sym->hasFlag(FLAG_SINGLE))
          write("single");
      }

      writeSymbol("type", sym, true);

    } else if (VarSymbol* vs = toVarSymbol(sym)) {
      if (vs->type->symbol->hasFlag(FLAG_SYNC))
        write("sync");

      if (vs->type->symbol->hasFlag(FLAG_SINGLE))
        write("single");

      writeSymbol("var", sym, true);

    } else if (isLabelSymbol(sym)) {
      writeSymbol("label", sym, true);

    } else if (isEnumSymbol(sym)) {
      writeSymbol("def", sym, true);

    }
  }

  return retval;
}

//
// NamedExpr
//
bool AstDump::visitEnter(NamedExpr* node) {
  if (isBlockStmt(node->parentExpr)) {
    newline();
  }

  fprintf(mFP, "(%s =", node->name);
  mNeedSpace = true;

  return true;
}

void AstDump::visitExit(NamedExpr* node) {
  write(false, ")", true);
}


//
// SymExpr
//
void AstDump::visit(SymExpr* node) {
  Symbol*    sym = node->var;
  VarSymbol* var = toVarSymbol(sym);

  if (isBlockStmt(node->parentExpr) == true) {
    newline();
  }

  if (var != 0 && var->immediate != 0) {
    const size_t bufSize = 128;
    char         imm[bufSize];
    char         buff[bufSize];

    snprint_imm(imm, bufSize, *var->immediate);
    sprintf(buff, "%s%s", imm, is_imag_type(var->type) ? "i" : "");

    write(buff);

  } else {
    writeSymbol(sym, false);
  }
}


//
// UnresolvedSymExpr
//
void AstDump::visit(UnresolvedSymExpr* node) {
  if (isBlockStmt(node->parentExpr)) {
    newline();
  }

  write(node->unresolved);
}


//
// BlockStmt
//
bool AstDump::visitEnter(BlockStmt* node) {
  newline();

  if (FnSymbol* fn = toFnSymbol(node->parentSymbol))
    if (node == fn->where)
      write(false, "where ", false);

  write("{");
  printBlockID(node);
  ++mIndent;

  return true;
}

void AstDump::visitExit(BlockStmt* node) {
  --mIndent;
  newline();
  write(false, "}", true);
  printBlockID(node);
}


//
// CondStmt
//
bool AstDump::visitEnter(CondStmt* node) {
  newline();
  write("if");

  return true;
}

//
// ExternBlockStmt
//
void AstDump::visit(ExternBlockStmt* node) {
  write("(ExternBlockStmt");
}


//
// GotoStmt
//
bool AstDump::visitEnter(GotoStmt* node) {
  switch (node->gotoTag) {
    case GOTO_NORMAL:      write("goto");           break;
    case GOTO_BREAK:       write("break");          break;
    case GOTO_CONTINUE:    write("continue");       break;
    case GOTO_RETURN:      write("gotoReturn");     break;
    case GOTO_GETITER_END: write("gotoGetiterEnd"); break;
    case GOTO_ITER_RESUME: write("gotoIterResume"); break;
    case GOTO_ITER_END:    write("gotoIterEnd");    break;
  }

  if (SymExpr* label = toSymExpr(node->label)) {
    if (label->var != gNil) {
      writeSymbol(label->var, true);
    }
  }

  return true;
}

//
// Helper functions
//

void AstDump::writeFnSymbol(FnSymbol* fn) {
  bool first = true;

  write("function");

  if (fn->_this && fn->_this->defPoint) {
    writeSymbol(fn->_this->type->symbol, false);
    write(false, ".", false);
  }

  writeSymbol(fn, true);

  write(false, "(", false);

  for_formals(formal, fn) {
    if (first) {
      first = false;
    } else {
      write(false, ",", true);
    }

    writeSymbol(formal, true);

    if (formal->typeExpr) {
      write(":");

      if (BlockStmt* block = toBlockStmt(formal->typeExpr))
        block->body.first()->accept(this);
      else
        formal->typeExpr->accept(this);
    }

    if (formal->defaultExpr) {
      write("=");

      if (BlockStmt* block = toBlockStmt(formal->defaultExpr))
        block->body.first()->accept(this);
      else
        formal->defaultExpr->accept(this);
    }
  }

  write( false, ")", true);

  switch (fn->retTag) {
    case RET_VALUE:                 break;
    case RET_VAR:   write("var");   break;
    case RET_PARAM: write("param"); break;
    case RET_TYPE:  write("type");  break;
  }

  if (fn->retType && fn->retType->symbol) {
    writeSymbol(":", fn->retType->symbol, false);
  }
}

void AstDump::writeSymbol(const char* tag, Symbol* sym, bool def) {
  write(tag);
  writeSymbol(sym, def);
}

void AstDump::writeSymbol(Symbol* sym, bool def) {
  if (def) {
    if (ArgSymbol* arg = toArgSymbol(sym)) {
      switch (arg->intent) {
        case INTENT_IN:        write("in arg");        break;
        case INTENT_INOUT:     write("inout arg");     break;
        case INTENT_OUT:       write("out arg");       break;
        case INTENT_CONST:     write("const arg");     break;
        case INTENT_CONST_IN:  write("const in arg");  break;
        case INTENT_CONST_REF: write("const ref arg"); break;
        case INTENT_REF:       write("ref arg");       break;
        case INTENT_PARAM:     write("param arg");     break;
        case INTENT_TYPE:      write("type arg");      break;
        case INTENT_BLANK:     write("arg");           break;
      }
    }
  }

  write(sym->name);

  if (fLogIds)
    fprintf(mFP, "[%d]", sym->id);

  if (def &&
      !toTypeSymbol(sym) &&
      sym->type &&
      sym->type->symbol &&
      sym->type != dtUnknown) {
    write(false, ":", false);
    writeSymbol(sym->type->symbol, false);
  }

  if (sym->hasFlag(FLAG_GENERIC))
    write(false, "?", false);

  mNeedSpace = true;
}

void AstDump::write(const char* text) {
  write(true, text, true);
}

void AstDump::write(bool spaceBefore, const char* text, bool spaceAfter) {
  if (spaceBefore == true && mNeedSpace == true)
    fputc(' ', mFP);

  fputs(text, mFP);

  mNeedSpace = spaceAfter;
}
  
void AstDump::printBlockID(Expr* expr) {
  if (fdump_html_print_block_IDs)
    fprintf(mFP, " %d", expr->id);
}

void AstDump::newline() {
  fputc('\n', mFP);

  for (int i = 0; i < 2 * mIndent; ++i)
    fputc(' ', mFP);

  mNeedSpace = false;
}