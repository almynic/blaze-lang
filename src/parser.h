#ifndef blaze_parser_h
#define blaze_parser_h

#include "common.h"
#include "scanner.h"
#include "ast.h"

typedef struct {
    Scanner scanner;
    Token current;
    Token previous;
    bool hadError;
    bool panicMode;
    const char* source;  // Source code for error context display
} Parser;

// Initialize the parser with source code
void initParser(Parser* parser, const char* source);

// Parse and return a list of statements (the program)
// Returns NULL on error, caller must free the statements
Stmt** parse(Parser* parser, int* stmtCount);

// Free a list of statements
void freeStatements(Stmt** statements, int count);

#endif
