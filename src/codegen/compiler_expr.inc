// ============================================================================
// Expression Compilation
// ============================================================================

static void compileExpr(Expr* expr);
static void compileMatchExpr(Expr* expr);

static void compileLiteral(Expr* expr) {
    LiteralExpr* lit = &expr->as.literal;

    switch (lit->token.type) {
        case TOKEN_NIL:
            emitByte(expr->line, OP_NIL);
            break;
        case TOKEN_TRUE:
            emitByte(expr->line, OP_TRUE);
            break;
        case TOKEN_FALSE:
            emitByte(expr->line, OP_FALSE);
            break;
        case TOKEN_INT:
            emitConstant(expr->line, INT_VAL(strtoll(lit->token.start, NULL, 10)));
            break;
        case TOKEN_FLOAT:
            emitConstant(expr->line, FLOAT_VAL(strtod(lit->token.start, NULL)));
            break;
        case TOKEN_STRING: {
            // Use the value directly if it's already set (for interpolated strings)
            // Otherwise, create string object from token (skip the quotes)
            if (IS_OBJ(lit->value) && AS_OBJ(lit->value)->type == OBJ_STRING) {
                emitConstant(expr->line, lit->value);
            } else {
                const char* start = lit->token.start + 1;
                int length = lit->token.length - 2;
                ObjString* string = copyString(start, length);
                emitConstant(expr->line, OBJ_VAL(string));
            }
            break;
        }
        default:
            compileError(expr->line, "Unknown literal type.");
    }
}

static void compileUnary(Expr* expr) {
    UnaryExpr* unary = &expr->as.unary;

    // Constant folding: if operand is a literal, evaluate at compile time
    if (unary->operand->kind == EXPR_LITERAL) {
        Value operand = unary->operand->as.literal.value;
        Value result;
        bool canFold = true;

        switch (unary->op.type) {
            case TOKEN_MINUS:
                if (IS_INT(operand)) {
                    result = INT_VAL(-AS_INT(operand));
                } else if (IS_FLOAT(operand)) {
                    result = FLOAT_VAL(-AS_FLOAT(operand));
                } else {
                    canFold = false;
                }
                break;

            case TOKEN_BANG:
                if (IS_BOOL(operand)) {
                    result = BOOL_VAL(!AS_BOOL(operand));
                } else {
                    canFold = false;
                }
                break;

            case TOKEN_TILDE:
                if (IS_INT(operand)) {
                    result = INT_VAL(~AS_INT(operand));
                } else {
                    canFold = false;
                }
                break;

            default:
                canFold = false;
                break;
        }

        if (canFold) {
            emitConstant(expr->line, result);
            return;
        }
    }

    // Non-constant expression, compile normally
    compileExpr(unary->operand);

    switch (unary->op.type) {
        case TOKEN_MINUS:
            if (unary->type && unary->type->kind == TYPE_FLOAT) {
                emitByte(expr->line, OP_NEGATE_FLOAT);
            } else {
                emitByte(expr->line, OP_NEGATE_INT);
            }
            break;
        case TOKEN_BANG:
            emitByte(expr->line, OP_NOT);
            break;
        case TOKEN_TILDE:
            emitByte(expr->line, OP_BITWISE_NOT_INT);
            break;
        default:
            compileError(expr->line, "Unknown unary operator.");
    }
}

static void compileBinary(Expr* expr) {
    BinaryExpr* binary = &expr->as.binary;

    // Constant folding: if both operands are literals, evaluate at compile time
    if (binary->left->kind == EXPR_LITERAL && binary->right->kind == EXPR_LITERAL) {
        Value left = binary->left->as.literal.value;
        Value right = binary->right->as.literal.value;
        Value result;
        bool canFold = true;

        switch (binary->op.type) {
            // Arithmetic (ints, floats, mixed) + string concatenation
            case TOKEN_PLUS:
                if (IS_INT(left) && IS_INT(right)) {
                    result = INT_VAL(AS_INT(left) + AS_INT(right));
                } else if (IS_FLOAT(left) && IS_FLOAT(right)) {
                    result = FLOAT_VAL(AS_FLOAT(left) + AS_FLOAT(right));
                } else if (IS_INT(left) && IS_FLOAT(right)) {
                    result = FLOAT_VAL((double)AS_INT(left) + AS_FLOAT(right));
                } else if (IS_FLOAT(left) && IS_INT(right)) {
                    result = FLOAT_VAL(AS_FLOAT(left) + (double)AS_INT(right));
                } else if (IS_OBJ(left) && IS_OBJ(right) &&
                           AS_OBJ(left)->type == OBJ_STRING &&
                           AS_OBJ(right)->type == OBJ_STRING) {
                    ObjString* a = AS_STRING(left);
                    ObjString* b = AS_STRING(right);
                    int length = a->length + b->length;
                    char* chars = ALLOCATE(char, length + 1);
                    memcpy(chars, a->chars, a->length);
                    memcpy(chars + a->length, b->chars, b->length);
                    chars[length] = '\0';
                    result = OBJ_VAL(takeString(chars, length));
                } else {
                    canFold = false;
                }
                break;

            case TOKEN_MINUS:
                if (IS_INT(left) && IS_INT(right)) {
                    result = INT_VAL(AS_INT(left) - AS_INT(right));
                } else if (IS_FLOAT(left) && IS_FLOAT(right)) {
                    result = FLOAT_VAL(AS_FLOAT(left) - AS_FLOAT(right));
                } else if (IS_INT(left) && IS_FLOAT(right)) {
                    result = FLOAT_VAL((double)AS_INT(left) - AS_FLOAT(right));
                } else if (IS_FLOAT(left) && IS_INT(right)) {
                    result = FLOAT_VAL(AS_FLOAT(left) - (double)AS_INT(right));
                } else {
                    canFold = false;
                }
                break;

            case TOKEN_STAR:
                if (IS_INT(left) && IS_INT(right)) {
                    result = INT_VAL(AS_INT(left) * AS_INT(right));
                } else if (IS_FLOAT(left) && IS_FLOAT(right)) {
                    result = FLOAT_VAL(AS_FLOAT(left) * AS_FLOAT(right));
                } else if (IS_INT(left) && IS_FLOAT(right)) {
                    result = FLOAT_VAL((double)AS_INT(left) * AS_FLOAT(right));
                } else if (IS_FLOAT(left) && IS_INT(right)) {
                    result = FLOAT_VAL(AS_FLOAT(left) * (double)AS_INT(right));
                } else {
                    canFold = false;
                }
                break;

            case TOKEN_SLASH:
                if (IS_INT(left) && IS_INT(right)) {
                    if (AS_INT(right) == 0) {
                        canFold = false;  // Leave division-by-zero to runtime path.
                    } else {
                        result = INT_VAL(AS_INT(left) / AS_INT(right));
                    }
                } else if (IS_FLOAT(left) && IS_FLOAT(right)) {
                    result = FLOAT_VAL(AS_FLOAT(left) / AS_FLOAT(right));
                } else if (IS_INT(left) && IS_FLOAT(right)) {
                    result = FLOAT_VAL((double)AS_INT(left) / AS_FLOAT(right));
                } else if (IS_FLOAT(left) && IS_INT(right)) {
                    result = FLOAT_VAL(AS_FLOAT(left) / (double)AS_INT(right));
                } else {
                    canFold = false;
                }
                break;

            case TOKEN_PERCENT:
                if (IS_INT(left) && IS_INT(right)) {
                    if (AS_INT(right) == 0) {
                        canFold = false;  // Leave modulo-by-zero to runtime path.
                    } else {
                        result = INT_VAL(AS_INT(left) % AS_INT(right));
                    }
                } else {
                    canFold = false;
                }
                break;

            // Bitwise operators on integers
            case TOKEN_AMPERSAND:
                if (IS_INT(left) && IS_INT(right)) {
                    result = INT_VAL(AS_INT(left) & AS_INT(right));
                } else {
                    canFold = false;
                }
                break;

            case TOKEN_PIPE:
                if (IS_INT(left) && IS_INT(right)) {
                    result = INT_VAL(AS_INT(left) | AS_INT(right));
                } else {
                    canFold = false;
                }
                break;

            case TOKEN_CARET:
                if (IS_INT(left) && IS_INT(right)) {
                    result = INT_VAL(AS_INT(left) ^ AS_INT(right));
                } else {
                    canFold = false;
                }
                break;

            // Shifts on integers
            case TOKEN_LSHIFT: {
                if (IS_INT(left) && IS_INT(right)) {
                    int64_t shift = AS_INT(right);
                    if (shift < 0 || shift >= 64) {
                        canFold = false;
                    } else {
                        result = INT_VAL(AS_INT(left) << shift);
                    }
                } else {
                    canFold = false;
                }
                break;
            }

            case TOKEN_RSHIFT: {
                if (IS_INT(left) && IS_INT(right)) {
                    int64_t shift = AS_INT(right);
                    if (shift < 0 || shift >= 64) {
                        canFold = false;
                    } else {
                        result = INT_VAL(AS_INT(left) >> shift);
                    }
                } else {
                    canFold = false;
                }
                break;
            }

            // Comparison operators
            case TOKEN_LESS:
                if (IS_INT(left) && IS_INT(right)) {
                    result = BOOL_VAL(AS_INT(left) < AS_INT(right));
                } else if (IS_FLOAT(left) && IS_FLOAT(right)) {
                    result = BOOL_VAL(AS_FLOAT(left) < AS_FLOAT(right));
                } else {
                    canFold = false;
                }
                break;

            case TOKEN_LESS_EQUAL:
                if (IS_INT(left) && IS_INT(right)) {
                    result = BOOL_VAL(AS_INT(left) <= AS_INT(right));
                } else if (IS_FLOAT(left) && IS_FLOAT(right)) {
                    result = BOOL_VAL(AS_FLOAT(left) <= AS_FLOAT(right));
                } else {
                    canFold = false;
                }
                break;

            case TOKEN_GREATER:
                if (IS_INT(left) && IS_INT(right)) {
                    result = BOOL_VAL(AS_INT(left) > AS_INT(right));
                } else if (IS_FLOAT(left) && IS_FLOAT(right)) {
                    result = BOOL_VAL(AS_FLOAT(left) > AS_FLOAT(right));
                } else {
                    canFold = false;
                }
                break;

            case TOKEN_GREATER_EQUAL:
                if (IS_INT(left) && IS_INT(right)) {
                    result = BOOL_VAL(AS_INT(left) >= AS_INT(right));
                } else if (IS_FLOAT(left) && IS_FLOAT(right)) {
                    result = BOOL_VAL(AS_FLOAT(left) >= AS_FLOAT(right));
                } else {
                    canFold = false;
                }
                break;

            case TOKEN_EQUAL_EQUAL:
                // Simple equality for constants
                if (IS_BOOL(left) && IS_BOOL(right)) {
                    result = BOOL_VAL(AS_BOOL(left) == AS_BOOL(right));
                } else if (IS_NIL(left) && IS_NIL(right)) {
                    result = BOOL_VAL(true);
                } else if (IS_INT(left) && IS_INT(right)) {
                    result = BOOL_VAL(AS_INT(left) == AS_INT(right));
                } else if (IS_FLOAT(left) && IS_FLOAT(right)) {
                    result = BOOL_VAL(AS_FLOAT(left) == AS_FLOAT(right));
                } else if (IS_OBJ(left) && IS_OBJ(right)) {
                    result = BOOL_VAL(AS_OBJ(left) == AS_OBJ(right));  // Pointer equality
                } else {
                    result = BOOL_VAL(false);
                }
                break;

            case TOKEN_BANG_EQUAL:
                // Simple inequality for constants
                if (IS_BOOL(left) && IS_BOOL(right)) {
                    result = BOOL_VAL(AS_BOOL(left) != AS_BOOL(right));
                } else if (IS_NIL(left) && IS_NIL(right)) {
                    result = BOOL_VAL(false);
                } else if (IS_INT(left) && IS_INT(right)) {
                    result = BOOL_VAL(AS_INT(left) != AS_INT(right));
                } else if (IS_FLOAT(left) && IS_FLOAT(right)) {
                    result = BOOL_VAL(AS_FLOAT(left) != AS_FLOAT(right));
                } else if (IS_OBJ(left) && IS_OBJ(right)) {
                    result = BOOL_VAL(AS_OBJ(left) != AS_OBJ(right));
                } else {
                    result = BOOL_VAL(true);
                }
                break;

            default:
                canFold = false;
                break;
        }

        if (canFold) {
            // Emit the folded constant directly
            emitConstant(expr->line, result);
            return;
        }
    }

    // Non-constant expression, compile normally
    compileExpr(binary->left);
    compileExpr(binary->right);

    bool isFloat = (binary->type && binary->type->kind == TYPE_FLOAT);
    bool isUnknown = (!binary->type || binary->type->kind == TYPE_UNKNOWN);
    bool isString = (binary->type && binary->type->kind == TYPE_STRING);

    switch (binary->op.type) {
        case TOKEN_PLUS:
            if (isString) {
                emitByte(expr->line, OP_CONCAT);
            } else if (isUnknown) {
                emitByte(expr->line, OP_ADD_MIXED);
            } else {
                emitByte(expr->line, isFloat ? OP_ADD_FLOAT : OP_ADD_INT);
            }
            break;
        case TOKEN_MINUS:
            emitByte(expr->line, isUnknown ? OP_SUBTRACT_MIXED
                                         : (isFloat ? OP_SUBTRACT_FLOAT : OP_SUBTRACT_INT));
            break;
        case TOKEN_STAR:
            emitByte(expr->line, isUnknown ? OP_MULTIPLY_MIXED
                                           : (isFloat ? OP_MULTIPLY_FLOAT : OP_MULTIPLY_INT));
            break;
        case TOKEN_SLASH:
            emitByte(expr->line, isUnknown ? OP_DIVIDE_MIXED
                                           : (isFloat ? OP_DIVIDE_FLOAT : OP_DIVIDE_INT));
            break;
        case TOKEN_PERCENT:
            emitByte(expr->line, OP_MODULO_INT);
            break;

        case TOKEN_AMPERSAND:
            emitByte(expr->line, OP_BITWISE_AND_INT);
            break;

        case TOKEN_PIPE:
            emitByte(expr->line, OP_BITWISE_OR_INT);
            break;

        case TOKEN_CARET:
            emitByte(expr->line, OP_BITWISE_XOR_INT);
            break;

        case TOKEN_LSHIFT:
            emitByte(expr->line, OP_SHIFT_LEFT_INT);
            break;

        case TOKEN_RSHIFT:
            emitByte(expr->line, OP_SHIFT_RIGHT_INT);
            break;
        case TOKEN_EQUAL_EQUAL:
            emitByte(expr->line, OP_EQUAL);
            break;
        case TOKEN_BANG_EQUAL:
            emitByte(expr->line, OP_NOT_EQUAL);
            break;
        case TOKEN_LESS:
            emitByte(expr->line, OP_LESS);
            break;
        case TOKEN_LESS_EQUAL:
            emitByte(expr->line, OP_LESS_EQUAL);
            break;
        case TOKEN_GREATER:
            emitByte(expr->line, OP_GREATER);
            break;
        case TOKEN_GREATER_EQUAL:
            emitByte(expr->line, OP_GREATER_EQUAL);
            break;
        case TOKEN_DOT_DOT:
            emitByte(expr->line, OP_RANGE);
            break;
        default:
            compileError(expr->line, "Unknown binary operator.");
    }
}

static void compileLogical(Expr* expr) {
    LogicalExpr* logical = &expr->as.logical;

    // Constant folding: if both operands are boolean literals, evaluate at compile time
    if (logical->left->kind == EXPR_LITERAL && logical->right->kind == EXPR_LITERAL) {
        Value left = logical->left->as.literal.value;
        Value right = logical->right->as.literal.value;

        if (IS_BOOL(left) && IS_BOOL(right)) {
            Value result;
            if (logical->op.type == TOKEN_AND) {
                result = BOOL_VAL(AS_BOOL(left) && AS_BOOL(right));
            } else {  // TOKEN_OR
                result = BOOL_VAL(AS_BOOL(left) || AS_BOOL(right));
            }
            emitConstant(expr->line, result);
            return;
        }
    }

    // Partial folding: if left is a constant boolean, we can sometimes short-circuit
    if (logical->left->kind == EXPR_LITERAL) {
        Value left = logical->left->as.literal.value;
        if (IS_BOOL(left)) {
            if (logical->op.type == TOKEN_AND) {
                if (!AS_BOOL(left)) {
                    // false && anything = false
                    emitConstant(expr->line, BOOL_VAL(false));
                    return;
                } else {
                    // true && right = right
                    compileExpr(logical->right);
                    return;
                }
            } else {  // TOKEN_OR
                if (AS_BOOL(left)) {
                    // true || anything = true
                    emitConstant(expr->line, BOOL_VAL(true));
                    return;
                } else {
                    // false || right = right
                    compileExpr(logical->right);
                    return;
                }
            }
        }
    }

    // Non-constant expression, compile with short-circuit evaluation
    if (logical->op.type == TOKEN_AND) {
        compileExpr(logical->left);
        int endJump = emitJump(expr->line, OP_JUMP_IF_FALSE);
        emitByte(expr->line, OP_POP);
        compileExpr(logical->right);
        patchJump(endJump);
    } else {
        compileExpr(logical->left);
        int elseJump = emitJump(expr->line, OP_JUMP_IF_FALSE);
        int endJump = emitJump(expr->line, OP_JUMP);
        patchJump(elseJump);
        emitByte(expr->line, OP_POP);
        compileExpr(logical->right);
        patchJump(endJump);
    }
}

static void compileNullCoalesce(Expr* expr) {
    NullCoalesceExpr* coalesce = &expr->as.null_coalesce;

    // For x ?? y:
    // - Evaluate x
    // - If x is NOT nil, use x (skip y)
    // - If x is nil, pop x and evaluate y

    compileExpr(coalesce->left);               // [x]
    emitByte(expr->line, OP_DUP);              // [x, x]
    emitByte(expr->line, OP_NIL);              // [x, x, nil]
    emitByte(expr->line, OP_EQUAL);            // [x, isNil?]
    int useLeftJump = emitJump(expr->line, OP_JUMP_IF_FALSE);
    // x is nil - pop isNil? and x, evaluate y
    emitByte(expr->line, OP_POP);              // [x]
    emitByte(expr->line, OP_POP);              // []
    compileExpr(coalesce->right);              // [y]
    int endJump = emitJump(expr->line, OP_JUMP);
    // x is not nil - pop isNil?, keep x
    patchJump(useLeftJump);
    emitByte(expr->line, OP_POP);              // [x]
    patchJump(endJump);
}

static void compileVariable(Expr* expr) {
    VariableExpr* var = &expr->as.variable;
    int arg = resolveLocal(current, &var->name);

    if (arg != -1) {
        emitBytes(expr->line, OP_GET_LOCAL, (uint8_t)arg);
    } else if ((arg = resolveUpvalue(current, &var->name)) != -1) {
        emitBytes(expr->line, OP_GET_UPVALUE, (uint8_t)arg);
    } else {
        // Global variable - store the name as a constant for lookup
        ObjString* name = copyString(var->name.start, var->name.length);
        uint8_t constant = makeConstant(OBJ_VAL(name));
        emitBytes(expr->line, OP_GET_GLOBAL, constant);
    }
}

static void compileAssign(Expr* expr) {
    AssignExpr* assign = &expr->as.assign;

    compileExpr(assign->value);

    int arg = resolveLocal(current, &assign->name);
    if (arg != -1) {
        emitBytes(expr->line, OP_SET_LOCAL, (uint8_t)arg);
    } else if ((arg = resolveUpvalue(current, &assign->name)) != -1) {
        emitBytes(expr->line, OP_SET_UPVALUE, (uint8_t)arg);
    } else {
        // Global variable - use the variable name as constant
        ObjString* name = copyString(assign->name.start, assign->name.length);
        uint8_t constant = makeConstant(OBJ_VAL(name));
        emitBytes(expr->line, OP_SET_GLOBAL, constant);
    }
}

static void compileCall(Expr* expr) {
    CallExpr* call = &expr->as.call;

    if (call->explicitTypeArgCount > 0 && call->type != NULL &&
        call->type->kind == TYPE_GENERIC_INST && call->callee->kind == EXPR_VARIABLE) {
        char buf[512];
        mangleGenericInstType(call->type, buf, sizeof(buf));
        ObjString* name = copyString(buf, (int)strlen(buf));
        emitBytes(expr->line, OP_GET_GLOBAL, makeConstant(OBJ_VAL(name)));
        for (int i = 0; i < call->argCount; i++) {
            compileExpr(call->arguments[i]);
        }
        emitBytes(expr->line, OP_CALL, (uint8_t)call->argCount);
        return;
    }

    // Check if this is a call to print (built-in)
    if (call->callee->kind == EXPR_VARIABLE) {
        VariableExpr* callee = &call->callee->as.variable;
        if (callee->name.length == 5 &&
            memcmp(callee->name.start, "print", 5) == 0) {
            // Compile print as a built-in
            if (call->argCount == 1) {
                compileExpr(call->arguments[0]);
                emitByte(expr->line, OP_PRINT);
                return;
            }
        }
    }

    // Check if this is a super method call - optimize with OP_SUPER_INVOKE
    if (call->callee->kind == EXPR_SUPER) {
        if (currentClass == NULL) {
            compileError(expr->line, "Cannot use 'super' outside of a class.");
            return;
        }
        if (!currentClass->hasSuperclass) {
            compileError(expr->line, "Cannot use 'super' in a class with no superclass.");
            return;
        }

        SuperExpr* superExpr = &call->callee->as.super_;
        ObjString* methodName = copyString(superExpr->method.start, superExpr->method.length);

        // Get 'this' (receiver at slot 0)
        emitBytes(expr->line, OP_GET_LOCAL, 0);

        // Compile arguments
        for (int i = 0; i < call->argCount; i++) {
            compileExpr(call->arguments[i]);
        }

        // Get superclass from 'super' local
        Token superToken;
        superToken.start = "super";
        superToken.length = 5;
        superToken.line = expr->line;
        int arg = resolveLocal(current, &superToken);
        if (arg != -1) {
            emitBytes(expr->line, OP_GET_LOCAL, (uint8_t)arg);
        } else if ((arg = resolveUpvalue(current, &superToken)) != -1) {
            emitBytes(expr->line, OP_GET_UPVALUE, (uint8_t)arg);
        } else {
            compileError(expr->line, "Could not resolve 'super'.");
            return;
        }

        // Emit OP_SUPER_INVOKE
        emitBytes(expr->line, OP_SUPER_INVOKE, makeConstant(OBJ_VAL(methodName)));
        emitByte(expr->line, (uint8_t)call->argCount);
        return;
    }

    // Regular function call
    compileExpr(call->callee);

    for (int i = 0; i < call->argCount; i++) {
        compileExpr(call->arguments[i]);
    }

    emitBytes(expr->line, OP_CALL, (uint8_t)call->argCount);
}

static void compileExpr(Expr* expr) {
    if (expr == NULL) return;

    switch (expr->kind) {
        case EXPR_LITERAL:
            compileLiteral(expr);
            break;
        case EXPR_UNARY:
            compileUnary(expr);
            break;
        case EXPR_BINARY:
            compileBinary(expr);
            break;
        case EXPR_GROUPING:
            compileExpr(expr->as.grouping.expression);
            break;
        case EXPR_VARIABLE:
            compileVariable(expr);
            break;
        case EXPR_ASSIGN:
            compileAssign(expr);
            break;
        case EXPR_LOGICAL:
            compileLogical(expr);
            break;
        case EXPR_NULL_COALESCE:
            compileNullCoalesce(expr);
            break;
        case EXPR_CALL:
            compileCall(expr);
            break;
        case EXPR_LAMBDA: {
            LambdaExpr* lambda = &expr->as.lambda;

            // Create new compiler for the lambda (anonymous function)
            Compiler lambdaCompiler;
            initCompiler(&lambdaCompiler, current, FN_TYPE_FUNCTION);
            beginScope();

            // Set function name to anonymous
            current->function->name = copyString("<lambda>", 8);
            current->function->arity = lambda->paramCount;

            // Define parameters as locals
            for (int i = 0; i < lambda->paramCount; i++) {
                addLocal(lambda->params[i], NULL, false);
            }

            if (lambda->isBlockBody) {
                // Compile lambda block body; explicit returns are handled by statement compilation.
                compileStmt(lambda->blockBody);
            } else {
                // Compile expression body and return it.
                compileExpr(lambda->body);
                emitByte(expr->line, OP_RETURN);
            }

            ObjFunction* function = endCompiler(expr->line);

            // Emit closure
            emitBytes(expr->line, OP_CLOSURE, makeConstant(OBJ_VAL(function)));

            // Emit upvalue info
            for (int i = 0; i < function->upvalueCount; i++) {
                emitByte(expr->line, lambdaCompiler.upvalues[i].isLocal ? 1 : 0);
                emitByte(expr->line, lambdaCompiler.upvalues[i].index);
            }
            break;
        }
        case EXPR_ARRAY: {
            // Handle array literals with possible spread expressions
            // Strategy: build chunks of non-spread elements and concatenate with spread arrays

            int elementCount = expr->as.array.elementCount;
            if (elementCount == 0) {
                // Empty array
                emitBytes(expr->line, OP_ARRAY, 0);
                break;
            }

            // Check if we have any spreads
            bool hasSpread = false;
            for (int i = 0; i < elementCount; i++) {
                if (expr->as.array.elements[i]->kind == EXPR_SPREAD) {
                    hasSpread = true;
                    break;
                }
            }

            if (!hasSpread) {
                // No spreads, simple case
                for (int i = 0; i < elementCount; i++) {
                    compileExpr(expr->as.array.elements[i]);
                }
                emitBytes(expr->line, OP_ARRAY, (uint8_t)elementCount);
                break;
            }

            // Has spreads - build in chunks and concatenate
            bool hasResult = false;  // Track if we have a result array on stack
            int chunkStart = 0;

            for (int i = 0; i <= elementCount; i++) {
                bool isSpread = (i < elementCount && expr->as.array.elements[i]->kind == EXPR_SPREAD);
                bool isEnd = (i == elementCount);

                if (isSpread || isEnd) {
                    // Emit any accumulated non-spread elements as an array
                    int chunkSize = i - chunkStart;
                    if (chunkSize > 0) {
                        for (int j = chunkStart; j < i; j++) {
                            compileExpr(expr->as.array.elements[j]);
                        }
                        emitBytes(expr->line, OP_ARRAY, (uint8_t)chunkSize);
                        if (hasResult) {
                            emitByte(expr->line, OP_ARRAY_CONCAT);
                        }
                        hasResult = true;
                    }

                    // If this is a spread, emit the spread array and concatenate
                    if (isSpread) {
                        compileExpr(expr->as.array.elements[i]->as.spread.operand);
                        if (hasResult) {
                            emitByte(expr->line, OP_ARRAY_CONCAT);
                        } else {
                            hasResult = true;
                        }
                        chunkStart = i + 1;
                    }
                }
            }

            // If we never built a result (all spreads with no regular elements), create empty array
            if (!hasResult) {
                emitBytes(expr->line, OP_ARRAY, 0);
            }
            break;
        }
        case EXPR_SPREAD:
            // Spread expressions should only appear within array literals
            // and are handled there. If we encounter one here, just compile
            // the operand (though the type checker should have caught this).
            compileExpr(expr->as.spread.operand);
            break;
        case EXPR_INDEX:
            compileExpr(expr->as.index.object);
            compileExpr(expr->as.index.index);
            emitByte(expr->line, OP_INDEX_GET);
            break;
        case EXPR_INDEX_SET:
            compileExpr(expr->as.index_set.object);
            compileExpr(expr->as.index_set.index);
            compileExpr(expr->as.index_set.value);
            emitByte(expr->line, OP_INDEX_SET);
            break;
        case EXPR_GET: {
            compileExpr(expr->as.get.object);
            ObjString* name = copyString(expr->as.get.name.start, expr->as.get.name.length);

            if (expr->as.get.isOptional) {
                // Optional chaining: x?.y
                // If x is nil, leave nil on stack; otherwise access property
                emitByte(expr->line, OP_DUP);           // [x, x]
                emitByte(expr->line, OP_NIL);           // [x, x, nil]
                emitByte(expr->line, OP_EQUAL);         // [x, isNil?]
                int skipJump = emitJump(expr->line, OP_JUMP_IF_FALSE);
                // x is nil - pop comparison result, leave nil on stack
                emitByte(expr->line, OP_POP);           // [x] (nil)
                int endJump = emitJump(expr->line, OP_JUMP);
                // x is not nil - pop comparison result, access property
                patchJump(skipJump);
                emitByte(expr->line, OP_POP);           // [x]
                emitBytes(expr->line, OP_GET_PROPERTY, makeConstant(OBJ_VAL(name)));
                patchJump(endJump);
            } else {
                // Normal property access
                emitBytes(expr->line, OP_GET_PROPERTY, makeConstant(OBJ_VAL(name)));
            }
            break;
        }
        case EXPR_SET: {
            compileExpr(expr->as.set.object);
            compileExpr(expr->as.set.value);
            ObjString* name = copyString(expr->as.set.name.start, expr->as.set.name.length);
            emitBytes(expr->line, OP_SET_PROPERTY, makeConstant(OBJ_VAL(name)));
            break;
        }
        case EXPR_THIS:
            if (currentClass == NULL) {
                compileError(expr->line, "Cannot use 'this' outside of a class.");
                break;
            }
            emitBytes(expr->line, OP_GET_LOCAL, 0);
            break;
        case EXPR_SUPER: {
            if (currentClass == NULL) {
                compileError(expr->line, "Cannot use 'super' outside of a class.");
                break;
            }
            if (!currentClass->hasSuperclass) {
                compileError(expr->line, "Cannot use 'super' in a class with no superclass.");
                break;
            }

            // Get the method name
            ObjString* methodName = copyString(expr->as.super_.method.start,
                                               expr->as.super_.method.length);

            // Get 'this' (receiver at slot 0)
            emitBytes(expr->line, OP_GET_LOCAL, 0);

            // Get superclass from the 'super' local variable
            Token superToken;
            superToken.start = "super";
            superToken.length = 5;
            superToken.line = expr->line;
            int arg = resolveLocal(current, &superToken);
            if (arg != -1) {
                emitBytes(expr->line, OP_GET_LOCAL, (uint8_t)arg);
            } else if ((arg = resolveUpvalue(current, &superToken)) != -1) {
                emitBytes(expr->line, OP_GET_UPVALUE, (uint8_t)arg);
            } else {
                compileError(expr->line, "Could not resolve 'super'.");
                break;
            }

            // Emit OP_GET_SUPER to bind the method
            emitBytes(expr->line, OP_GET_SUPER, makeConstant(OBJ_VAL(methodName)));
            break;
        }
        case EXPR_MATCH:
            compileMatchExpr(expr);
            break;
    }
}

/* Match expression: same control flow as compileMatchStmt (dup, compare tag,
 * OP_JUMP_IF_FALSE to next arm). Leaves the chosen arm's value on the stack;
 * non-wildcard arms endScope then OP_JUMP to a common merge; unmatched falls
 * through to OP_NIL when there is no wildcard. */
static void compileMatchExpr(Expr* expr) {
    MatchExpr* match = &expr->as.match;

    // Compile the value to match (leaves it on stack)
    compileExpr(match->matchValue);

    // Store end jumps to patch later
    int* endJumps = ALLOCATE(int, match->caseCount);
    int endJumpCount = 0;

    for (int i = 0; i < match->caseCount; i++) {
        ExprCaseClause* c = &match->cases[i];

        if (c->isWildcard) {
            beginScope();
            // Wildcard: pop the value and evaluate the result expression
            emitByte(expr->line, OP_POP);
            compileExpr(c->value);
            endScope(expr->line);
            // No need to jump - wildcard should be last
        } else {
            emitByte(expr->line, OP_DUP);

            bool callPattern = (c->pattern != NULL && c->pattern->kind == EXPR_CALL &&
                                c->pattern->as.call.callee->kind == EXPR_VARIABLE);
            bool destructurePattern = c->destructureCount > 0;

            if (callPattern) {
                CallExpr* call = &c->pattern->as.call;
                ObjString* tagStr = copyString("$tag", 4);
                emitBytes(expr->line, OP_GET_PROPERTY, makeConstant(OBJ_VAL(tagStr)));
                VariableExpr* callee = &call->callee->as.variable;
                ObjString* wantTag = copyString(callee->name.start, callee->name.length);
                emitBytes(expr->line, OP_CONSTANT, makeConstant(OBJ_VAL(wantTag)));
                emitByte(expr->line, OP_EQUAL);
            } else if (destructurePattern) {
                ObjString* tagStr = copyString("$tag", 4);
                emitBytes(expr->line, OP_GET_PROPERTY, makeConstant(OBJ_VAL(tagStr)));
                compileExpr(c->pattern);
                emitBytes(expr->line, OP_GET_PROPERTY, makeConstant(OBJ_VAL(tagStr)));
                emitByte(expr->line, OP_EQUAL);
            } else {
                compileExpr(c->pattern);
                emitByte(expr->line, OP_EQUAL);
            }

            int nextCase = emitJump(expr->line, OP_JUMP_IF_FALSE);
            emitByte(expr->line, OP_POP);

            beginScope();

            if (callPattern || destructurePattern) {
                int subjectLocal = current->localCount;
                Token subTok;
                subTok.start = "";
                subTok.length = 0;
                subTok.line = expr->line;
                addLocal(subTok, NULL, false);
                current->locals[subjectLocal].depth = current->scopeDepth;

                if (callPattern) {
                    CallExpr* call = &c->pattern->as.call;
                    for (int j = 0; j < call->argCount; j++) {
                        Expr* arg = call->arguments[j];
                        if (arg->kind != EXPR_VARIABLE) {
                            compileError(expr->line, "Match pattern arguments must be variable names.");
                            break;
                        }
                        emitBytes(expr->line, OP_GET_LOCAL, (uint8_t)subjectLocal);
                        char fieldName[16];
                        snprintf(fieldName, sizeof(fieldName), "$%d", j);
                        emitBytes(expr->line, OP_GET_PROPERTY,
                                  makeConstant(OBJ_VAL(copyString(fieldName, (int)strlen(fieldName)))));
                        addLocal(arg->as.variable.name, NULL, false);
                        current->locals[current->localCount - 1].depth = current->scopeDepth;
                    }
                }
                if (destructurePattern) {
                    for (int j = 0; j < c->destructureCount; j++) {
                        emitBytes(expr->line, OP_GET_LOCAL, (uint8_t)subjectLocal);
                        char fieldName[16];
                        snprintf(fieldName, sizeof(fieldName), "$%d", j);
                        emitBytes(expr->line, OP_GET_PROPERTY,
                                  makeConstant(OBJ_VAL(copyString(fieldName, (int)strlen(fieldName)))));
                        addLocal(c->destructureParams[j], NULL, false);
                        current->locals[current->localCount - 1].depth = current->scopeDepth;
                    }
                }
            } else {
                emitByte(expr->line, OP_POP);
            }

            compileExpr(c->value);

            endScope(expr->line);
            endJumps[endJumpCount++] = emitJump(expr->line, OP_JUMP);
            // Patch next case jump
            patchJump(nextCase);
            // Pop the comparison result (false)
            emitByte(expr->line, OP_POP);
        }
    }

    // If no wildcard and no match, pop the value and push nil
    if (match->caseCount > 0 && !match->cases[match->caseCount - 1].isWildcard) {
        emitByte(expr->line, OP_POP);
        emitByte(expr->line, OP_NIL);  // Default to nil if no match
    }

    // Patch all end jumps
    for (int i = 0; i < endJumpCount; i++) {
        patchJump(endJumps[i]);
    }

    FREE_ARRAY(int, endJumps, match->caseCount);
}
