// ============================================================================
// Type Parsing
// ============================================================================

static TypeNode* typeAnnotation(Parser* parser) {
    TypeNode* type = NULL;

    // Array type: [T]
    if (match(parser, TOKEN_LEFT_BRACKET)) {
        Token bracket = parser->previous;
        TypeNode* elementType = typeAnnotation(parser);
        consume(parser, TOKEN_RIGHT_BRACKET, "Expected ']' after array element type.");
        type = newArrayTypeNode(elementType, bracket);
    }
    // Function type: fn(T, U) -> R
    else if (match(parser, TOKEN_FN)) {
        Token fnToken = parser->previous;
        consume(parser, TOKEN_LEFT_PAREN, "Expected '(' after 'fn' in type.");

        TypeNode** paramTypes = NULL;
        int paramCount = 0;
        int paramCapacity = 0;

        if (!check(parser, TOKEN_RIGHT_PAREN)) {
            do {
                if (paramCount >= paramCapacity) {
                    int oldCapacity = paramCapacity;
                    paramCapacity = GROW_CAPACITY(oldCapacity);
                    paramTypes = GROW_ARRAY(TypeNode*, paramTypes, oldCapacity, paramCapacity);
                }
                paramTypes[paramCount++] = typeAnnotation(parser);
            } while (match(parser, TOKEN_COMMA));
        }

        consume(parser, TOKEN_RIGHT_PAREN, "Expected ')' after function parameter types.");
        consume(parser, TOKEN_ARROW, "Expected '->' after function parameters in type.");

        TypeNode* returnType = typeAnnotation(parser);

        type = newFunctionTypeNode(paramTypes, paramCount, returnType, fnToken);
    }
    // Simple type: int, float, bool, string, or identifier (potentially generic)
    else if (match(parser, TOKEN_TYPE_INT) || match(parser, TOKEN_TYPE_FLOAT) ||
             match(parser, TOKEN_TYPE_BOOL) || match(parser, TOKEN_TYPE_STRING) ||
             match(parser, TOKEN_NIL) || match(parser, TOKEN_IDENTIFIER)) {
        // Check if this is a generic type: Identifier<T, U, ...>
        if (parser->previous.type == TOKEN_IDENTIFIER && check(parser, TOKEN_LESS)) {
            Token name = parser->previous;
            advance(parser); // consume '<'
            
            // Parse type arguments
            TypeNode** typeArgs = NULL;
            int typeArgCount = 0;
            int typeArgCapacity = 0;
            
            // Parse first type argument
            if (!check(parser, TOKEN_GREATER)) {
                do {
                    if (typeArgCount >= typeArgCapacity) {
                        int oldCapacity = typeArgCapacity;
                        typeArgCapacity = GROW_CAPACITY(oldCapacity);
                        typeArgs = GROW_ARRAY(TypeNode*, typeArgs, oldCapacity, typeArgCapacity);
                    }
                    typeArgs[typeArgCount++] = typeAnnotation(parser);
                } while (match(parser, TOKEN_COMMA));
            }
            
            consume(parser, TOKEN_GREATER, "Expected '>' after type arguments.");
            type = newGenericTypeNode(name, typeArgs, typeArgCount);
        } else {
            type = newSimpleTypeNode(parser->previous);
        }
    }
    else {
        error(parser, "Expected type.");
        return NULL;
    }

    // Check for optional type: T?
    if (match(parser, TOKEN_QUESTION)) {
        Token questionToken = parser->previous;
        type = newOptionalTypeNode(type, questionToken);
    }

    // Check for union type: T | U | V
    if (match(parser, TOKEN_PIPE)) {
        Token pipeToken = parser->previous;

        // Collect all types in the union
        TypeNode** types = NULL;
        int typeCount = 0;
        int typeCapacity = 0;

        // Add the first type
        if (typeCount >= typeCapacity) {
            int oldCapacity = typeCapacity;
            typeCapacity = GROW_CAPACITY(oldCapacity);
            types = GROW_ARRAY(TypeNode*, types, oldCapacity, typeCapacity);
        }
        types[typeCount++] = type;

        // Parse additional types separated by |
        do {
            if (typeCount >= typeCapacity) {
                int oldCapacity = typeCapacity;
                typeCapacity = GROW_CAPACITY(oldCapacity);
                types = GROW_ARRAY(TypeNode*, types, oldCapacity, typeCapacity);
            }

            // Parse base type
            TypeNode* nextType = NULL;
            if (match(parser, TOKEN_LEFT_BRACKET)) {
                Token bracket = parser->previous;
                TypeNode* elementType = typeAnnotation(parser);
                consume(parser, TOKEN_RIGHT_BRACKET, "Expected ']' after array element type.");
                nextType = newArrayTypeNode(elementType, bracket);
            }
            else if (match(parser, TOKEN_TYPE_INT) || match(parser, TOKEN_TYPE_FLOAT) ||
                     match(parser, TOKEN_TYPE_BOOL) || match(parser, TOKEN_TYPE_STRING) ||
                     match(parser, TOKEN_NIL) || match(parser, TOKEN_IDENTIFIER)) {
                // Check if this is a generic type: Identifier<T, U, ...>
                if (parser->previous.type == TOKEN_IDENTIFIER && check(parser, TOKEN_LESS)) {
                    Token name = parser->previous;
                    advance(parser); // consume '<'
                    
                    // Parse type arguments
                    TypeNode** typeArgs = NULL;
                    int typeArgCount = 0;
                    int typeArgCapacity = 0;
                    
                    // Parse first type argument
                    if (!check(parser, TOKEN_GREATER)) {
                        do {
                            if (typeArgCount >= typeArgCapacity) {
                                int oldCapacity = typeArgCapacity;
                                typeArgCapacity = GROW_CAPACITY(oldCapacity);
                                typeArgs = GROW_ARRAY(TypeNode*, typeArgs, oldCapacity, typeArgCapacity);
                            }
                            typeArgs[typeArgCount++] = typeAnnotation(parser);
                        } while (match(parser, TOKEN_COMMA));
                    }
                    
                    consume(parser, TOKEN_GREATER, "Expected '>' after type arguments.");
                    nextType = newGenericTypeNode(name, typeArgs, typeArgCount);
                } else {
                    nextType = newSimpleTypeNode(parser->previous);
                }
            }
            else {
                error(parser, "Expected type after '|'.");
                FREE_ARRAY(TypeNode*, types, typeCapacity);
                return NULL;
            }

            // Optional on individual union member
            if (match(parser, TOKEN_QUESTION)) {
                Token questionToken = parser->previous;
                nextType = newOptionalTypeNode(nextType, questionToken);
            }

            types[typeCount++] = nextType;
        } while (match(parser, TOKEN_PIPE));

        type = newUnionTypeNode(types, typeCount, pipeToken);
    }

    return type;
}
