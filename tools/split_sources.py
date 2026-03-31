#!/usr/bin/env python3
"""Split large compiler/typechecker/parser .c files into .inc fragments (single TU each)."""

from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CODEGEN = ROOT / "src" / "codegen"
SEM = ROOT / "src" / "semantics"
SYN = ROOT / "src" / "syntax"


def write(path: Path, text: str) -> None:
    path.write_text(text, encoding="utf-8")
    print(f"Wrote {path.relative_to(ROOT)}")


def split_compiler() -> None:
    src = (CODEGEN / "compiler.c").read_text(encoding="utf-8").splitlines(keepends=True)
    emit = "".join(src[38:254])
    expr = "".join(src[255:1247])
    stmt = "".join(src[1248:2248])
    tail = "".join(src[19:26]) + "".join(src[2252:2329])
    write(CODEGEN / "compiler_emit.inc", emit)
    write(CODEGEN / "compiler_expr.inc", expr)
    write(CODEGEN / "compiler_stmt.inc", stmt)

    compiler_c = f'''/* AST → bytecode: expressions, control flow, functions/classes, enums, match,
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

{tail}'''
    write(CODEGEN / "compiler.c", compiler_c)


def split_typechecker() -> None:
    src = (SEM / "typechecker.c").read_text(encoding="utf-8").splitlines(keepends=True)
    err_sym = "".join(src[9:118])
    # Lines 119-405, excluding forward decls at 143-144
    narrow = "".join(src[118:142]) + "".join(src[144:405])
    resolve = "".join(src[405:555])
    # Lines 556-1476, excluding forward decls at 560-564
    expr = "".join(src[555:559]) + "".join(src[564:1476])
    # Lines 1477-2319, excluding forward decl at 1481
    stmt = "".join(src[1476:1480]) + "".join(src[1481:2319])
    tail = "".join(src[2319:2839])

    write(SEM / "typechecker_err_symbols.inc", err_sym)
    write(SEM / "typechecker_narrow.inc", narrow)
    write(SEM / "typechecker_resolve.inc", resolve)
    write(SEM / "typechecker_expr.inc", expr)
    write(SEM / "typechecker_stmt.inc", stmt)

    tc_c = f'''/* Typechecker implementation: walks AST, resolves names, checks calls,
 * inheritance, generic bounds, and records instantiations for monomorph. */

#include "typechecker.h"
#include "typechecker_internal.h"
#include "generic.h"
#include "memory.h"
#include "colors.h"
#include <stdarg.h>

static Type* checkExpr(TypeChecker* checker, Expr* expr);
static Type* checkMatchPatternExpr(TypeChecker* checker, Expr* expr, Type* valueType, int line);
static Type* checkMatchPatternCase(TypeChecker* checker, Expr* pattern, Token* destructureParams,
                                   int destructureCount, Type* valueType, int line);
static Type* checkMatch(TypeChecker* checker, Expr* expr);
static void checkStmt(TypeChecker* checker, Stmt* stmt);

#include "typechecker_err_symbols.inc"
#include "typechecker_narrow.inc"
#include "typechecker_resolve.inc"
#include "typechecker_expr.inc"
#include "typechecker_stmt.inc"

{tail}'''
    write(SEM / "typechecker.c", tc_c)


def split_parser() -> None:
    src = (SYN / "parser.c").read_text(encoding="utf-8").splitlines(keepends=True)
    infra = "".join(src[8:225])
    typ = "".join(src[226:394])
    expr = "".join(src[395:1060])
    stmt = "".join(src[1061:1994])
    tail = "".join(src[1995:2034])

    write(SYN / "parser_infra.inc", infra)
    write(SYN / "parser_type.inc", typ)
    write(SYN / "parser_expr.inc", expr)
    write(SYN / "parser_stmt.inc", stmt)

    p_c = f'''/* Parser implementation: grammar for declarations, types, expressions,
 * pattern forms; speculative parsing for generic call lookahead. */

#include "parser.h"
#include "parser_internal.h"
#include "memory.h"
#include "object.h"
#include "colors.h"

#include "parser_infra.inc"
#include "parser_type.inc"
#include "parser_expr.inc"
#include "parser_stmt.inc"

{tail}'''
    write(SYN / "parser.c", p_c)


def main() -> None:
    split_compiler()
    split_typechecker()
    split_parser()


if __name__ == "__main__":
    main()
