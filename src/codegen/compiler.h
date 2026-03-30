#ifndef blaze_compiler_h
#define blaze_compiler_h

/* Single-pass compiler state: locals, upvalues, nested Compiler chain.
 * `compile` emits bytecode into ObjFunction chunks. */

#include "common.h"
#include "ast.h"
#include "chunk.h"
#include "types.h"
#include "object.h"

// Local variable in compiler
typedef struct {
    Token name;
    int depth;
    Type* type;
    bool isConst;
    bool isCaptured;    // Is this local captured by a closure?
} Local;

// Upvalue in compiler
typedef struct {
    uint8_t index;
    bool isLocal;
} Upvalue;

// Compiler function type (what kind of function is being compiled)
typedef enum {
    FN_TYPE_SCRIPT,
    FN_TYPE_FUNCTION,
    FN_TYPE_METHOD,
    FN_TYPE_INITIALIZER,
} CompilerFnType;

// Compiler state
typedef struct Compiler {
    struct Compiler* enclosing;     // Enclosing compiler for functions
    ObjFunction* function;          // Function being compiled
    CompilerFnType type;            // Type of function

    Local locals[UINT8_COUNT];      // Local variables
    int localCount;
    Upvalue upvalues[UINT8_COUNT];  // Captured variables
    int scopeDepth;
    Type* currentFunctionReturn;    // Return type of current function
} Compiler;

// Initialize a compiler
void initCompiler(Compiler* compiler, Compiler* enclosing, CompilerFnType type);

// Compile a list of statements to bytecode
// Returns the compiled function, or NULL on error
ObjFunction* compile(Stmt** statements, int count);

// Emit lowered generic classes first, then the main script (same compiler session).
ObjFunction* compileWithPrependedClasses(Stmt** prepend, int prependCount,
                                         Stmt** statements, int count);

// Compile for REPL (top-level variables are globals for persistence)
ObjFunction* compileRepl(Stmt** statements, int count);

ObjFunction* compileReplWithPrependedClasses(Stmt** prepend, int prependCount,
                                             Stmt** statements, int count);

// Mark compiler roots for GC
void markCompilerRoots(void);

#endif
