// ============================================================================
// Parser Infrastructure
// ============================================================================

void initParser(Parser* parser, const char* source) {
    initScanner(&parser->scanner, source);
    parser->hadError = false;
    parser->panicMode = false;
    parser->speculativeDepth = 0;
    parser->current.type = TOKEN_EOF;
    parser->previous.type = TOKEN_EOF;
    parser->source = source;  // Store source for error reporting
}

static void errorAtWithSuggestion(Parser* parser, Token* token, const char* message, const char* suggestion) {
    if (parser->speculativeDepth > 0) {
        parser->hadError = true;
        return;
    }
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
            case TOKEN_ENUM:
            case TOKEN_FN:
            case TOKEN_TYPE_ALIAS:
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
static Stmt* block(Parser* parser);
static TypeNode* typeAnnotation(Parser* parser);
static Expr* matchExpression(Parser* parser);
static void parseAndDiscardTypeArguments(Parser* parser);
