/* Typechecker implementation: walks AST, resolves names, checks calls,
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

// ============================================================================
// Public API
// ============================================================================

// Helper to define a native function type
static void defineNativeFunction(TypeChecker* checker, const char* name,
                                 Type** paramTypes, int paramCount, Type* returnType) {
    Type* funcType = createFunctionType(paramTypes, paramCount, returnType);
    defineSymbol(checker, name, (int)strlen(name), funcType, true);
}

void initTypeChecker(TypeChecker* checker) {
    initSymbolTable(&checker->symbols);
    checker->currentFunctionReturn = NULL;
    checker->currentClass = NULL;
    checker->currentClassStmt = NULL;
    checker->hadError = false;
    checker->genericInsts = NULL;
    checker->genericInstCount = 0;
    checker->genericInstCapacity = 0;
    checker->classDeclTypes = NULL;
    checker->classDeclStmts = NULL;
    checker->classDeclCount = 0;
    checker->classDeclCapacity = 0;

    // Define built-in native functions

    // Time functions
    defineNativeFunction(checker, "clock", NULL, 0, createFloatType());
    defineNativeFunction(checker, "time", NULL, 0, createIntType());
    {
        Type** params = ALLOCATE(Type*, 2);
        params[0] = createStringType();
        params[1] = createIntType();
        defineNativeFunction(checker, "formatTime", params, 2, createStringType());
    }
    {
        Type** params = ALLOCATE(Type*, 2);
        params[0] = createStringType();
        params[1] = createIntType();
        defineNativeFunction(checker, "formatTimeUtc", params, 2, createStringType());
    }

    // String/Array functions
    {
        Type** params = ALLOCATE(Type*, 1);
        params[0] = createUnknownType();  // Works with strings and arrays
        defineNativeFunction(checker, "len", params, 1, createIntType());
    }
    {
        Type** params = ALLOCATE(Type*, 3);
        params[0] = createStringType();
        params[1] = createIntType();
        params[2] = createIntType();
        defineNativeFunction(checker, "substr", params, 3, createStringType());
    }
    {
        Type** params = ALLOCATE(Type*, 2);
        params[0] = createStringType();
        params[1] = createStringType();
        defineNativeFunction(checker, "concat", params, 2, createStringType());
    }
    {
        Type** params = ALLOCATE(Type*, 2);
        params[0] = createStringType();
        params[1] = createIntType();
        defineNativeFunction(checker, "charAt", params, 2, createStringType());
    }
    {
        Type** params = ALLOCATE(Type*, 2);
        params[0] = createStringType();
        params[1] = createStringType();
        defineNativeFunction(checker, "indexOf", params, 2, createIntType());
    }
    {
        Type** params = ALLOCATE(Type*, 1);
        params[0] = createStringType();
        defineNativeFunction(checker, "toUpper", params, 1, createStringType());
    }
    {
        Type** params = ALLOCATE(Type*, 1);
        params[0] = createStringType();
        defineNativeFunction(checker, "toLower", params, 1, createStringType());
    }
    {
        Type** params = ALLOCATE(Type*, 1);
        params[0] = createStringType();
        defineNativeFunction(checker, "trim", params, 1, createStringType());
    }

    // Math functions - use UNKNOWN for generic numeric types
    {
        Type** params = ALLOCATE(Type*, 1);
        params[0] = createUnknownType();  // int or float
        defineNativeFunction(checker, "abs", params, 1, createUnknownType());
    }
    {
        Type** params = ALLOCATE(Type*, 1);
        params[0] = createUnknownType();
        defineNativeFunction(checker, "sqrt", params, 1, createFloatType());
    }
    {
        Type** params = ALLOCATE(Type*, 2);
        params[0] = createUnknownType();
        params[1] = createUnknownType();
        defineNativeFunction(checker, "pow", params, 2, createFloatType());
    }
    {
        Type** params = ALLOCATE(Type*, 1);
        params[0] = createUnknownType();
        defineNativeFunction(checker, "sin", params, 1, createFloatType());
    }
    {
        Type** params = ALLOCATE(Type*, 1);
        params[0] = createUnknownType();
        defineNativeFunction(checker, "cos", params, 1, createFloatType());
    }
    {
        Type** params = ALLOCATE(Type*, 1);
        params[0] = createUnknownType();
        defineNativeFunction(checker, "tan", params, 1, createFloatType());
    }
    {
        Type** params = ALLOCATE(Type*, 1);
        params[0] = createUnknownType();
        defineNativeFunction(checker, "log", params, 1, createFloatType());
    }
    {
        Type** params = ALLOCATE(Type*, 1);
        params[0] = createUnknownType();
        defineNativeFunction(checker, "log10", params, 1, createFloatType());
    }
    {
        Type** params = ALLOCATE(Type*, 1);
        params[0] = createUnknownType();
        defineNativeFunction(checker, "exp", params, 1, createFloatType());
    }
    {
        Type** params = ALLOCATE(Type*, 1);
        params[0] = createUnknownType();
        defineNativeFunction(checker, "floor", params, 1, createIntType());
    }
    {
        Type** params = ALLOCATE(Type*, 1);
        params[0] = createUnknownType();
        defineNativeFunction(checker, "ceil", params, 1, createIntType());
    }
    {
        Type** params = ALLOCATE(Type*, 1);
        params[0] = createUnknownType();
        defineNativeFunction(checker, "round", params, 1, createIntType());
    }
    {
        Type** params = ALLOCATE(Type*, 2);
        params[0] = createUnknownType();
        params[1] = createUnknownType();
        defineNativeFunction(checker, "min", params, 2, createUnknownType());
    }
    {
        Type** params = ALLOCATE(Type*, 2);
        params[0] = createUnknownType();
        params[1] = createUnknownType();
        defineNativeFunction(checker, "max", params, 2, createUnknownType());
    }

    // Type conversion functions
    {
        Type** params = ALLOCATE(Type*, 1);
        params[0] = createUnknownType();
        defineNativeFunction(checker, "toString", params, 1, createStringType());
    }
    {
        Type** params = ALLOCATE(Type*, 1);
        params[0] = createUnknownType();
        defineNativeFunction(checker, "toInt", params, 1, createIntType());
    }
    {
        Type** params = ALLOCATE(Type*, 1);
        params[0] = createUnknownType();
        defineNativeFunction(checker, "toFloat", params, 1, createFloatType());
    }

    // I/O functions
    {
        Type** params = ALLOCATE(Type*, 1);
        params[0] = createStringType();
        defineNativeFunction(checker, "input", params, 1, createStringType());
    }
    {
        Type** params = ALLOCATE(Type*, 1);
        params[0] = createStringType();
        defineNativeFunction(checker, "writeStr", params, 1, createUnknownType());
    }
    {
        Type** params = ALLOCATE(Type*, 1);
        params[0] = createStringType();
        defineNativeFunction(checker, "writeLine", params, 1, createUnknownType());
    }
    defineNativeFunction(checker, "flushOut", NULL, 0, createUnknownType());
    {
        Type** params = ALLOCATE(Type*, 1);
        params[0] = createUnknownType();
        defineNativeFunction(checker, "type", params, 1, createStringType());
    }

    // Array functions
    {
        Type** params = ALLOCATE(Type*, 2);
        params[0] = createUnknownType();  // array
        params[1] = createUnknownType();  // value
        defineNativeFunction(checker, "push", params, 2, createUnknownType());
    }
    {
        Type** params = ALLOCATE(Type*, 1);
        params[0] = createUnknownType();  // array
        defineNativeFunction(checker, "pop", params, 1, createUnknownType());
    }
    {
        Type** params = ALLOCATE(Type*, 1);
        params[0] = createUnknownType();  // array
        defineNativeFunction(checker, "first", params, 1, createUnknownType());
    }
    {
        Type** params = ALLOCATE(Type*, 1);
        params[0] = createUnknownType();  // array
        defineNativeFunction(checker, "last", params, 1, createUnknownType());
    }
    {
        Type** params = ALLOCATE(Type*, 2);
        params[0] = createUnknownType();  // array
        params[1] = createUnknownType();  // value
        defineNativeFunction(checker, "contains", params, 2, createBoolType());
    }
    {
        Type** params = ALLOCATE(Type*, 1);
        params[0] = createUnknownType();  // array
        defineNativeFunction(checker, "reverse", params, 1, createUnknownType());
    }
    {
        Type** params = ALLOCATE(Type*, 1);
        params[0] = createUnknownType();  // array
        defineNativeFunction(checker, "clear", params, 1, createUnknownType());
    }
    {
        Type** params = ALLOCATE(Type*, 3);
        params[0] = createUnknownType();  // array
        params[1] = createIntType();      // start
        params[2] = createIntType();      // end
        defineNativeFunction(checker, "slice", params, 3, createUnknownType());
    }
    {
        Type** params = ALLOCATE(Type*, 2);
        params[0] = createUnknownType();  // array
        params[1] = createStringType();   // separator
        defineNativeFunction(checker, "join", params, 2, createStringType());
    }
    {
        Type** params = ALLOCATE(Type*, 2);
        params[0] = createStringType();   // string
        params[1] = createStringType();   // separator
        defineNativeFunction(checker, "split", params, 2, createUnknownType());
    }
    {
        Type** params = ALLOCATE(Type*, 3);
        params[0] = createStringType();   // string
        params[1] = createStringType();   // old
        params[2] = createStringType();   // new
        defineNativeFunction(checker, "replace", params, 3, createStringType());
    }

    // Additional string functions
    {
        Type** params = ALLOCATE(Type*, 2);
        params[0] = createStringType();   // string
        params[1] = createStringType();   // prefix
        defineNativeFunction(checker, "startsWith", params, 2, createBoolType());
    }
    {
        Type** params = ALLOCATE(Type*, 2);
        params[0] = createStringType();   // string
        params[1] = createStringType();   // suffix
        defineNativeFunction(checker, "endsWith", params, 2, createBoolType());
    }
    {
        Type** params = ALLOCATE(Type*, 2);
        params[0] = createStringType();   // string
        params[1] = createIntType();      // count
        defineNativeFunction(checker, "repeat", params, 2, createStringType());
    }

    // Random functions
    defineNativeFunction(checker, "random", NULL, 0, createFloatType());
    {
        Type** params = ALLOCATE(Type*, 2);
        params[0] = createIntType();      // min
        params[1] = createIntType();      // max
        defineNativeFunction(checker, "randomInt", params, 2, createIntType());
    }

    // Array sorting
    {
        Type** params = ALLOCATE(Type*, 1);
        params[0] = createUnknownType();  // array
        defineNativeFunction(checker, "sort", params, 1, createUnknownType());
    }

    // File I/O functions
    {
        Type** params = ALLOCATE(Type*, 1);
        params[0] = createStringType();   // path
        defineNativeFunction(checker, "readFile", params, 1, createStringType());
    }
    {
        Type** params = ALLOCATE(Type*, 2);
        params[0] = createStringType();   // path
        params[1] = createStringType();   // content
        defineNativeFunction(checker, "writeFile", params, 2, createBoolType());
    }
    {
        Type** params = ALLOCATE(Type*, 2);
        params[0] = createStringType();   // path
        params[1] = createStringType();   // content
        defineNativeFunction(checker, "appendFile", params, 2, createBoolType());
    }
    {
        Type** params = ALLOCATE(Type*, 1);
        params[0] = createStringType();   // path
        defineNativeFunction(checker, "fileExists", params, 1, createBoolType());
    }
    {
        Type** params = ALLOCATE(Type*, 1);
        params[0] = createStringType();   // path
        defineNativeFunction(checker, "deleteFile", params, 1, createBoolType());
    }

    // Hash map / hash set builtins
    defineNativeFunction(checker, "hashMap", NULL, 0, createUnknownType());
    {
        Type** params = ALLOCATE(Type*, 1);
        params[0] = createIntType();      // capacity
        defineNativeFunction(checker, "hashMapWithCapacity", params, 1, createUnknownType());
    }
    defineNativeFunction(checker, "hashSet", NULL, 0, createUnknownType());
    {
        Type** params = ALLOCATE(Type*, 1);
        params[0] = createIntType();      // capacity
        defineNativeFunction(checker, "hashSetWithCapacity", params, 1, createUnknownType());
    }

    {
        Type** params = ALLOCATE(Type*, 3);
        params[0] = createUnknownType();  // map
        params[1] = createUnknownType();  // key
        params[2] = createUnknownType();  // value
        defineNativeFunction(checker, "hashMapSet", params, 3, createUnknownType());
    }
    {
        Type** params = ALLOCATE(Type*, 3);
        params[0] = createUnknownType();  // map
        params[1] = createIntType();      // key
        params[2] = createIntType();      // value
        defineNativeFunction(checker, "hashMapSetInt", params, 3, createUnknownType());
    }
    {
        Type** params = ALLOCATE(Type*, 2);
        params[0] = createUnknownType();  // map
        params[1] = createUnknownType();  // key
        defineNativeFunction(checker, "hashMapGet", params, 2, createUnknownType());
    }
    {
        Type** params = ALLOCATE(Type*, 2);
        params[0] = createUnknownType();  // map
        params[1] = createUnknownType();  // key
        defineNativeFunction(checker, "hashMapHas", params, 2, createBoolType());
    }
    {
        Type** params = ALLOCATE(Type*, 2);
        params[0] = createUnknownType();  // map
        params[1] = createIntType();      // key
        defineNativeFunction(checker, "hashMapHasInt", params, 2, createBoolType());
    }
    {
        Type** params = ALLOCATE(Type*, 2);
        params[0] = createUnknownType();  // map
        params[1] = createUnknownType();  // key
        defineNativeFunction(checker, "hashMapDelete", params, 2, createBoolType());
    }
    {
        Type** params = ALLOCATE(Type*, 1);
        params[0] = createUnknownType();  // map
        defineNativeFunction(checker, "hashMapSize", params, 1, createIntType());
    }
    {
        Type** params = ALLOCATE(Type*, 1);
        params[0] = createUnknownType();  // map
        defineNativeFunction(checker, "hashMapClear", params, 1, createUnknownType());
    }
    {
        Type** params = ALLOCATE(Type*, 1);
        params[0] = createUnknownType();  // map
        defineNativeFunction(checker, "hashMapKeys", params, 1, createUnknownType());
    }
    {
        Type** params = ALLOCATE(Type*, 1);
        params[0] = createUnknownType();  // map
        defineNativeFunction(checker, "hashMapValues", params, 1, createUnknownType());
    }
    {
        Type** params = ALLOCATE(Type*, 3);
        params[0] = createUnknownType();  // map
        params[1] = createIntType();      // start
        params[2] = createIntType();      // end
        defineNativeFunction(checker, "hashMapFillRange", params, 3, createUnknownType());
    }
    {
        Type** params = ALLOCATE(Type*, 3);
        params[0] = createUnknownType();  // map
        params[1] = createIntType();      // start
        params[2] = createIntType();      // end
        defineNativeFunction(checker, "hashMapCountPresentRange", params, 3, createIntType());
    }
    {
        Type** params = ALLOCATE(Type*, 3);
        params[0] = createUnknownType();  // map
        params[1] = createIntType();      // start
        params[2] = createIntType();      // end
        defineNativeFunction(checker, "hashMapFillStringKeysRange", params, 3, createUnknownType());
    }
    {
        Type** params = ALLOCATE(Type*, 3);
        params[0] = createUnknownType();  // map
        params[1] = createIntType();      // start
        params[2] = createIntType();      // end
        defineNativeFunction(checker, "hashMapCountPresentStringKeysRange", params, 3, createIntType());
    }
    {
        Type** params = ALLOCATE(Type*, 2);
        params[0] = createUnknownType();  // map
        params[1] = createUnknownType();  // keys array
        defineNativeFunction(checker, "hashMapCountPresentKeys", params, 2, createIntType());
    }
    {
        Type** params = ALLOCATE(Type*, 3);
        params[0] = createUnknownType();  // map
        params[1] = createUnknownType();  // keys
        params[2] = createUnknownType();  // values
        defineNativeFunction(checker, "hashMapSetBulk", params, 3, createUnknownType());
    }

    {
        Type** params = ALLOCATE(Type*, 2);
        params[0] = createUnknownType();  // set
        params[1] = createUnknownType();  // value
        defineNativeFunction(checker, "hashSetAdd", params, 2, createUnknownType());
    }
    {
        Type** params = ALLOCATE(Type*, 2);
        params[0] = createUnknownType();  // set
        params[1] = createIntType();      // value
        defineNativeFunction(checker, "hashSetAddInt", params, 2, createUnknownType());
    }
    {
        Type** params = ALLOCATE(Type*, 2);
        params[0] = createUnknownType();  // set
        params[1] = createUnknownType();  // value
        defineNativeFunction(checker, "hashSetHas", params, 2, createBoolType());
    }
    {
        Type** params = ALLOCATE(Type*, 2);
        params[0] = createUnknownType();  // set
        params[1] = createUnknownType();  // value
        defineNativeFunction(checker, "hashSetDelete", params, 2, createBoolType());
    }
    {
        Type** params = ALLOCATE(Type*, 1);
        params[0] = createUnknownType();  // set
        defineNativeFunction(checker, "hashSetSize", params, 1, createIntType());
    }
    {
        Type** params = ALLOCATE(Type*, 1);
        params[0] = createUnknownType();  // set
        defineNativeFunction(checker, "hashSetClear", params, 1, createUnknownType());
    }
    {
        Type** params = ALLOCATE(Type*, 1);
        params[0] = createUnknownType();  // set
        defineNativeFunction(checker, "hashSetValues", params, 1, createUnknownType());
    }
    {
        Type** params = ALLOCATE(Type*, 3);
        params[0] = createUnknownType();  // set
        params[1] = createIntType();      // count
        params[2] = createIntType();      // mod
        defineNativeFunction(checker, "hashSetAddModRange", params, 3, createUnknownType());
    }
}

void freeTypeChecker(TypeChecker* checker) {
    FREE_ARRAY(GenericInstToCheck, checker->genericInsts, checker->genericInstCapacity);
    checker->genericInsts = NULL;
    checker->genericInstCount = 0;
    checker->genericInstCapacity = 0;
    FREE_ARRAY(Type*, checker->classDeclTypes, checker->classDeclCapacity);
    FREE_ARRAY(Stmt*, checker->classDeclStmts, checker->classDeclCapacity);
    checker->classDeclTypes = NULL;
    checker->classDeclStmts = NULL;
    checker->classDeclCount = 0;
    checker->classDeclCapacity = 0;
    freeSymbolTable(&checker->symbols);
}

bool typeCheck(TypeChecker* checker, Stmt** statements, int count) {
    for (int i = 0; i < count; i++) {
        checkStmt(checker, statements[i]);
    }
    if (!checker->hadError) {
        checkGenericInstanceBodies(checker);
    }
    return !checker->hadError;
}
