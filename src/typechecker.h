#ifndef blaze_typechecker_h
#define blaze_typechecker_h

#include "common.h"
#include "ast.h"
#include "types.h"

// Symbol table entry
typedef struct {
    const char* name;
    int nameLength;
    Type* type;
    bool isConst;
    int depth;          // Scope depth (0 = global)
} Symbol;

// Symbol table for tracking variables and their types
typedef struct {
    Symbol* symbols;
    int count;
    int capacity;
    int scopeDepth;
} SymbolTable;

// Type checker state
typedef struct {
    SymbolTable symbols;
    Type* currentFunctionReturn;    // Expected return type of current function
    Type* currentClass;             // Current class being checked (for 'this')
    bool hadError;
} TypeChecker;

// Initialize the type checker
void initTypeChecker(TypeChecker* checker);

// Free the type checker
void freeTypeChecker(TypeChecker* checker);

// Type check a list of statements (the program)
// Returns true if no type errors were found
bool typeCheck(TypeChecker* checker, Stmt** statements, int count);

// Convert a TypeNode (syntax) to a Type (semantic)
Type* resolveTypeNode(TypeChecker* checker, TypeNode* node);

// Define a symbol in the type checker's symbol table
void defineSymbol(TypeChecker* checker, const char* name, int length,
                  Type* type, bool isConst);

#endif
