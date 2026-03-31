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

/* Monomorph lowering only applies to fully concrete instantiations. Template-only
 * uses (e.g. Base<T> with T still a type parameter) must not be queued — they
 * would mangle to names like Base__unknown and emit useless prepended classes. */
static bool genericInstHasConcreteTypeArgs(Type* t) {
    if (t == NULL || t->kind != TYPE_GENERIC_INST) {
        return false;
    }
    GenericInst* gi = &t->as.genericInst;
    for (int i = 0; i < gi->typeArgCount; i++) {
        Type* a = gi->typeArgs[i];
        if (a == NULL) {
            return false;
        }
        switch (a->kind) {
            case TYPE_TYPE_PARAM:
            case TYPE_UNKNOWN:
            case TYPE_ERROR:
                return false;
            case TYPE_GENERIC_INST:
                if (!genericInstHasConcreteTypeArgs(a)) {
                    return false;
                }
                break;
            default:
                break;
        }
    }
    return true;
}

static void pushGenericInst(TypeChecker* checker, Type* instType, Stmt* templateStmt) {
    for (int i = 0; i < checker->genericInstCount; i++) {
        if (typesEqual(checker->genericInsts[i].instType, instType)) {
            return;
        }
    }

    if (instType->kind == TYPE_GENERIC_INST && !genericInstHasConcreteTypeArgs(instType)) {
        return;
    }

    /* Superclass monomorphs must be emitted first (prepend order). */
    if (templateStmt->kind == STMT_CLASS && instType->kind == TYPE_GENERIC_INST) {
        ClassStmt* cs = &templateStmt->as.class_;
        if (cs->superclassType != NULL) {
            Type** repl = instType->as.genericInst.typeArgs;
            TypeNode* subSuper = substituteTypeNodeForClass(cs->superclassType, cs, repl, templateStmt->line);
            if (subSuper != NULL) {
                Type* superT = resolveTypeNode(checker, subSuper);
                freeTypeNode(subSuper);
                if (superT != NULL && superT->kind == TYPE_GENERIC_INST) {
                    Type* tpl = superT->as.genericInst.genericType;
                    if (tpl != NULL && tpl->kind == TYPE_GENERIC_CLASS_TEMPLATE) {
                        Stmt* superStmt = tpl->as.genericClassTemplate->classStmt;
                        pushGenericInst(checker, superT, superStmt);
                    }
                }
            }
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

static void registerClassDecl(TypeChecker* checker, Type* classType, Stmt* classStmt) {
    if (classType == NULL || classType->kind != TYPE_CLASS || classStmt == NULL) return;
    for (int i = 0; i < checker->classDeclCount; i++) {
        if (checker->classDeclTypes[i] == classType) return;
    }
    if (checker->classDeclCount >= checker->classDeclCapacity) {
        int old = checker->classDeclCapacity;
        checker->classDeclCapacity = GROW_CAPACITY(old);
        checker->classDeclTypes = GROW_ARRAY(Type*, checker->classDeclTypes, old, checker->classDeclCapacity);
        checker->classDeclStmts = GROW_ARRAY(Stmt*, checker->classDeclStmts, old, checker->classDeclCapacity);
    }
    checker->classDeclTypes[checker->classDeclCount] = classType;
    checker->classDeclStmts[checker->classDeclCount] = classStmt;
    checker->classDeclCount++;
}

static Stmt* findClassDecl(TypeChecker* checker, Type* classType) {
    if (classType == NULL) return NULL;
    for (int i = 0; i < checker->classDeclCount; i++) {
        if (checker->classDeclTypes[i] == classType) {
            return checker->classDeclStmts[i];
        }
    }
    return NULL;
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

