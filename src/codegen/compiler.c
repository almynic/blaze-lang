/* AST → bytecode: expressions, control flow, functions/classes, enums, match,
 * generics (prepended lowered classes). Emits OP_* into Chunk constant pools. */

#include "compiler.h"
#include "compiler_internal.h"
#include "memory.h"
#include "object.h"
#include "types.h"
#include <stdio.h>
#include <string.h>

Compiler* compiler_current = NULL;
ClassCompiler* compiler_class_current = NULL;
bool compiler_had_error = false;

#define current compiler_current
#define currentClass compiler_class_current
#define hadError compiler_had_error

static ObjFunction* endCompiler(int line);
static bool compileStmt(Stmt* stmt);
static void emitGetGlobalForClassType(Type* t, int line);
static void compileExpr(Expr* expr);
static void compileMatchExpr(Expr* expr);

#include "compiler_emit.inc"
#include "compiler_expr.inc"
#include "compiler_stmt.inc"

void markCompilerRoots(void) {
    Compiler* compiler = current;
    while (compiler != NULL) {
        markObject((Obj*)compiler->function);
        compiler = compiler->enclosing;
    }
}
void initCompiler(Compiler* compiler, Compiler* enclosing, CompilerFnType type) {
    compiler->enclosing = enclosing;
    compiler->function = NULL;
    compiler->type = type;
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
    compiler->currentFunctionReturn = NULL;
    compiler->activeFinallyCount = 0;

    compiler->function = newFunction();

    current = compiler;

    // Reserve slot 0 for the function itself or 'this' in methods
    Local* local = &current->locals[current->localCount++];
    local->depth = 0;
    local->isCaptured = false;
    local->isConst = true;
    local->type = NULL;
    if (type != FN_TYPE_FUNCTION) {
        local->name.start = "this";
        local->name.length = 4;
    } else {
        local->name.start = "";
        local->name.length = 0;
    }
}

static ObjFunction* compileImpl(Stmt** prepend, int prependCount,
                                Stmt** statements, int count, bool replMode) {
    Compiler compiler;
    initCompiler(&compiler, NULL, FN_TYPE_SCRIPT);
    hadError = false;

    // Emit monomorphized classes at scope depth 0 so they become true globals.
    for (int i = 0; i < prependCount; i++) {
        compileClassStmt(prepend[i]);
    }

    if (!replMode) {
        beginScope();
    }

    for (int i = 0; i < count; i++) {
        bool isTerminator = compileStmt(statements[i]);

        if (isTerminator && i + 1 < count) {
            Stmt* nextStmt = statements[i + 1];
            if (nextStmt != NULL) {
                fprintf(stderr, "Warning [line %d]: Unreachable code after return/throw.\n",
                        nextStmt->line);
            }
            break;
        }
    }

    ObjFunction* function = endCompiler(0);
    return hadError ? NULL : function;
}

ObjFunction* compile(Stmt** statements, int count) {
    return compileImpl(NULL, 0, statements, count, false);
}

ObjFunction* compileWithPrependedClasses(Stmt** prepend, int prependCount,
                                         Stmt** statements, int count) {
    return compileImpl(prepend, prependCount, statements, count, false);
}

ObjFunction* compileRepl(Stmt** statements, int count) {
    return compileImpl(NULL, 0, statements, count, true);
}

ObjFunction* compileReplWithPrependedClasses(Stmt** prepend, int prependCount,
                                             Stmt** statements, int count) {
    return compileImpl(prepend, prependCount, statements, count, true);
}
