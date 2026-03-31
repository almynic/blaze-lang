// ============================================================================
// Statement Compilation
// ============================================================================

static void compileExpressionStmt(Stmt* stmt) {
    compileExpr(stmt->as.expression.expression);
    emitByte(stmt->line, OP_POP);
}

static void compilePrintStmt(Stmt* stmt) {
    compileExpr(stmt->as.print.expression);
    emitByte(stmt->line, OP_PRINT);
}

static void compileVarStmt(Stmt* stmt) {
    VarStmt* var = &stmt->as.var;

    // Handle array destructuring
    if (var->destructureKind == DESTRUCTURE_ARRAY) {
        // For each variable in the destructuring pattern
        for (int i = 0; i < var->destructureCount; i++) {
            Token name = var->destructureNames[i];

            if (i == var->restIndex) {
                // Rest parameter: slice from index i to end
                compileExpr(var->initializer);        // Array on stack
                emitConstant(stmt->line, INT_VAL(i)); // Start index
                emitByte(stmt->line, OP_NIL);         // End index (nil = to end)
                emitByte(stmt->line, OP_ARRAY_SLICE); // Create slice
            } else {
                // Regular element: recompile initializer and get element at index i
                compileExpr(var->initializer);  // Array on stack
                emitConstant(stmt->line, INT_VAL(i)); // Push index
                emitByte(stmt->line, OP_INDEX_GET);     // Get array[i]
            }

            // Define the variable (local or global)
            if (current->scopeDepth > 0) {
                addLocal(name, NULL, var->isConst);
            } else {
                ObjString* varName = copyString(name.start, name.length);
                uint8_t global = makeConstant(OBJ_VAL(varName));
                emitBytes(stmt->line, OP_DEFINE_GLOBAL, global);
            }
        }

        return;
    } else if (var->destructureKind == DESTRUCTURE_OBJECT) {
        // Handle object destructuring
        // For each property in the destructuring pattern
        for (int i = 0; i < var->destructureCount; i++) {
            Token name = var->destructureNames[i];

            // Get property from object
            compileExpr(var->initializer);  // Object on stack
            ObjString* propName = copyString(name.start, name.length);
            emitBytes(stmt->line, OP_GET_PROPERTY, makeConstant(OBJ_VAL(propName)));

            // Define the variable (local or global)
            if (current->scopeDepth > 0) {
                addLocal(name, NULL, var->isConst);
            } else {
                ObjString* varName = copyString(name.start, name.length);
                uint8_t global = makeConstant(OBJ_VAL(varName));
                emitBytes(stmt->line, OP_DEFINE_GLOBAL, global);
            }
        }

        return;
    }

    // Simple variable declaration
    if (var->initializer != NULL) {
        compileExpr(var->initializer);
    } else {
        emitByte(stmt->line, OP_NIL);
    }

    if (current->scopeDepth > 0) {
        addLocal(var->name, var->type, var->isConst);
    } else {
        ObjString* name = copyString(var->name.start, var->name.length);
        uint8_t global = makeConstant(OBJ_VAL(name));
        emitBytes(stmt->line, OP_DEFINE_GLOBAL, global);
    }
}

static void compileBlockStmt(Stmt* stmt) {
    beginScope();
    for (int i = 0; i < stmt->as.block.count; i++) {
        bool isTerminator = compileStmt(stmt->as.block.statements[i]);

        // Dead code elimination: warn and skip remaining statements after a terminator
        if (isTerminator && i + 1 < stmt->as.block.count) {
            // Optional: emit warning about unreachable code
            Stmt* nextStmt = stmt->as.block.statements[i + 1];
            if (nextStmt != NULL) {
                // Note: This is a warning, not an error, so we don't set hadError
                fprintf(stderr, "Warning [line %d]: Unreachable code after return/throw.\n",
                        nextStmt->line);
            }
            // Stop compiling remaining statements in this block
            break;
        }
    }
    endScope(stmt->line);
}

static void compileIfStmt(Stmt* stmt) {
    IfStmt* ifStmt = &stmt->as.if_;

    compileExpr(ifStmt->condition);

    int thenJump = emitJump(stmt->line, OP_JUMP_IF_FALSE);
    emitByte(stmt->line, OP_POP);

    compileStmt(ifStmt->thenBranch);

    int elseJump = emitJump(stmt->line, OP_JUMP);

    patchJump(thenJump);
    emitByte(stmt->line, OP_POP);

    if (ifStmt->elseBranch != NULL) {
        compileStmt(ifStmt->elseBranch);
    }

    patchJump(elseJump);
}

static void compileWhileStmt(Stmt* stmt) {
    WhileStmt* whileStmt = &stmt->as.while_;

    int loopStart = currentChunk()->count;

    compileExpr(whileStmt->condition);

    int exitJump = emitJump(stmt->line, OP_JUMP_IF_FALSE);
    emitByte(stmt->line, OP_POP);

    compileStmt(whileStmt->body);

    emitLoop(stmt->line, loopStart);

    patchJump(exitJump);
    emitByte(stmt->line, OP_POP);
}

static bool isUnrollableRange(Expr* iterable, int64_t* start, int64_t* end) {
    if (iterable->kind != EXPR_BINARY) return false;
    BinaryExpr* binary = &iterable->as.binary;
    if (binary->op.type != TOKEN_DOT_DOT) return false;

    if (binary->left->kind != EXPR_LITERAL || binary->right->kind != EXPR_LITERAL) return false;

    Value leftVal = binary->left->as.literal.value;
    Value rightVal = binary->right->as.literal.value;

    if (!IS_INT(leftVal) || !IS_INT(rightVal)) return false;

    *start = AS_INT(leftVal);
    *end = AS_INT(rightVal);

    int64_t diff = (*end > *start) ? (*end - *start) : (*start - *end);
    return diff >= 0 && diff <= 16;
}

static bool isUnrollableArray(Expr* iterable) {
    if (iterable->kind != EXPR_ARRAY) return false;
    ArrayExpr* array = &iterable->as.array;

    // Only unroll if all elements are simple or we are sure it's worth it.
    // Fixed size arrays up to 16 elements are good candidates.
    if (array->elementCount < 0 || array->elementCount > 16) return false;

    // Don't unroll if there are spread operators, as they have dynamic size
    for (int i = 0; i < array->elementCount; i++) {
        if (array->elements[i]->kind == EXPR_SPREAD) return false;
    }

    return true;
}

static void compileForStmt(Stmt* stmt) {
    ForStmt* forStmt = &stmt->as.for_;

    // Optimization: Loop Unrolling
    int64_t start, end;
    if (isUnrollableRange(forStmt->iterable, &start, &end)) {
        if (start <= end) {
            for (int64_t i = start; i < end; i++) {
                beginScope();
                emitConstant(stmt->line, INT_VAL(i));
                addLocal(forStmt->variable, createIntType(), false);
                compileStmt(forStmt->body);
                endScope(stmt->line);
            }
        } else {
            for (int64_t i = start; i > end; i--) {
                beginScope();
                emitConstant(stmt->line, INT_VAL(i));
                addLocal(forStmt->variable, createIntType(), false);
                compileStmt(forStmt->body);
                endScope(stmt->line);
            }
        }
        return;
    }

    if (isUnrollableArray(forStmt->iterable)) {
        ArrayExpr* array = &forStmt->iterable->as.array;
        for (int i = 0; i < array->elementCount; i++) {
            beginScope();
            compileExpr(array->elements[i]);
            // Type will be inferred or checked by the type checker already
            addLocal(forStmt->variable, NULL, false);
            compileStmt(forStmt->body);
            endScope(stmt->line);
        }
        return;
    }

    beginScope();

    // Compile the iterable and store in a hidden local (the array)
    compileExpr(forStmt->iterable);
    Token arrayToken;
    arrayToken.start = "$array";
    arrayToken.length = 6;
    arrayToken.line = stmt->line;
    addLocal(arrayToken, NULL, false);

    // Get array length and store in hidden local
    emitBytes(stmt->line, OP_GET_LOCAL, (uint8_t)(current->localCount - 1));
    emitByte(stmt->line, OP_ARRAY_LENGTH);
    Token lengthToken;
    lengthToken.start = "$length";
    lengthToken.length = 7;
    lengthToken.line = stmt->line;
    addLocal(lengthToken, createIntType(), false);

    // Initialize index to 0
    emitConstant(stmt->line, INT_VAL(0));
    Token indexToken;
    indexToken.start = "$index";
    indexToken.length = 6;
    indexToken.line = stmt->line;
    addLocal(indexToken, createIntType(), false);

    // Loop start
    int loopStart = currentChunk()->count;

    // Check if index < length
    // Get index
    emitBytes(stmt->line, OP_GET_LOCAL, (uint8_t)(current->localCount - 1));
    // Get length
    emitBytes(stmt->line, OP_GET_LOCAL, (uint8_t)(current->localCount - 2));
    // Compare index < length
    emitByte(stmt->line, OP_LESS);

    // Jump to end if index >= length
    int exitJump = emitJump(stmt->line, OP_JUMP_IF_FALSE);
    emitByte(stmt->line, OP_POP);  // Pop the comparison result

    // Get array[index] - this becomes the loop variable
    // Get array
    emitBytes(stmt->line, OP_GET_LOCAL, (uint8_t)(current->localCount - 3));
    // Get index
    emitBytes(stmt->line, OP_GET_LOCAL, (uint8_t)(current->localCount - 1));
    // Index into array
    emitByte(stmt->line, OP_INDEX_GET);

    // Add the loop variable
    addLocal(forStmt->variable, NULL, false);

    // Compile the body
    compileStmt(forStmt->body);

    // Pop the loop variable (it goes out of scope each iteration)
    emitByte(stmt->line, OP_POP);
    current->localCount--;

    // Increment index: index = index + 1
    emitBytes(stmt->line, OP_GET_LOCAL, (uint8_t)(current->localCount - 1));
    emitConstant(stmt->line, INT_VAL(1));
    emitByte(stmt->line, OP_ADD_INT);
    emitBytes(stmt->line, OP_SET_LOCAL, (uint8_t)(current->localCount - 1));
    emitByte(stmt->line, OP_POP);  // Pop the assignment result

    // Loop back
    emitLoop(stmt->line, loopStart);

    // Patch the exit jump
    patchJump(exitJump);
    emitByte(stmt->line, OP_POP);  // Pop the false comparison result

    endScope(stmt->line);
}

static ObjFunction* endCompiler(int line) {
    emitReturn(line);
    ObjFunction* function = current->function;
    current = current->enclosing;
    return function;
}

static void compileFunctionStmt(Stmt* stmt) {
    FunctionStmt* func = &stmt->as.function;

    // At script top-level (scopeDepth == 1 for scripts), functions should be global only
    // For nested scopes, define as local for recursive calls
    bool isTopLevel = (current->scopeDepth <= 1 && current->type == FN_TYPE_SCRIPT);
    if (current->scopeDepth > 0 && !isTopLevel) {
        addLocal(func->name, func->type, true);
    }

    // Create new compiler for the function
    Compiler funcCompiler;
    initCompiler(&funcCompiler, current, FN_TYPE_FUNCTION);
    beginScope();

    // Set function name
    current->function->name = copyString(func->name.start, func->name.length);
    current->function->arity = func->paramCount;

    // Define parameters as locals
    for (int i = 0; i < func->paramCount; i++) {
        Type* paramType = NULL;
        if (func->type && i < func->type->as.function.paramCount) {
            paramType = func->type->as.function.paramTypes[i];
        }
        addLocal(func->params[i], paramType, false);
    }

    // Compile function body
    if (func->body != NULL && func->body->kind == STMT_BLOCK) {
        BlockStmt* block = &func->body->as.block;
        for (int i = 0; i < block->count; i++) {
            bool isTerminator = compileStmt(block->statements[i]);

            // Dead code elimination in function bodies
            if (isTerminator && i + 1 < block->count) {
                Stmt* nextStmt = block->statements[i + 1];
                if (nextStmt != NULL) {
                    fprintf(stderr, "Warning [line %d]: Unreachable code after return/throw.\n",
                            nextStmt->line);
                }
                break;
            }
        }
    }

    ObjFunction* function = endCompiler(stmt->line);

    // Emit closure
    emitBytes(stmt->line, OP_CLOSURE, makeConstant(OBJ_VAL(function)));

    // Emit upvalue info
    for (int i = 0; i < function->upvalueCount; i++) {
        emitByte(stmt->line, funcCompiler.upvalues[i].isLocal ? 1 : 0);
        emitByte(stmt->line, funcCompiler.upvalues[i].index);
    }

    // Define the function globally if at top level
    // Note: scopeDepth is 1 at script top-level due to beginScope() in compile()
    if (current->scopeDepth <= 1 && current->type == FN_TYPE_SCRIPT) {
        ObjString* name = copyString(func->name.start, func->name.length);
        emitBytes(stmt->line, OP_DEFINE_GLOBAL, makeConstant(OBJ_VAL(name)));
    }
}

static void compileReturnStmt(Stmt* stmt) {
    ReturnStmt* ret = &stmt->as.return_;

    // Initializers must return 'this', no tail calls
    if (current->type == FN_TYPE_INITIALIZER) {
        if (ret->value != NULL) {
            compileError(stmt->line, "Cannot return a value from an initializer.");
        }
        emitBytes(stmt->line, OP_GET_LOCAL, 0);  // Return 'this'
        emitReturnFinallyBodies(stmt->line);
        emitByte(stmt->line, OP_RETURN);
        return;
    }

    // Check for tail call pattern: return func(...)
    if (ret->value != NULL && ret->value->kind == EXPR_CALL) {
        CallExpr* call = &ret->value->as.call;

        // Skip built-in print() optimization
        if (call->callee->kind == EXPR_VARIABLE) {
            VariableExpr* callee = &call->callee->as.variable;
            if (callee->name.length == 5 && memcmp(callee->name.start, "print", 5) == 0) {
                if (call->argCount == 1) {
                    compileExpr(call->arguments[0]);
                    emitByte(stmt->line, OP_PRINT);
                    emitReturnFinallyBodies(stmt->line);
                    emitByte(stmt->line, OP_RETURN);
                    return;
                }
            }
        }

        // Skip super method calls (complex semantics)
        if (call->callee->kind == EXPR_SUPER) {
            compileExpr(ret->value);
            emitReturnFinallyBodies(stmt->line);
            emitByte(stmt->line, OP_RETURN);
            return;
        }

        if (current->activeFinallyCount > 0) {
            // Preserve return value while executing enclosing finally blocks.
            compileExpr(ret->value);
            Token retTok;
            retTok.start = "";
            retTok.length = 0;
            retTok.line = stmt->line;
            addLocal(retTok, NULL, false);
            current->locals[current->localCount - 1].depth = current->scopeDepth;
            int retSlot = current->localCount - 1;

            emitReturnFinallyBodies(stmt->line);
            emitBytes(stmt->line, OP_GET_LOCAL, (uint8_t)retSlot);
            emitByte(stmt->line, OP_RETURN);
            // The synthetic local only exists for this terminating return path.
            current->localCount--;
            return;
        }

        // Emit tail call optimization
        compileExpr(call->callee);
        for (int i = 0; i < call->argCount; i++) {
            compileExpr(call->arguments[i]);
        }
        emitBytes(stmt->line, OP_TAIL_CALL, (uint8_t)call->argCount);
        return;
    }

    // Normal return path
    if (ret->value != NULL) {
        compileExpr(ret->value);
    } else {
        emitByte(stmt->line, OP_NIL);
    }

    emitReturnFinallyBodies(stmt->line);
    emitByte(stmt->line, OP_RETURN);
}

static void compileMethod(FunctionStmt* method, int line) {
    // Determine if this is an initializer
    bool isInitializer = (method->name.length == 4 &&
                          memcmp(method->name.start, "init", 4) == 0);

    // Create new compiler for the method
    Compiler methodCompiler;
    initCompiler(&methodCompiler, current, isInitializer ? FN_TYPE_INITIALIZER : FN_TYPE_METHOD);
    beginScope();

    // Set method name
    current->function->name = copyString(method->name.start, method->name.length);
    current->function->arity = method->paramCount;

    // Define parameters as locals
    for (int i = 0; i < method->paramCount; i++) {
        Type* paramType = NULL;
        if (method->type && i < method->type->as.function.paramCount) {
            paramType = method->type->as.function.paramTypes[i];
        }
        addLocal(method->params[i], paramType, false);
    }

    // Compile method body
    if (method->body != NULL && method->body->kind == STMT_BLOCK) {
        BlockStmt* block = &method->body->as.block;
        for (int i = 0; i < block->count; i++) {
            bool isTerminator = compileStmt(block->statements[i]);

            // Dead code elimination in method bodies
            if (isTerminator && i + 1 < block->count) {
                Stmt* nextStmt = block->statements[i + 1];
                if (nextStmt != NULL) {
                    fprintf(stderr, "Warning [line %d]: Unreachable code after return/throw.\n",
                            nextStmt->line);
                }
                break;
            }
        }
    }

    // For initializers, return 'this' instead of nil
    if (isInitializer) {
        emitBytes(line, OP_GET_LOCAL, 0);  // Get 'this'
        emitByte(line, OP_RETURN);
    }

    ObjFunction* function = endCompiler(line);

    // Emit closure
    emitBytes(line, OP_CLOSURE, makeConstant(OBJ_VAL(function)));

    // Emit upvalue info
    for (int i = 0; i < function->upvalueCount; i++) {
        emitByte(line, methodCompiler.upvalues[i].isLocal ? 1 : 0);
        emitByte(line, methodCompiler.upvalues[i].index);
    }
}

/* Match statement: subject stays under DUP until an arm matches. After
 * OP_JUMP_IF_FALSE, only the success path runs beginScope / bindings / body.
 * endScope must run before the arm's OP_JUMP to exit: otherwise pattern locals
 * stay on the stack across the jump and corrupt later matches. Failed arms
 * patch nextCase and POP the false comparison without touching that scope. */
static void compileMatchStmt(Stmt* stmt) {
    MatchStmt* matchStmt = &stmt->as.match;

    // Compile the value to match
    compileExpr(matchStmt->value);

    // Store end jumps to patch later
    int* endJumps = ALLOCATE(int, matchStmt->caseCount);
    int endJumpCount = 0;

    for (int i = 0; i < matchStmt->caseCount; i++) {
        CaseClause* c = &matchStmt->cases[i];

        if (c->isWildcard) {
            beginScope();
            // Wildcard: pop the value and execute body
            emitByte(stmt->line, OP_POP);
            compileStmt(c->body);
            endScope(stmt->line);
            // No need to jump - wildcard should be last
        } else {
            emitByte(stmt->line, OP_DUP);

            bool callPattern = (c->pattern != NULL && c->pattern->kind == EXPR_CALL &&
                                c->pattern->as.call.callee->kind == EXPR_VARIABLE);
            bool destructurePattern = c->destructureCount > 0;

            if (callPattern) {
                CallExpr* call = &c->pattern->as.call;
                ObjString* tagStr = copyString("$tag", 4);
                emitBytes(stmt->line, OP_GET_PROPERTY, makeConstant(OBJ_VAL(tagStr)));
                VariableExpr* callee = &call->callee->as.variable;
                ObjString* wantTag = copyString(callee->name.start, callee->name.length);
                emitBytes(stmt->line, OP_CONSTANT, makeConstant(OBJ_VAL(wantTag)));
                emitByte(stmt->line, OP_EQUAL);
            } else if (destructurePattern) {
                ObjString* tagStr = copyString("$tag", 4);
                emitBytes(stmt->line, OP_GET_PROPERTY, makeConstant(OBJ_VAL(tagStr)));
                compileExpr(c->pattern);
                emitBytes(stmt->line, OP_GET_PROPERTY, makeConstant(OBJ_VAL(tagStr)));
                emitByte(stmt->line, OP_EQUAL);
            } else {
                compileExpr(c->pattern);
                emitByte(stmt->line, OP_EQUAL);
            }

            int nextCase = emitJump(stmt->line, OP_JUMP_IF_FALSE);
            emitByte(stmt->line, OP_POP);

            // Bindings and body run only on the true path; a separate scope ensures
            // endScope POPs do not run after a failed branch (which would corrupt the stack).
            beginScope();

            if (callPattern || destructurePattern) {
                int subjectLocal = current->localCount;
                Token subTok;
                subTok.start = "";
                subTok.length = 0;
                subTok.line = stmt->line;
                addLocal(subTok, NULL, false);
                current->locals[subjectLocal].depth = current->scopeDepth;

                if (callPattern) {
                    CallExpr* call = &c->pattern->as.call;
                    for (int j = 0; j < call->argCount; j++) {
                        Expr* arg = call->arguments[j];
                        if (arg->kind != EXPR_VARIABLE) {
                            compileError(stmt->line, "Match pattern arguments must be variable names.");
                            break;
                        }
                        emitBytes(stmt->line, OP_GET_LOCAL, (uint8_t)subjectLocal);
                        char fieldName[16];
                        snprintf(fieldName, sizeof(fieldName), "$%d", j);
                        emitBytes(stmt->line, OP_GET_PROPERTY,
                                  makeConstant(OBJ_VAL(copyString(fieldName, (int)strlen(fieldName)))));
                        addLocal(arg->as.variable.name, NULL, false);
                        current->locals[current->localCount - 1].depth = current->scopeDepth;
                    }
                }
                if (destructurePattern) {
                    for (int j = 0; j < c->destructureCount; j++) {
                        emitBytes(stmt->line, OP_GET_LOCAL, (uint8_t)subjectLocal);
                        char fieldName[16];
                        snprintf(fieldName, sizeof(fieldName), "$%d", j);
                        emitBytes(stmt->line, OP_GET_PROPERTY,
                                  makeConstant(OBJ_VAL(copyString(fieldName, (int)strlen(fieldName)))));
                        addLocal(c->destructureParams[j], NULL, false);
                        current->locals[current->localCount - 1].depth = current->scopeDepth;
                    }
                }
            } else {
                emitByte(stmt->line, OP_POP);
            }

            compileStmt(c->body);
            // Pop binding locals on the true path before jumping out of the arm.
            endScope(stmt->line);
            endJumps[endJumpCount++] = emitJump(stmt->line, OP_JUMP);
            // Patch next case jump
            patchJump(nextCase);
            // Pop the comparison result (false)
            emitByte(stmt->line, OP_POP);
        }
    }

    // If no wildcard and no match, pop the value
    if (matchStmt->caseCount > 0 && !matchStmt->cases[matchStmt->caseCount - 1].isWildcard) {
        emitByte(stmt->line, OP_POP);
    }

    // Patch all end jumps
    for (int i = 0; i < endJumpCount; i++) {
        patchJump(endJumps[i]);
    }

    FREE_ARRAY(int, endJumps, matchStmt->caseCount);
}

static void compileTryStmt(Stmt* stmt) {
    TryStmt* tryStmt = &stmt->as.try_;

    // Emit OP_TRY with placeholder for catch offset
    emitByte(stmt->line, OP_TRY);
    int catchJump = currentChunk()->count;
    emitByte(stmt->line, 0xff);  // Placeholder high byte
    emitByte(stmt->line, 0xff);  // Placeholder low byte

    // Compile try body
    if (tryStmt->finallyBody != NULL) {
        pushActiveFinallyBody(tryStmt->finallyBody, stmt->line);
    }
    compileStmt(tryStmt->tryBody);
    if (tryStmt->finallyBody != NULL) {
        popActiveFinallyBody();
    }

    // Emit OP_TRY_END (no exception occurred)
    emitByte(stmt->line, OP_TRY_END);

    // Compile finally block for normal path (no exception)
    if (tryStmt->finallyBody != NULL) {
        compileStmt(tryStmt->finallyBody);
    }

    // Jump over catch block
    int endJump = emitJump(stmt->line, OP_JUMP);

    // Patch catch offset
    int catchOffset = currentChunk()->count;
    currentChunk()->code[catchJump] = (catchOffset >> 8) & 0xff;
    currentChunk()->code[catchJump + 1] = catchOffset & 0xff;

    // Compile catch block
    // Exception message is on the stack - store it in the catch variable
    beginScope();
    addLocal(tryStmt->catchVar, createStringType(), false);
    // Value is already on stack, mark local as initialized
    current->locals[current->localCount - 1].depth = current->scopeDepth;

    if (tryStmt->finallyBody != NULL) {
        pushActiveFinallyBody(tryStmt->finallyBody, stmt->line);
    }
    compileStmt(tryStmt->catchBody);
    if (tryStmt->finallyBody != NULL) {
        popActiveFinallyBody();
    }
    endScope(stmt->line);

    // Compile finally block for catch path
    if (tryStmt->finallyBody != NULL) {
        compileStmt(tryStmt->finallyBody);
    }

    // Patch end jump
    patchJump(endJump);
}

static void compileThrowStmt(Stmt* stmt) {
    ThrowStmt* throwStmt = &stmt->as.throw_;

    // Compile the exception value
    compileExpr(throwStmt->value);

    // Emit throw
    emitByte(stmt->line, OP_THROW);
}

/* Push the enum's class object: nested variant closures may close over the
 * enclosing scope, so resolve local / upvalue / global like any other name. */
static void emitLoadEnumClassRef(EnumStmt* enumStmt, int line) {
    Token* name = &enumStmt->name;
    int loc = resolveLocal(current, name);
    if (loc != -1) {
        emitBytes(line, OP_GET_LOCAL, (uint8_t)loc);
    } else if ((loc = resolveUpvalue(current, name)) != -1) {
        emitBytes(line, OP_GET_UPVALUE, (uint8_t)loc);
    } else {
        ObjString* os = copyString(name->start, name->length);
        emitBytes(line, OP_GET_GLOBAL, makeConstant(OBJ_VAL(os)));
    }
}

/* Emit one ObjClass per enum; variants with payloads become closures that
 * build instances (SET_PROPERTY $tag, $n, …); unit variants and the class
 * itself are stored in klass->fields so patterns like `None` and `Some(x)`
 * resolve at runtime. Each variant name is also defined as a global/local. */
static void compileEnumStmt(Stmt* stmt) {
    EnumStmt* enumStmt = &stmt->as.enum_;

    ObjString* enumName = copyString(enumStmt->name.start, enumStmt->name.length);
    uint8_t nameConstant = makeConstant(OBJ_VAL(enumName));

    emitBytes(stmt->line, OP_CLASS, nameConstant);

    bool enumGlobal = (current->scopeDepth <= 1 && current->type == FN_TYPE_SCRIPT);
    if (enumGlobal) {
        emitBytes(stmt->line, OP_DEFINE_GLOBAL, nameConstant);
    } else {
        addLocal(enumStmt->name, enumStmt->type, false);
    }

    if (enumGlobal) {
        emitBytes(stmt->line, OP_GET_GLOBAL, nameConstant);
    } else {
        int slot = resolveLocal(current, &enumStmt->name);
        emitBytes(stmt->line, OP_GET_LOCAL, (uint8_t)slot);
    }

    for (int i = 0; i < enumStmt->variantCount; i++) {
        EnumVariant* v = &enumStmt->variants[i];
        ObjString* variantName = copyString(v->name.start, v->name.length);
        uint8_t variantNameConst = makeConstant(OBJ_VAL(variantName));

        emitByte(stmt->line, OP_DUP);

        if (v->fieldCount > 0) {
            Compiler variantCompiler;
            initCompiler(&variantCompiler, current, FN_TYPE_FUNCTION);
            beginScope();

            current->function->name = variantName;
            current->function->arity = v->fieldCount;

            for (int j = 0; j < v->fieldCount; j++) {
                char buf[16];
                int len = snprintf(buf, sizeof(buf), "$%d", j);
                ObjString* ps = copyString(buf, len);
                Token paramTok;
                paramTok.start = ps->chars;
                paramTok.length = len;
                paramTok.line = v->name.line;
                addLocal(paramTok, NULL, false);
            }

            emitLoadEnumClassRef(enumStmt, stmt->line);
            emitBytes(stmt->line, OP_CALL, 0);

            // Property name / value constants must live in the variant function's chunk.
            uint8_t tagKeyIn = makeConstant(OBJ_VAL(copyString("$tag", 4)));
            uint8_t tagValIn = makeConstant(OBJ_VAL(variantName));

            emitByte(stmt->line, OP_DUP);
            emitBytes(stmt->line, OP_CONSTANT, tagValIn);
            emitBytes(stmt->line, OP_SET_PROPERTY, tagKeyIn);
            emitByte(stmt->line, OP_POP);

            for (int j = 0; j < v->fieldCount; j++) {
                emitByte(stmt->line, OP_DUP);
                emitBytes(stmt->line, OP_GET_LOCAL, (uint8_t)(j + 1));
                char fieldBuf[16];
                int flen = snprintf(fieldBuf, sizeof(fieldBuf), "$%d", j);
                ObjString* fs = copyString(fieldBuf, flen);
                uint8_t fieldKeyIn = makeConstant(OBJ_VAL(fs));
                emitBytes(stmt->line, OP_SET_PROPERTY, fieldKeyIn);
                emitByte(stmt->line, OP_POP);
            }

            emitByte(stmt->line, OP_RETURN);

            ObjFunction* function = endCompiler(stmt->line);
            emitBytes(stmt->line, OP_CLOSURE, makeConstant(OBJ_VAL(function)));
            for (int u = 0; u < function->upvalueCount; u++) {
                emitByte(stmt->line, variantCompiler.upvalues[u].isLocal ? 1 : 0);
                emitByte(stmt->line, variantCompiler.upvalues[u].index);
            }
        } else {
            uint8_t tagKeyUnit = makeConstant(OBJ_VAL(copyString("$tag", 4)));
            emitByte(stmt->line, OP_DUP);
            emitBytes(stmt->line, OP_CALL, 0);
            emitByte(stmt->line, OP_DUP);
            emitBytes(stmt->line, OP_CONSTANT, variantNameConst);
            emitBytes(stmt->line, OP_SET_PROPERTY, tagKeyUnit);
            emitByte(stmt->line, OP_POP);
        }

        emitBytes(stmt->line, OP_SET_PROPERTY, variantNameConst);
        emitByte(stmt->line, OP_POP);

        // Mirror typechecker: each variant name is a top-level binding (global or local)
        // so match patterns like `None` and call patterns like `Some(x)` resolve at runtime.
        emitByte(stmt->line, OP_DUP);
        emitBytes(stmt->line, OP_GET_PROPERTY, variantNameConst);
        if (enumGlobal) {
            emitBytes(stmt->line, OP_DEFINE_GLOBAL, variantNameConst);
        } else {
            addLocal(v->name, enumStmt->type, true);
        }
    }

    emitByte(stmt->line, OP_POP);
}

static void compileInterfaceStmt(Stmt* stmt) {
    (void)stmt;
    // Interfaces are compile-time only; no bytecode.
}

static void compileClassStmt(Stmt* stmt) {
    ClassStmt* classStmt = &stmt->as.class_;

    if (classStmt->typeParamCount > 0) {
        return;
    }

    // Create class name constant
    ObjString* className = copyString(classStmt->name.start, classStmt->name.length);
    uint8_t nameConstant = makeConstant(OBJ_VAL(className));

    // Emit OP_CLASS to create the class
    emitBytes(stmt->line, OP_CLASS, nameConstant);

    // Define the class globally (or locally) BEFORE compiling methods
    // so methods can reference the class
    if (current->scopeDepth > 0) {
        addLocal(classStmt->name, classStmt->type, false);
    } else {
        emitBytes(stmt->line, OP_DEFINE_GLOBAL, nameConstant);
    }

    // Set up class compiler context for 'this'
    ClassCompiler classCompiler;
    classCompiler.enclosing = currentClass;
    /* Monomorphized generic classes may only set superclassResolved (see generic.c). */
    classCompiler.hasSuperclass =
        classStmt->superclassType != NULL || classStmt->superclassResolved != NULL;
    currentClass = &classCompiler;

    // Handle inheritance (superclass may be a script local from a prior `class` stmt)
    if (classStmt->superclassType != NULL || classStmt->superclassResolved != NULL) {
        if (classStmt->superclassType != NULL) {
            if (classStmt->superclassType->kind == TYPE_NODE_SIMPLE) {
                Token nm = classStmt->superclassType->as.simple.name;
                int arg = resolveLocal(current, &nm);
                if (arg != -1) {
                    emitBytes(stmt->line, OP_GET_LOCAL, (uint8_t)arg);
                } else if ((arg = resolveUpvalue(current, &nm)) != -1) {
                    emitBytes(stmt->line, OP_GET_UPVALUE, (uint8_t)arg);
                } else {
                    ObjString* superStr = copyString(nm.start, nm.length);
                    emitBytes(stmt->line, OP_GET_GLOBAL, makeConstant(OBJ_VAL(superStr)));
                }
            } else if (classStmt->superclassResolved != NULL) {
                emitGetGlobalForClassType(classStmt->superclassResolved, stmt->line);
            } else {
                compileError(stmt->line, "Could not resolve superclass for inheritance.");
            }
        } else if (classStmt->superclassResolved != NULL) {
            emitGetGlobalForClassType(classStmt->superclassResolved, stmt->line);
        } else {
            compileError(stmt->line, "Could not resolve superclass for inheritance.");
        }

        // Get the class back on the stack (may be global even if we're in a 'super' scope)
        {
            int slot = resolveLocal(current, &classStmt->name);
            if (slot != -1) {
                emitBytes(stmt->line, OP_GET_LOCAL, (uint8_t)slot);
            } else {
                emitBytes(stmt->line, OP_GET_GLOBAL, nameConstant);
            }
        }

        emitByte(stmt->line, OP_INHERIT);
        emitByte(stmt->line, OP_POP);  // Pop subclass

        // Create a scope for 'super'
        beginScope();
        Token superToken;
        superToken.start = "super";
        superToken.length = 5;
        superToken.line = stmt->line;
        addLocal(superToken, NULL, false);
    }

    // Get class back on stack for method binding
    {
        int slot = resolveLocal(current, &classStmt->name);
        if (slot != -1) {
            emitBytes(stmt->line, OP_GET_LOCAL, (uint8_t)slot);
        } else {
            emitBytes(stmt->line, OP_GET_GLOBAL, nameConstant);
        }
    }

    // Compile methods
    for (int i = 0; i < classStmt->methodCount; i++) {
        FunctionStmt* method = &classStmt->methods[i];
        ObjString* methodName = copyString(method->name.start, method->name.length);

        compileMethod(method, stmt->line);

        // Bind method to class
        emitBytes(stmt->line, OP_METHOD, makeConstant(OBJ_VAL(methodName)));
    }

    // Pop the class from the stack
    emitByte(stmt->line, OP_POP);

    // Close super scope if we had a superclass
    if (classStmt->superclassType != NULL || classStmt->superclassResolved != NULL) {
        endScope(stmt->line);
    }

    currentClass = currentClass->enclosing;
}

static bool compileStmt(Stmt* stmt) {
    if (stmt == NULL) return false;

    switch (stmt->kind) {
        case STMT_EXPRESSION:
            compileExpressionStmt(stmt);
            return false;
        case STMT_PRINT:
            compilePrintStmt(stmt);
            return false;
        case STMT_VAR:
            compileVarStmt(stmt);
            return false;
        case STMT_BLOCK:
            compileBlockStmt(stmt);
            return false;  // Blocks themselves don't terminate, their contents might
        case STMT_IF:
            compileIfStmt(stmt);
            return false;  // If statements don't unconditionally terminate
        case STMT_WHILE:
            compileWhileStmt(stmt);
            return false;
        case STMT_FOR:
            compileForStmt(stmt);
            return false;
        case STMT_FUNCTION:
            compileFunctionStmt(stmt);
            return false;
        case STMT_RETURN:
            compileReturnStmt(stmt);
            return true;  // Return is a terminator
        case STMT_CLASS:
            compileClassStmt(stmt);
            return false;
        case STMT_INTERFACE:
            compileInterfaceStmt(stmt);
            return false;
        case STMT_ENUM:
            compileEnumStmt(stmt);
            return false;
        case STMT_MATCH:
            compileMatchStmt(stmt);
            return false;
        case STMT_TRY:
            compileTryStmt(stmt);
            return false;
        case STMT_THROW:
            compileThrowStmt(stmt);
            return true;  // Throw is a terminator
        case STMT_IMPORT:
            // TODO: Module imports are processed before compilation
            // For now, this is a no-op as modules are loaded at the VM level
            return false;
        case STMT_TYPE_ALIAS:
            return false;
    }

    return false;
}

