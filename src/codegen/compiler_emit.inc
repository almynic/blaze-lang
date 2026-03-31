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

static void emitGetGlobalForClassType(Type* t, int line) {
    char buf[512];
    const char* nameStart;
    int nameLen;
    if (t->kind == TYPE_CLASS) {
        nameStart = t->as.classType->name;
        nameLen = t->as.classType->nameLength;
    } else if (t->kind == TYPE_GENERIC_INST) {
        mangleGenericInstType(t, buf, sizeof(buf));
        nameStart = buf;
        nameLen = (int)strlen(buf);
    } else {
        compileError(line, "Invalid superclass type for inheritance.");
        return;
    }
    /* Prepended monomorph classes live in script locals (compileImpl beginScope). */
    Token nm;
    nm.type = TOKEN_IDENTIFIER;
    nm.start = nameStart;
    nm.length = nameLen;
    nm.line = line;
    int arg = resolveLocal(current, &nm);
    if (arg != -1) {
        emitBytes(line, OP_GET_LOCAL, (uint8_t)arg);
    } else if ((arg = resolveUpvalue(current, &nm)) != -1) {
        emitBytes(line, OP_GET_UPVALUE, (uint8_t)arg);
    } else {
        ObjString* name = copyString(nameStart, nameLen);
        emitBytes(line, OP_GET_GLOBAL, makeConstant(OBJ_VAL(name)));
    }
}
