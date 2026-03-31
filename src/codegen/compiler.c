/* AST → bytecode: expressions, control flow, functions/classes, enums, match,
 * generics (prepended lowered classes). Emits OP_* into Chunk constant pools. */

#include "compiler.h"
#include "memory.h"
#include "object.h"
#include "types.h"
#include <stdio.h>
#include <string.h>

// Current compiler (for nested functions)
static Compiler* current = NULL;

// Forward declarations
static ObjFunction* endCompiler(int line);
static bool compileStmt(Stmt* stmt);  // Returns true if statement is a terminator

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

static void emitGetGlobalForClassType(Type* t, int line) {
    char buf[512];
    if (t->kind == TYPE_CLASS) {
        ObjString* name = copyString(t->as.classType->name, t->as.classType->nameLength);
        emitBytes(line, OP_GET_GLOBAL, makeConstant(OBJ_VAL(name)));
    } else if (t->kind == TYPE_GENERIC_INST) {
        mangleGenericInstType(t, buf, sizeof(buf));
        ObjString* name = copyString(buf, (int)strlen(buf));
        emitBytes(line, OP_GET_GLOBAL, makeConstant(OBJ_VAL(name)));
    } else {
        compileError(line, "Invalid superclass type for inheritance.");
    }
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

static void pushActiveFinallyBody(Stmt* finallyBody, int line) {
    if (current->activeFinallyCount >= COMPILER_MAX_FINALLY_DEPTH) {
        compileError(line, "Too many nested try/finally blocks.");
        return;
    }
    current->activeFinallyBodies[current->activeFinallyCount++] = finallyBody;
}

static void popActiveFinallyBody(void) {
    if (current->activeFinallyCount <= 0) {
        return;
    }
    current->activeFinallyCount--;
}

static void emitReturnFinallyBodies(int line) {
    for (int i = current->activeFinallyCount - 1; i >= 0; i--) {
        Stmt* finallyBody = current->activeFinallyBodies[i];
        if (finallyBody != NULL) {
            compileStmt(finallyBody);
        } else {
            compileError(line, "Internal compiler error: missing finally block.");
        }
    }
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
    classCompiler.hasSuperclass = classStmt->superclassType != NULL;
    currentClass = &classCompiler;

    // Handle inheritance (superclass may be a script local from a prior `class` stmt)
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
    if (classStmt->superclassType != NULL) {
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
    compiler->activeFinallyCount = 0;

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

static ObjFunction* compileImpl(Stmt** prepend, int prependCount,
                                Stmt** statements, int count, bool replMode) {
    Compiler compiler;
    initCompiler(&compiler, NULL, FN_TYPE_SCRIPT);
    hadError = false;

    // Emit monomorphized classes at scope depth 0 so they become true globals.
    for (int i = 0; i < prependCount; i++) {
        compileClassStmt(prepend[i]);
    }

    if (!replMode) {
        beginScope();
    }

    for (int i = 0; i < count; i++) {
        bool isTerminator = compileStmt(statements[i]);

        if (isTerminator && i + 1 < count) {
            Stmt* nextStmt = statements[i + 1];
            if (nextStmt != NULL) {
                fprintf(stderr, "Warning [line %d]: Unreachable code after return/throw.\n",
                        nextStmt->line);
            }
            break;
        }
    }

    ObjFunction* function = endCompiler(0);
    return hadError ? NULL : function;
}

ObjFunction* compile(Stmt** statements, int count) {
    return compileImpl(NULL, 0, statements, count, false);
}

ObjFunction* compileWithPrependedClasses(Stmt** prepend, int prependCount,
                                         Stmt** statements, int count) {
    return compileImpl(prepend, prependCount, statements, count, false);
}

ObjFunction* compileRepl(Stmt** statements, int count) {
    return compileImpl(NULL, 0, statements, count, true);
}

ObjFunction* compileReplWithPrependedClasses(Stmt** prepend, int prependCount,
                                             Stmt** statements, int count) {
    return compileImpl(prepend, prependCount, statements, count, true);
}
