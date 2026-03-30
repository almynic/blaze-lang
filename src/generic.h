#ifndef blaze_generic_h
#define blaze_generic_h

/* Generic monomorph pass: after typechecking, emit concrete ClassStmt nodes
 * per instantiation so the compiler sees ordinary classes. */

#include "ast.h"
#include "typechecker.h"

// Build one concrete ClassStmt per generic instantiation (monomorph).
// Caller must free each Stmt with freeLoweredMonomorphClassStmt, then FREE_ARRAY the pointer array.
bool lowerGenericClasses(TypeChecker* checker, Stmt*** outLowered, int* outCount);

// Free a lowered monomorph class; method bodies are NOT freed (still owned by template AST).
void freeLoweredMonomorphClassStmt(Stmt* stmt);

#endif
