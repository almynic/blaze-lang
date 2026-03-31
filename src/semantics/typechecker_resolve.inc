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

