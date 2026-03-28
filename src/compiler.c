#include "compiler.h"
#include "memory.h"
#include "object.h"
#include <string.h>

// Current compiler (for nested functions)
static Compiler* current = NULL;

// Forward declarations
static ObjFunction* endCompiler(int line);

// Mark compiler roots for GC
void markCompilerRoots(void) {
    Compiler* compiler = current;
    while (compiler != NULL) {
        markObject((Obj*)compiler->function);
        compiler = compiler->enclosing;
    }
}

// Class compiler for tracking class context
typedef struct ClassCompiler {
    struct ClassCompiler* enclosing;
    bool hasSuperclass;
} ClassCompiler;

static ClassCompiler* currentClass = NULL;

// Error tracking
static bool hadError = false;

// ============================================================================
// Error Reporting
// ============================================================================

static void compileError(int line, const char* message) {
    hadError = true;
    fprintf(stderr, "[line %d] Compile error: %s\n", line, message);
}

// ============================================================================
// Chunk Helpers
// ============================================================================

static Chunk* currentChunk(void) {
    return &current->function->chunk;
}

static void emitByte(int line, uint8_t byte) {
    writeChunk(currentChunk(), byte, line);
}

static void emitBytes(int line, uint8_t byte1, uint8_t byte2) {
    emitByte(line, byte1);
    emitByte(line, byte2);
}

static void emitReturn(int line) {
    emitByte(line, OP_NIL);
    emitByte(line, OP_RETURN);
}

static uint8_t makeConstant(Value value) {
    int constant = addConstant(currentChunk(), value);
    if (constant > UINT8_MAX) {
        compileError(0, "Too many constants in one chunk.");
        return 0;
    }
    return (uint8_t)constant;
}

static void emitConstant(int line, Value value) {
    emitBytes(line, OP_CONSTANT, makeConstant(value));
}

static int emitJump(int line, uint8_t instruction) {
    emitByte(line, instruction);
    emitByte(line, 0xff);
    emitByte(line, 0xff);
    return currentChunk()->count - 2;
}

static void patchJump(int offset) {
    int jump = currentChunk()->count - offset - 2;

    if (jump > UINT16_MAX) {
        compileError(0, "Too much code to jump over.");
    }

    currentChunk()->code[offset] = (jump >> 8) & 0xff;
    currentChunk()->code[offset + 1] = jump & 0xff;
}

static void emitLoop(int line, int loopStart) {
    emitByte(line, OP_LOOP);

    int offset = currentChunk()->count - loopStart + 2;
    if (offset > UINT16_MAX) {
        compileError(line, "Loop body too large.");
    }

    emitByte(line, (offset >> 8) & 0xff);
    emitByte(line, offset & 0xff);
}

// ============================================================================
// Scope Management
// ============================================================================

static void beginScope(void) {
    current->scopeDepth++;
}

static void endScope(int line) {
    current->scopeDepth--;

    while (current->localCount > 0 &&
           current->locals[current->localCount - 1].depth > current->scopeDepth) {
        if (current->locals[current->localCount - 1].isCaptured) {
            emitByte(line, OP_CLOSE_UPVALUE);
        } else {
            emitByte(line, OP_POP);
        }
        current->localCount--;
    }
}

static void addLocal(Token name, Type* type, bool isConst) {
    if (current->localCount == UINT8_COUNT) {
        compileError(name.line, "Too many local variables in function.");
        return;
    }

    Local* local = &current->locals[current->localCount++];
    local->name = name;
    local->depth = current->scopeDepth;
    local->type = type;
    local->isConst = isConst;
    local->isCaptured = false;
}

static int resolveLocal(Compiler* compiler, Token* name) {
    for (int i = compiler->localCount - 1; i >= 0; i--) {
        Local* local = &compiler->locals[i];
        if (name->length == local->name.length &&
            memcmp(name->start, local->name.start, name->length) == 0) {
            return i;
        }
    }
    return -1;
}

static int addUpvalue(Compiler* compiler, uint8_t index, bool isLocal) {
    int upvalueCount = compiler->function->upvalueCount;

    // Check if we already have this upvalue
    for (int i = 0; i < upvalueCount; i++) {
        Upvalue* upvalue = &compiler->upvalues[i];
        if (upvalue->index == index && upvalue->isLocal == isLocal) {
            return i;
        }
    }

    if (upvalueCount == UINT8_COUNT) {
        compileError(0, "Too many closure variables in function.");
        return 0;
    }

    compiler->upvalues[upvalueCount].isLocal = isLocal;
    compiler->upvalues[upvalueCount].index = index;
    return compiler->function->upvalueCount++;
}

static int resolveUpvalue(Compiler* compiler, Token* name) {
    if (compiler->enclosing == NULL) return -1;

    int local = resolveLocal(compiler->enclosing, name);
    if (local != -1) {
        compiler->enclosing->locals[local].isCaptured = true;
        return addUpvalue(compiler, (uint8_t)local, true);
    }

    int upvalue = resolveUpvalue(compiler->enclosing, name);
    if (upvalue != -1) {
        return addUpvalue(compiler, (uint8_t)upvalue, false);
    }

    return -1;
}

// ============================================================================
// Expression Compilation
// ============================================================================

static void compileExpr(Expr* expr);
static void compileStmt(Stmt* stmt);
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
            // Create string object (skip the quotes)
            const char* start = lit->token.start + 1;
            int length = lit->token.length - 2;
            ObjString* string = copyString(start, length);
            emitConstant(expr->line, OBJ_VAL(string));
            break;
        }
        default:
            compileError(expr->line, "Unknown literal type.");
    }
}

static void compileUnary(Expr* expr) {
    UnaryExpr* unary = &expr->as.unary;

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
        default:
            compileError(expr->line, "Unknown unary operator.");
    }
}

static void compileBinary(Expr* expr) {
    BinaryExpr* binary = &expr->as.binary;

    compileExpr(binary->left);
    compileExpr(binary->right);

    bool isFloat = (binary->type && binary->type->kind == TYPE_FLOAT);
    bool isString = (binary->type && binary->type->kind == TYPE_STRING);

    switch (binary->op.type) {
        case TOKEN_PLUS:
            if (isString) {
                emitByte(expr->line, OP_CONCAT);
            } else {
                emitByte(expr->line, isFloat ? OP_ADD_FLOAT : OP_ADD_INT);
            }
            break;
        case TOKEN_MINUS:
            emitByte(expr->line, isFloat ? OP_SUBTRACT_FLOAT : OP_SUBTRACT_INT);
            break;
        case TOKEN_STAR:
            emitByte(expr->line, isFloat ? OP_MULTIPLY_FLOAT : OP_MULTIPLY_INT);
            break;
        case TOKEN_SLASH:
            emitByte(expr->line, isFloat ? OP_DIVIDE_FLOAT : OP_DIVIDE_INT);
            break;
        case TOKEN_PERCENT:
            emitByte(expr->line, OP_MODULO_INT);
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

            // Compile lambda body expression
            compileExpr(lambda->body);

            // Return the result of the body expression
            emitByte(expr->line, OP_RETURN);

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
        case EXPR_ARRAY:
            for (int i = 0; i < expr->as.array.elementCount; i++) {
                compileExpr(expr->as.array.elements[i]);
            }
            emitBytes(expr->line, OP_ARRAY, (uint8_t)expr->as.array.elementCount);
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
            // Wildcard: pop the value and evaluate the result expression
            emitByte(expr->line, OP_POP);
            compileExpr(c->value);
            // No need to jump - wildcard should be last
        } else {
            // Duplicate the value for comparison
            emitByte(expr->line, OP_DUP);
            // Compile the pattern
            compileExpr(c->pattern);
            // Compare
            emitByte(expr->line, OP_EQUAL);
            // Jump to next case if not equal
            int nextCase = emitJump(expr->line, OP_JUMP_IF_FALSE);
            // Pop the comparison result (true)
            emitByte(expr->line, OP_POP);
            // Pop the duplicated value
            emitByte(expr->line, OP_POP);
            // Compile the result expression (leaves value on stack)
            compileExpr(c->value);
            // Jump to end
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
        compileStmt(stmt->as.block.statements[i]);
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

static void compileForStmt(Stmt* stmt) {
    ForStmt* forStmt = &stmt->as.for_;

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
            compileStmt(block->statements[i]);
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

    if (current->type == FN_TYPE_INITIALIZER) {
        if (ret->value != NULL) {
            compileError(stmt->line, "Cannot return a value from an initializer.");
        }
        emitBytes(stmt->line, OP_GET_LOCAL, 0);  // Return 'this'
    } else if (ret->value != NULL) {
        compileExpr(ret->value);
    } else {
        emitByte(stmt->line, OP_NIL);
    }

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
            compileStmt(block->statements[i]);
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
            // Wildcard: pop the value and execute body
            emitByte(stmt->line, OP_POP);
            compileStmt(c->body);
            // No need to jump - wildcard should be last
        } else {
            // Duplicate the value for comparison
            emitByte(stmt->line, OP_DUP);
            // Compile the pattern
            compileExpr(c->pattern);
            // Compare
            emitByte(stmt->line, OP_EQUAL);
            // Jump to next case if not equal
            int nextCase = emitJump(stmt->line, OP_JUMP_IF_FALSE);
            // Pop the comparison result (true)
            emitByte(stmt->line, OP_POP);
            // Pop the duplicated value
            emitByte(stmt->line, OP_POP);
            // Execute the body
            compileStmt(c->body);
            // Jump to end
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
    compileStmt(tryStmt->tryBody);

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

    compileStmt(tryStmt->catchBody);
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

static void compileClassStmt(Stmt* stmt) {
    ClassStmt* classStmt = &stmt->as.class_;

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
    classCompiler.hasSuperclass = classStmt->hasSuperclass;
    currentClass = &classCompiler;

    // Handle inheritance
    if (classStmt->hasSuperclass) {
        // Load superclass
        int arg = resolveLocal(current, &classStmt->superclass);
        if (arg != -1) {
            emitBytes(stmt->line, OP_GET_LOCAL, (uint8_t)arg);
        } else if ((arg = resolveUpvalue(current, &classStmt->superclass)) != -1) {
            emitBytes(stmt->line, OP_GET_UPVALUE, (uint8_t)arg);
        } else {
            ObjString* superName = copyString(classStmt->superclass.start, classStmt->superclass.length);
            emitBytes(stmt->line, OP_GET_GLOBAL, makeConstant(OBJ_VAL(superName)));
        }

        // Get the class back on the stack
        if (current->scopeDepth > 0) {
            int slot = resolveLocal(current, &classStmt->name);
            emitBytes(stmt->line, OP_GET_LOCAL, (uint8_t)slot);
        } else {
            emitBytes(stmt->line, OP_GET_GLOBAL, nameConstant);
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
    if (current->scopeDepth > 0) {
        int slot = resolveLocal(current, &classStmt->name);
        emitBytes(stmt->line, OP_GET_LOCAL, (uint8_t)slot);
    } else {
        emitBytes(stmt->line, OP_GET_GLOBAL, nameConstant);
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
    if (classStmt->hasSuperclass) {
        endScope(stmt->line);
    }

    currentClass = currentClass->enclosing;
}

static void compileStmt(Stmt* stmt) {
    if (stmt == NULL) return;

    switch (stmt->kind) {
        case STMT_EXPRESSION:
            compileExpressionStmt(stmt);
            break;
        case STMT_PRINT:
            compilePrintStmt(stmt);
            break;
        case STMT_VAR:
            compileVarStmt(stmt);
            break;
        case STMT_BLOCK:
            compileBlockStmt(stmt);
            break;
        case STMT_IF:
            compileIfStmt(stmt);
            break;
        case STMT_WHILE:
            compileWhileStmt(stmt);
            break;
        case STMT_FOR:
            compileForStmt(stmt);
            break;
        case STMT_FUNCTION:
            compileFunctionStmt(stmt);
            break;
        case STMT_RETURN:
            compileReturnStmt(stmt);
            break;
        case STMT_CLASS:
            compileClassStmt(stmt);
            break;
        case STMT_MATCH:
            compileMatchStmt(stmt);
            break;
        case STMT_TRY:
            compileTryStmt(stmt);
            break;
        case STMT_THROW:
            compileThrowStmt(stmt);
            break;
        case STMT_IMPORT:
            // TODO: Module imports are processed before compilation
            // For now, this is a no-op as modules are loaded at the VM level
            break;
    }
}

// ============================================================================
// Public API
// ============================================================================

void initCompiler(Compiler* compiler, Compiler* enclosing, CompilerFnType type) {
    compiler->enclosing = enclosing;
    compiler->function = NULL;
    compiler->type = type;
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
    compiler->currentFunctionReturn = NULL;

    compiler->function = newFunction();

    current = compiler;

    // Reserve slot 0 for the function itself or 'this' in methods
    Local* local = &current->locals[current->localCount++];
    local->depth = 0;
    local->isCaptured = false;
    local->isConst = true;
    local->type = NULL;
    if (type != FN_TYPE_FUNCTION) {
        local->name.start = "this";
        local->name.length = 4;
    } else {
        local->name.start = "";
        local->name.length = 0;
    }
}

ObjFunction* compile(Stmt** statements, int count) {
    Compiler compiler;
    initCompiler(&compiler, NULL, FN_TYPE_SCRIPT);
    hadError = false;

    // Start at scope depth 1 so top-level variables are locals
    beginScope();

    for (int i = 0; i < count; i++) {
        compileStmt(statements[i]);
    }

    ObjFunction* function = endCompiler(0);

    return hadError ? NULL : function;
}

ObjFunction* compileRepl(Stmt** statements, int count) {
    Compiler compiler;
    initCompiler(&compiler, NULL, FN_TYPE_SCRIPT);
    hadError = false;

    // REPL mode: Don't call beginScope() so top-level variables are globals
    // This allows them to persist between REPL inputs

    for (int i = 0; i < count; i++) {
        compileStmt(statements[i]);
    }

    ObjFunction* function = endCompiler(0);

    return hadError ? NULL : function;
}
