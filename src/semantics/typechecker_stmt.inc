// ============================================================================
// Statement Type Checking
// ============================================================================


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
    registerClassDecl(checker, classType, stmt);

    if (classStmt->superclassType != NULL) {
        Type* st = resolveTypeNode(checker, classStmt->superclassType);
        classStmt->superclassResolved = st;
        if (st->kind == TYPE_CLASS) {
            classType->as.classType->superclass = st->as.classType;
        }
    }

    // Set current class for 'this' checking
    Type* previousClass = checker->currentClass;
    Stmt* previousClassStmt = checker->currentClassStmt;
    checker->currentClass = classType;
    checker->currentClassStmt = stmt;

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
    checker->currentClassStmt = previousClassStmt;
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
        Stmt* prevClassStmt = checker->currentClassStmt;
        checker->currentClass = inst;
        checker->currentClassStmt = stmt;

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
        checker->currentClassStmt = prevClassStmt;
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

static bool tokenEqualsCString(Token t, const char* s) {
    int n = (int)strlen(s);
    return t.length == n && memcmp(t.start, s, (size_t)n) == 0;
}

static bool importPathIsStdArray(Token path) {
    if (tokenEqualsCString(path, "std/array")) return true;
    // Some scanner/parser paths may include quotes in token bounds.
    if (path.length == 11 && path.start[0] == '"' && path.start[10] == '"' &&
        memcmp(path.start + 1, "std/array", 9) == 0) {
        return true;
    }
    return false;
}

static void defineImportedStdArraySymbol(TypeChecker* checker, Token name) {
    if (tokenEqualsCString(name, "map")) {
        Type** params = ALLOCATE(Type*, 2);
        params[0] = createUnknownType();
        Type** fnParams = ALLOCATE(Type*, 1);
        fnParams[0] = createUnknownType();
        params[1] = createFunctionType(fnParams, 1, createUnknownType());
        defineSymbol(checker, "map", 3, createFunctionType(params, 2, createArrayType(createUnknownType())), true);
    } else if (tokenEqualsCString(name, "filter")) {
        Type** params = ALLOCATE(Type*, 2);
        params[0] = createUnknownType();
        Type** predParams = ALLOCATE(Type*, 1);
        predParams[0] = createUnknownType();
        params[1] = createFunctionType(predParams, 1, createBoolType());
        defineSymbol(checker, "filter", 6, createFunctionType(params, 2, createArrayType(createUnknownType())), true);
    } else if (tokenEqualsCString(name, "reduce")) {
        Type** params = ALLOCATE(Type*, 3);
        params[0] = createUnknownType();
        params[1] = createUnknownType();
        Type** accParams = ALLOCATE(Type*, 2);
        accParams[0] = createUnknownType();
        accParams[1] = createUnknownType();
        params[2] = createFunctionType(accParams, 2, createUnknownType());
        defineSymbol(checker, "reduce", 6, createFunctionType(params, 3, createUnknownType()), true);
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
            // Import type checking is mostly handled during module loading.
            // For selective std/array imports, predeclare key HOF symbols so
            // expressions like map(...)[0] are recognized as array-typed.
            ImportStmt* importStmt = &stmt->as.import;
            if (importStmt->nameCount > 0 && importPathIsStdArray(importStmt->path)) {
                for (int i = 0; i < importStmt->nameCount; i++) {
                    defineImportedStdArraySymbol(checker, importStmt->names[i]);
                }
            }
            break;
        }
    }
}

