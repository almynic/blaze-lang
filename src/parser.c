#include "parser.h"
#include "memory.h"
#include "object.h"
#include "colors.h"

// ============================================================================
// Parser Infrastructure
// ============================================================================

void initParser(Parser* parser, const char* source) {
    initScanner(&parser->scanner, source);
    parser->hadError = false;
    parser->panicMode = false;
    parser->current.type = TOKEN_EOF;
    parser->previous.type = TOKEN_EOF;
    parser->source = source;  // Store source for error reporting
}

static void errorAtWithSuggestion(Parser* parser, Token* token, const char* message, const char* suggestion) {
    if (parser->panicMode) return;
    parser->panicMode = true;

    bool useColors = colorsEnabled();

    // Error header with line number
    if (useColors) {
        fprintf(stderr, "\n%s[line %d]%s %sError%s",
                COLOR_BOLD, token->line, COLOR_RESET,
                COLOR_BRIGHT_RED, COLOR_RESET);
    } else {
        fprintf(stderr, "\n[line %d] Error", token->line);
    }

    // Error location
    if (token->type == TOKEN_EOF) {
        if (useColors) {
            fprintf(stderr, " at %send%s", COLOR_YELLOW, COLOR_RESET);
        } else {
            fprintf(stderr, " at end");
        }
    } else if (token->type == TOKEN_ERROR) {
        // Nothing
    } else {
        if (useColors) {
            fprintf(stderr, " at %s'%.*s'%s",
                    COLOR_YELLOW, token->length, token->start, COLOR_RESET);
        } else {
            fprintf(stderr, " at '%.*s'", token->length, token->start);
        }
    }

    // Error message
    if (useColors) {
        fprintf(stderr, ": %s%s%s\n", COLOR_BOLD, message, COLOR_RESET);
    } else {
        fprintf(stderr, ": %s\n", message);
    }

    // Show code context with caret pointing to error
    if (parser->source != NULL && token->type != TOKEN_EOF) {
        // Find the start of the line
        const char* lineStart = token->start;
        while (lineStart > parser->source && lineStart[-1] != '\n') {
            lineStart--;
        }

        // Find the end of the line
        const char* lineEnd = token->start;
        while (*lineEnd != '\0' && *lineEnd != '\n') {
            lineEnd++;
        }

        // Print the line
        if (useColors) {
            fprintf(stderr, "\n    %s", COLOR_GRAY);
        } else {
            fprintf(stderr, "\n    ");
        }
        int lineLength = (int)(lineEnd - lineStart);
        fprintf(stderr, "%.*s", lineLength, lineStart);
        if (useColors) {
            fprintf(stderr, "%s\n", COLOR_RESET);
        } else {
            fprintf(stderr, "\n");
        }

        // Print the caret
        fprintf(stderr, "    ");
        int caretPos = (int)(token->start - lineStart);
        for (int i = 0; i < caretPos; i++) {
            fprintf(stderr, " ");
        }
        if (useColors) {
            fprintf(stderr, "%s^%s\n", COLOR_BRIGHT_RED, COLOR_RESET);
        } else {
            fprintf(stderr, "^\n");
        }
    }

    // Print suggestion if provided
    if (suggestion != NULL) {
        if (useColors) {
            fprintf(stderr, "\n    %s💡 Hint:%s %s\n",
                    COLOR_BRIGHT_CYAN, COLOR_RESET, suggestion);
        } else {
            fprintf(stderr, "\n    💡 Hint: %s\n", suggestion);
        }
    }

    parser->hadError = true;
}

static void errorAt(Parser* parser, Token* token, const char* message) {
    errorAtWithSuggestion(parser, token, message, NULL);
}

static void error(Parser* parser, const char* message) {
    errorAt(parser, &parser->previous, message);
}

static void errorAtCurrent(Parser* parser, const char* message) {
    errorAt(parser, &parser->current, message);
}

static void advance(Parser* parser) {
    parser->previous = parser->current;

    for (;;) {
        parser->current = scanToken(&parser->scanner);
        if (parser->current.type != TOKEN_ERROR) break;

        errorAtCurrent(parser, parser->current.start);
    }
}

static void consume(Parser* parser, TokenType type, const char* message) {
    if (parser->current.type == type) {
        advance(parser);
        return;
    }

    errorAtCurrent(parser, message);
}

static void consumeWithSuggestion(Parser* parser, TokenType type, const char* message, const char* suggestion) {
    if (parser->current.type == type) {
        advance(parser);
        return;
    }

    errorAtWithSuggestion(parser, &parser->current, message, suggestion);
}

static bool check(Parser* parser, TokenType type) {
    return parser->current.type == type;
}

static bool match(Parser* parser, TokenType type) {
    if (!check(parser, type)) return false;
    advance(parser);
    return true;
}

static void skipNewlines(Parser* parser) {
    while (match(parser, TOKEN_NEWLINE)) {
        // Skip
    }
}

static void consumeNewlineOrSemicolon(Parser* parser) {
    if (check(parser, TOKEN_NEWLINE) || check(parser, TOKEN_SEMICOLON)) {
        advance(parser);
        skipNewlines(parser);
        return;
    }
    if (check(parser, TOKEN_EOF) || check(parser, TOKEN_RIGHT_BRACE)) {
        return;
    }
    errorAtCurrent(parser, "Expected newline or ';' after statement.");
}

static void synchronize(Parser* parser) {
    parser->panicMode = false;

    while (parser->current.type != TOKEN_EOF) {
        if (parser->previous.type == TOKEN_NEWLINE) return;
        if (parser->previous.type == TOKEN_SEMICOLON) return;

        switch (parser->current.type) {
            case TOKEN_CLASS:
            case TOKEN_FN:
            case TOKEN_LET:
            case TOKEN_CONST:
            case TOKEN_FOR:
            case TOKEN_IF:
            case TOKEN_WHILE:
            case TOKEN_PRINT:
            case TOKEN_RETURN:
                return;
            default:
                ; // Do nothing
        }

        advance(parser);
    }
}

// Forward declarations
static Expr* expression(Parser* parser);
static Stmt* declaration(Parser* parser);
static Stmt* statement(Parser* parser);
static TypeNode* typeAnnotation(Parser* parser);
static Expr* matchExpression(Parser* parser);

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
    // Simple type: int, float, bool, string, or identifier
    else if (match(parser, TOKEN_TYPE_INT) || match(parser, TOKEN_TYPE_FLOAT) ||
             match(parser, TOKEN_TYPE_BOOL) || match(parser, TOKEN_TYPE_STRING) ||
             match(parser, TOKEN_IDENTIFIER)) {
        type = newSimpleTypeNode(parser->previous);
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
                     match(parser, TOKEN_IDENTIFIER)) {
                nextType = newSimpleTypeNode(parser->previous);
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

// ============================================================================
// Expression Parsing (Pratt-style precedence climbing)
// ============================================================================

typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,    // =
    PREC_OR,            // ||
    PREC_AND,           // &&
    PREC_EQUALITY,      // == !=
    PREC_COMPARISON,    // < > <= >=
    PREC_RANGE,         // ..
    PREC_TERM,          // + -
    PREC_FACTOR,        // * / %
    PREC_UNARY,         // ! -
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
                Expr* toStringCall = newCallExpr(toStringVar, stringToken, args, 1);

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

    // Try to parse as lambda parameters
    // Look for pattern: (id, id, ...) =>

    // First, check if next token could start params
    if (check(parser, TOKEN_RIGHT_PAREN)) {
        // () - could be empty lambda or empty grouping (invalid)
        advance(parser); // consume )
        if (match(parser, TOKEN_FAT_ARROW)) {
            // It's a lambda with no params
            Token arrow = parser->previous;
            Expr* body = expression(parser);
            return newLambdaExpr(arrow, NULL, NULL, 0, NULL, body);
        }
        error(parser, "Expected expression inside parentheses.");
        return NULL;
    }

    if (check(parser, TOKEN_IDENTIFIER)) {
        // Could be lambda params or start of expression
        // Speculatively parse as lambda params: id [, id]*

        int capacity = 8;
        int count = 0;
        Token* params = ALLOCATE(Token, capacity);

        // First parameter
        advance(parser);  // Consume identifier
        params[count++] = parser->previous;

        // Check for more parameters
        while (match(parser, TOKEN_COMMA)) {
            if (!check(parser, TOKEN_IDENTIFIER)) {
                // Not a simple parameter list, this isn't a lambda
                // We need to backtrack - but we can't easily
                // Instead, error out for now
                error(parser, "Expected parameter name after ','.");
                FREE_ARRAY(Token, params, capacity);
                return NULL;
            }
            advance(parser);  // Consume identifier
            if (count >= capacity) {
                int oldCapacity = capacity;
                capacity = GROW_CAPACITY(oldCapacity);
                params = GROW_ARRAY(Token, params, oldCapacity, capacity);
            }
            params[count++] = parser->previous;
        }

        consume(parser, TOKEN_RIGHT_PAREN, "Expected ')' after parameters.");

        if (match(parser, TOKEN_FAT_ARROW)) {
            // It's a lambda
            Token arrow = parser->previous;

            TypeNode** paramTypes = ALLOCATE(TypeNode*, count);
            for (int i = 0; i < count; i++) {
                paramTypes[i] = NULL;  // No type annotation
            }

            Expr* body = expression(parser);
            return newLambdaExpr(arrow, params, paramTypes, count, NULL, body);
        }

        // Not a lambda - it was a grouping expression
        // We consumed identifiers as tokens, need to reconstruct
        if (count == 1) {
            // Single identifier - create a variable expression
            Expr* expr = newVariableExpr(params[0]);
            FREE_ARRAY(Token, params, capacity);
            return newGroupingExpr(expr);
        }

        // Multiple identifiers without => is an error (comma expression not supported here)
        error(parser, "Expected '=>' for lambda or single expression in parentheses.");
        FREE_ARRAY(Token, params, capacity);
        return NULL;
    }

    // Regular expression in parentheses
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
            elements[elementCount++] = expression(parser);
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

static Expr* finishCall(Parser* parser, Expr* callee) {
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
    return newCallExpr(callee, paren, arguments, argCount);
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
    [TOKEN_BANG_EQUAL]    = {NULL,     binary, PREC_EQUALITY},
    [TOKEN_EQUAL]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EQUAL_EQUAL]   = {NULL,     binary, PREC_EQUALITY},
    [TOKEN_GREATER]       = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL] = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_LESS]          = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_LESS_EQUAL]    = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_ARROW]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FAT_ARROW]     = {NULL,     NULL,   PREC_NONE},
    [TOKEN_DOT_DOT]       = {NULL,     binary, PREC_RANGE},
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
            left = finishCall(parser, left);
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
    consume(parser, TOKEN_IDENTIFIER, "Expected function name.");
    Token name = parser->previous;

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

    return newFunctionStmt(name, params, paramTypes, paramCount, returnType, body);
}

static Stmt* classDeclaration(Parser* parser) {
    consume(parser, TOKEN_IDENTIFIER, "Expected class name.");
    Token name = parser->previous;

    Token superclass;
    bool hasSuperclass = false;
    if (match(parser, TOKEN_EXTENDS)) {
        consume(parser, TOKEN_IDENTIFIER, "Expected superclass name.");
        superclass = parser->previous;
        hasSuperclass = true;
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

    return newClassStmt(name, superclass, hasSuperclass, fields, fieldCount, methods, methodCount);
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

        // Check for wildcard pattern '_'
        if (check(parser, TOKEN_IDENTIFIER)) {
            Token t = parser->current;
            if (t.length == 1 && t.start[0] == '_') {
                advance(parser);  // consume _
                cases[count].pattern = NULL;
                cases[count].isWildcard = true;
            } else {
                // Not a wildcard - parse as literal expression
                cases[count].pattern = parsePrecedence(parser, PREC_PRIMARY);
                cases[count].isWildcard = false;
            }
        } else {
            // Literal pattern (numbers, strings, booleans)
            cases[count].pattern = parsePrecedence(parser, PREC_PRIMARY);
            cases[count].isWildcard = false;
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

        // Check for wildcard pattern '_'
        if (check(parser, TOKEN_IDENTIFIER)) {
            Token t = parser->current;
            if (t.length == 1 && t.start[0] == '_') {
                advance(parser);  // consume _
                cases[count].pattern = NULL;
                cases[count].isWildcard = true;
            } else {
                // Not a wildcard - parse as literal expression
                cases[count].pattern = parsePrecedence(parser, PREC_PRIMARY);
                cases[count].isWildcard = false;
            }
        } else {
            // Literal pattern (numbers, strings, booleans)
            cases[count].pattern = parsePrecedence(parser, PREC_PRIMARY);
            cases[count].isWildcard = false;
        }

        // Expect '=>' followed by an expression
        consume(parser, TOKEN_FAT_ARROW, "Expected '=>' after match pattern in match expression.");

        // Parse the value expression for this case
        cases[count].value = expression(parser);

        count++;

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
    } else if (match(parser, TOKEN_FN)) {
        stmt = functionDeclaration(parser);
    } else if (match(parser, TOKEN_CLASS)) {
        stmt = classDeclaration(parser);
    } else {
        stmt = statement(parser);
    }

    if (parser->panicMode) {
        synchronize(parser);
    }

    return stmt;
}

// ============================================================================
// Public API
// ============================================================================

Stmt** parse(Parser* parser, int* stmtCount) {
    advance(parser); // Prime the parser

    Stmt** statements = NULL;
    int count = 0;
    int capacity = 0;

    while (!check(parser, TOKEN_EOF)) {
        Stmt* stmt = declaration(parser);
        if (stmt != NULL) {
            if (count >= capacity) {
                int oldCapacity = capacity;
                capacity = GROW_CAPACITY(oldCapacity);
                statements = GROW_ARRAY(Stmt*, statements, oldCapacity, capacity);
            }
            statements[count++] = stmt;
        }
    }

    *stmtCount = count;

    if (parser->hadError) {
        freeStatements(statements, count);
        return NULL;
    }

    return statements;
}

void freeStatements(Stmt** statements, int count) {
    for (int i = 0; i < count; i++) {
        freeStmt(statements[i]);
    }
    FREE_ARRAY(Stmt*, statements, count);
}
