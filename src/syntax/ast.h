#ifndef blaze_ast_h
#define blaze_ast_h

/* Abstract syntax: expression and statement tagged unions, type-annotation
 * nodes (`TypeNode`), and tags for match/destructuring/generics. */

#include "common.h"
#include "scanner.h"
#include "types.h"
#include "value.h"

// Forward declarations
typedef struct Expr Expr;
typedef struct Stmt Stmt;
typedef struct TypeNode TypeNode;

// ============================================================================
// Type Nodes (for type annotations in source)
// ============================================================================

typedef enum {
    TYPE_NODE_SIMPLE,       // int, float, bool, string, or identifier
    TYPE_NODE_ARRAY,        // [T]
    TYPE_NODE_FUNCTION,     // fn(T, U) -> R
    TYPE_NODE_OPTIONAL,     // T?
    TYPE_NODE_UNION,        // T | U
    TYPE_NODE_GENERIC,      // Identifier<T, U, ...>
} TypeNodeKind;

struct TypeNode {
    TypeNodeKind kind;
    Token token;            // For error reporting
    union {
        struct {
            Token name;     // int, float, bool, string, or class name
        } simple;
        struct {
            TypeNode* elementType;
        } array;
        struct {
            TypeNode** paramTypes;
            int paramCount;
            TypeNode* returnType;
        } function;
        struct {
            TypeNode* innerType;
        } optional;
        struct {
            TypeNode** types;
            int typeCount;
        } unionType;
        struct {
            Token name;             // Generic type identifier (e.g., Array, Map)
            TypeNode** typeArgs;    // Type arguments (e.g., [T, U])
            int typeArgCount;
        } generic;
    } as;
};

// ============================================================================
// Expressions
// ============================================================================

typedef enum {
    EXPR_LITERAL,           // 42, 3.14, "hello", true, false, nil
    EXPR_UNARY,             // -x, !x
    EXPR_BINARY,            // a + b, a == b, etc.
    EXPR_GROUPING,          // (expr)
    EXPR_VARIABLE,          // x
    EXPR_ASSIGN,            // x = value
    EXPR_LOGICAL,           // a && b, a || b
    EXPR_NULL_COALESCE,     // a ?? b
    EXPR_CALL,              // func(args)
    EXPR_LAMBDA,            // (x) => x * 2
    EXPR_ARRAY,             // [1, 2, 3]
    EXPR_SPREAD,            // ...arr
    EXPR_INDEX,             // array[index]
    EXPR_INDEX_SET,         // array[index] = value
    EXPR_GET,               // object.property
    EXPR_SET,               // object.property = value
    EXPR_THIS,              // this
    EXPR_SUPER,             // super.method
    EXPR_MATCH,             // match expr { ... }
} ExprKind;

typedef struct {
    Token token;
    Value value;            // The actual value
    Type* type;             // Resolved type
} LiteralExpr;

typedef struct {
    Token op;
    Expr* operand;
    Type* type;
} UnaryExpr;

typedef struct {
    Expr* left;
    Token op;
    Expr* right;
    Type* type;
} BinaryExpr;

typedef struct {
    Expr* expression;
    Type* type;
} GroupingExpr;

typedef struct {
    Token name;
    Type* type;
} VariableExpr;

typedef struct {
    Token name;
    Expr* value;
    Type* type;
} AssignExpr;

typedef struct {
    Expr* left;
    Token op;               // && or ||
    Expr* right;
    Type* type;
} LogicalExpr;

typedef struct {
    Expr* left;
    Token op;               // ??
    Expr* right;
    Type* type;
} NullCoalesceExpr;

typedef struct {
    Expr* callee;
    Token paren;            // For error reporting
    Expr** arguments;
    int argCount;
    Type* type;
    TypeNode** explicitTypeArgs; // foo<int>(...) — NULL for normal calls
    int explicitTypeArgCount;
} CallExpr;

typedef struct {
    Token arrow;            // => token for error reporting
    Token* params;          // Parameter names
    TypeNode** paramTypes;  // Parameter types (optional in lambdas)
    int paramCount;
    TypeNode* returnType;   // Return type (optional)
    Expr* body;             // Expression body (if !isBlockBody)
    Stmt* blockBody;        // Block body (if isBlockBody)
    bool isBlockBody;
    Type* type;
} LambdaExpr;

typedef struct {
    Token bracket;
    Expr** elements;
    int elementCount;
    Type* type;
} ArrayExpr;

typedef struct {
    Token dots;              // ... token
    Expr* operand;           // Expression being spread
    Type* type;
} SpreadExpr;

typedef struct {
    Expr* object;
    Token bracket;
    Expr* index;
    Type* type;
} IndexExpr;

typedef struct {
    Expr* object;
    Token bracket;
    Expr* index;
    Expr* value;
    Type* type;
} IndexSetExpr;

typedef struct {
    Expr* object;
    Token name;
    bool isOptional;  // true for ?. operator
    Type* type;
} GetExpr;

typedef struct {
    Expr* object;
    Token name;
    Expr* value;
    Type* type;
} SetExpr;

typedef struct {
    Token keyword;
    Type* type;
} ThisExpr;

typedef struct {
    Token keyword;
    Token method;
    Type* type;
} SuperExpr;

// Case clause for match expressions
typedef struct {
    Expr* pattern;          // Literal pattern (int, bool, string) or NULL for wildcard
    Token* destructureParams; // Names for destructuring
    int destructureCount;
    bool isWildcard;        // true if this is the _ pattern
    Expr* value;            // Expression to evaluate if pattern matches
} ExprCaseClause;

typedef struct {
    Token keyword;
    Expr* matchValue;       // Value being matched
    ExprCaseClause* cases;
    int caseCount;
    Type* type;             // Result type (all arms must be compatible)
} MatchExpr;

struct Expr {
    ExprKind kind;
    int line;
    union {
        LiteralExpr literal;
        UnaryExpr unary;
        BinaryExpr binary;
        GroupingExpr grouping;
        VariableExpr variable;
        AssignExpr assign;
        LogicalExpr logical;
        NullCoalesceExpr null_coalesce;
        CallExpr call;
        LambdaExpr lambda;
        ArrayExpr array;
        SpreadExpr spread;
        IndexExpr index;
        IndexSetExpr index_set;
        GetExpr get;
        SetExpr set;
        ThisExpr this_;
        SuperExpr super_;
        MatchExpr match;
    } as;
};

// ============================================================================
// Statements
// ============================================================================

typedef enum {
    STMT_EXPRESSION,        // expr
    STMT_PRINT,             // print(expr)
    STMT_VAR,               // let x: T = expr
    STMT_BLOCK,             // { stmts }
    STMT_IF,                // if cond { } else { }
    STMT_WHILE,             // while cond { }
    STMT_FOR,               // for x in iter { }
    STMT_FUNCTION,          // fn name(params) -> T { }
    STMT_RETURN,            // return expr
    STMT_CLASS,             // class Name<T> extends Super { }
    STMT_INTERFACE,         // interface Name { fn ... -> ... }
    STMT_TYPE_ALIAS,        // type Name = TypeAnnotation
    STMT_ENUM,              // enum Name { variants }
    STMT_MATCH,             // match expr { cases }
    STMT_TRY,               // try { } catch (e) { }
    STMT_THROW,             // throw expr
    STMT_IMPORT,            // import "path" or import { a, b } from "path"
} StmtKind;

typedef struct {
    Expr* expression;
} ExpressionStmt;

typedef struct {
    Token keyword;
    Expr* expression;
} PrintStmt;

typedef enum {
    DESTRUCTURE_NONE,
    DESTRUCTURE_ARRAY,
    DESTRUCTURE_OBJECT
} DestructureKind;

typedef struct {
    Token name;             // Used for simple declarations
    TypeNode* typeAnnotation;
    Expr* initializer;
    bool isConst;
    Type* type;             // Resolved type
    // Destructuring support
    DestructureKind destructureKind;  // Type of destructuring
    Token* destructureNames; // Variable names in [a, b, c] or {x, y} pattern
    int destructureCount;   // Number of destructure names
    int restIndex;          // Index of ...rest param for arrays (-1 if none)
} VarStmt;

typedef struct {
    Stmt** statements;
    int count;
} BlockStmt;

typedef struct {
    Token keyword;
    Expr* condition;
    Stmt* thenBranch;
    Stmt* elseBranch;       // Can be NULL
} IfStmt;

typedef struct {
    Token keyword;
    Expr* condition;
    Stmt* body;
} WhileStmt;

typedef struct {
    Token keyword;
    Token variable;
    TypeNode* varType;      // Optional type annotation
    Expr* iterable;
    Stmt* body;
} ForStmt;

typedef struct {
    Token name;
    Token* typeParams;      // Type parameter names
    int typeParamCount;
    Token* params;
    TypeNode** paramTypes;
    int paramCount;
    TypeNode* returnType;
    Stmt* body;             // Block statement
    Type* type;             // Function type
} FunctionStmt;

typedef struct {
    Token keyword;
    Expr* value;            // Can be NULL
} ReturnStmt;

// Field declaration in a class
typedef struct {
    Token name;
    TypeNode* type;
} FieldDecl;

typedef struct {
    Token name;
    Token* params;
    TypeNode** paramTypes;
    int paramCount;
    TypeNode* returnType;
} InterfaceMethodDecl;

typedef struct {
    Token name;
    InterfaceMethodDecl* methods;
    int methodCount;
    Type* type;
} InterfaceStmt;

typedef struct {
    Token name;
    Token* typeParams;      // Generic type parameter names (NULL if typeParamCount == 0)
    int typeParamCount;
    TypeParamVariance* typeParamVariances; // Parallel to typeParams; NULL if typeParamCount == 0
    Token* typeParamBounds; // Optional interface bound per type param (same count); length 0 = unbounded
    TypeNode* superclassType;  // NULL if no extends (supports Foo or Foo<T>)
    Token* implementsNames;    // Interface names (not owned beyond stmt lifetime)
    int implementsCount;
    Type* superclassResolved;  // Filled by type checker / lowering for compiler
    FieldDecl* fields;
    int fieldCount;
    FunctionStmt* methods;
    int methodCount;
    Type* type;
} ClassStmt;

typedef struct {
    Token keyword;          // 'type' token
    Token name;
    TypeNode* target;
} TypeAliasStmt;

typedef struct {
    Token name;
    TypeNode** fieldTypes;
    int fieldCount;
} EnumVariant;

typedef struct {
    Token name;
    EnumVariant* variants;
    int variantCount;
    Type* type;
} EnumStmt;

// Case clause for match statement
typedef struct {
    Expr* pattern;          // Literal pattern (int, bool, string) or NULL for wildcard
    Token* destructureParams; // Names for destructuring (e.g. Variant(a, b))
    int destructureCount;
    bool isWildcard;        // true if this is the _ pattern
    Stmt* body;             // Body to execute if pattern matches
} CaseClause;

typedef struct {
    Token keyword;
    Expr* value;            // Value being matched
    CaseClause* cases;
    int caseCount;
} MatchStmt;

typedef struct {
    Token keyword;          // 'try' token
    Stmt* tryBody;          // Block to try
    Token catchVar;         // Variable name for caught exception
    Stmt* catchBody;        // Block to execute on catch
    Stmt* finallyBody;      // Block to execute always (can be NULL)
} TryStmt;

typedef struct {
    Token keyword;          // 'throw' token
    Expr* value;            // Exception value to throw
} ThrowStmt;

typedef struct {
    Token keyword;          // 'import' token
    Token path;             // Module path string
    Token* names;           // Imported names (for selective import)
    int nameCount;          // Number of names (0 = import all)
} ImportStmt;

struct Stmt {
    StmtKind kind;
    int line;
    union {
        ExpressionStmt expression;
        PrintStmt print;
        VarStmt var;
        BlockStmt block;
        IfStmt if_;
        WhileStmt while_;
        ForStmt for_;
        FunctionStmt function;
        ReturnStmt return_;
        ClassStmt class_;
        InterfaceStmt interface_;
        TypeAliasStmt type_alias;
        EnumStmt enum_;
        MatchStmt match;
        TryStmt try_;
        ThrowStmt throw_;
        ImportStmt import;
    } as;
};

// ============================================================================
// AST Construction Functions
// ============================================================================

// Type nodes
TypeNode* newSimpleTypeNode(Token name);
TypeNode* newArrayTypeNode(TypeNode* elementType, Token bracket);
TypeNode* newFunctionTypeNode(TypeNode** paramTypes, int paramCount,
                                TypeNode* returnType, Token fnToken);
TypeNode* newOptionalTypeNode(TypeNode* innerType, Token questionToken);
TypeNode* newUnionTypeNode(TypeNode** types, int typeCount, Token pipeToken);
TypeNode* newGenericTypeNode(Token name, TypeNode** typeArgs, int typeArgCount);
void freeTypeNode(TypeNode* node);

// Expressions
Expr* newLiteralExpr(Token token, Value value);
Expr* newUnaryExpr(Token op, Expr* operand);
Expr* newBinaryExpr(Expr* left, Token op, Expr* right);
Expr* newGroupingExpr(Expr* expression);
Expr* newVariableExpr(Token name);
Expr* newAssignExpr(Token name, Expr* value);
Expr* newLogicalExpr(Expr* left, Token op, Expr* right);
Expr* newNullCoalesceExpr(Expr* left, Token op, Expr* right);
Expr* newCallExpr(Expr* callee, Token paren, Expr** arguments, int argCount,
                  TypeNode** explicitTypeArgs, int explicitTypeArgCount);
Expr* newLambdaExpr(Token arrow, Token* params, TypeNode** paramTypes,
                    int paramCount, TypeNode* returnType, Expr* body);
Expr* newLambdaBlockExpr(Token arrow, Token* params, TypeNode** paramTypes,
                         int paramCount, TypeNode* returnType, Stmt* blockBody);
Expr* newArrayExpr(Token bracket, Expr** elements, int elementCount);
Expr* newSpreadExpr(Token dots, Expr* operand);
Expr* newIndexExpr(Expr* object, Token bracket, Expr* index);
Expr* newIndexSetExpr(Expr* object, Token bracket, Expr* index, Expr* value);
Expr* newGetExpr(Expr* object, Token name, bool isOptional);
Expr* newSetExpr(Expr* object, Token name, Expr* value);
Expr* newThisExpr(Token keyword);
Expr* newSuperExpr(Token keyword, Token method);
Expr* newMatchExpr(Token keyword, Expr* matchValue, ExprCaseClause* cases, int caseCount);
void freeExpr(Expr* expr);

// Statements
Stmt* newExpressionStmt(Expr* expression);
Stmt* newPrintStmt(Token keyword, Expr* expression);
Stmt* newVarStmt(Token name, TypeNode* type, Expr* initializer, bool isConst);
Stmt* newArrayDestructureStmt(Token* names, int count, int restIndex, TypeNode* type, Expr* initializer, bool isConst);
Stmt* newObjectDestructureStmt(Token* names, int count, TypeNode* type, Expr* initializer, bool isConst);
Stmt* newBlockStmt(Stmt** statements, int count);
Stmt* newIfStmt(Token keyword, Expr* condition, Stmt* thenBranch, Stmt* elseBranch);
Stmt* newWhileStmt(Token keyword, Expr* condition, Stmt* body);
Stmt* newForStmt(Token keyword, Token variable, TypeNode* varType,
                 Expr* iterable, Stmt* body);
Stmt* newFunctionStmt(Token name, Token* typeParams, int typeParamCount,
                       Token* params, TypeNode** paramTypes,
                       int paramCount, TypeNode* returnType, Stmt* body);
Stmt* newReturnStmt(Token keyword, Expr* value);
Stmt* newClassStmt(Token name, Token* typeParams, int typeParamCount,
                   TypeParamVariance* typeParamVariances,
                   Token* typeParamBounds,
                   TypeNode* superclassType,
                   Token* implementsNames, int implementsCount,
                   FieldDecl* fields, int fieldCount,
                   FunctionStmt* methods, int methodCount);
Stmt* newInterfaceStmt(Token name, InterfaceMethodDecl* methods, int methodCount);
Stmt* newTypeAliasStmt(Token keyword, Token name, TypeNode* target);
Stmt* newEnumStmt(Token name, EnumVariant* variants, int variantCount);
Stmt* newMatchStmt(Token keyword, Expr* value, CaseClause* cases, int caseCount);
Stmt* newTryStmt(Token keyword, Stmt* tryBody, Token catchVar, Stmt* catchBody, Stmt* finallyBody);
Stmt* newThrowStmt(Token keyword, Expr* value);
Stmt* newImportStmt(Token keyword, Token path, Token* names, int nameCount);
void freeStmt(Stmt* stmt);

// ============================================================================
// AST Printing (for debugging)
// ============================================================================

void printExpr(Expr* expr);
void printStmt(Stmt* stmt);
void printTypeNode(TypeNode* node);

#endif
