/* Scanner implementation: identifiers, numbers, strings, comments, and
 * keyword recognition. */

#include "scanner.h"

void initScanner(Scanner* scanner, const char* source) {
    scanner->start = source;
    scanner->current = source;
    scanner->line = 1;
}

static bool isAtEnd(Scanner* scanner) {
    return *scanner->current == '\0';
}

static char advance(Scanner* scanner) {
    scanner->current++;
    return scanner->current[-1];
}

static char peek(Scanner* scanner) {
    return *scanner->current;
}

static char peekNext(Scanner* scanner) {
    if (isAtEnd(scanner)) return '\0';
    return scanner->current[1];
}

static bool match(Scanner* scanner, char expected) {
    if (isAtEnd(scanner)) return false;
    if (*scanner->current != expected) return false;
    scanner->current++;
    return true;
}

static Token makeToken(Scanner* scanner, TokenType type) {
    Token token;
    token.type = type;
    token.start = scanner->start;
    token.length = (int)(scanner->current - scanner->start);
    token.line = scanner->line;
    return token;
}

static Token errorToken(Scanner* scanner, const char* message) {
    Token token;
    token.type = TOKEN_ERROR;
    token.start = message;
    token.length = (int)strlen(message);
    token.line = scanner->line;
    return token;
}

static void skipWhitespace(Scanner* scanner) {
    for (;;) {
        char c = peek(scanner);
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                advance(scanner);
                break;
            case '/':
                if (peekNext(scanner) == '/') {
                    // Single-line comment
                    while (peek(scanner) != '\n' && !isAtEnd(scanner)) {
                        advance(scanner);
                    }
                } else {
                    return;
                }
                break;
            default:
                return;
        }
    }
}

static bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

static bool isAlpha(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           c == '_';
}

static TokenType checkKeyword(Scanner* scanner, int start, int length,
                              const char* rest, TokenType type) {
    if (scanner->current - scanner->start == start + length &&
        memcmp(scanner->start + start, rest, length) == 0) {
        return type;
    }
    return TOKEN_IDENTIFIER;
}

static TokenType identifierType(Scanner* scanner) {
    switch (scanner->start[0]) {
        case 'b': return checkKeyword(scanner, 1, 3, "ool", TOKEN_TYPE_BOOL);
        case 'c':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'a': return checkKeyword(scanner, 2, 3, "tch", TOKEN_CATCH);
                    case 'l': return checkKeyword(scanner, 2, 3, "ass", TOKEN_CLASS);
                    case 'o': return checkKeyword(scanner, 2, 3, "nst", TOKEN_CONST);
                }
            }
            break;
        case 'e':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'l': return checkKeyword(scanner, 2, 2, "se", TOKEN_ELSE);
                    case 'x': return checkKeyword(scanner, 2, 5, "tends", TOKEN_EXTENDS);
                    case 'n': return checkKeyword(scanner, 2, 2, "um", TOKEN_ENUM);
                }
            }
            break;
        case 'f':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'a': return checkKeyword(scanner, 2, 3, "lse", TOKEN_FALSE);
                    case 'i': return checkKeyword(scanner, 2, 5, "nally", TOKEN_FINALLY);
                    case 'l': return checkKeyword(scanner, 2, 3, "oat", TOKEN_TYPE_FLOAT);
                    case 'n': return checkKeyword(scanner, 2, 0, "", TOKEN_FN);
                    case 'o': return checkKeyword(scanner, 2, 1, "r", TOKEN_FOR);
                }
            }
            break;
        case 'i':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'f': return checkKeyword(scanner, 2, 0, "", TOKEN_IF);
                    case 'm':
                        if (scanner->current - scanner->start == 10) {
                            return checkKeyword(scanner, 0, 10, "implements", TOKEN_IMPLEMENTS);
                        }
                        return checkKeyword(scanner, 2, 4, "port", TOKEN_IMPORT);
                    case 'n':
                        if (scanner->current - scanner->start == 2) {
                            return TOKEN_IN;
                        }
                        if (scanner->current - scanner->start == 9) {
                            return checkKeyword(scanner, 0, 9, "interface", TOKEN_INTERFACE);
                        }
                        return checkKeyword(scanner, 2, 1, "t", TOKEN_TYPE_INT);
                }
            }
            break;
        case 'l': return checkKeyword(scanner, 1, 2, "et", TOKEN_LET);
        case 'm':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'a': return checkKeyword(scanner, 2, 3, "tch", TOKEN_MATCH);
                    case 'o': return checkKeyword(scanner, 2, 4, "dule", TOKEN_MODULE);
                }
            }
            break;
        case 'n': return checkKeyword(scanner, 1, 2, "il", TOKEN_NIL);
        case 'o':
            if (scanner->current - scanner->start > 1 && scanner->start[1] == 'u') {
                return checkKeyword(scanner, 0, 3, "out", TOKEN_OUT);
            }
            break;
        case 'p': return checkKeyword(scanner, 1, 4, "rint", TOKEN_PRINT);
        case 'r': return checkKeyword(scanner, 1, 5, "eturn", TOKEN_RETURN);
        case 's':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 't': return checkKeyword(scanner, 2, 4, "ring", TOKEN_TYPE_STRING);
                    case 'u': return checkKeyword(scanner, 2, 3, "per", TOKEN_SUPER);
                }
            }
            break;
        case 't':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'y':
                        return checkKeyword(scanner, 2, 2, "pe", TOKEN_TYPE_ALIAS);
                    case 'h':
                        if (scanner->current - scanner->start > 2) {
                            switch (scanner->start[2]) {
                                case 'i': return checkKeyword(scanner, 3, 1, "s", TOKEN_THIS);
                                case 'r': return checkKeyword(scanner, 3, 2, "ow", TOKEN_THROW);
                            }
                        }
                        break;
                    case 'r':
                        if (scanner->current - scanner->start > 2) {
                            switch (scanner->start[2]) {
                                case 'u': return checkKeyword(scanner, 3, 1, "e", TOKEN_TRUE);
                                case 'y': return checkKeyword(scanner, 3, 0, "", TOKEN_TRY);
                            }
                        }
                        break;
                }
            }
            break;
        case 'w': return checkKeyword(scanner, 1, 4, "hile", TOKEN_WHILE);
    }
    return TOKEN_IDENTIFIER;
}

static Token identifier(Scanner* scanner) {
    while (isAlpha(peek(scanner)) || isDigit(peek(scanner))) {
        advance(scanner);
    }
    return makeToken(scanner, identifierType(scanner));
}

static Token number(Scanner* scanner) {
    while (isDigit(peek(scanner))) {
        advance(scanner);
    }

    // Look for a decimal part
    if (peek(scanner) == '.' && isDigit(peekNext(scanner))) {
        // Consume the "."
        advance(scanner);

        while (isDigit(peek(scanner))) {
            advance(scanner);
        }
        return makeToken(scanner, TOKEN_FLOAT);
    }

    return makeToken(scanner, TOKEN_INT);
}

static Token string(Scanner* scanner) {
    while (peek(scanner) != '"' && !isAtEnd(scanner)) {
        if (peek(scanner) == '\n') scanner->line++;
        if (peek(scanner) == '\\' && peekNext(scanner) != '\0') {
            advance(scanner); // Skip escape character
        }
        advance(scanner);
    }

    if (isAtEnd(scanner)) {
        return errorToken(scanner, "Unterminated string.");
    }

    // The closing quote
    advance(scanner);
    return makeToken(scanner, TOKEN_STRING);
}

Token scanToken(Scanner* scanner) {
    skipWhitespace(scanner);
    scanner->start = scanner->current;

    if (isAtEnd(scanner)) return makeToken(scanner, TOKEN_EOF);

    char c = advance(scanner);

    if (isAlpha(c)) return identifier(scanner);
    if (isDigit(c)) return number(scanner);

    switch (c) {
        // Single character tokens
        case '(': return makeToken(scanner, TOKEN_LEFT_PAREN);
        case ')': return makeToken(scanner, TOKEN_RIGHT_PAREN);
        case '{': return makeToken(scanner, TOKEN_LEFT_BRACE);
        case '}': return makeToken(scanner, TOKEN_RIGHT_BRACE);
        case '[': return makeToken(scanner, TOKEN_LEFT_BRACKET);
        case ']': return makeToken(scanner, TOKEN_RIGHT_BRACKET);
        case ',': return makeToken(scanner, TOKEN_COMMA);
        case ';': return makeToken(scanner, TOKEN_SEMICOLON);
        case ':': return makeToken(scanner, TOKEN_COLON);
        case '+': return makeToken(scanner, TOKEN_PLUS);
        case '*': return makeToken(scanner, TOKEN_STAR);
        case '/': return makeToken(scanner, TOKEN_SLASH);
        case '%': return makeToken(scanner, TOKEN_PERCENT);

        // One or two character tokens
        case '!':
            return makeToken(scanner, match(scanner, '=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
        case '=':
            if (match(scanner, '=')) return makeToken(scanner, TOKEN_EQUAL_EQUAL);
            if (match(scanner, '>')) return makeToken(scanner, TOKEN_FAT_ARROW);
            return makeToken(scanner, TOKEN_EQUAL);
        case '<':
            if (match(scanner, '<')) return makeToken(scanner, TOKEN_LSHIFT);
            return makeToken(scanner, match(scanner, '=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
        case '>':
            if (match(scanner, '>')) return makeToken(scanner, TOKEN_RSHIFT);
            return makeToken(scanner, match(scanner, '=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
        case '-':
            return makeToken(scanner, match(scanner, '>') ? TOKEN_ARROW : TOKEN_MINUS);
        case '.':
            if (match(scanner, '.')) {
                // We have at least two dots
                if (match(scanner, '.')) {
                    // We have three dots
                    return makeToken(scanner, TOKEN_DOT_DOT_DOT);
                }
                // Just two dots
                return makeToken(scanner, TOKEN_DOT_DOT);
            }
            // Just one dot
            return makeToken(scanner, TOKEN_DOT);
        case '&':
            if (match(scanner, '&')) return makeToken(scanner, TOKEN_AND);
            return makeToken(scanner, TOKEN_AMPERSAND);
        case '|':
            return makeToken(scanner, match(scanner, '|') ? TOKEN_OR : TOKEN_PIPE);
        case '^':
            return makeToken(scanner, TOKEN_CARET);
        case '~':
            return makeToken(scanner, TOKEN_TILDE);
        case '?':
            if (match(scanner, '.')) return makeToken(scanner, TOKEN_QUESTION_DOT);
            if (match(scanner, '?')) return makeToken(scanner, TOKEN_QUESTION_QUESTION);
            return makeToken(scanner, TOKEN_QUESTION);

        // Newline (statement terminator)
        case '\n':
            scanner->line++;
            return makeToken(scanner, TOKEN_NEWLINE);

        // String literal
        case '"': return string(scanner);
    }

    return errorToken(scanner, "Unexpected character.");
}

const char* tokenTypeName(TokenType type) {
    switch (type) {
        case TOKEN_LEFT_PAREN:    return "LEFT_PAREN";
        case TOKEN_RIGHT_PAREN:   return "RIGHT_PAREN";
        case TOKEN_LEFT_BRACE:    return "LEFT_BRACE";
        case TOKEN_RIGHT_BRACE:   return "RIGHT_BRACE";
        case TOKEN_LEFT_BRACKET:  return "LEFT_BRACKET";
        case TOKEN_RIGHT_BRACKET: return "RIGHT_BRACKET";
        case TOKEN_COMMA:         return "COMMA";
        case TOKEN_DOT:           return "DOT";
        case TOKEN_SEMICOLON:     return "SEMICOLON";
        case TOKEN_COLON:         return "COLON";
        case TOKEN_PLUS:          return "PLUS";
        case TOKEN_MINUS:         return "MINUS";
        case TOKEN_STAR:          return "STAR";
        case TOKEN_SLASH:         return "SLASH";
        case TOKEN_PERCENT:       return "PERCENT";
        case TOKEN_BANG:          return "BANG";
        case TOKEN_BANG_EQUAL:    return "BANG_EQUAL";
        case TOKEN_EQUAL:         return "EQUAL";
        case TOKEN_EQUAL_EQUAL:   return "EQUAL_EQUAL";
        case TOKEN_GREATER:       return "GREATER";
        case TOKEN_GREATER_EQUAL: return "GREATER_EQUAL";
        case TOKEN_LESS:          return "LESS";
        case TOKEN_LESS_EQUAL:    return "LESS_EQUAL";
        case TOKEN_ARROW:         return "ARROW";
        case TOKEN_FAT_ARROW:     return "FAT_ARROW";
        case TOKEN_DOT_DOT:       return "DOT_DOT";
        case TOKEN_DOT_DOT_DOT:   return "DOT_DOT_DOT";
        case TOKEN_AND:           return "AND";
        case TOKEN_OR:            return "OR";
        case TOKEN_PIPE:          return "PIPE";
        case TOKEN_AMPERSAND:     return "AMPERSAND";
        case TOKEN_CARET:         return "CARET";
        case TOKEN_TILDE:         return "TILDE";
        case TOKEN_LSHIFT:       return "LSHIFT";
        case TOKEN_RSHIFT:       return "RSHIFT";
        case TOKEN_QUESTION:      return "QUESTION";
        case TOKEN_QUESTION_DOT:  return "QUESTION_DOT";
        case TOKEN_QUESTION_QUESTION: return "QUESTION_QUESTION";
        case TOKEN_IDENTIFIER:    return "IDENTIFIER";
        case TOKEN_STRING:        return "STRING";
        case TOKEN_INT:           return "INT";
        case TOKEN_FLOAT:         return "FLOAT";
        case TOKEN_LET:           return "LET";
        case TOKEN_CONST:         return "CONST";
        case TOKEN_FN:            return "FN";
        case TOKEN_RETURN:        return "RETURN";
        case TOKEN_IF:            return "IF";
        case TOKEN_ELSE:          return "ELSE";
        case TOKEN_WHILE:         return "WHILE";
        case TOKEN_FOR:           return "FOR";
        case TOKEN_IN:            return "IN";
        case TOKEN_OUT:           return "OUT";
        case TOKEN_CLASS:         return "CLASS";
        case TOKEN_INTERFACE:     return "INTERFACE";
        case TOKEN_IMPLEMENTS:    return "IMPLEMENTS";
        case TOKEN_EXTENDS:       return "EXTENDS";
        case TOKEN_THIS:          return "THIS";
        case TOKEN_SUPER:         return "SUPER";
        case TOKEN_IMPORT:      return "IMPORT";
        case TOKEN_MODULE:      return "MODULE";
        case TOKEN_ENUM:        return "ENUM";
        case TOKEN_MATCH:       return "MATCH";

        case TOKEN_TRUE:          return "TRUE";
        case TOKEN_FALSE:         return "FALSE";
        case TOKEN_NIL:           return "NIL";
        case TOKEN_PRINT:         return "PRINT";
        case TOKEN_TYPE_ALIAS:    return "TYPE_ALIAS";
        case TOKEN_TYPE_INT:      return "TYPE_INT";
        case TOKEN_TYPE_FLOAT:    return "TYPE_FLOAT";
        case TOKEN_TYPE_BOOL:     return "TYPE_BOOL";
        case TOKEN_TYPE_STRING:   return "TYPE_STRING";
        case TOKEN_NEWLINE:       return "NEWLINE";
        case TOKEN_ERROR:         return "ERROR";
        case TOKEN_EOF:           return "EOF";
        default:                  return "UNKNOWN";
    }
}
