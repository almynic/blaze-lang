#include "typechecker.h"
#include "memory.h"
#include "colors.h"
#include <stdarg.h>

// ============================================================================
// Error Reporting
// ============================================================================

static void typeError(TypeChecker* checker, int line, const char* message) {
    checker->hadError = true;

    if (colorsEnabled()) {
        fprintf(stderr, "%s[line %d]%s %sType error:%s %s\n",
                COLOR_BOLD, line, COLOR_RESET,
                COLOR_BRIGHT_RED, COLOR_RESET, message);
    } else {
        fprintf(stderr, "[line %d] Type error: %s\n", line, message);
    }
}

static void typeErrorFormat(TypeChecker* checker, int line, const char* format, ...) {
    checker->hadError = true;

    if (colorsEnabled()) {
        fprintf(stderr, "%s[line %d]%s %sType error:%s ",
                COLOR_BOLD, line, COLOR_RESET,
                COLOR_BRIGHT_RED, COLOR_RESET);
    } else {
        fprintf(stderr, "[line %d] Type error: ", line);
    }

    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);

    fprintf(stderr, "\n");
}

// ============================================================================
// Symbol Table
// ============================================================================

static void initSymbolTable(SymbolTable* table) {
    table->symbols = NULL;
    table->count = 0;
    table->capacity = 0;
    table->scopeDepth = 0;
}

static void freeSymbolTable(SymbolTable* table) {
    FREE_ARRAY(Symbol, table->symbols, table->capacity);
    initSymbolTable(table);
}

static void beginScope(TypeChecker* checker) {
    checker->symbols.scopeDepth++;
}

static void endScope(TypeChecker* checker) {
    // Remove symbols from the current scope
    while (checker->symbols.count > 0 &&
           checker->symbols.symbols[checker->symbols.count - 1].depth ==
               checker->symbols.scopeDepth) {
        checker->symbols.count--;
    }
    checker->symbols.scopeDepth--;
}

void defineSymbol(TypeChecker* checker, const char* name, int length,
                  Type* type, bool isConst) {
    SymbolTable* table = &checker->symbols;

    // Check for redefinition in same scope
    for (int i = table->count - 1; i >= 0; i--) {
        Symbol* sym = &table->symbols[i];
        if (sym->depth < table->scopeDepth) break;

        if (sym->nameLength == length &&
            memcmp(sym->name, name, length) == 0) {
            // Already defined in this scope - error will be reported elsewhere
            return;
        }
    }

    if (table->count >= table->capacity) {
        int oldCapacity = table->capacity;
        table->capacity = GROW_CAPACITY(oldCapacity);
        table->symbols = GROW_ARRAY(Symbol, table->symbols, oldCapacity, table->capacity);
    }

    Symbol* symbol = &table->symbols[table->count++];
    symbol->name = name;
    symbol->nameLength = length;
    symbol->type = type;
    symbol->isConst = isConst;
    symbol->depth = table->scopeDepth;
}

static Symbol* lookupSymbol(TypeChecker* checker, const char* name, int length) {
    SymbolTable* table = &checker->symbols;

    for (int i = table->count - 1; i >= 0; i--) {
        Symbol* sym = &table->symbols[i];
        if (sym->nameLength == length &&
            memcmp(sym->name, name, length) == 0) {
            return sym;
        }
    }

    return NULL;
}

// ============================================================================
// Type Narrowing
// ============================================================================

typedef struct {
    bool isTypeGuard;           // Whether this is a valid type guard
    const char* varName;        // Variable being narrowed
    int varNameLength;
    Type* narrowedType;         // Type to narrow to
    bool isPositive;            // true for ==, false for !=
} TypeGuard;

// Helper to create an empty (invalid) type guard
static TypeGuard noTypeGuard() {
    TypeGuard guard;
    guard.isTypeGuard = false;
    guard.varName = NULL;
    guard.varNameLength = 0;
    guard.narrowedType = NULL;
    guard.isPositive = true;
    return guard;
}

// Forward declaration
static Type* checkExpr(TypeChecker* checker, Expr* expr);
static void checkStmt(TypeChecker* checker, Stmt* stmt);

static void pushGenericInst(TypeChecker* checker, Type* instType, Stmt* templateStmt) {
    for (int i = 0; i < checker->genericInstCount; i++) {
        if (typesEqual(checker->genericInsts[i].instType, instType)) {
            return;
        }
    }
    if (checker->genericInstCount >= checker->genericInstCapacity) {
        int old = checker->genericInstCapacity;
        checker->genericInstCapacity = GROW_CAPACITY(old);
        checker->genericInsts = GROW_ARRAY(GenericInstToCheck, checker->genericInsts,
                                           old, checker->genericInstCapacity);
    }
    checker->genericInsts[checker->genericInstCount].instType = instType;
    checker->genericInsts[checker->genericInstCount].templateStmt = templateStmt;
    checker->genericInstCount++;
}

// Detect type guards of the form: type(x) == "int" or x == nil
static TypeGuard analyzeTypeGuard(Expr* condition) {
    if (condition->kind != EXPR_BINARY) {
        return noTypeGuard();
    }

    BinaryExpr* binary = &condition->as.binary;

    // Check for == or != operators
    if (binary->op.type != TOKEN_EQUAL_EQUAL && binary->op.type != TOKEN_BANG_EQUAL) {
        return noTypeGuard();
    }

    bool isPositive = (binary->op.type == TOKEN_EQUAL_EQUAL);

    // Pattern 1: type(x) == "typename"
    // Check if left side is type(x) and right side is a string literal
    if (binary->left->kind == EXPR_CALL && binary->right->kind == EXPR_LITERAL) {
        CallExpr* call = &binary->left->as.call;

        // Check if callee is 'type' function
        if (call->callee->kind == EXPR_VARIABLE) {
            VariableExpr* callee = &call->callee->as.variable;
            if (callee->name.length == 4 && memcmp(callee->name.start, "type", 4) == 0) {
                // Check we have exactly one argument
                if (call->argCount == 1 && call->arguments[0]->kind == EXPR_VARIABLE) {
                    VariableExpr* var = &call->arguments[0]->as.variable;
                    LiteralExpr* typeName = &binary->right->as.literal;

                    // Check that right side is a string literal
                    if (typeName->token.type == TOKEN_STRING) {
                        TypeGuard guard;
                        guard.isTypeGuard = true;
                        guard.varName = var->name.start;
                        guard.varNameLength = var->name.length;
                        guard.isPositive = isPositive;

                        // Parse the type name (strip quotes)
                        const char* typeStr = typeName->token.start + 1;  // Skip opening quote
                        int typeLen = typeName->token.length - 2;          // Remove both quotes

                        if (typeLen == 3 && memcmp(typeStr, "int", 3) == 0) {
                            guard.narrowedType = createIntType();
                        } else if (typeLen == 5 && memcmp(typeStr, "float", 5) == 0) {
                            guard.narrowedType = createFloatType();
                        } else if (typeLen == 4 && memcmp(typeStr, "bool", 4) == 0) {
                            guard.narrowedType = createBoolType();
                        } else if (typeLen == 6 && memcmp(typeStr, "string", 6) == 0) {
                            guard.narrowedType = createStringType();
                        } else if (typeLen == 3 && memcmp(typeStr, "nil", 3) == 0) {
                            guard.narrowedType = createNilType();
                        } else {
                            // Unknown type name, not a valid type guard
                            return noTypeGuard();
                        }

                        return guard;
                    }
                }
            }
        }
    }

    // Pattern 2: x == nil or x != nil
    if (binary->left->kind == EXPR_VARIABLE && binary->right->kind == EXPR_LITERAL) {
        VariableExpr* var = &binary->left->as.variable;
        LiteralExpr* literal = &binary->right->as.literal;

        if (literal->token.type == TOKEN_NIL) {
            TypeGuard guard;
            guard.isTypeGuard = true;
            guard.varName = var->name.start;
            guard.varNameLength = var->name.length;
            guard.isPositive = isPositive;
            guard.narrowedType = createNilType();
            return guard;
        }
    }

    // Pattern 3: nil == x or nil != x (reversed)
    if (binary->left->kind == EXPR_LITERAL && binary->right->kind == EXPR_VARIABLE) {
        LiteralExpr* literal = &binary->left->as.literal;
        VariableExpr* var = &binary->right->as.variable;

        if (literal->token.type == TOKEN_NIL) {
            TypeGuard guard;
            guard.isTypeGuard = true;
            guard.varName = var->name.start;
            guard.varNameLength = var->name.length;
            guard.isPositive = isPositive;
            guard.narrowedType = createNilType();
            return guard;
        }
    }

    return noTypeGuard();
}

// Apply type narrowing by shadowing a variable with a narrowed type
static void applyTypeNarrowing(TypeChecker* checker, TypeGuard* guard) {
    if (!guard->isTypeGuard) return;

    // Look up the original variable
    Symbol* original = lookupSymbol(checker, guard->varName, guard->varNameLength);
    if (original == NULL) {
        return;  // Variable doesn't exist, error will be reported elsewhere
    }

    // Check if the original type is a union type
    if (original->type->kind != TYPE_UNION && original->type->kind != TYPE_OPTIONAL) {
        // Not a union/optional type, no narrowing needed
        return;
    }

    Type* narrowedType = NULL;

    if (guard->isPositive) {
        // For positive guard (==), narrow to the specific type
        if (guard->narrowedType->kind == TYPE_NIL) {
            // x == nil: narrow to nil
            narrowedType = createNilType();
        } else {
            // type(x) == "int": narrow to int
            narrowedType = guard->narrowedType;
        }
    } else {
        // For negative guard (!=), remove the type from the union
        if (guard->narrowedType->kind == TYPE_NIL && original->type->kind == TYPE_OPTIONAL) {
            // x != nil: for optional T?, narrow to T
            narrowedType = original->type->as.optional.innerType;
        } else if (original->type->kind == TYPE_UNION) {
            // type(x) != "int": remove int from the union
            UnionType* unionType = &original->type->as.unionType;
            Type** remainingTypes = ALLOCATE(Type*, unionType->typeCount);
            int remainingCount = 0;

            for (int i = 0; i < unionType->typeCount; i++) {
                if (!typesEqual(unionType->types[i], guard->narrowedType)) {
                    remainingTypes[remainingCount++] = unionType->types[i];
                }
            }

            if (remainingCount == 0) {
                // All types removed, shouldn't happen but handle gracefully
                FREE_ARRAY(Type*, remainingTypes, unionType->typeCount);
                return;
            } else if (remainingCount == 1) {
                narrowedType = remainingTypes[0];
            } else {
                narrowedType = createUnionType(remainingTypes, remainingCount);
            }

            FREE_ARRAY(Type*, remainingTypes, unionType->typeCount);
        }
    }

    if (narrowedType != NULL) {
        // Shadow the variable with the narrowed type in the current scope
        defineSymbol(checker, guard->varName, guard->varNameLength, narrowedType, original->isConst);
    }
}

// ============================================================================
// Type Resolution
// ============================================================================

Type* resolveTypeNode(TypeChecker* checker, TypeNode* node) {
    if (node == NULL) return createUnknownType();

    switch (node->kind) {
        case TYPE_NODE_SIMPLE: {
            Token name = node->as.simple.name;

            // Built-in types
            if (name.length == 3 && memcmp(name.start, "int", 3) == 0) {
                return createIntType();
            }
            if (name.length == 5 && memcmp(name.start, "float", 5) == 0) {
                return createFloatType();
            }
            if (name.length == 4 && memcmp(name.start, "bool", 4) == 0) {
                return createBoolType();
            }
            if (name.length == 6 && memcmp(name.start, "string", 6) == 0) {
                return createStringType();
            }
            if (name.length == 3 && memcmp(name.start, "nil", 3) == 0) {
                return createNilType();
            }

            // Could be a user-defined type (class, alias, generic inst, or type parameter)
            Symbol* sym = lookupSymbol(checker, name.start, name.length);
            if (sym != NULL) {
                if (sym->type->kind == TYPE_GENERIC_CLASS_TEMPLATE) {
                    typeErrorFormat(checker, name.line,
                                   "Generic class '%.*s' requires type arguments (e.g. %.*s<int>).",
                                   name.length, name.start, name.length, name.start);
                    return createErrorType();
                }
                if (sym->type->kind == TYPE_CLASS || sym->type->kind == TYPE_TYPE_PARAM ||
                    sym->type->kind == TYPE_GENERIC_INST || sym->type->kind == TYPE_INTERFACE) {
                    return sym->type;
                }
            }

            typeErrorFormat(checker, name.line, "Unknown type '%.*s'",
                           name.length, name.start);
            return createErrorType();
        }

        case TYPE_NODE_ARRAY: {
            Type* elementType = resolveTypeNode(checker, node->as.array.elementType);
            return createArrayType(elementType);
        }

        case TYPE_NODE_FUNCTION: {
            int paramCount = node->as.function.paramCount;
            Type** paramTypes = NULL;

            if (paramCount > 0) {
                paramTypes = ALLOCATE(Type*, paramCount);
                for (int i = 0; i < paramCount; i++) {
                    paramTypes[i] = resolveTypeNode(checker, node->as.function.paramTypes[i]);
                }
            }

            Type* returnType = resolveTypeNode(checker, node->as.function.returnType);
            return createFunctionType(paramTypes, paramCount, returnType);
        }

        case TYPE_NODE_OPTIONAL: {
            Type* innerType = resolveTypeNode(checker, node->as.optional.innerType);
            return createOptionalType(innerType);
        }

        case TYPE_NODE_UNION: {
            int typeCount = node->as.unionType.typeCount;
            Type** types = ALLOCATE(Type*, typeCount);

            for (int i = 0; i < typeCount; i++) {
                types[i] = resolveTypeNode(checker, node->as.unionType.types[i]);
            }

            return createUnionType(types, typeCount);
        }
        case TYPE_NODE_GENERIC: {
            Token name = node->as.generic.name;
            Symbol* sym = lookupSymbol(checker, name.start, name.length);
            if (sym == NULL || sym->type->kind != TYPE_GENERIC_CLASS_TEMPLATE) {
                typeErrorFormat(checker, name.line, "Unknown generic type '%.*s'.",
                               name.length, name.start);
                return createErrorType();
            }
            Stmt* stmt = (Stmt*)sym->type->as.genericClassTemplate->classStmt;
            ClassStmt* cs = &stmt->as.class_;
            if (node->as.generic.typeArgCount != cs->typeParamCount) {
                typeErrorFormat(checker, name.line, "Wrong number of type arguments for '%.*s'.",
                               name.length, name.start);
                return createErrorType();
            }
            Type** args = ALLOCATE(Type*, node->as.generic.typeArgCount);
            for (int i = 0; i < node->as.generic.typeArgCount; i++) {
                args[i] = resolveTypeNode(checker, node->as.generic.typeArgs[i]);
            }
            if (cs->typeParamBounds != NULL) {
                for (int bi = 0; bi < cs->typeParamCount; bi++) {
                    if (cs->typeParamBounds[bi].length == 0) continue;
                    Symbol* bsym = lookupSymbol(checker, cs->typeParamBounds[bi].start,
                                                cs->typeParamBounds[bi].length);
                    if (bsym == NULL || bsym->type->kind != TYPE_INTERFACE) {
                        typeErrorFormat(checker, name.line, "Unknown interface bound '%.*s'.",
                                        cs->typeParamBounds[bi].length, cs->typeParamBounds[bi].start);
                        continue;
                    }
                    Type* arg = args[bi];
                    if (arg->kind == TYPE_INT || arg->kind == TYPE_FLOAT || arg->kind == TYPE_BOOL ||
                        arg->kind == TYPE_STRING || arg->kind == TYPE_NIL) {
                        typeErrorFormat(checker, name.line,
                                        "Type argument does not satisfy interface bound.");
                        continue;
                    }
                    if (arg->kind == TYPE_CLASS || arg->kind == TYPE_INSTANCE) {
                        ClassType* ct = arg->as.classType;
                        bool ok = false;
                        if (ct != NULL && ct->implementedInterfaces != NULL) {
                            for (int k = 0; k < ct->implementedInterfaceCount; k++) {
                                if (typesEqual(ct->implementedInterfaces[k], bsym->type)) {
                                    ok = true;
                                    break;
                                }
                            }
                        }
                        if (!ok) {
                            typeErrorFormat(checker, name.line,
                                            "Type argument does not satisfy interface bound.");
                        }
                    } else {
                        typeErrorFormat(checker, name.line,
                                        "Type argument does not satisfy interface bound.");
                    }
                }
            }
            Type* inst = createGenericInstType(sym->type, args, node->as.generic.typeArgCount);
            FREE_ARRAY(Type*, args, node->as.generic.typeArgCount);
            pushGenericInst(checker, inst, stmt);
            return inst;
        }
    }

    return createErrorType();
}

// ============================================================================
// Expression Type Checking
// ============================================================================

static Type* checkExpr(TypeChecker* checker, Expr* expr);
static Type* checkMatchPatternExpr(TypeChecker* checker, Expr* expr, Type* valueType, int line);
static Type* checkMatchPatternCase(TypeChecker* checker, Expr* pattern, Token* destructureParams,
                                   int destructureCount, Type* valueType, int line);
static Type* checkMatch(TypeChecker* checker, Expr* expr);

static Type* checkLiteral(TypeChecker* checker, Expr* expr) {
    (void)checker;
    LiteralExpr* lit = &expr->as.literal;


    // Get type from literal value (NaN boxed)
    if (IS_NIL(lit->value)) {
        return createNilType();
    } else if (IS_BOOL(lit->value)) {
        return createBoolType();
    } else if (IS_INT(lit->value)) {
        return createIntType();
    } else if (IS_FLOAT(lit->value)) {
        return createFloatType();
    } else if (IS_OBJ(lit->value)) {
        return createStringType(); // Assuming string for now
    }

    return createErrorType();
}

static Type* checkUnary(TypeChecker* checker, Expr* expr) {
    UnaryExpr* unary = &expr->as.unary;
    Type* operandType = checkExpr(checker, unary->operand);

    switch (unary->op.type) {
        case TOKEN_MINUS:
            if (operandType->kind == TYPE_INT) {
                return createIntType();
            }
            if (operandType->kind == TYPE_FLOAT) {
                return createFloatType();
            }
            typeError(checker, expr->line, "Operand of '-' must be a number.");
            return createErrorType();

        case TOKEN_BANG:
            if (operandType->kind == TYPE_BOOL) {
                return createBoolType();
            }
            typeError(checker, expr->line, "Operand of '!' must be a bool.");
            return createErrorType();

        default:
            return createErrorType();
    }
}

static Type* checkBinary(TypeChecker* checker, Expr* expr) {
    BinaryExpr* binary = &expr->as.binary;
    Type* leftType = checkExpr(checker, binary->left);
    Type* rightType = checkExpr(checker, binary->right);

    // Skip further checking if either operand had an error
    if (leftType->kind == TYPE_ERROR || rightType->kind == TYPE_ERROR) {
        return createErrorType();
    }

    // If either operand is UNKNOWN/type-param (generic placeholder), be lenient.
    if (leftType->kind == TYPE_UNKNOWN || rightType->kind == TYPE_UNKNOWN ||
        leftType->kind == TYPE_TYPE_PARAM || rightType->kind == TYPE_TYPE_PARAM) {
        if (binary->op.type == TOKEN_EQUAL_EQUAL || binary->op.type == TOKEN_BANG_EQUAL ||
            binary->op.type == TOKEN_LESS || binary->op.type == TOKEN_LESS_EQUAL ||
            binary->op.type == TOKEN_GREATER || binary->op.type == TOKEN_GREATER_EQUAL) {
            return createBoolType();
        }
        return createUnknownType();
    }

    switch (binary->op.type) {
        // Arithmetic operators
        case TOKEN_PLUS:
        case TOKEN_MINUS:
        case TOKEN_STAR:
        case TOKEN_SLASH:
            if (leftType->kind == TYPE_INT && rightType->kind == TYPE_INT) {
                return createIntType();
            }
            if (leftType->kind == TYPE_FLOAT && rightType->kind == TYPE_FLOAT) {
                return createFloatType();
            }
            if ((leftType->kind == TYPE_INT && rightType->kind == TYPE_FLOAT) ||
                (leftType->kind == TYPE_FLOAT && rightType->kind == TYPE_INT)) {
                return createFloatType();
            }
            // String concatenation
            if (binary->op.type == TOKEN_PLUS &&
                leftType->kind == TYPE_STRING && rightType->kind == TYPE_STRING) {
                return createStringType();
            }
            typeError(checker, expr->line, "Invalid operand types for arithmetic operator.");
            return createErrorType();

        case TOKEN_PERCENT:
            if (leftType->kind == TYPE_INT && rightType->kind == TYPE_INT) {
                return createIntType();
            }
            typeError(checker, expr->line, "Operands of '%' must be integers.");
            return createErrorType();

        // Comparison operators
        case TOKEN_LESS:
        case TOKEN_LESS_EQUAL:
        case TOKEN_GREATER:
        case TOKEN_GREATER_EQUAL:
            if (typeIsNumeric(leftType) && typeIsNumeric(rightType)) {
                return createBoolType();
            }
            typeError(checker, expr->line, "Operands of comparison must be numbers.");
            return createErrorType();

        // Equality operators
        case TOKEN_EQUAL_EQUAL:
        case TOKEN_BANG_EQUAL:
            // Allow comparing same types or numeric types
            if (typesEqual(leftType, rightType) ||
                (typeIsNumeric(leftType) && typeIsNumeric(rightType))) {
                return createBoolType();
            }
            // Allow comparing optional types with nil
            if ((leftType->kind == TYPE_OPTIONAL && rightType->kind == TYPE_NIL) ||
                (leftType->kind == TYPE_NIL && rightType->kind == TYPE_OPTIONAL)) {
                return createBoolType();
            }
            // Allow comparing union types with their constituent types (for type guards)
            if (leftType->kind == TYPE_UNION && typeIsInUnion(rightType, leftType)) {
                return createBoolType();
            }
            if (rightType->kind == TYPE_UNION && typeIsInUnion(leftType, rightType)) {
                return createBoolType();
            }
            typeError(checker, expr->line, "Cannot compare values of different types.");
            return createErrorType();

        // Range operator
        case TOKEN_DOT_DOT:
            if (leftType->kind == TYPE_INT && rightType->kind == TYPE_INT) {
                // Range produces an iterable of ints
                return createArrayType(createIntType());
            }
            typeError(checker, expr->line, "Range bounds must be integers.");
            return createErrorType();

        default:
            return createErrorType();
    }
}

static Type* checkLogical(TypeChecker* checker, Expr* expr) {
    LogicalExpr* logical = &expr->as.logical;
    Type* leftType = checkExpr(checker, logical->left);
    Type* rightType = checkExpr(checker, logical->right);

    if (leftType->kind != TYPE_BOOL) {
        typeError(checker, expr->line, "Left operand of logical operator must be bool.");
    }
    if (rightType->kind != TYPE_BOOL) {
        typeError(checker, expr->line, "Right operand of logical operator must be bool.");
    }

    return createBoolType();
}

static Type* checkNullCoalesce(TypeChecker* checker, Expr* expr) {
    NullCoalesceExpr* coalesce = &expr->as.null_coalesce;
    Type* leftType = checkExpr(checker, coalesce->left);
    Type* rightType = checkExpr(checker, coalesce->right);

    // For x ?? y:
    // - x should be optional (or could be nil)
    // - If y is optional, result is optional
    // - If y is not optional, result is the unwrapped type
    if (leftType->kind == TYPE_OPTIONAL) {
        Type* innerType = leftType->as.optional.innerType;

        // If right is also optional with same inner type, result is optional
        if (rightType->kind == TYPE_OPTIONAL) {
            Type* rightInner = rightType->as.optional.innerType;
            if (!typesEqual(innerType, rightInner) &&
                innerType->kind != TYPE_UNKNOWN && rightInner->kind != TYPE_UNKNOWN) {
                typeError(checker, expr->line, "Both sides of ?? must have compatible types.");
            }
            return rightType;  // Result is still optional
        }

        // Right is non-optional, check it matches inner type
        if (!typeIsAssignableTo(rightType, innerType) && rightType->kind != TYPE_UNKNOWN) {
            typeError(checker, expr->line, "Right operand of ?? must match the inner type of optional.");
        }
        return innerType;  // Result is unwrapped type
    }

    // If left is nil type, result type is the right type
    if (leftType->kind == TYPE_NIL) {
        return rightType;
    }

    // If left is not optional, ?? is a no-op but allow it
    // Type check still ensures right matches left
    if (!typeIsAssignableTo(rightType, leftType) && leftType->kind != TYPE_UNKNOWN) {
        typeError(checker, expr->line, "Types in ?? expression must be compatible.");
    }

    return leftType->kind == TYPE_UNKNOWN ? rightType : leftType;
}

static Type* checkVariable(TypeChecker* checker, Expr* expr) {
    VariableExpr* var = &expr->as.variable;
    Symbol* sym = lookupSymbol(checker, var->name.start, var->name.length);

    if (sym == NULL) {
        typeErrorFormat(checker, expr->line, "Undefined variable '%.*s'.",
                       var->name.length, var->name.start);
        return createErrorType();
    }

    return sym->type;
}

static Type* checkAssign(TypeChecker* checker, Expr* expr) {
    AssignExpr* assign = &expr->as.assign;
    Symbol* sym = lookupSymbol(checker, assign->name.start, assign->name.length);

    if (sym == NULL) {
        typeErrorFormat(checker, expr->line, "Undefined variable '%.*s'.",
                       assign->name.length, assign->name.start);
        return createErrorType();
    }

    if (sym->isConst) {
        typeErrorFormat(checker, expr->line, "Cannot assign to constant '%.*s'.",
                       assign->name.length, assign->name.start);
        return createErrorType();
    }

    Type* valueType = checkExpr(checker, assign->value);

    if (!typeIsAssignableTo(valueType, sym->type)) {
        typeError(checker, expr->line, "Type mismatch in assignment.");
        return createErrorType();
    }

    return sym->type;
}

static Type* checkCall(TypeChecker* checker, Expr* expr) {
    CallExpr* call = &expr->as.call;

    if (call->explicitTypeArgCount > 0 && call->callee->kind == EXPR_VARIABLE) {
        VariableExpr* calleeVar = &call->callee->as.variable;
        Symbol* sym = lookupSymbol(checker, calleeVar->name.start, calleeVar->name.length);
        if (sym != NULL && sym->type->kind == TYPE_GENERIC_CLASS_TEMPLATE) {
            Stmt* stmt = (Stmt*)sym->type->as.genericClassTemplate->classStmt;
            ClassStmt* cs = &stmt->as.class_;
            if (call->explicitTypeArgCount != cs->typeParamCount) {
                typeErrorFormat(checker, expr->line,
                               "Wrong number of type arguments for '%.*s'.",
                               calleeVar->name.length, calleeVar->name.start);
                return createErrorType();
            }
            Type** args = ALLOCATE(Type*, call->explicitTypeArgCount);
            for (int i = 0; i < call->explicitTypeArgCount; i++) {
                args[i] = resolveTypeNode(checker, call->explicitTypeArgs[i]);
            }
            Type* inst = createGenericInstType(sym->type, args, call->explicitTypeArgCount);
            FREE_ARRAY(Type*, args, call->explicitTypeArgCount);
            pushGenericInst(checker, inst, stmt);
            for (int i = 0; i < call->argCount; i++) {
                checkExpr(checker, call->arguments[i]);
            }
            return inst;
        }
    }

    Type* calleeType = checkExpr(checker, call->callee);

    if (calleeType->kind == TYPE_ERROR) {
        return createErrorType();
    }

    if (calleeType->kind == TYPE_GENERIC_CLASS_TEMPLATE) {
        typeError(checker, expr->line,
                  "Generic class constructor requires explicit type arguments (e.g. Box<int>(...)).");
        return createErrorType();
    }

    // Calling a class = instantiation
    if (calleeType->kind == TYPE_CLASS) {
        // Check arguments against init method if we have that info
        // For now, just allow any arguments and return the instance type
        for (int i = 0; i < call->argCount; i++) {
            checkExpr(checker, call->arguments[i]);
        }
        return calleeType;  // Class type is also the instance type
    }

    if (calleeType->kind == TYPE_GENERIC_INST) {
        for (int i = 0; i < call->argCount; i++) {
            checkExpr(checker, call->arguments[i]);
        }
        return calleeType;
    }

    // Allow calling UNKNOWN types (e.g., method calls where we don't track method types)
    if (calleeType->kind == TYPE_UNKNOWN) {
        for (int i = 0; i < call->argCount; i++) {
            checkExpr(checker, call->arguments[i]);
        }
        return createUnknownType();
    }

    if (calleeType->kind != TYPE_FUNCTION) {
        typeError(checker, expr->line, "Can only call functions and classes.");
        return createErrorType();
    }

    FunctionType* funcType = &calleeType->as.function;

    if (call->argCount != funcType->paramCount) {
        typeErrorFormat(checker, expr->line,
                       "Expected %d arguments but got %d.",
                       funcType->paramCount, call->argCount);
        return createErrorType();
    }

    for (int i = 0; i < call->argCount; i++) {
        Type* argType = checkExpr(checker, call->arguments[i]);
        if (!typeIsAssignableTo(argType, funcType->paramTypes[i])) {
            typeErrorFormat(checker, expr->line,
                           "Argument %d type mismatch.", i + 1);
        }
    }

    return funcType->returnType;
}

static Type* checkArray(TypeChecker* checker, Expr* expr) {
    ArrayExpr* array = &expr->as.array;

    if (array->elementCount == 0) {
        // Return an array with unknown element type - can be assigned to any array type
        return createArrayType(createUnknownType());
    }

    // Get element type from first element
    // If it's a spread, extract the element type from the spread array
    Type* elementType = NULL;
    Expr* firstElem = array->elements[0];
    if (firstElem->kind == EXPR_SPREAD) {
        Type* operandType = checkExpr(checker, firstElem->as.spread.operand);
        if (operandType->kind != TYPE_ARRAY) {
            typeError(checker, firstElem->line, "Spread operator can only be used on arrays.");
            return createErrorType();
        }
        elementType = operandType->as.array.elementType;
    } else {
        elementType = checkExpr(checker, firstElem);
    }

    // Check remaining elements
    for (int i = 1; i < array->elementCount; i++) {
        Type* elemType = NULL;
        Expr* elem = array->elements[i];

        if (elem->kind == EXPR_SPREAD) {
            Type* operandType = checkExpr(checker, elem->as.spread.operand);
            if (operandType->kind != TYPE_ARRAY) {
                typeError(checker, elem->line, "Spread operator can only be used on arrays.");
                return createErrorType();
            }
            elemType = operandType->as.array.elementType;
        } else {
            elemType = checkExpr(checker, elem);
        }

        if (!typesEqual(elemType, elementType)) {
            typeError(checker, expr->line, "Array elements must have the same type.");
            return createErrorType();
        }
    }

    return createArrayType(elementType);
}

static Type* checkIndex(TypeChecker* checker, Expr* expr) {
    IndexExpr* idx = &expr->as.index;
    Type* objectType = checkExpr(checker, idx->object);
    Type* indexType = checkExpr(checker, idx->index);

    if (objectType->kind != TYPE_ARRAY) {
        typeError(checker, expr->line, "Can only index into arrays.");
        return createErrorType();
    }

    if (indexType->kind != TYPE_INT) {
        typeError(checker, expr->line, "Array index must be an integer.");
        return createErrorType();
    }

    return objectType->as.array.elementType;
}

static Type* checkIndexSet(TypeChecker* checker, Expr* expr) {
    IndexSetExpr* idx = &expr->as.index_set;
    Type* objectType = checkExpr(checker, idx->object);
    Type* indexType = checkExpr(checker, idx->index);
    Type* valueType = checkExpr(checker, idx->value);

    if (objectType->kind != TYPE_ARRAY) {
        typeError(checker, expr->line, "Can only index into arrays.");
        return createErrorType();
    }

    if (indexType->kind != TYPE_INT) {
        typeError(checker, expr->line, "Array index must be an integer.");
        return createErrorType();
    }

    Type* elementType = objectType->as.array.elementType;
    if (!typeIsAssignableTo(valueType, elementType)) {
        typeError(checker, expr->line, "Type mismatch in array assignment.");
        return createErrorType();
    }

    return valueType;
}

static Type* checkGet(TypeChecker* checker, Expr* expr) {
    GetExpr* get = &expr->as.get;
    Type* objectType = checkExpr(checker, get->object);

    // Handle optional types - unwrap the inner type
    if (objectType->kind == TYPE_OPTIONAL) {
        if (get->isOptional) {
            // Optional chaining on optional type is valid
            objectType = objectType->as.optional.innerType;
        } else {
            typeError(checker, expr->line, "Cannot access property on optional type. Use ?. for optional chaining.");
            return createErrorType();
        }
    }

    // Allow property access on class types (instances) and unknown types
    if (objectType->kind != TYPE_INSTANCE && objectType->kind != TYPE_CLASS &&
        objectType->kind != TYPE_GENERIC_INST &&
        objectType->kind != TYPE_UNKNOWN) {
        typeError(checker, expr->line, "Only instances have properties.");
        return createErrorType();
    }

    // For optional chaining, result type is optional
    Type* resultType = createUnknownType();  // Full field lookup would require class metadata
    if (get->isOptional) {
        return createOptionalType(resultType);
    }
    return resultType;
}

static Type* checkThis(TypeChecker* checker, Expr* expr) {
    if (checker->currentClass == NULL) {
        typeError(checker, expr->line, "'this' can only be used inside a class.");
        return createErrorType();
    }
    return checker->currentClass;
}

static Type* checkLambda(TypeChecker* checker, Expr* expr) {
    LambdaExpr* lambda = &expr->as.lambda;

    // Resolve parameter types
    Type** paramTypes = NULL;
    if (lambda->paramCount > 0) {
        paramTypes = ALLOCATE(Type*, lambda->paramCount);
        for (int i = 0; i < lambda->paramCount; i++) {
            if (lambda->paramTypes[i] != NULL) {
                paramTypes[i] = resolveTypeNode(checker, lambda->paramTypes[i]);
            } else {
                // Lambda parameter without type annotation - infer as UNKNOWN
                paramTypes[i] = createUnknownType();
            }
        }
    }

    // Type check the body in a new scope with parameters defined
    beginScope(checker);

    // Define parameters in scope
    for (int i = 0; i < lambda->paramCount; i++) {
        defineSymbol(checker, lambda->params[i].start, lambda->params[i].length,
                     paramTypes[i], false);
    }

    // Determine return type: either explicit annotation or inferred from expression body.
    Type* returnType;
    if (lambda->returnType != NULL) {
        returnType = resolveTypeNode(checker, lambda->returnType);
    } else {
        returnType = createUnknownType();
    }

    if (lambda->isBlockBody) {
        Type* previousReturn = checker->currentFunctionReturn;
        checker->currentFunctionReturn = returnType;
        checkStmt(checker, lambda->blockBody);
        checker->currentFunctionReturn = previousReturn;
    } else if (lambda->returnType != NULL) {
        Type* bodyType = checkExpr(checker, lambda->body);
        if (!typeIsAssignableTo(bodyType, returnType) &&
            bodyType->kind != TYPE_UNKNOWN && bodyType->kind != TYPE_ERROR) {
            typeError(checker, expr->line, "Lambda body type doesn't match declared return type.");
        }
    } else {
        // Infer return type from expression body.
        returnType = checkExpr(checker, lambda->body);
    }

    endScope(checker);

    return createFunctionType(paramTypes, lambda->paramCount, returnType);
}

static Type* checkMatchPatternExpr(TypeChecker* checker, Expr* expr, Type* valueType, int line) {
    if (expr == NULL) return createErrorType();

    switch (expr->kind) {
        case EXPR_LITERAL:
            return checkLiteral(checker, expr);

        case EXPR_VARIABLE: {
            Symbol* sym = lookupSymbol(checker, expr->as.variable.name.start, expr->as.variable.name.length);
            if (sym == NULL) {
                typeErrorFormat(checker, line, "Undefined variable '%.*s'.",
                                expr->as.variable.name.length, expr->as.variable.name.start);
                return createErrorType();
            }
            Type* st = sym->type;
            if (st->kind == TYPE_FUNCTION) {
                FunctionType* ft = &st->as.function;
                if (typesEqual(ft->returnType, valueType) || valueType->kind == TYPE_UNKNOWN) {
                    return valueType->kind == TYPE_UNKNOWN ? ft->returnType : valueType;
                }
            }
            if (typesEqual(st, valueType)) {
                return st;
            }
            return st;
        }

        case EXPR_CALL: {
            CallExpr* call = &expr->as.call;
            if (call->callee->kind != EXPR_VARIABLE || call->explicitTypeArgCount > 0) {
                return checkExpr(checker, expr);
            }
            VariableExpr* calleeVar = &call->callee->as.variable;
            Symbol* sym = lookupSymbol(checker, calleeVar->name.start, calleeVar->name.length);
            if (sym == NULL) {
                typeErrorFormat(checker, line, "Undefined callee '%.*s'.",
                                calleeVar->name.length, calleeVar->name.start);
                return createErrorType();
            }
            Type* calleeType = sym->type;
            if (calleeType->kind == TYPE_FUNCTION) {
                FunctionType* ft = &calleeType->as.function;
                if (typesEqual(ft->returnType, valueType) || valueType->kind == TYPE_UNKNOWN) {
                    if (call->argCount != ft->paramCount) {
                        typeErrorFormat(checker, line, "Pattern expects %d argument(s) for this variant.",
                                        ft->paramCount);
                        return createErrorType();
                    }
                    for (int i = 0; i < call->argCount; i++) {
                        Expr* arg = call->arguments[i];
                        if (arg->kind == EXPR_VARIABLE) {
                            VariableExpr* av = &arg->as.variable;
                            defineSymbol(checker, av->name.start, av->name.length, ft->paramTypes[i], false);
                        } else {
                            Type* at = checkExpr(checker, arg);
                            if (!typeIsAssignableTo(at, ft->paramTypes[i])) {
                                typeError(checker, line, "Pattern argument type mismatch.");
                            }
                        }
                    }
                    return ft->returnType;
                }
            }
            return checkExpr(checker, expr);
        }

        default:
            return checkExpr(checker, expr);
    }
}

static Type* checkMatchPatternCase(TypeChecker* checker, Expr* pattern, Token* destructureParams,
                                   int destructureCount, Type* valueType, int line) {
    if (pattern == NULL) return createErrorType();

    if (destructureCount > 0 && pattern->kind == EXPR_VARIABLE) {
        VariableExpr* ve = &pattern->as.variable;
        Symbol* sym = lookupSymbol(checker, ve->name.start, ve->name.length);
        if (sym != NULL && sym->type->kind == TYPE_CLASS && typesEqual(sym->type, valueType)) {
            typeErrorFormat(checker, line,
                            "This variant has no payload; remove the '(...)' bindings after the name.");
            return sym->type;
        }
        if (sym != NULL && sym->type->kind == TYPE_FUNCTION) {
            FunctionType* ft = &sym->type->as.function;
            if (typesEqual(ft->returnType, valueType) || valueType->kind == TYPE_UNKNOWN) {
                if (destructureCount != ft->paramCount) {
                    typeErrorFormat(checker, line, "Pattern expects %d binding(s) but this variant has %d field(s).",
                                    destructureCount, ft->paramCount);
                } else {
                    for (int j = 0; j < destructureCount; j++) {
                        defineSymbol(checker, destructureParams[j].start, destructureParams[j].length,
                                     ft->paramTypes[j], false);
                    }
                }
                return ft->returnType;
            }
        }
    }

    Type* patternType = checkMatchPatternExpr(checker, pattern, valueType, line);

    if (destructureCount > 0) {
        for (int j = 0; j < destructureCount; j++) {
            defineSymbol(checker, destructureParams[j].start, destructureParams[j].length,
                         createUnknownType(), false);
        }
    }

    return patternType;
}

static Type* checkMatch(TypeChecker* checker, Expr* expr) {
    MatchExpr* match = &expr->as.match;

    // Check the value being matched
    Type* matchValueType = checkExpr(checker, match->matchValue);

    if (match->caseCount == 0) {
        typeError(checker, expr->line, "Match expression must have at least one case.");
        return createErrorType();
    }

    // Check all case patterns and collect result types
    Type** resultTypes = ALLOCATE(Type*, match->caseCount);
    int resultCount = 0;

    for (int i = 0; i < match->caseCount; i++) {
        ExprCaseClause* caseClause = &match->cases[i];

        beginScope(checker);

        // Check pattern (unless it's a wildcard)
        if (!caseClause->isWildcard && caseClause->pattern != NULL) {
            Type* patternType = checkMatchPatternCase(checker, caseClause->pattern,
                                                      caseClause->destructureParams,
                                                      caseClause->destructureCount,
                                                      matchValueType, expr->line);

            if (!typeIsAssignableTo(patternType, matchValueType) &&
                matchValueType->kind != TYPE_INSTANCE &&
                matchValueType->kind != TYPE_CLASS &&
                matchValueType->kind != TYPE_UNKNOWN) {
                typeError(checker, expr->line,
                    "Match pattern type does not match the value being matched.");
            }
        }

        // Check the result value expression
        Type* valueType = checkExpr(checker, caseClause->value);
        resultTypes[resultCount++] = valueType;

        endScope(checker);
    }

    // Determine the result type: if all arms return the same type, use that;
    // otherwise create a union type
    Type* resultType = resultTypes[0];

    for (int i = 1; i < resultCount; i++) {
        if (!typesEqual(resultType, resultTypes[i])) {
            // Types differ - create a union type
            Type** unionTypes = ALLOCATE(Type*, resultCount);
            for (int j = 0; j < resultCount; j++) {
                unionTypes[j] = resultTypes[j];
            }
            resultType = createUnionType(unionTypes, resultCount);
            break;
        }
    }

    FREE_ARRAY(Type*, resultTypes, match->caseCount);

    return resultType;
}

static Type* checkExpr(TypeChecker* checker, Expr* expr) {
    if (expr == NULL) return createErrorType();

    Type* type = NULL;

    switch (expr->kind) {
        case EXPR_LITERAL:
            type = checkLiteral(checker, expr);
            break;
        case EXPR_UNARY:
            type = checkUnary(checker, expr);
            break;
        case EXPR_BINARY:
            type = checkBinary(checker, expr);
            break;
        case EXPR_GROUPING:
            type = checkExpr(checker, expr->as.grouping.expression);
            break;
        case EXPR_VARIABLE:
            type = checkVariable(checker, expr);
            break;
        case EXPR_ASSIGN:
            type = checkAssign(checker, expr);
            break;
        case EXPR_LOGICAL:
            type = checkLogical(checker, expr);
            break;
        case EXPR_NULL_COALESCE:
            type = checkNullCoalesce(checker, expr);
            break;
        case EXPR_CALL:
            type = checkCall(checker, expr);
            break;
        case EXPR_LAMBDA:
            type = checkLambda(checker, expr);
            break;
        case EXPR_ARRAY:
            type = checkArray(checker, expr);
            break;
        case EXPR_SPREAD:
            // Spread expressions are checked within checkArray context
            // If checked standalone, verify it's an array and return the array type
            type = checkExpr(checker, expr->as.spread.operand);
            if (type->kind != TYPE_ARRAY) {
                typeError(checker, expr->line, "Spread operator can only be used on arrays.");
                return createErrorType();
            }
            // Return the same array type (not the element type)
            break;
        case EXPR_INDEX:
            type = checkIndex(checker, expr);
            break;
        case EXPR_INDEX_SET:
            type = checkIndexSet(checker, expr);
            break;
        case EXPR_GET:
            type = checkGet(checker, expr);
            break;
        case EXPR_SET:
            // Similar to get, plus check value type
            type = checkExpr(checker, expr->as.set.value);
            break;
        case EXPR_THIS:
            type = checkThis(checker, expr);
            break;
        case EXPR_SUPER:
            // TODO: Implement super type checking
            type = createUnknownType();
            break;
        case EXPR_MATCH:
            type = checkMatch(checker, expr);
            break;
    }

    // Store the type in the expression-specific field for use by the compiler
    switch (expr->kind) {
        case EXPR_LITERAL:   expr->as.literal.type = type; break;
        case EXPR_UNARY:     expr->as.unary.type = type; break;
        case EXPR_BINARY:    expr->as.binary.type = type; break;
        case EXPR_GROUPING:  expr->as.grouping.type = type; break;
        case EXPR_VARIABLE:  expr->as.variable.type = type; break;
        case EXPR_ASSIGN:    expr->as.assign.type = type; break;
        case EXPR_LOGICAL:   expr->as.logical.type = type; break;
        case EXPR_NULL_COALESCE: expr->as.null_coalesce.type = type; break;
        case EXPR_CALL:      expr->as.call.type = type; break;
        case EXPR_LAMBDA:    expr->as.lambda.type = type; break;
        case EXPR_ARRAY:     expr->as.array.type = type; break;
        case EXPR_SPREAD:    expr->as.spread.type = type; break;
        case EXPR_INDEX:     expr->as.index.type = type; break;
        case EXPR_INDEX_SET: expr->as.index_set.type = type; break;
        case EXPR_GET:       expr->as.get.type = type; break;
        case EXPR_SET:       expr->as.set.type = type; break;
        case EXPR_THIS:      expr->as.this_.type = type; break;
        case EXPR_SUPER:     expr->as.super_.type = type; break;
        case EXPR_MATCH:     expr->as.match.type = type; break;
    }

    return type;
}

// ============================================================================
// Statement Type Checking
// ============================================================================

static void checkStmt(TypeChecker* checker, Stmt* stmt);

static void checkExpressionStmt(TypeChecker* checker, Stmt* stmt) {
    checkExpr(checker, stmt->as.expression.expression);
}

static void checkPrintStmt(TypeChecker* checker, Stmt* stmt) {
    checkExpr(checker, stmt->as.print.expression);
}

static void checkVarStmt(TypeChecker* checker, Stmt* stmt) {
    VarStmt* var = &stmt->as.var;

    // Handle destructuring
    if (var->destructureKind == DESTRUCTURE_ARRAY) {
        if (var->initializer == NULL) {
            typeError(checker, stmt->line, "Array destructuring requires an initializer.");
            return;
        }

        // Check initializer type - must be an array
        Type* initType = checkExpr(checker, var->initializer);

        if (initType->kind != TYPE_ARRAY) {
            typeError(checker, stmt->line, "Cannot destructure non-array value.");
            return;
        }

        Type* elementType = initType->as.array.elementType;

        // If there's a type annotation, verify it's an array type
        if (var->typeAnnotation != NULL) {
            Type* declaredType = resolveTypeNode(checker, var->typeAnnotation);
            if (declaredType->kind != TYPE_ARRAY) {
                typeError(checker, stmt->line, "Destructuring type annotation must be an array type.");
            } else if (!typeIsAssignableTo(elementType, declaredType->as.array.elementType)) {
                typeError(checker, stmt->line, "Array element type doesn't match declared type.");
            }
        }

        // Define each destructured variable
        for (int i = 0; i < var->destructureCount; i++) {
            Token name = var->destructureNames[i];
            Type* varType;

            // Rest parameter gets array type, others get element type
            if (i == var->restIndex) {
                varType = initType; // Same array type
            } else {
                varType = elementType;
            }

            defineSymbol(checker, name.start, name.length, varType, var->isConst);
        }

        var->type = initType;
        return;
    } else if (var->destructureKind == DESTRUCTURE_OBJECT) {
        if (var->initializer == NULL) {
            typeError(checker, stmt->line, "Object destructuring requires an initializer.");
            return;
        }

        // Check initializer type - must be a class instance
        Type* initType = checkExpr(checker, var->initializer);

        if (initType->kind != TYPE_CLASS && initType->kind != TYPE_INSTANCE && initType->kind != TYPE_UNKNOWN) {
            typeError(checker, stmt->line, "Cannot destructure non-object value.");
            return;
        }

        // Define each destructured variable
        // Note: We use createUnknownType() for each field since we don't have full
        // class metadata at compile time. Property existence will be checked at runtime.
        for (int i = 0; i < var->destructureCount; i++) {
            Token name = var->destructureNames[i];
            Type* fieldType = createUnknownType();
            defineSymbol(checker, name.start, name.length, fieldType, var->isConst);
        }

        var->type = initType;
        return;
    }

    // Simple variable declaration
    Type* declaredType = NULL;
    if (var->typeAnnotation != NULL) {
        declaredType = resolveTypeNode(checker, var->typeAnnotation);
    }

    Type* initType = NULL;
    if (var->initializer != NULL) {
        initType = checkExpr(checker, var->initializer);
    }

    Type* varType;
    if (declaredType != NULL && initType != NULL) {
        // Check that initializer matches declared type
        if (!typeIsAssignableTo(initType, declaredType)) {
            typeError(checker, stmt->line, "Initializer type doesn't match declared type.");
        }
        varType = declaredType;
    } else if (declaredType != NULL) {
        varType = declaredType;
    } else if (initType != NULL) {
        varType = initType;
    } else {
        typeError(checker, stmt->line, "Variable must have type annotation or initializer.");
        varType = createErrorType();
    }

    var->type = varType;
    defineSymbol(checker, var->name.start, var->name.length, varType, var->isConst);
}

static void checkBlockStmt(TypeChecker* checker, Stmt* stmt) {
    beginScope(checker);
    for (int i = 0; i < stmt->as.block.count; i++) {
        checkStmt(checker, stmt->as.block.statements[i]);
    }
    endScope(checker);
}

static void checkIfStmt(TypeChecker* checker, Stmt* stmt) {
    IfStmt* ifStmt = &stmt->as.if_;
    Type* condType = checkExpr(checker, ifStmt->condition);

    if (condType->kind != TYPE_BOOL) {
        typeError(checker, stmt->line, "If condition must be a bool.");
    }

    // Analyze condition for type guards
    TypeGuard guard = analyzeTypeGuard(ifStmt->condition);

    // Check then branch with type narrowing if applicable
    if (guard.isTypeGuard) {
        // Apply narrowing in then-branch
        if (ifStmt->thenBranch->kind == STMT_BLOCK) {
            // Block statement already creates a scope
            beginScope(checker);
            applyTypeNarrowing(checker, &guard);
            for (int i = 0; i < ifStmt->thenBranch->as.block.count; i++) {
                checkStmt(checker, ifStmt->thenBranch->as.block.statements[i]);
            }
            endScope(checker);
        } else {
            // Non-block statement, create a scope
            beginScope(checker);
            applyTypeNarrowing(checker, &guard);
            checkStmt(checker, ifStmt->thenBranch);
            endScope(checker);
        }
    } else {
        checkStmt(checker, ifStmt->thenBranch);
    }

    // Check else branch with inverted type narrowing if applicable
    if (ifStmt->elseBranch != NULL) {
        if (guard.isTypeGuard) {
            // Invert the guard for the else branch
            TypeGuard invertedGuard = guard;
            invertedGuard.isPositive = !guard.isPositive;

            if (ifStmt->elseBranch->kind == STMT_BLOCK) {
                beginScope(checker);
                applyTypeNarrowing(checker, &invertedGuard);
                for (int i = 0; i < ifStmt->elseBranch->as.block.count; i++) {
                    checkStmt(checker, ifStmt->elseBranch->as.block.statements[i]);
                }
                endScope(checker);
            } else {
                beginScope(checker);
                applyTypeNarrowing(checker, &invertedGuard);
                checkStmt(checker, ifStmt->elseBranch);
                endScope(checker);
            }
        } else {
            checkStmt(checker, ifStmt->elseBranch);
        }
    }
}

static void checkWhileStmt(TypeChecker* checker, Stmt* stmt) {
    WhileStmt* whileStmt = &stmt->as.while_;
    Type* condType = checkExpr(checker, whileStmt->condition);

    if (condType->kind != TYPE_BOOL) {
        typeError(checker, stmt->line, "While condition must be a bool.");
    }

    checkStmt(checker, whileStmt->body);
}

static void checkForStmt(TypeChecker* checker, Stmt* stmt) {
    ForStmt* forStmt = &stmt->as.for_;
    Type* iterableType = checkExpr(checker, forStmt->iterable);

    Type* elementType;
    if (iterableType->kind == TYPE_ARRAY) {
        elementType = iterableType->as.array.elementType;
    } else {
        typeError(checker, stmt->line, "For loop iterable must be an array or range.");
        elementType = createErrorType();
    }

    // Check declared type if present
    if (forStmt->varType != NULL) {
        Type* declaredType = resolveTypeNode(checker, forStmt->varType);
        if (!typeIsAssignableTo(elementType, declaredType)) {
            typeError(checker, stmt->line, "Loop variable type doesn't match iterable element type.");
        }
        elementType = declaredType;
    }

    beginScope(checker);
    defineSymbol(checker, forStmt->variable.start, forStmt->variable.length,
                 elementType, false);
    checkStmt(checker, forStmt->body);
    endScope(checker);
}

static void checkFunctionStmt(TypeChecker* checker, Stmt* stmt) {
    FunctionStmt* func = &stmt->as.function;

    // Type parameters are in scope for signature resolution and body checking.
    beginScope(checker);
    for (int i = 0; i < func->typeParamCount; i++) {
        Token tp = func->typeParams[i];
        Type* tpType = createTypeParamType(tp.start, tp.length, i);
        defineSymbol(checker, tp.start, tp.length, tpType, true);
    }

    // Build function type
    Type** paramTypes = NULL;
    if (func->paramCount > 0) {
        paramTypes = ALLOCATE(Type*, func->paramCount);
        for (int i = 0; i < func->paramCount; i++) {
            paramTypes[i] = resolveTypeNode(checker, func->paramTypes[i]);
        }
    }

    Type* returnType = createNilType();
    if (func->returnType != NULL) {
        returnType = resolveTypeNode(checker, func->returnType);
    }

    Type* funcType = createFunctionType(paramTypes, func->paramCount, returnType);
    func->type = funcType;

    endScope(checker);

    // Define the function in current scope
    defineSymbol(checker, func->name.start, func->name.length, funcType, true);

    // Check function body in new scope
    Type* previousReturn = checker->currentFunctionReturn;
    checker->currentFunctionReturn = returnType;

    beginScope(checker);

    // Re-introduce type parameters inside the function body scope.
    for (int i = 0; i < func->typeParamCount; i++) {
        Token tp = func->typeParams[i];
        Type* tpType = createTypeParamType(tp.start, tp.length, i);
        defineSymbol(checker, tp.start, tp.length, tpType, true);
    }

    // Define parameters
    for (int i = 0; i < func->paramCount; i++) {
        defineSymbol(checker, func->params[i].start, func->params[i].length,
                     paramTypes[i], false);
    }

    checkStmt(checker, func->body);

    endScope(checker);
    checker->currentFunctionReturn = previousReturn;
}

static void checkReturnStmt(TypeChecker* checker, Stmt* stmt) {
    ReturnStmt* ret = &stmt->as.return_;

    if (checker->currentFunctionReturn == NULL) {
        typeError(checker, stmt->line, "Return statement outside of function.");
        return;
    }

    Type* returnType = createNilType();
    if (ret->value != NULL) {
        returnType = checkExpr(checker, ret->value);
    }

    // Be lenient with UNKNOWN types (from property access in methods)
    if (returnType->kind == TYPE_UNKNOWN) {
        return;
    }

    if (!typeIsAssignableTo(returnType, checker->currentFunctionReturn)) {
        typeError(checker, stmt->line, "Return type doesn't match function declaration.");
    }
}

static void checkTypeAliasStmt(TypeChecker* checker, Stmt* stmt) {
    TypeAliasStmt* alias = &stmt->as.type_alias;
    Type* target = resolveTypeNode(checker, alias->target);
    defineSymbol(checker, alias->name.start, alias->name.length, target, true);
}

static bool classStmtSatisfiesInterface(TypeChecker* checker, ClassStmt* classStmt, Type* ifaceType) {
    InterfaceType* iface = ifaceType->as.interfaceType;
    for (int i = 0; i < iface->methodCount; i++) {
        const char* want = iface->methodNameStarts[i];
        int wantLen = iface->methodNameLengths[i];
        Type* required = iface->methodSignatures[i];
        bool found = false;
        for (int j = 0; j < classStmt->methodCount; j++) {
            FunctionStmt* m = &classStmt->methods[j];
            if (m->name.length == wantLen && memcmp(m->name.start, want, (size_t)wantLen) == 0) {
                found = true;
                if (!typesEqual(m->type, required)) {
                    typeErrorFormat(checker, classStmt->name.line,
                                    "Method '%.*s' does not match interface.", wantLen, want);
                    return false;
                }
                break;
            }
        }
        if (!found) {
            typeErrorFormat(checker, classStmt->name.line, "Missing interface method '%.*s'.",
                            wantLen, want);
            return false;
        }
    }
    return true;
}

static void applyImplementsToClassType(Type* classType, ClassStmt* classStmt, TypeChecker* checker) {
    if (classStmt->implementsCount == 0) return;
    if (classType->kind != TYPE_CLASS) return;
    ClassType* ct = classType->as.classType;
    ct->implementedInterfaces = ALLOCATE(Type*, classStmt->implementsCount);
    ct->implementedInterfaceCount = classStmt->implementsCount;
    for (int i = 0; i < classStmt->implementsCount; i++) {
        Token iname = classStmt->implementsNames[i];
        Symbol* sym = lookupSymbol(checker, iname.start, iname.length);
        if (sym == NULL || sym->type->kind != TYPE_INTERFACE) {
            typeErrorFormat(checker, classStmt->name.line, "Unknown interface '%.*s'.",
                            iname.length, iname.start);
            ct->implementedInterfaces[i] = createErrorType();
            continue;
        }
        ct->implementedInterfaces[i] = sym->type;
        classStmtSatisfiesInterface(checker, classStmt, sym->type);
    }
}

static void checkInterfaceStmt(TypeChecker* checker, Stmt* stmt) {
    InterfaceStmt* is = &stmt->as.interface_;
    int n = is->methodCount;
    Type** sigs = ALLOCATE(Type*, n);
    const char** starts = ALLOCATE(const char*, n);
    int* lens = ALLOCATE(int, n);
    for (int i = 0; i < n; i++) {
        InterfaceMethodDecl* m = &is->methods[i];
        Type** pts = NULL;
        if (m->paramCount > 0) {
            pts = ALLOCATE(Type*, m->paramCount);
            for (int j = 0; j < m->paramCount; j++) {
                pts[j] = resolveTypeNode(checker, m->paramTypes[j]);
            }
        }
        Type* ret = createNilType();
        if (m->returnType != NULL) ret = resolveTypeNode(checker, m->returnType);
        sigs[i] = createFunctionType(pts, m->paramCount, ret);
        starts[i] = m->name.start;
        lens[i] = m->name.length;
    }
    Type* iface = createInterfaceType(is->name.start, is->name.length, starts, lens, sigs, n);
    FREE_ARRAY(const char*, starts, n);
    FREE_ARRAY(int, lens, n);
    is->type = iface;
    defineSymbol(checker, is->name.start, is->name.length, iface, true);
}

static void verifyImplementsForInstantiation(TypeChecker* checker, Stmt* stmt, ClassStmt* cs,
                                             Type** repl) {
    if (cs->implementsCount == 0) return;
    for (int ii = 0; ii < cs->implementsCount; ii++) {
        Token iname = cs->implementsNames[ii];
        Symbol* sym = lookupSymbol(checker, iname.start, iname.length);
        if (sym == NULL || sym->type->kind != TYPE_INTERFACE) {
            typeErrorFormat(checker, stmt->line, "Unknown interface '%.*s'.",
                            iname.length, iname.start);
            continue;
        }
        InterfaceType* iface = sym->type->as.interfaceType;
        for (int mi = 0; mi < iface->methodCount; mi++) {
            const char* want = iface->methodNameStarts[mi];
            int wantLen = iface->methodNameLengths[mi];
            Type* required = iface->methodSignatures[mi];
            bool found = false;
            for (int j = 0; j < cs->methodCount; j++) {
                FunctionStmt* m = &cs->methods[j];
                if (m->name.length == wantLen && memcmp(m->name.start, want, (size_t)wantLen) == 0) {
                    found = true;
                    FunctionType* ft = &m->type->as.function;
                    Type** subParams = NULL;
                    if (m->paramCount > 0) {
                        subParams = ALLOCATE(Type*, m->paramCount);
                        for (int k = 0; k < m->paramCount; k++) {
                            subParams[k] = substituteTypeInType(ft->paramTypes[k], repl,
                                                                cs->typeParamCount);
                        }
                    }
                    Type* subRet = substituteTypeInType(ft->returnType, repl, cs->typeParamCount);
                    Type* got = createFunctionType(subParams, m->paramCount, subRet);
                    if (!typesEqual(got, required)) {
                        typeErrorFormat(checker, stmt->line,
                                        "Method '%.*s' does not match interface for this generic instantiation.",
                                        wantLen, want);
                    }
                    freeType(got);
                    break;
                }
            }
            if (!found) {
                typeErrorFormat(checker, stmt->line, "Missing interface method '%.*s'.",
                                wantLen, want);
            }
        }
    }
}

static bool typeUsesOnlyClassTypeParams(Type* t, int typeParamCount) {
    if (t == NULL) return true;
    switch (t->kind) {
        case TYPE_TYPE_PARAM:
            return t->as.typeParam.index >= 0 && t->as.typeParam.index < typeParamCount;
        case TYPE_ARRAY:
            return typeUsesOnlyClassTypeParams(t->as.array.elementType, typeParamCount);
        case TYPE_OPTIONAL:
            return typeUsesOnlyClassTypeParams(t->as.optional.innerType, typeParamCount);
        case TYPE_FUNCTION: {
            FunctionType* fn = &t->as.function;
            for (int i = 0; i < fn->paramCount; i++) {
                if (!typeUsesOnlyClassTypeParams(fn->paramTypes[i], typeParamCount)) return false;
            }
            return typeUsesOnlyClassTypeParams(fn->returnType, typeParamCount);
        }
        case TYPE_UNION: {
            UnionType* u = &t->as.unionType;
            for (int i = 0; i < u->typeCount; i++) {
                if (!typeUsesOnlyClassTypeParams(u->types[i], typeParamCount)) return false;
            }
            return true;
        }
        case TYPE_GENERIC_INST: {
            GenericInst* gi = &t->as.genericInst;
            for (int i = 0; i < gi->typeArgCount; i++) {
                if (!typeUsesOnlyClassTypeParams(gi->typeArgs[i], typeParamCount)) return false;
            }
            return true;
        }
        case TYPE_ERROR:
            return true;
        default:
            return true;
    }
}

static void checkClassStmt(TypeChecker* checker, Stmt* stmt) {
    ClassStmt* classStmt = &stmt->as.class_;

    if (classStmt->typeParamCount > 0) {
        Type* templateType = createGenericClassTemplateType(
            classStmt->name.start, classStmt->name.length, stmt);
        if (classStmt->typeParamVariances != NULL) {
            attachGenericTemplateVariances(templateType, classStmt->typeParamVariances,
                                           classStmt->typeParamCount);
        }
        classStmt->type = templateType;
        defineSymbol(checker, classStmt->name.start, classStmt->name.length,
                     templateType, true);

        beginScope(checker);
        for (int i = 0; i < classStmt->typeParamCount; i++) {
            Token tp = classStmt->typeParams[i];
            Type* tpType = createTypeParamType(tp.start, tp.length, i);
            defineSymbol(checker, tp.start, tp.length, tpType, true);
        }

        for (int i = 0; i < classStmt->fieldCount; i++) {
            (void)resolveTypeNode(checker, classStmt->fields[i].type);
        }

        for (int i = 0; i < classStmt->methodCount; i++) {
            FunctionStmt* method = &classStmt->methods[i];
            Type** paramTypes = NULL;
            if (method->paramCount > 0) {
                paramTypes = ALLOCATE(Type*, method->paramCount);
                for (int j = 0; j < method->paramCount; j++) {
                    paramTypes[j] = resolveTypeNode(checker, method->paramTypes[j]);
                }
            }
            Type* returnType = createNilType();
            if (method->returnType != NULL) {
                returnType = resolveTypeNode(checker, method->returnType);
            }
            method->type = createFunctionType(paramTypes, method->paramCount, returnType);
        }

        if (classStmt->superclassType != NULL) {
            classStmt->superclassResolved = resolveTypeNode(checker, classStmt->superclassType);
            Type* st = classStmt->superclassResolved;
            if (st != NULL && st->kind != TYPE_ERROR &&
                !typeUsesOnlyClassTypeParams(st, classStmt->typeParamCount)) {
                typeError(checker, stmt->line,
                          "Superclass may only use this class's type parameters (or concrete types).");
            }
        }

        endScope(checker);
        return;
    }

    // Create class type
    Type* classType = createClassType(classStmt->name.start, classStmt->name.length);
    classStmt->type = classType;

    // Define the class in current scope
    defineSymbol(checker, classStmt->name.start, classStmt->name.length,
                 classType, true);

    if (classStmt->superclassType != NULL) {
        Type* st = resolveTypeNode(checker, classStmt->superclassType);
        classStmt->superclassResolved = st;
        if (st->kind == TYPE_CLASS) {
            classType->as.classType->superclass = st->as.classType;
        }
    }

    // Set current class for 'this' checking
    Type* previousClass = checker->currentClass;
    checker->currentClass = classType;

    // Check methods
    for (int i = 0; i < classStmt->methodCount; i++) {
        FunctionStmt* method = &classStmt->methods[i];

        // Build method type
        Type** paramTypes = NULL;
        if (method->paramCount > 0) {
            paramTypes = ALLOCATE(Type*, method->paramCount);
            for (int j = 0; j < method->paramCount; j++) {
                paramTypes[j] = resolveTypeNode(checker, method->paramTypes[j]);
            }
        }

        Type* returnType = createNilType();
        if (method->returnType != NULL) {
            returnType = resolveTypeNode(checker, method->returnType);
        }

        Type* methodType = createFunctionType(paramTypes, method->paramCount, returnType);
        method->type = methodType;

        // Check method body
        Type* previousReturn = checker->currentFunctionReturn;
        checker->currentFunctionReturn = returnType;

        beginScope(checker);

        // Define parameters
        for (int j = 0; j < method->paramCount; j++) {
            defineSymbol(checker, method->params[j].start, method->params[j].length,
                         paramTypes[j], false);
        }

        checkStmt(checker, method->body);

        endScope(checker);
        checker->currentFunctionReturn = previousReturn;
    }

    applyImplementsToClassType(classType, classStmt, checker);

    checker->currentClass = previousClass;
}

static void checkGenericInstanceBodies(TypeChecker* checker) {
    for (int gi = 0; gi < checker->genericInstCount; gi++) {
        GenericInstToCheck* g = &checker->genericInsts[gi];
        Stmt* stmt = g->templateStmt;
        ClassStmt* cs = &stmt->as.class_;
        Type* inst = g->instType;

        Type** repl = ALLOCATE(Type*, cs->typeParamCount);
        for (int j = 0; j < cs->typeParamCount; j++) {
            repl[j] = inst->as.genericInst.typeArgs[j];
        }

        Type* prevClass = checker->currentClass;
        checker->currentClass = inst;

        for (int mi = 0; mi < cs->methodCount; mi++) {
            FunctionStmt* method = &cs->methods[mi];
            FunctionType* ft = &method->type->as.function;

            Type** paramTypes = NULL;
            if (method->paramCount > 0) {
                paramTypes = ALLOCATE(Type*, method->paramCount);
                for (int j = 0; j < method->paramCount; j++) {
                    paramTypes[j] = substituteTypeInType(ft->paramTypes[j], repl, cs->typeParamCount);
                }
            }
            Type* returnType = substituteTypeInType(ft->returnType, repl, cs->typeParamCount);

            Type* previousReturn = checker->currentFunctionReturn;
            checker->currentFunctionReturn = returnType;

            beginScope(checker);

            Token thisTok;
            thisTok.start = "this";
            thisTok.length = 4;
            thisTok.line = stmt->line;
            defineSymbol(checker, thisTok.start, thisTok.length, inst, false);

            for (int j = 0; j < method->paramCount; j++) {
                defineSymbol(checker, method->params[j].start, method->params[j].length,
                             paramTypes[j], false);
            }

            checkStmt(checker, method->body);

            endScope(checker);
            checker->currentFunctionReturn = previousReturn;

            if (paramTypes != NULL) {
                FREE_ARRAY(Type*, paramTypes, method->paramCount);
            }
        }

        verifyImplementsForInstantiation(checker, stmt, cs, repl);

        FREE_ARRAY(Type*, repl, cs->typeParamCount);
        checker->currentClass = prevClass;
    }
}

static void checkEnumStmt(TypeChecker* checker, Stmt* stmt) {
    EnumStmt* enumStmt = &stmt->as.enum_;

    // Create enum type
    Type* enumType = createClassType(enumStmt->name.start, enumStmt->name.length);
    enumStmt->type = enumType;

    // Define the enum in current scope
    defineSymbol(checker, enumStmt->name.start, enumStmt->name.length, enumType, true);

    // Define variants in current scope as functions or constants
    for (int i = 0; i < enumStmt->variantCount; i++) {
        EnumVariant* v = &enumStmt->variants[i];

        Type* variantType;
        if (v->fieldCount > 0) {
            // Variant with fields is a constructor function
            Type** fieldTypes = ALLOCATE(Type*, v->fieldCount);
            for (int j = 0; j < v->fieldCount; j++) {
                fieldTypes[j] = resolveTypeNode(checker, v->fieldTypes[j]);
            }
            variantType = createFunctionType(fieldTypes, v->fieldCount, enumType);
        } else {
            // Variant without fields is a constant of the enum type
            variantType = enumType;
        }

        defineSymbol(checker, v->name.start, v->name.length, variantType, true);
    }
}

static void checkMatchStmt(TypeChecker* checker, Stmt* stmt) {
    MatchStmt* matchStmt = &stmt->as.match;
    Type* valueType = checkExpr(checker, matchStmt->value);

    for (int i = 0; i < matchStmt->caseCount; i++) {
        CaseClause* c = &matchStmt->cases[i];

        beginScope(checker);

        if (!c->isWildcard && c->pattern != NULL) {
            Type* patternType = checkMatchPatternCase(checker, c->pattern, c->destructureParams,
                                                      c->destructureCount, valueType, stmt->line);
            if (!typeIsAssignableTo(patternType, valueType) &&
                valueType->kind != TYPE_INSTANCE &&
                valueType->kind != TYPE_CLASS &&
                valueType->kind != TYPE_UNKNOWN) {
                typeError(checker, stmt->line, "Match pattern type doesn't match value type.");
            }
        }

        checkStmt(checker, c->body);
        endScope(checker);
    }
}

static void checkStmt(TypeChecker* checker, Stmt* stmt) {
    if (stmt == NULL) return;

    switch (stmt->kind) {
        case STMT_EXPRESSION:
            checkExpressionStmt(checker, stmt);
            break;
        case STMT_PRINT:
            checkPrintStmt(checker, stmt);
            break;
        case STMT_VAR:
            checkVarStmt(checker, stmt);
            break;
        case STMT_BLOCK:
            checkBlockStmt(checker, stmt);
            break;
        case STMT_IF:
            checkIfStmt(checker, stmt);
            break;
        case STMT_WHILE:
            checkWhileStmt(checker, stmt);
            break;
        case STMT_FOR:
            checkForStmt(checker, stmt);
            break;
        case STMT_FUNCTION:
            checkFunctionStmt(checker, stmt);
            break;
        case STMT_RETURN:
            checkReturnStmt(checker, stmt);
            break;
        case STMT_CLASS:
            checkClassStmt(checker, stmt);
            break;
        case STMT_INTERFACE:
            checkInterfaceStmt(checker, stmt);
            break;
        case STMT_TYPE_ALIAS:
            checkTypeAliasStmt(checker, stmt);
            break;
        case STMT_ENUM:
            checkEnumStmt(checker, stmt);
            break;
        case STMT_MATCH:
            checkMatchStmt(checker, stmt);
            break;
        case STMT_TRY: {
            TryStmt* tryStmt = &stmt->as.try_;
            checkStmt(checker, tryStmt->tryBody);

            // Define the exception variable in catch scope
            beginScope(checker);
            defineSymbol(checker, tryStmt->catchVar.start, tryStmt->catchVar.length,
                         createStringType(), false);  // Exception is a string message
            checkStmt(checker, tryStmt->catchBody);
            endScope(checker);

            // Check finally block if present
            if (tryStmt->finallyBody != NULL) {
                checkStmt(checker, tryStmt->finallyBody);
            }
            break;
        }
        case STMT_THROW: {
            ThrowStmt* throwStmt = &stmt->as.throw_;
            checkExpr(checker, throwStmt->value);
            // Any value can be thrown
            break;
        }
        case STMT_IMPORT: {
            // Import type checking is handled during module loading
            // The symbols will be added when the module is executed
            break;
        }
    }
}

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
    checker->hadError = false;
    checker->genericInsts = NULL;
    checker->genericInstCount = 0;
    checker->genericInstCapacity = 0;

    // Define built-in native functions

    // Time functions
    defineNativeFunction(checker, "clock", NULL, 0, createFloatType());
    defineNativeFunction(checker, "time", NULL, 0, createIntType());

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
}

void freeTypeChecker(TypeChecker* checker) {
    FREE_ARRAY(GenericInstToCheck, checker->genericInsts, checker->genericInstCapacity);
    checker->genericInsts = NULL;
    checker->genericInstCount = 0;
    checker->genericInstCapacity = 0;
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
