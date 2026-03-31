// ============================================================================
// Expression Type Checking
// ============================================================================


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

        case TOKEN_TILDE:
            if (operandType->kind == TYPE_INT) {
                return createIntType();
            }
            typeError(checker, expr->line, "Operand of '~' must be an int.");
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

        // Bitwise operators (integers only)
        case TOKEN_AMPERSAND:
        case TOKEN_PIPE:
        case TOKEN_CARET:
            if (leftType->kind == TYPE_INT && rightType->kind == TYPE_INT) {
                return createIntType();
            }
            typeError(checker, expr->line, "Bitwise operators require int operands.");
            return createErrorType();

        // Shifts (integers only)
        case TOKEN_LSHIFT:
        case TOKEN_RSHIFT:
            if (leftType->kind == TYPE_INT && rightType->kind == TYPE_INT) {
                return createIntType();
            }
            typeError(checker, expr->line, "Shift operators require int operands.");
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

static Stmt* classStmtFromType(TypeChecker* checker, Type* type) {
    if (type == NULL) return NULL;
    if (type->kind == TYPE_CLASS) {
        return findClassDecl(checker, type);
    }
    if (type->kind == TYPE_GENERIC_INST) {
        Type* genericType = type->as.genericInst.genericType;
        if (genericType != NULL && genericType->kind == TYPE_GENERIC_CLASS_TEMPLATE) {
            return (Stmt*)genericType->as.genericClassTemplate->classStmt;
        }
    }
    return NULL;
}

static Type* checkSuper(TypeChecker* checker, Expr* expr) {
    if (checker->currentClass == NULL || checker->currentClassStmt == NULL) {
        typeError(checker, expr->line, "'super' can only be used inside a class method.");
        return createErrorType();
    }

    ClassStmt* currentClassStmt = &checker->currentClassStmt->as.class_;
    Type* superType = currentClassStmt->superclassResolved;
    if (superType == NULL) {
        typeError(checker, expr->line, "Cannot use 'super' in a class with no superclass.");
        return createErrorType();
    }
    if (superType->kind != TYPE_CLASS && superType->kind != TYPE_GENERIC_INST) {
        typeError(checker, expr->line, "Superclass type is invalid for 'super' access.");
        return createErrorType();
    }

    Stmt* superStmtRaw = classStmtFromType(checker, superType);
    if (superStmtRaw == NULL || superStmtRaw->kind != STMT_CLASS) {
        typeError(checker, expr->line, "Could not resolve superclass declaration for 'super'.");
        return createErrorType();
    }

    ClassStmt* superClassStmt = &superStmtRaw->as.class_;
    SuperExpr* superExpr = &expr->as.super_;
    if (superType->kind == TYPE_GENERIC_INST && superClassStmt->typeParamCount > 0) {
        GenericInst* sgi = &superType->as.genericInst;
        if (sgi->typeArgCount != superClassStmt->typeParamCount) {
            typeError(checker, expr->line,
                      "Superclass type argument count does not match its declaration for 'super'.");
            return createErrorType();
        }
    }

    for (int i = 0; i < superClassStmt->methodCount; i++) {
        FunctionStmt* method = &superClassStmt->methods[i];
        if (method->name.length == superExpr->method.length &&
            memcmp(method->name.start, superExpr->method.start, (size_t)superExpr->method.length) == 0) {
            Type* result;
            if (method->type == NULL) {
                result = createUnknownType();
            } else if (superType->kind == TYPE_GENERIC_INST && superClassStmt->typeParamCount > 0) {
                result = substituteTypeInType(method->type,
                                              superType->as.genericInst.typeArgs,
                                              superClassStmt->typeParamCount);
            } else {
                result = method->type;
            }

            /* Inside a generic class instantiation, map the subclass type parameters
             * (e.g. T) to concrete type arguments (e.g. int) so super calls are checked
             * against the monomorphized signature. */
            if (checker->currentClass != NULL &&
                checker->currentClass->kind == TYPE_GENERIC_INST &&
                checker->currentClassStmt != NULL) {
                ClassStmt* childStmt = &checker->currentClassStmt->as.class_;
                if (childStmt->typeParamCount > 0) {
                    GenericInst* ci = &checker->currentClass->as.genericInst;
                    if (ci->typeArgCount != childStmt->typeParamCount) {
                        typeError(checker, expr->line,
                                  "Generic class type arguments do not match type parameters.");
                        return createErrorType();
                    }
                    result = substituteTypeInType(result, ci->typeArgs, childStmt->typeParamCount);
                }
            }
            return result;
        }
    }

    typeErrorFormat(checker, expr->line, "Superclass has no method '%.*s'.",
                    superExpr->method.length, superExpr->method.start);
    return createErrorType();
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
            type = checkSuper(checker, expr);
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

