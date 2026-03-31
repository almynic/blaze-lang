// ============================================================================
// Statement Parsing
// ============================================================================

static Stmt* block(Parser* parser) {
    Stmt** statements = NULL;
    int count = 0;
    int capacity = 0;

    skipNewlines(parser);

    while (!check(parser, TOKEN_RIGHT_BRACE) && !check(parser, TOKEN_EOF)) {
        Stmt* stmt = declaration(parser);
        if (stmt != NULL) {
            if (count >= capacity) {
                int oldCapacity = capacity;
                capacity = GROW_CAPACITY(oldCapacity);
                statements = GROW_ARRAY(Stmt*, statements, oldCapacity, capacity);
            }
            statements[count++] = stmt;
        }
        skipNewlines(parser);
    }

    consume(parser, TOKEN_RIGHT_BRACE, "Expected '}' after block.");
    return newBlockStmt(statements, count);
}

static Stmt* printStatement(Parser* parser) {
    Token keyword = parser->previous;
    consume(parser, TOKEN_LEFT_PAREN, "Expected '(' after 'print'.");
    Expr* value = expression(parser);
    consume(parser, TOKEN_RIGHT_PAREN, "Expected ')' after value.");
    consumeNewlineOrSemicolon(parser);
    return newPrintStmt(keyword, value);
}

static Stmt* ifStatement(Parser* parser) {
    Token keyword = parser->previous;
    Expr* condition = expression(parser);

    consume(parser, TOKEN_LEFT_BRACE, "Expected '{' after if condition.");
    Stmt* thenBranch = block(parser);

    Stmt* elseBranch = NULL;
    skipNewlines(parser);
    if (match(parser, TOKEN_ELSE)) {
        if (match(parser, TOKEN_IF)) {
            // else if
            elseBranch = ifStatement(parser);
        } else {
            consume(parser, TOKEN_LEFT_BRACE, "Expected '{' after 'else'.");
            elseBranch = block(parser);
        }
    }

    return newIfStmt(keyword, condition, thenBranch, elseBranch);
}

static Stmt* whileStatement(Parser* parser) {
    Token keyword = parser->previous;
    Expr* condition = expression(parser);

    consume(parser, TOKEN_LEFT_BRACE, "Expected '{' after while condition.");
    Stmt* body = block(parser);

    return newWhileStmt(keyword, condition, body);
}

static Stmt* forStatement(Parser* parser) {
    Token keyword = parser->previous;

    consume(parser, TOKEN_IDENTIFIER, "Expected variable name after 'for'.");
    Token variable = parser->previous;

    TypeNode* varType = NULL;
    if (match(parser, TOKEN_COLON)) {
        varType = typeAnnotation(parser);
    }

    consume(parser, TOKEN_IN, "Expected 'in' after for variable.");

    Expr* iterable = expression(parser);

    consume(parser, TOKEN_LEFT_BRACE, "Expected '{' after for iterable.");
    Stmt* body = block(parser);

    return newForStmt(keyword, variable, varType, iterable, body);
}

static Stmt* returnStatement(Parser* parser) {
    Token keyword = parser->previous;
    Expr* value = NULL;

    if (!check(parser, TOKEN_NEWLINE) && !check(parser, TOKEN_SEMICOLON) &&
        !check(parser, TOKEN_RIGHT_BRACE) && !check(parser, TOKEN_EOF)) {
        value = expression(parser);
    }

    consumeNewlineOrSemicolon(parser);
    return newReturnStmt(keyword, value);
}

static Stmt* varDeclaration(Parser* parser, bool isConst) {
    // Check for array destructuring: let [a, b, c] = ...
    if (check(parser, TOKEN_LEFT_BRACKET)) {
        advance(parser); // consume '['

        int capacity = 4;
        int count = 0;
        Token* names = ALLOCATE(Token, capacity);
        int restIndex = -1;

        if (!check(parser, TOKEN_RIGHT_BRACKET)) {
            do {
                // Check for rest parameter: ...rest
                bool isRest = false;
                if (match(parser, TOKEN_DOT_DOT_DOT)) {
                    isRest = true;
                    if (restIndex != -1) {
                        error(parser, "Only one rest parameter allowed in destructuring.");
                    }
                    restIndex = count;
                }

                if (count >= capacity) {
                    int old = capacity;
                    capacity = GROW_CAPACITY(old);
                    names = GROW_ARRAY(Token, names, old, capacity);
                }

                consume(parser, TOKEN_IDENTIFIER, "Expected variable name in destructuring pattern.");
                names[count++] = parser->previous;

                // Rest parameter must be last
                if (isRest && check(parser, TOKEN_COMMA)) {
                    error(parser, "Rest parameter must be last in destructuring pattern.");
                    // Continue parsing to avoid cascading errors
                }
            } while (match(parser, TOKEN_COMMA));
        }

        consume(parser, TOKEN_RIGHT_BRACKET, "Expected ']' after destructuring pattern.");

        // Optional type annotation (applies to the whole array)
        TypeNode* type = NULL;
        if (match(parser, TOKEN_COLON)) {
            type = typeAnnotation(parser);
        }

        // Destructuring requires initializer
        Expr* initializer = NULL;
        if (match(parser, TOKEN_EQUAL)) {
            initializer = expression(parser);
        } else {
            error(parser, "Array destructuring requires an initializer.");
        }

        consumeNewlineOrSemicolon(parser);
        return newArrayDestructureStmt(names, count, restIndex, type, initializer, isConst);
    }

    // Check for object destructuring: let {x, y} = ...
    if (check(parser, TOKEN_LEFT_BRACE)) {
        advance(parser); // consume '{'

        int capacity = 4;
        int count = 0;
        Token* names = ALLOCATE(Token, capacity);

        if (!check(parser, TOKEN_RIGHT_BRACE)) {
            do {
                if (count >= capacity) {
                    int old = capacity;
                    capacity = GROW_CAPACITY(old);
                    names = GROW_ARRAY(Token, names, old, capacity);
                }

                consume(parser, TOKEN_IDENTIFIER, "Expected property name in object destructuring pattern.");
                names[count++] = parser->previous;
            } while (match(parser, TOKEN_COMMA));
        }

        consume(parser, TOKEN_RIGHT_BRACE, "Expected '}' after object destructuring pattern.");

        // Optional type annotation
        TypeNode* type = NULL;
        if (match(parser, TOKEN_COLON)) {
            type = typeAnnotation(parser);
        }

        // Destructuring requires initializer
        Expr* initializer = NULL;
        if (match(parser, TOKEN_EQUAL)) {
            initializer = expression(parser);
        } else {
            error(parser, "Object destructuring requires an initializer.");
        }

        consumeNewlineOrSemicolon(parser);
        return newObjectDestructureStmt(names, count, type, initializer, isConst);
    }

    // Simple variable declaration
    consume(parser, TOKEN_IDENTIFIER, "Expected variable name.");
    Token name = parser->previous;

    TypeNode* type = NULL;
    if (match(parser, TOKEN_COLON)) {
        type = typeAnnotation(parser);
    }

    Expr* initializer = NULL;
    if (match(parser, TOKEN_EQUAL)) {
        initializer = expression(parser);
    } else if (isConst) {
        error(parser, "Constant must have initializer.");
    }

    consumeNewlineOrSemicolon(parser);
    return newVarStmt(name, type, initializer, isConst);
}

static Stmt* functionDeclaration(Parser* parser) {
    // Check for type parameters: fn<T, U>(params) -> returnType
    Token* typeParams = NULL;
    int typeParamCount = 0;
    int typeParamCapacity = 0;

    consume(parser, TOKEN_IDENTIFIER, "Expected function name.");
    Token name = parser->previous;

    // Check for type parameters
    if (match(parser, TOKEN_LESS)) {
        // Parse type parameters
        if (!check(parser, TOKEN_GREATER)) {
            do {
                if (typeParamCount >= typeParamCapacity) {
                    int oldCapacity = typeParamCapacity;
                    typeParamCapacity = GROW_CAPACITY(oldCapacity);
                    typeParams = GROW_ARRAY(Token, typeParams, oldCapacity, typeParamCapacity);
                }

                consume(parser, TOKEN_IDENTIFIER, "Expected type parameter name.");
                typeParams[typeParamCount++] = parser->previous;
            } while (match(parser, TOKEN_COMMA));
        }

        consume(parser, TOKEN_GREATER, "Expected '>' after type parameters.");
    }

    consume(parser, TOKEN_LEFT_PAREN, "Expected '(' after function name.");

    Token* params = NULL;
    TypeNode** paramTypes = NULL;
    int paramCount = 0;
    int paramCapacity = 0;

    if (!check(parser, TOKEN_RIGHT_PAREN)) {
        do {
            if (paramCount >= paramCapacity) {
                int oldCapacity = paramCapacity;
                paramCapacity = GROW_CAPACITY(oldCapacity);
                params = GROW_ARRAY(Token, params, oldCapacity, paramCapacity);
                paramTypes = GROW_ARRAY(TypeNode*, paramTypes, oldCapacity, paramCapacity);
            }

            consume(parser, TOKEN_IDENTIFIER, "Expected parameter name.");
            params[paramCount] = parser->previous;

            consume(parser, TOKEN_COLON, "Expected ':' after parameter name.");
            paramTypes[paramCount] = typeAnnotation(parser);

            paramCount++;
        } while (match(parser, TOKEN_COMMA));
    }

    consumeWithSuggestion(parser, TOKEN_RIGHT_PAREN, "Expected ')' after parameters.",
                          "Did you forget to close the parameter list with ')'?");

    TypeNode* returnType = NULL;
    if (match(parser, TOKEN_ARROW)) {
        returnType = typeAnnotation(parser);
    }

    consume(parser, TOKEN_LEFT_BRACE, "Expected '{' before function body.");
    Stmt* body = block(parser);

    return newFunctionStmt(name, typeParams, typeParamCount, params, paramTypes, paramCount, returnType, body);
}

static Stmt* interfaceDeclaration(Parser* parser) {
    consume(parser, TOKEN_IDENTIFIER, "Expected interface name.");
    Token name = parser->previous;

    consume(parser, TOKEN_LEFT_BRACE, "Expected '{' before interface body.");
    skipNewlines(parser);

    int capacity = 8;
    int count = 0;
    InterfaceMethodDecl* methods = ALLOCATE(InterfaceMethodDecl, capacity);

    while (!check(parser, TOKEN_RIGHT_BRACE) && !check(parser, TOKEN_EOF)) {
        if (!match(parser, TOKEN_FN)) {
            error(parser, "Expected 'fn' for interface method.");
            break;
        }

        consume(parser, TOKEN_IDENTIFIER, "Expected method name.");
        Token methodName = parser->previous;

        consume(parser, TOKEN_LEFT_PAREN, "Expected '(' after method name.");

        Token* params = NULL;
        TypeNode** paramTypes = NULL;
        int paramCount = 0;
        int pCapacity = 0;

        if (!check(parser, TOKEN_RIGHT_PAREN)) {
            do {
                if (paramCount >= pCapacity) {
                    int old = pCapacity;
                    pCapacity = GROW_CAPACITY(old);
                    params = GROW_ARRAY(Token, params, old, pCapacity);
                    paramTypes = GROW_ARRAY(TypeNode*, paramTypes, old, pCapacity);
                }

                consume(parser, TOKEN_IDENTIFIER, "Expected parameter name.");
                params[paramCount] = parser->previous;

                consume(parser, TOKEN_COLON, "Expected ':' after parameter name.");
                paramTypes[paramCount] = typeAnnotation(parser);

                paramCount++;
            } while (match(parser, TOKEN_COMMA));
        }

        consume(parser, TOKEN_RIGHT_PAREN, "Expected ')' after parameters.");

        TypeNode* returnType = NULL;
        if (match(parser, TOKEN_ARROW)) {
            returnType = typeAnnotation(parser);
        }

        if (count >= capacity) {
            int oldCapacity = capacity;
            capacity = GROW_CAPACITY(oldCapacity);
            methods = GROW_ARRAY(InterfaceMethodDecl, methods, oldCapacity, capacity);
        }

        methods[count].name = methodName;
        methods[count].params = params;
        methods[count].paramTypes = paramTypes;
        methods[count].paramCount = paramCount;
        methods[count].returnType = returnType;
        count++;

        skipNewlines(parser);
    }

    consume(parser, TOKEN_RIGHT_BRACE, "Expected '}' after interface body.");
    return newInterfaceStmt(name, methods, count);
}

static Stmt* classDeclaration(Parser* parser) {
    consume(parser, TOKEN_IDENTIFIER, "Expected class name.");
    Token name = parser->previous;

    Token* typeParams = NULL;
    TypeParamVariance* typeParamVariances = NULL;
    Token* typeParamBounds = NULL;
    int typeParamCount = 0;
    int typeParamCapacity = 0;
    if (match(parser, TOKEN_LESS)) {
        if (!check(parser, TOKEN_GREATER)) {
            do {
                if (typeParamCount >= typeParamCapacity) {
                    int oldCapacity = typeParamCapacity;
                    typeParamCapacity = GROW_CAPACITY(oldCapacity);
                    typeParams = GROW_ARRAY(Token, typeParams, oldCapacity, typeParamCapacity);
                    typeParamVariances = GROW_ARRAY(TypeParamVariance, typeParamVariances, oldCapacity,
                                                    typeParamCapacity);
                    typeParamBounds = GROW_ARRAY(Token, typeParamBounds, oldCapacity, typeParamCapacity);
                }
                TypeParamVariance variance = TYPE_PARAM_VAR_INVARIANT;
                if (match(parser, TOKEN_OUT)) {
                    variance = TYPE_PARAM_VAR_OUT;
                } else if (match(parser, TOKEN_IN)) {
                    variance = TYPE_PARAM_VAR_IN;
                }
                consume(parser, TOKEN_IDENTIFIER, "Expected type parameter name.");
                typeParams[typeParamCount] = parser->previous;
                typeParamVariances[typeParamCount] = variance;
                Token boundTok;
                boundTok.type = TOKEN_IDENTIFIER;
                boundTok.start = NULL;
                boundTok.length = 0;
                boundTok.line = parser->previous.line;
                if (match(parser, TOKEN_COLON)) {
                    consume(parser, TOKEN_IDENTIFIER, "Expected bound interface name.");
                    boundTok = parser->previous;
                }
                typeParamBounds[typeParamCount] = boundTok;
                typeParamCount++;
            } while (match(parser, TOKEN_COMMA));
        }
        consume(parser, TOKEN_GREATER, "Expected '>' after type parameters.");
    }

    TypeNode* superclassType = NULL;
    if (match(parser, TOKEN_EXTENDS)) {
        superclassType = typeAnnotation(parser);
    }

    Token* implementsNames = NULL;
    int implementsCount = 0;
    int implementsCapacity = 0;
    if (match(parser, TOKEN_IMPLEMENTS)) {
        do {
            if (implementsCount >= implementsCapacity) {
                int old = implementsCapacity;
                implementsCapacity = GROW_CAPACITY(old);
                implementsNames = GROW_ARRAY(Token, implementsNames, old, implementsCapacity);
            }
            consume(parser, TOKEN_IDENTIFIER, "Expected interface name.");
            implementsNames[implementsCount++] = parser->previous;
        } while (match(parser, TOKEN_COMMA));
    }

    consume(parser, TOKEN_LEFT_BRACE, "Expected '{' before class body.");
    skipNewlines(parser);

    FieldDecl* fields = NULL;
    int fieldCount = 0;
    int fieldCapacity = 0;

    FunctionStmt* methods = NULL;
    int methodCount = 0;
    int methodCapacity = 0;

    while (!check(parser, TOKEN_RIGHT_BRACE) && !check(parser, TOKEN_EOF)) {
        if (match(parser, TOKEN_FN)) {
            // Method declaration
            consume(parser, TOKEN_IDENTIFIER, "Expected method name.");
            Token methodName = parser->previous;

            consume(parser, TOKEN_LEFT_PAREN, "Expected '(' after method name.");

            Token* params = NULL;
            TypeNode** paramTypes = NULL;
            int paramCount = 0;
            int pCapacity = 0;

            if (!check(parser, TOKEN_RIGHT_PAREN)) {
                do {
                    if (paramCount >= pCapacity) {
                        int old = pCapacity;
                        pCapacity = GROW_CAPACITY(old);
                        params = GROW_ARRAY(Token, params, old, pCapacity);
                        paramTypes = GROW_ARRAY(TypeNode*, paramTypes, old, pCapacity);
                    }

                    consume(parser, TOKEN_IDENTIFIER, "Expected parameter name.");
                    params[paramCount] = parser->previous;

                    consume(parser, TOKEN_COLON, "Expected ':' after parameter name.");
                    paramTypes[paramCount] = typeAnnotation(parser);

                    paramCount++;
                } while (match(parser, TOKEN_COMMA));
            }

            consume(parser, TOKEN_RIGHT_PAREN, "Expected ')' after parameters.");

            TypeNode* returnType = NULL;
            if (match(parser, TOKEN_ARROW)) {
                returnType = typeAnnotation(parser);
            }

            consume(parser, TOKEN_LEFT_BRACE, "Expected '{' before method body.");
            Stmt* body = block(parser);

            if (methodCount >= methodCapacity) {
                int old = methodCapacity;
                methodCapacity = GROW_CAPACITY(old);
                methods = GROW_ARRAY(FunctionStmt, methods, old, methodCapacity);
            }

            methods[methodCount].name = methodName;
            methods[methodCount].typeParams = NULL;
            methods[methodCount].typeParamCount = 0;
            methods[methodCount].params = params;
            methods[methodCount].paramTypes = paramTypes;
            methods[methodCount].paramCount = paramCount;
            methods[methodCount].returnType = returnType;
            methods[methodCount].body = body;
            methods[methodCount].type = NULL;
            methodCount++;
        } else if (match(parser, TOKEN_IDENTIFIER)) {
            // Field declaration
            Token fieldName = parser->previous;
            consume(parser, TOKEN_COLON, "Expected ':' after field name.");
            TypeNode* fieldType = typeAnnotation(parser);

            if (fieldCount >= fieldCapacity) {
                int oldCapacity = fieldCapacity;
                fieldCapacity = GROW_CAPACITY(oldCapacity);
                fields = GROW_ARRAY(FieldDecl, fields, oldCapacity, fieldCapacity);
            }
            fields[fieldCount].name = fieldName;
            fields[fieldCount].type = fieldType;
            fieldCount++;

            consumeNewlineOrSemicolon(parser);
        } else {
            error(parser, "Expected field or method declaration in class.");
            advance(parser);
        }

        skipNewlines(parser);
    }

    consume(parser, TOKEN_RIGHT_BRACE, "Expected '}' after class body.");

    return newClassStmt(name, typeParams, typeParamCount, typeParamVariances,
                        typeParamBounds, superclassType, implementsNames, implementsCount,
                        fields, fieldCount, methods, methodCount);
}

static Stmt* typeAliasDeclaration(Parser* parser) {
    Token keyword = parser->previous;
    consume(parser, TOKEN_IDENTIFIER, "Expected type alias name.");
    Token name = parser->previous;
    consume(parser, TOKEN_EQUAL, "Expected '=' in type alias.");
    TypeNode* target = typeAnnotation(parser);
    consumeNewlineOrSemicolon(parser);
    return newTypeAliasStmt(keyword, name, target);
}

static Stmt* enumDeclaration(Parser* parser) {
    consume(parser, TOKEN_IDENTIFIER, "Expected enum name.");
    Token name = parser->previous;

    consume(parser, TOKEN_LEFT_BRACE, "Expected '{' before enum body.");
    skipNewlines(parser);

    int capacity = 8;
    int count = 0;
    EnumVariant* variants = ALLOCATE(EnumVariant, capacity);

    while (!check(parser, TOKEN_RIGHT_BRACE) && !check(parser, TOKEN_EOF)) {
        if (count >= capacity) {
            int oldCapacity = capacity;
            capacity = GROW_CAPACITY(oldCapacity);
            variants = GROW_ARRAY(EnumVariant, variants, oldCapacity, capacity);
        }

        consume(parser, TOKEN_IDENTIFIER, "Expected variant name.");
        variants[count].name = parser->previous;
        variants[count].fieldTypes = NULL;
        variants[count].fieldCount = 0;

        // Optional fields: Variant(type1, type2)
        if (match(parser, TOKEN_LEFT_PAREN)) {
            int fCapacity = 4;
            int fCount = 0;
            TypeNode** fieldTypes = ALLOCATE(TypeNode*, fCapacity);

            if (!check(parser, TOKEN_RIGHT_PAREN)) {
                do {
                    if (fCount >= fCapacity) {
                        int old = fCapacity;
                        fCapacity = GROW_CAPACITY(old);
                        fieldTypes = GROW_ARRAY(TypeNode*, fieldTypes, old, fCapacity);
                    }
                    fieldTypes[fCount++] = typeAnnotation(parser);
                } while (match(parser, TOKEN_COMMA));
            }

            consume(parser, TOKEN_RIGHT_PAREN, "Expected ')' after variant fields.");
            variants[count].fieldTypes = fieldTypes;
            variants[count].fieldCount = fCount;
        }

        count++;
        skipNewlines(parser);

        // Optional comma or newline
        if (match(parser, TOKEN_COMMA)) {
            skipNewlines(parser);
        }
    }

    consume(parser, TOKEN_RIGHT_BRACE, "Expected '}' after enum body.");

    return newEnumStmt(name, variants, count);
}

static Stmt* expressionStatement(Parser* parser) {
    Expr* expr = expression(parser);
    consumeNewlineOrSemicolon(parser);
    return newExpressionStmt(expr);
}

static Stmt* matchStatement(Parser* parser) {
    Token keyword = parser->previous;

    // Parse the value to match
    Expr* value = expression(parser);

    consume(parser, TOKEN_LEFT_BRACE, "Expected '{' after match value.");
    skipNewlines(parser);

    // Parse case clauses
    int capacity = 8;
    int count = 0;
    CaseClause* cases = ALLOCATE(CaseClause, capacity);

    while (!check(parser, TOKEN_RIGHT_BRACE) && !check(parser, TOKEN_EOF)) {
        if (count >= capacity) {
            int oldCapacity = capacity;
            capacity = GROW_CAPACITY(oldCapacity);
            cases = GROW_ARRAY(CaseClause, cases, oldCapacity, capacity);
        }

        cases[count].destructureParams = NULL;
        cases[count].destructureCount = 0;

        // Check for wildcard pattern '_'
        if (check(parser, TOKEN_IDENTIFIER) && parser->current.length == 1 && parser->current.start[0] == '_') {
            advance(parser);  // consume _
            cases[count].pattern = NULL;
            cases[count].isWildcard = true;
        } else {
            // Literal pattern or Enum variant Color.Red
            cases[count].pattern = parsePrecedence(parser, PREC_CALL);
            cases[count].isWildcard = false;

            // Check for destructuring: Variant(a, b)
            if (match(parser, TOKEN_LEFT_PAREN)) {
                    int dCapacity = 4;
                    int dCount = 0;
                    Token* dParams = ALLOCATE(Token, dCapacity);

                    if (!check(parser, TOKEN_RIGHT_PAREN)) {
                        do {
                            if (dCount >= dCapacity) {
                                int old = dCapacity;
                                dCapacity = GROW_CAPACITY(old);
                                dParams = GROW_ARRAY(Token, dParams, old, dCapacity);
                            }
                            consume(parser, TOKEN_IDENTIFIER, "Expected parameter name in destructuring.");
                            dParams[dCount++] = parser->previous;
                        } while (match(parser, TOKEN_COMMA));
                    }

                    consume(parser, TOKEN_RIGHT_PAREN, "Expected ')' after destructuring parameters.");
                    cases[count].destructureParams = dParams;
                    cases[count].destructureCount = dCount;
                }
        }

        // Expect '=>' or directly a block
        if (match(parser, TOKEN_FAT_ARROW)) {
            // => { body } or => expr
            if (match(parser, TOKEN_LEFT_BRACE)) {
                cases[count].body = block(parser);
            } else {
                // Single expression body
                Expr* expr = expression(parser);
                cases[count].body = newExpressionStmt(expr);
                consumeNewlineOrSemicolon(parser);
            }
        } else if (match(parser, TOKEN_LEFT_BRACE)) {
            // Direct block without =>
            cases[count].body = block(parser);
        } else {
            error(parser, "Expected '=>' or '{' after match pattern.");
            return NULL;
        }

        count++;
        if (cases[count - 1].isWildcard) {
            skipNewlines(parser);
            if (!check(parser, TOKEN_RIGHT_BRACE) && !check(parser, TOKEN_EOF)) {
                error(parser, "Wildcard '_' case must be the last match arm.");
                return NULL;
            }
        }
        skipNewlines(parser);
    }

    consume(parser, TOKEN_RIGHT_BRACE, "Expected '}' after match cases.");

    return newMatchStmt(keyword, value, cases, count);
}

static Expr* matchExpression(Parser* parser) {
    Token keyword = parser->previous;

    // Parse the value to match
    Expr* value = expression(parser);

    consume(parser, TOKEN_LEFT_BRACE, "Expected '{' after match value.");
    skipNewlines(parser);

    // Parse case clauses as expressions
    int capacity = 8;
    int count = 0;
    ExprCaseClause* cases = ALLOCATE(ExprCaseClause, capacity);

    while (!check(parser, TOKEN_RIGHT_BRACE) && !check(parser, TOKEN_EOF)) {
        if (count >= capacity) {
            int oldCapacity = capacity;
            capacity = GROW_CAPACITY(oldCapacity);
            cases = GROW_ARRAY(ExprCaseClause, cases, oldCapacity, capacity);
        }

        cases[count].destructureParams = NULL;
        cases[count].destructureCount = 0;

        // Check for wildcard pattern '_'
        if (check(parser, TOKEN_IDENTIFIER) && parser->current.length == 1 && parser->current.start[0] == '_') {
            advance(parser);  // consume _
            cases[count].pattern = NULL;
            cases[count].isWildcard = true;
        } else {
            // Literal pattern or Enum variant Color.Red
            cases[count].pattern = parsePrecedence(parser, PREC_CALL);
            cases[count].isWildcard = false;

            // Check for destructuring: Variant(a, b)
            if (match(parser, TOKEN_LEFT_PAREN)) {
                    int dCapacity = 4;
                    int dCount = 0;
                    Token* dParams = ALLOCATE(Token, dCapacity);

                    if (!check(parser, TOKEN_RIGHT_PAREN)) {
                        do {
                            if (dCount >= dCapacity) {
                                int old = dCapacity;
                                dCapacity = GROW_CAPACITY(old);
                                dParams = GROW_ARRAY(Token, dParams, old, dCapacity);
                            }
                            consume(parser, TOKEN_IDENTIFIER, "Expected parameter name in destructuring.");
                            dParams[dCount++] = parser->previous;
                        } while (match(parser, TOKEN_COMMA));
                    }

                    consume(parser, TOKEN_RIGHT_PAREN, "Expected ')' after destructuring parameters.");
                    cases[count].destructureParams = dParams;
                    cases[count].destructureCount = dCount;
                }
        }

        // Expect '=>' followed by an expression
        consume(parser, TOKEN_FAT_ARROW, "Expected '=>' after match pattern in match expression.");

        // Parse the value expression for this case
        cases[count].value = expression(parser);

        count++;
        if (cases[count - 1].isWildcard) {
            skipNewlines(parser);
            if (!check(parser, TOKEN_RIGHT_BRACE) && !check(parser, TOKEN_EOF)) {
                error(parser, "Wildcard '_' case must be the last match arm.");
                return NULL;
            }
        }

        // Optional comma or newline between cases
        if (!check(parser, TOKEN_RIGHT_BRACE)) {
            if (match(parser, TOKEN_COMMA)) {
                skipNewlines(parser);
            } else {
                consumeNewlineOrSemicolon(parser);
            }
        }
        skipNewlines(parser);
    }

    consume(parser, TOKEN_RIGHT_BRACE, "Expected '}' after match cases.");

    return newMatchExpr(keyword, value, cases, count);
}

static Stmt* tryStatement(Parser* parser) {
    Token keyword = parser->previous;

    // Parse try body
    consume(parser, TOKEN_LEFT_BRACE, "Expected '{' after 'try'.");
    Stmt* tryBody = block(parser);

    // Parse catch clause
    consume(parser, TOKEN_CATCH, "Expected 'catch' after try block.");
    consume(parser, TOKEN_LEFT_PAREN, "Expected '(' after 'catch'.");
    consume(parser, TOKEN_IDENTIFIER, "Expected exception variable name.");
    Token catchVar = parser->previous;
    consume(parser, TOKEN_RIGHT_PAREN, "Expected ')' after exception variable.");

    consume(parser, TOKEN_LEFT_BRACE, "Expected '{' after catch clause.");
    Stmt* catchBody = block(parser);

    // Parse optional finally clause
    Stmt* finallyBody = NULL;
    if (match(parser, TOKEN_FINALLY)) {
        consume(parser, TOKEN_LEFT_BRACE, "Expected '{' after 'finally'.");
        finallyBody = block(parser);
    }

    return newTryStmt(keyword, tryBody, catchVar, catchBody, finallyBody);
}

static Stmt* throwStatement(Parser* parser) {
    Token keyword = parser->previous;
    Expr* value = expression(parser);
    consumeNewlineOrSemicolon(parser);
    return newThrowStmt(keyword, value);
}

static Stmt* statement(Parser* parser) {
    if (match(parser, TOKEN_PRINT)) {
        return printStatement(parser);
    }
    if (match(parser, TOKEN_IF)) {
        return ifStatement(parser);
    }
    if (match(parser, TOKEN_WHILE)) {
        return whileStatement(parser);
    }
    if (match(parser, TOKEN_FOR)) {
        return forStatement(parser);
    }
    if (match(parser, TOKEN_RETURN)) {
        return returnStatement(parser);
    }
    if (match(parser, TOKEN_MATCH)) {
        return matchStatement(parser);
    }
    if (match(parser, TOKEN_TRY)) {
        return tryStatement(parser);
    }
    if (match(parser, TOKEN_THROW)) {
        return throwStatement(parser);
    }
    if (match(parser, TOKEN_LEFT_BRACE)) {
        return block(parser);
    }

    return expressionStatement(parser);
}

static Stmt* importDeclaration(Parser* parser) {
    Token keyword = parser->previous;

    // Check for selective import: import { a, b } from "path"
    if (match(parser, TOKEN_LEFT_BRACE)) {
        // Parse imported names
        int capacity = 8;
        int count = 0;
        Token* names = ALLOCATE(Token, capacity);

        do {
            if (count >= capacity) {
                int oldCapacity = capacity;
                capacity = GROW_CAPACITY(oldCapacity);
                names = GROW_ARRAY(Token, names, oldCapacity, capacity);
            }
            consume(parser, TOKEN_IDENTIFIER, "Expected import name.");
            names[count++] = parser->previous;
        } while (match(parser, TOKEN_COMMA));

        consume(parser, TOKEN_RIGHT_BRACE, "Expected '}' after import names.");

        // Expect 'from' keyword (we'll use identifier check)
        if (!check(parser, TOKEN_IDENTIFIER) ||
            parser->current.length != 4 ||
            memcmp(parser->current.start, "from", 4) != 0) {
            error(parser, "Expected 'from' after import names.");
            return NULL;
        }
        advance(parser);  // consume 'from'

        consume(parser, TOKEN_STRING, "Expected module path string.");
        Token path = parser->previous;

        consumeNewlineOrSemicolon(parser);
        return newImportStmt(keyword, path, names, count);
    }

    // Simple import: import "path"
    consume(parser, TOKEN_STRING, "Expected module path string.");
    Token path = parser->previous;

    consumeNewlineOrSemicolon(parser);
    return newImportStmt(keyword, path, NULL, 0);
}

static Stmt* declaration(Parser* parser) {
    skipNewlines(parser);

    if (check(parser, TOKEN_EOF)) {
        return NULL;
    }

    Stmt* stmt = NULL;

    if (match(parser, TOKEN_IMPORT)) {
        stmt = importDeclaration(parser);
    } else if (match(parser, TOKEN_LET)) {
        stmt = varDeclaration(parser, false);
    } else if (match(parser, TOKEN_CONST)) {
        stmt = varDeclaration(parser, true);
    } else if (match(parser, TOKEN_TYPE_ALIAS)) {
        stmt = typeAliasDeclaration(parser);
    } else if (match(parser, TOKEN_FN)) {
        stmt = functionDeclaration(parser);
    } else if (match(parser, TOKEN_CLASS)) {
        stmt = classDeclaration(parser);
    } else if (match(parser, TOKEN_INTERFACE)) {
        stmt = interfaceDeclaration(parser);
    } else if (match(parser, TOKEN_ENUM)) {
        stmt = enumDeclaration(parser);
    } else {
        stmt = statement(parser);
    }

    if (parser->panicMode) {
        synchronize(parser);
    }

    return stmt;
}
