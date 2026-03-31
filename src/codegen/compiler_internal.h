#ifndef BLAZE_COMPILER_INTERNAL_H
#define BLAZE_COMPILER_INTERNAL_H

/* Shared types for compiler.c fragments (compiler_emit.inc, compiler_expr.inc, compiler_stmt.inc). */

#include "compiler.h"
#include "ast.h"

typedef struct ClassCompiler {
    struct ClassCompiler* enclosing;
    bool hasSuperclass;
} ClassCompiler;

#endif
