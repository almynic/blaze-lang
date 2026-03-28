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

            // Could be a class name - look it up
            Symbol* sym = lookupSymbol(checker, name.start, name.length);
            if (sym != NULL && sym->type->kind == TYPE_CLASS) {
                return sym->type;
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
    }

    return createErrorType();
}

// ============================================================================
// Expression Type Checking
// ============================================================================

static Type* checkExpr(TypeChecker* checker, Expr* expr);
static Type* checkMatch(TypeChecker* checker, Expr* expr);

static Type* checkLiteral(TypeChecker* checker, Expr* expr) {
    (void)checker;
    LiteralExpr* lit = &expr->as.literal;

    switch (lit->value.type) {
        case VAL_NIL:   return createNilType();
        case VAL_BOOL:  return createBoolType();
        case VAL_INT:   return createIntType();
        case VAL_FLOAT: return createFloatType();
        case VAL_OBJ:   return createStringType(); // Assuming string for now
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

    // If either operand is UNKNOWN (e.g., from property access), be lenient
    if (leftType->kind == TYPE_UNKNOWN || rightType->kind == TYPE_UNKNOWN) {
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
    Type* calleeType = checkExpr(checker, call->callee);

    if (calleeType->kind == TYPE_ERROR) {
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

    // Determine return type: either explicit annotation or inferred from body
    Type* returnType;
    if (lambda->returnType != NULL) {
        returnType = resolveTypeNode(checker, lambda->returnType);
        // Check that body matches declared return type
        Type* bodyType = checkExpr(checker, lambda->body);
        if (!typeIsAssignableTo(bodyType, returnType) &&
            bodyType->kind != TYPE_UNKNOWN && bodyType->kind != TYPE_ERROR) {
            typeError(checker, expr->line, "Lambda body type doesn't match declared return type.");
        }
    } else {
        // Infer return type from body expression
        returnType = checkExpr(checker, lambda->body);
    }

    endScope(checker);

    return createFunctionType(paramTypes, lambda->paramCount, returnType);
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

        // Check pattern (unless it's a wildcard)
        if (!caseClause->isWildcard) {
            Type* patternType = checkExpr(checker, caseClause->pattern);

            // Pattern should be assignable to match value type
            if (!typeIsAssignableTo(patternType, matchValueType)) {
                typeError(checker, expr->line,
                    "Match pattern type does not match the value being matched.");
            }
        }

        // Check the result value expression
        Type* valueType = checkExpr(checker, caseClause->value);
        resultTypes[resultCount++] = valueType;
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

    // Define the function in current scope
    defineSymbol(checker, func->name.start, func->name.length, funcType, true);

    // Check function body in new scope
    Type* previousReturn = checker->currentFunctionReturn;
    checker->currentFunctionReturn = returnType;

    beginScope(checker);

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

static void checkClassStmt(TypeChecker* checker, Stmt* stmt) {
    ClassStmt* classStmt = &stmt->as.class_;

    // Create class type
    Type* classType = createClassType(classStmt->name.start, classStmt->name.length);
    classStmt->type = classType;

    // Define the class in current scope
    defineSymbol(checker, classStmt->name.start, classStmt->name.length,
                 classType, true);

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

    checker->currentClass = previousClass;
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
        case STMT_MATCH: {
            MatchStmt* matchStmt = &stmt->as.match;
            Type* valueType = checkExpr(checker, matchStmt->value);

            for (int i = 0; i < matchStmt->caseCount; i++) {
                CaseClause* c = &matchStmt->cases[i];
                if (!c->isWildcard && c->pattern != NULL) {
                    Type* patternType = checkExpr(checker, c->pattern);
                    // Check that pattern type matches value type
                    if (!typesEqual(valueType, patternType) &&
                        valueType->kind != TYPE_UNKNOWN &&
                        patternType->kind != TYPE_UNKNOWN) {
                        typeError(checker, stmt->line, "Match pattern type doesn't match value type.");
                    }
                }
                checkStmt(checker, c->body);
            }
            break;
        }
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
    freeSymbolTable(&checker->symbols);
}

bool typeCheck(TypeChecker* checker, Stmt** statements, int count) {
    for (int i = 0; i < count; i++) {
        checkStmt(checker, statements[i]);
    }
    return !checker->hadError;
}
