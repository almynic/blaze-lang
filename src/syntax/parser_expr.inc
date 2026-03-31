// ============================================================================
// Expression Parsing (Pratt-style precedence climbing)
// ============================================================================

typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,    // =
    PREC_OR,            // ||
    PREC_AND,           // &&
    PREC_BITWISE_OR,    // |
    PREC_BITWISE_XOR,   // ^
    PREC_BITWISE_AND,   // &
    PREC_EQUALITY,      // == !=
    PREC_COMPARISON,    // < > <= >=
    PREC_SHIFT,         // << >>
    PREC_RANGE,         // ..
    PREC_TERM,          // + -
    PREC_FACTOR,        // * / %
    PREC_UNARY,         // ! - ~
    PREC_CALL,          // . () []
    PREC_PRIMARY
} Precedence;

typedef Expr* (*ParseFn)(Parser* parser, bool canAssign);

typedef struct {
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

static Expr* parsePrecedence(Parser* parser, Precedence precedence);
static ParseRule* getRule(TokenType type);

// Helper function to parse string interpolation
// Converts "Hello, ${name}!" to "Hello, " + toString(name) + "!"
static Expr* parseInterpolatedString(Parser* parser, Token stringToken) {
    const char* start = stringToken.start + 1;  // Skip opening quote
    int length = stringToken.length - 2;         // Remove both quotes
    const char* end = start + length;

    Expr* result = NULL;
    const char* current = start;

    while (current < end) {
        // Find next interpolation or end of string
        const char* interpStart = current;
        while (interpStart < end && !(interpStart[0] == '$' && interpStart + 1 < end && interpStart[1] == '{')) {
            interpStart++;
        }

        // Create literal for the string part before interpolation
        if (interpStart > current) {
            int partLen = interpStart - current;
            ObjString* partStr = copyString(current, partLen);
            Expr* partExpr = newLiteralExpr(stringToken, OBJ_VAL(partStr));

            if (result == NULL) {
                result = partExpr;
            } else {
                // Concatenate with previous part
                Token plusToken = stringToken;
                plusToken.type = TOKEN_PLUS;
                result = newBinaryExpr(result, plusToken, partExpr);
            }
        }

        // Check if we found an interpolation
        if (interpStart < end && interpStart[0] == '$' && interpStart + 1 < end && interpStart[1] == '{') {
            // Find the matching closing brace
            const char* exprStart = interpStart + 2;  // Skip ${
            const char* exprEnd = exprStart;
            int braceDepth = 1;

            while (exprEnd < end && braceDepth > 0) {
                if (*exprEnd == '{') braceDepth++;
                else if (*exprEnd == '}') braceDepth--;
                if (braceDepth > 0) exprEnd++;
            }

            if (braceDepth != 0) {
                error(parser, "Unterminated interpolation in string.");
                return result ? result : newLiteralExpr(stringToken, OBJ_VAL(copyString("", 0)));
            }

            // Parse the expression inside ${}
            int exprLen = exprEnd - exprStart;
            if (exprLen > 0) {
                // Create a temporary scanner for the interpolated expression
                Scanner tempScanner;
                initScanner(&tempScanner, exprStart);
                tempScanner.start = exprStart;
                tempScanner.current = exprStart;
                tempScanner.line = stringToken.line;

                // Save parser state
                Scanner savedScanner = parser->scanner;
                Token savedCurrent = parser->current;
                Token savedPrevious = parser->previous;

                // Set up parser to scan the interpolated expression
                parser->scanner = tempScanner;
                advance(parser);  // Prime the parser

                // Parse the expression
                Expr* interpExpr = expression(parser);

                // Wrap the expression in toString() call
                Token toStringName = stringToken;
                toStringName.type = TOKEN_IDENTIFIER;
                toStringName.start = "toString";
                toStringName.length = 8;
                Expr* toStringVar = newVariableExpr(toStringName);

                Expr** args = ALLOCATE(Expr*, 1);
                args[0] = interpExpr;
                Expr* toStringCall = newCallExpr(toStringVar, stringToken, args, 1, NULL, 0);

                // Restore parser state
                parser->scanner = savedScanner;
                parser->current = savedCurrent;
                parser->previous = savedPrevious;

                // Concatenate with result
                if (result == NULL) {
                    result = toStringCall;
                } else {
                    Token plusToken = stringToken;
                    plusToken.type = TOKEN_PLUS;
                    result = newBinaryExpr(result, plusToken, toStringCall);
                }
            }

            current = exprEnd + 1;  // Move past the closing }
        } else {
            break;  // No more interpolations
        }
    }

    // If no interpolations were found, return a simple string literal
    if (result == NULL) {
        ObjString* string = copyString(start, length);
        result = newLiteralExpr(stringToken, OBJ_VAL(string));
    }

    return result;
}

static Expr* literal(Parser* parser, bool canAssign) {
    (void)canAssign;
    Token token = parser->previous;
    Value value;

    switch (token.type) {
        case TOKEN_NIL:
            value = NIL_VAL;
            break;
        case TOKEN_TRUE:
            value = BOOL_VAL(true);
            break;
        case TOKEN_FALSE:
            value = BOOL_VAL(false);
            break;
        case TOKEN_INT: {
            int64_t num = strtoll(token.start, NULL, 10);
            value = INT_VAL(num);
            break;
        }
        case TOKEN_FLOAT: {
            double num = strtod(token.start, NULL);
            value = FLOAT_VAL(num);
            break;
        }
        case TOKEN_STRING: {
            // Check if string contains interpolations
            const char* str = token.start + 1;  // Skip opening quote
            int len = token.length - 2;          // Remove both quotes
            bool hasInterpolation = false;

            for (int i = 0; i < len - 1; i++) {
                if (str[i] == '$' && str[i + 1] == '{') {
                    hasInterpolation = true;
                    break;
                }
            }

            if (hasInterpolation) {
                return parseInterpolatedString(parser, token);
            }

            // Regular string literal
            ObjString* string = copyString(token.start + 1, token.length - 2);
            value = OBJ_VAL(string);
            break;
        }
        default:
            error(parser, "Unexpected literal.");
            value = NIL_VAL;
    }

    return newLiteralExpr(token, value);
}

static Expr* grouping(Parser* parser, bool canAssign) {
    (void)canAssign;
    // Speculatively parse lambda:
    // (a, b) => expr
    // (a: T, b: U) => expr
    // (a: T) -> R => expr
    Scanner savedScanner = parser->scanner;
    Token savedCurrent = parser->current;
    Token savedPrevious = parser->previous;

    bool lambdaParsed = false;
    Token* params = NULL;
    TypeNode** paramTypes = NULL;
    int paramCount = 0;
    int paramCapacity = 0;
    TypeNode* returnType = NULL;
    Token arrowToken;

    if (check(parser, TOKEN_RIGHT_PAREN)) {
        // Empty parameter list.
        advance(parser);
        if (match(parser, TOKEN_ARROW)) {
            returnType = typeAnnotation(parser);
        }
        if (match(parser, TOKEN_FAT_ARROW)) {
            arrowToken = parser->previous;
            lambdaParsed = true;
        }
    } else if (check(parser, TOKEN_IDENTIFIER)) {
        while (true) {
            if (!check(parser, TOKEN_IDENTIFIER)) {
                lambdaParsed = false;
                break;
            }

            if (paramCount >= paramCapacity) {
                int oldCapacity = paramCapacity;
                paramCapacity = GROW_CAPACITY(oldCapacity);
                params = GROW_ARRAY(Token, params, oldCapacity, paramCapacity);
                paramTypes = GROW_ARRAY(TypeNode*, paramTypes, oldCapacity, paramCapacity);
            }

            advance(parser);
            params[paramCount] = parser->previous;
            paramTypes[paramCount] = NULL;

            if (match(parser, TOKEN_COLON)) {
                paramTypes[paramCount] = typeAnnotation(parser);
            }
            paramCount++;

            if (!match(parser, TOKEN_COMMA)) break;
        }

        if (check(parser, TOKEN_RIGHT_PAREN)) {
            advance(parser);
            if (match(parser, TOKEN_ARROW)) {
                returnType = typeAnnotation(parser);
            }
            if (match(parser, TOKEN_FAT_ARROW)) {
                arrowToken = parser->previous;
                lambdaParsed = true;
            }
        }
    }

    if (lambdaParsed) {
        if (match(parser, TOKEN_LEFT_BRACE)) {
            Stmt* blockStmt = block(parser);
            return newLambdaBlockExpr(arrowToken, params, paramTypes, paramCount, returnType, blockStmt);
        }
        Expr* body = expression(parser);
        return newLambdaExpr(arrowToken, params, paramTypes, paramCount, returnType, body);
    }

    // Roll back and parse as a normal grouping expression.
    for (int i = 0; i < paramCount; i++) {
        freeTypeNode(paramTypes[i]);
    }
    FREE_ARRAY(TypeNode*, paramTypes, paramCapacity);
    FREE_ARRAY(Token, params, paramCapacity);
    freeTypeNode(returnType);

    parser->scanner = savedScanner;
    parser->current = savedCurrent;
    parser->previous = savedPrevious;

    Expr* expr = expression(parser);
    consume(parser, TOKEN_RIGHT_PAREN, "Expected ')' after expression.");
    return newGroupingExpr(expr);
}

static Expr* variable(Parser* parser, bool canAssign) {
    Token name = parser->previous;

    if (canAssign && match(parser, TOKEN_EQUAL)) {
        Expr* value = expression(parser);
        return newAssignExpr(name, value);
    }

    return newVariableExpr(name);
}

static void parseAndDiscardTypeArguments(Parser* parser) {
    // Parses generic arg list content and consumes trailing '>'.
    // Caller must have consumed the opening '<' already.
    if (!check(parser, TOKEN_GREATER)) {
        do {
            TypeNode* arg = typeAnnotation(parser);
            if (arg != NULL) {
                freeTypeNode(arg);
            }
        } while (match(parser, TOKEN_COMMA));
    }
    consume(parser, TOKEN_GREATER, "Expected '>' after type arguments.");
}

static TypeNode** parseTypeArgumentListForCall(Parser* parser, int* outCount) {
    TypeNode** typeArgs = NULL;
    int count = 0;
    int capacity = 0;
    if (!check(parser, TOKEN_GREATER)) {
        do {
            if (count >= capacity) {
                int oldCapacity = capacity;
                capacity = GROW_CAPACITY(oldCapacity);
                typeArgs = GROW_ARRAY(TypeNode*, typeArgs, oldCapacity, capacity);
            }
            typeArgs[count++] = typeAnnotation(parser);
        } while (match(parser, TOKEN_COMMA));
    }
    consume(parser, TOKEN_GREATER, "Expected '>' after type arguments.");
    *outCount = count;
    return typeArgs;
}

static Expr* thisExpr(Parser* parser, bool canAssign) {
    (void)canAssign;
    return newThisExpr(parser->previous);
}

static Expr* superExpr(Parser* parser, bool canAssign) {
    (void)canAssign;
    Token keyword = parser->previous;
    consume(parser, TOKEN_DOT, "Expected '.' after 'super'.");
    consume(parser, TOKEN_IDENTIFIER, "Expected superclass method name.");
    Token method = parser->previous;
    return newSuperExpr(keyword, method);
}

static Expr* matchExpr(Parser* parser, bool canAssign) {
    (void)canAssign;
    return matchExpression(parser);
}

static Expr* unary(Parser* parser, bool canAssign) {
    (void)canAssign;
    Token op = parser->previous;
    Expr* operand = parsePrecedence(parser, PREC_UNARY);
    return newUnaryExpr(op, operand);
}

static Expr* binary(Parser* parser, bool canAssign) {
    (void)canAssign;
    Token op = parser->previous;
    ParseRule* rule = getRule(op.type);
    Expr* right = parsePrecedence(parser, (Precedence)(rule->precedence + 1));

    // The left operand was already parsed and is on the "stack"
    // We need to get it from somewhere - this is handled by parsePrecedence
    return right; // This will be combined in parsePrecedence
}

static Expr* arrayLiteral(Parser* parser, bool canAssign) {
    (void)canAssign;
    Token bracket = parser->previous;

    Expr** elements = NULL;
    int elementCount = 0;
    int elementCapacity = 0;

    if (!check(parser, TOKEN_RIGHT_BRACKET)) {
        do {
            skipNewlines(parser);
            if (elementCount >= elementCapacity) {
                int oldCapacity = elementCapacity;
                elementCapacity = GROW_CAPACITY(oldCapacity);
                elements = GROW_ARRAY(Expr*, elements, oldCapacity, elementCapacity);
            }

            // Check for spread operator
            if (match(parser, TOKEN_DOT_DOT_DOT)) {
                Token dots = parser->previous;
                Expr* operand = expression(parser);
                elements[elementCount++] = newSpreadExpr(dots, operand);
            } else {
                elements[elementCount++] = expression(parser);
            }

            skipNewlines(parser);
        } while (match(parser, TOKEN_COMMA));
    }

    consume(parser, TOKEN_RIGHT_BRACKET, "Expected ']' after array elements.");
    return newArrayExpr(bracket, elements, elementCount);
}

static Expr* callExpr(Parser* parser, bool canAssign) {
    (void)parser;
    (void)canAssign;
    // This is called after we've parsed the callee and seen (
    // But we're handling this differently in parsePrecedence
    return NULL;
}

static Expr* finishCall(Parser* parser, Expr* callee, TypeNode** explicitTypeArgs,
                        int explicitTypeArgCount) {
    Token paren = parser->previous;

    Expr** arguments = NULL;
    int argCount = 0;
    int argCapacity = 0;

    if (!check(parser, TOKEN_RIGHT_PAREN)) {
        do {
            skipNewlines(parser);
            if (argCount >= argCapacity) {
                int oldCapacity = argCapacity;
                argCapacity = GROW_CAPACITY(oldCapacity);
                arguments = GROW_ARRAY(Expr*, arguments, oldCapacity, argCapacity);
            }
            if (argCount >= 255) {
                error(parser, "Cannot have more than 255 arguments.");
            }
            arguments[argCount++] = expression(parser);
            skipNewlines(parser);
        } while (match(parser, TOKEN_COMMA));
    }

    consume(parser, TOKEN_RIGHT_PAREN, "Expected ')' after arguments.");
    return newCallExpr(callee, paren, arguments, argCount, explicitTypeArgs, explicitTypeArgCount);
}

static Expr* dotExpr(Parser* parser, bool canAssign) {
    (void)parser;
    (void)canAssign;
    // This handles property access, combined in parsePrecedence
    return NULL;
}

static Expr* indexExpr(Parser* parser, bool canAssign) {
    (void)parser;
    (void)canAssign;
    // This handles array indexing, combined in parsePrecedence
    return NULL;
}

static Expr* andExpr(Parser* parser, bool canAssign) {
    (void)canAssign;
    return NULL;
}

static Expr* orExpr(Parser* parser, bool canAssign) {
    (void)canAssign;
    return NULL;
}

ParseRule rules[] = {
    [TOKEN_LEFT_PAREN]    = {grouping, callExpr, PREC_CALL},
    [TOKEN_RIGHT_PAREN]   = {NULL,     NULL,   PREC_NONE},
    [TOKEN_LEFT_BRACE]    = {NULL,     NULL,   PREC_NONE},
    [TOKEN_RIGHT_BRACE]   = {NULL,     NULL,   PREC_NONE},
    [TOKEN_LEFT_BRACKET]  = {arrayLiteral, indexExpr, PREC_CALL},
    [TOKEN_RIGHT_BRACKET] = {NULL,     NULL,   PREC_NONE},
    [TOKEN_COMMA]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_DOT]           = {NULL,     dotExpr, PREC_CALL},
    [TOKEN_SEMICOLON]     = {NULL,     NULL,   PREC_NONE},
    [TOKEN_COLON]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_PLUS]          = {NULL,     binary, PREC_TERM},
    [TOKEN_MINUS]         = {unary,    binary, PREC_TERM},
    [TOKEN_STAR]          = {NULL,     binary, PREC_FACTOR},
    [TOKEN_SLASH]         = {NULL,     binary, PREC_FACTOR},
    [TOKEN_PERCENT]       = {NULL,     binary, PREC_FACTOR},
    [TOKEN_BANG]          = {unary,    NULL,   PREC_NONE},
    [TOKEN_TILDE]         = {unary,    NULL,   PREC_NONE},
    [TOKEN_BANG_EQUAL]    = {NULL,     binary, PREC_EQUALITY},
    [TOKEN_EQUAL]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EQUAL_EQUAL]   = {NULL,     binary, PREC_EQUALITY},
    [TOKEN_GREATER]       = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL] = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_LESS]          = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_LESS_EQUAL]    = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_LSHIFT]        = {NULL,     binary, PREC_SHIFT},
    [TOKEN_RSHIFT]        = {NULL,     binary, PREC_SHIFT},
    [TOKEN_ARROW]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FAT_ARROW]     = {NULL,     NULL,   PREC_NONE},
    [TOKEN_DOT_DOT]       = {NULL,     binary, PREC_RANGE},
    [TOKEN_PIPE]          = {NULL,     binary, PREC_BITWISE_OR},
    [TOKEN_CARET]         = {NULL,     binary, PREC_BITWISE_XOR},
    [TOKEN_AMPERSAND]     = {NULL,     binary, PREC_BITWISE_AND},
    [TOKEN_AND]           = {NULL,     andExpr, PREC_AND},
    [TOKEN_OR]            = {NULL,     orExpr, PREC_OR},
    [TOKEN_QUESTION]      = {NULL,     NULL,   PREC_NONE},
    [TOKEN_QUESTION_DOT]  = {NULL,     dotExpr, PREC_CALL},
    [TOKEN_QUESTION_QUESTION] = {NULL, binary, PREC_OR},
    [TOKEN_IDENTIFIER]    = {variable, NULL,   PREC_NONE},
    [TOKEN_STRING]        = {literal,  NULL,   PREC_NONE},
    [TOKEN_INT]           = {literal,  NULL,   PREC_NONE},
    [TOKEN_FLOAT]         = {literal,  NULL,   PREC_NONE},
    [TOKEN_LET]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_CONST]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FN]            = {NULL,     NULL,   PREC_NONE},
    [TOKEN_RETURN]        = {NULL,     NULL,   PREC_NONE},
    [TOKEN_IF]            = {NULL,     NULL,   PREC_NONE},
    [TOKEN_ELSE]          = {NULL,     NULL,   PREC_NONE},
    [TOKEN_WHILE]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FOR]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_IN]            = {NULL,     NULL,   PREC_NONE},
    [TOKEN_CLASS]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EXTENDS]       = {NULL,     NULL,   PREC_NONE},
    [TOKEN_THIS]          = {thisExpr, NULL,   PREC_NONE},
    [TOKEN_SUPER]         = {superExpr, NULL,  PREC_NONE},
    [TOKEN_IMPORT]        = {NULL,     NULL,   PREC_NONE},
    [TOKEN_MODULE]        = {NULL,     NULL,   PREC_NONE},
    [TOKEN_MATCH]         = {matchExpr, NULL,  PREC_NONE},
    [TOKEN_TRUE]          = {literal,  NULL,   PREC_NONE},
    [TOKEN_FALSE]         = {literal,  NULL,   PREC_NONE},
    [TOKEN_NIL]           = {literal,  NULL,   PREC_NONE},
    [TOKEN_PRINT]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_TYPE_ALIAS]    = {variable, NULL,   PREC_NONE},
    [TOKEN_TYPE_INT]      = {NULL,     NULL,   PREC_NONE},
    [TOKEN_TYPE_FLOAT]    = {NULL,     NULL,   PREC_NONE},
    [TOKEN_TYPE_BOOL]     = {NULL,     NULL,   PREC_NONE},
    [TOKEN_TYPE_STRING]   = {NULL,     NULL,   PREC_NONE},
    [TOKEN_NEWLINE]       = {NULL,     NULL,   PREC_NONE},
    [TOKEN_ERROR]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EOF]           = {NULL,     NULL,   PREC_NONE},
};

static ParseRule* getRule(TokenType type) {
    return &rules[type];
}

typedef struct {
    Scanner scanner;
    Token current;
    Token previous;
} ParserCheckpoint;

static void parserCheckpoint(Parser* p, ParserCheckpoint* ck) {
    ck->scanner = p->scanner;
    ck->current = p->current;
    ck->previous = p->previous;
}

static void parserRestore(Parser* p, ParserCheckpoint* ck) {
    p->scanner = ck->scanner;
    p->current = ck->current;
    p->previous = ck->previous;
}

static Expr* parsePrecedence(Parser* parser, Precedence precedence) {
    advance(parser);
    ParseFn prefixRule = getRule(parser->previous.type)->prefix;
    if (prefixRule == NULL) {
        error(parser, "Expected expression.");
        return NULL;
    }

    bool canAssign = precedence <= PREC_ASSIGNMENT;
    Expr* left = prefixRule(parser, canAssign);

    while (precedence <= getRule(parser->current.type)->precedence) {
        advance(parser);
        Token op = parser->previous;

        // Handle special infix cases
        if (op.type == TOKEN_LEFT_PAREN) {
            left = finishCall(parser, left, NULL, 0);
        } else if (op.type == TOKEN_DOT || op.type == TOKEN_QUESTION_DOT) {
            bool isOptional = (op.type == TOKEN_QUESTION_DOT);
            consume(parser, TOKEN_IDENTIFIER, isOptional
                ? "Expected property name after '?.'."
                : "Expected property name after '.'.");
            Token name = parser->previous;

            if (canAssign && match(parser, TOKEN_EQUAL)) {
                if (isOptional) {
                    error(parser, "Cannot assign through optional chaining.");
                }
                Expr* value = expression(parser);
                left = newSetExpr(left, name, value);
            } else {
                left = newGetExpr(left, name, isOptional);
            }
        } else if (op.type == TOKEN_LEFT_BRACKET) {
            Token bracket = op;
            Expr* indexExpr = expression(parser);
            consume(parser, TOKEN_RIGHT_BRACKET, "Expected ']' after index.");

            if (canAssign && match(parser, TOKEN_EQUAL)) {
                Expr* value = expression(parser);
                left = newIndexSetExpr(left, bracket, indexExpr, value);
            } else {
                left = newIndexExpr(left, bracket, indexExpr);
            }
        } else if (op.type == TOKEN_AND) {
            Expr* right = parsePrecedence(parser, PREC_AND + 1);
            left = newLogicalExpr(left, op, right);
        } else if (op.type == TOKEN_OR) {
            Expr* right = parsePrecedence(parser, PREC_OR + 1);
            left = newLogicalExpr(left, op, right);
        } else if (op.type == TOKEN_QUESTION_QUESTION) {
            Expr* right = parsePrecedence(parser, PREC_OR + 1);
            left = newNullCoalesceExpr(left, op, right);
        } else if (op.type == TOKEN_LESS && left->kind == EXPR_VARIABLE) {
            // Maybe foo<T>(...) — try generic call; otherwise binary '<'
            ParserCheckpoint ck;
            parserCheckpoint(parser, &ck);
            bool hadErrBefore = parser->hadError;
            bool panicBefore = parser->panicMode;

            parser->speculativeDepth++;
            int typeArgCount = 0;
            TypeNode** typeArgs = parseTypeArgumentListForCall(parser, &typeArgCount);
            parser->speculativeDepth--;

            if (parser->hadError || !check(parser, TOKEN_LEFT_PAREN)) {
                if (typeArgs != NULL) {
                    for (int i = 0; i < typeArgCount; i++) {
                        freeTypeNode(typeArgs[i]);
                    }
                    FREE_ARRAY(TypeNode*, typeArgs, typeArgCount);
                }
                parserRestore(parser, &ck);
                parser->hadError = hadErrBefore;
                parser->panicMode = panicBefore;
                ParseRule* rule = getRule(op.type);
                Expr* right = parsePrecedence(parser, (Precedence)(rule->precedence + 1));
                left = newBinaryExpr(left, op, right);
            } else {
                advance(parser); // consume '('
                left = finishCall(parser, left, typeArgs, typeArgCount);
            }
        } else {
            // Regular binary operator
            ParseRule* rule = getRule(op.type);
            Expr* right = parsePrecedence(parser, (Precedence)(rule->precedence + 1));
            left = newBinaryExpr(left, op, right);
        }
    }

    if (canAssign && match(parser, TOKEN_EQUAL)) {
        error(parser, "Invalid assignment target.");
    }

    return left;
}

static Expr* expression(Parser* parser) {
    return parsePrecedence(parser, PREC_ASSIGNMENT);
}
