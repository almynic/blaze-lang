#ifndef blaze_scanner_h
#define blaze_scanner_h

/* Lexer: `TokenType` covers operators, keywords, literals; `scanToken` advances
 * through source and attaches line numbers for diagnostics. */

#include "common.h"

typedef enum {
    // Single-character tokens
    TOKEN_LEFT_PAREN,       // (
    TOKEN_RIGHT_PAREN,      // )
    TOKEN_LEFT_BRACE,       // {
    TOKEN_RIGHT_BRACE,      // }
    TOKEN_LEFT_BRACKET,     // [
    TOKEN_RIGHT_BRACKET,    // ]
    TOKEN_COMMA,            // ,
    TOKEN_DOT,              // .
    TOKEN_SEMICOLON,        // ; (optional, for multi-statement lines)
    TOKEN_COLON,            // :
    TOKEN_PLUS,             // +
    TOKEN_MINUS,            // -
    TOKEN_STAR,             // *
    TOKEN_SLASH,            // /
    TOKEN_PERCENT,          // %

    // One or two character tokens
    TOKEN_BANG,             // !
    TOKEN_BANG_EQUAL,       // !=
    TOKEN_EQUAL,            // =
    TOKEN_EQUAL_EQUAL,      // ==
    TOKEN_GREATER,          // >
    TOKEN_GREATER_EQUAL,    // >=
    TOKEN_LESS,             // <
    TOKEN_LESS_EQUAL,       // <=
    TOKEN_ARROW,            // ->
    TOKEN_FAT_ARROW,        // =>
    TOKEN_DOT_DOT,          // ..
    TOKEN_DOT_DOT_DOT,      // ...
    TOKEN_AND,              // &&
    TOKEN_OR,               // ||
    TOKEN_PIPE,             // |
    TOKEN_QUESTION,         // ?
    TOKEN_QUESTION_DOT,     // ?.
    TOKEN_QUESTION_QUESTION, // ??

    // Literals
    TOKEN_IDENTIFIER,
    TOKEN_STRING,
    TOKEN_INT,
    TOKEN_FLOAT,

    // Keywords
    TOKEN_LET,
    TOKEN_CONST,
    TOKEN_FN,
    TOKEN_RETURN,
    TOKEN_IF,
    TOKEN_ELSE,
    TOKEN_WHILE,
    TOKEN_FOR,
    TOKEN_IN,
    TOKEN_OUT,
    TOKEN_CLASS,
    TOKEN_INTERFACE,
    TOKEN_IMPLEMENTS,
    TOKEN_EXTENDS,
    TOKEN_THIS,
    TOKEN_SUPER,
    TOKEN_IMPORT,
    TOKEN_MODULE,
    TOKEN_ENUM,
    TOKEN_MATCH,
    TOKEN_TRY,
    TOKEN_CATCH,
    TOKEN_THROW,
    TOKEN_FINALLY,
    TOKEN_TRUE,
    TOKEN_FALSE,
    TOKEN_NIL,
    TOKEN_PRINT,            // Built-in for now
    TOKEN_TYPE_ALIAS,       // keyword `type` for type alias declarations

    // Type keywords
    TOKEN_TYPE_INT,         // int
    TOKEN_TYPE_FLOAT,       // float
    TOKEN_TYPE_BOOL,        // bool
    TOKEN_TYPE_STRING,      // string

    // Special tokens
    TOKEN_NEWLINE,          // Statement terminator
    TOKEN_ERROR,
    TOKEN_EOF,
} TokenType;

typedef struct {
    TokenType type;
    const char* start;      // Pointer to start of lexeme in source
    int length;             // Length of lexeme
    int line;               // Line number for error reporting
} Token;

typedef struct {
    const char* start;      // Start of current lexeme
    const char* current;    // Current position in source
    int line;               // Current line number
} Scanner;

// Initialize the scanner with source code
void initScanner(Scanner* scanner, const char* source);

// Scan and return the next token
Token scanToken(Scanner* scanner);

// Get a human-readable name for a token type
const char* tokenTypeName(TokenType type);

#endif
