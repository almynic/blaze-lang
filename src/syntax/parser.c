/* Parser implementation: grammar for declarations, types, expressions,
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

// ============================================================================
// Public API
// ============================================================================

Stmt** parse(Parser* parser, int* stmtCount) {
    advance(parser); // Prime the parser

    Stmt** statements = NULL;
    int count = 0;
    int capacity = 0;

    while (!check(parser, TOKEN_EOF)) {
        Stmt* stmt = declaration(parser);
        if (stmt != NULL) {
            if (count >= capacity) {
                int oldCapacity = capacity;
                capacity = GROW_CAPACITY(oldCapacity);
                statements = GROW_ARRAY(Stmt*, statements, oldCapacity, capacity);
            }
            statements[count++] = stmt;
        }
    }

    *stmtCount = count;

    if (parser->hadError) {
        freeStatements(statements, count);
        return NULL;
    }

    return statements;
}

void freeStatements(Stmt** statements, int count) {
    for (int i = 0; i < count; i++) {
        freeStmt(statements[i]);
    }
    FREE_ARRAY(Stmt*, statements, count);
}
