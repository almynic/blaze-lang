/* AST node allocation and deep-free helpers (type nodes, exprs, stmts). */

#include "ast.h"
#include "memory.h"

// ============================================================================
// Type Node Construction
// ============================================================================

TypeNode* newSimpleTypeNode(Token name) {
    TypeNode* node = ALLOCATE(TypeNode, 1);
    node->kind = TYPE_NODE_SIMPLE;
    node->token = name;
    node->as.simple.name = name;
    return node;
}

TypeNode* newArrayTypeNode(TypeNode* elementType, Token bracket) {
    TypeNode* node = ALLOCATE(TypeNode, 1);
    node->kind = TYPE_NODE_ARRAY;
    node->token = bracket;
    node->as.array.elementType = elementType;
    return node;
}

TypeNode* newFunctionTypeNode(TypeNode** paramTypes, int paramCount,
                               TypeNode* returnType, Token fnToken) {
    TypeNode* node = ALLOCATE(TypeNode, 1);
    node->kind = TYPE_NODE_FUNCTION;
    node->token = fnToken;
    node->as.function.paramTypes = paramTypes;
    node->as.function.paramCount = paramCount;
    node->as.function.returnType = returnType;
    return node;
}

TypeNode* newOptionalTypeNode(TypeNode* innerType, Token questionToken) {
    TypeNode* node = ALLOCATE(TypeNode, 1);
    node->kind = TYPE_NODE_OPTIONAL;
    node->token = questionToken;
    node->as.optional.innerType = innerType;
    return node;
}

TypeNode* newUnionTypeNode(TypeNode** types, int typeCount, Token pipeToken) {
    TypeNode* node = ALLOCATE(TypeNode, 1);
    node->kind = TYPE_NODE_UNION;
    node->token = pipeToken;
    node->as.unionType.types = types;
    node->as.unionType.typeCount = typeCount;
    return node;
}

TypeNode* newGenericTypeNode(Token name, TypeNode** typeArgs, int typeArgCount) {
    TypeNode* node = ALLOCATE(TypeNode, 1);
    node->kind = TYPE_NODE_GENERIC;
    node->token = name;
    node->as.generic.name = name;
    node->as.generic.typeArgs = typeArgs;
    node->as.generic.typeArgCount = typeArgCount;
    return node;
}

void freeTypeNode(TypeNode* node) {
    if (node == NULL) return;

    switch (node->kind) {
        case TYPE_NODE_SIMPLE:
            break;
        case TYPE_NODE_ARRAY:
            freeTypeNode(node->as.array.elementType);
            break;
        case TYPE_NODE_FUNCTION:
            for (int i = 0; i < node->as.function.paramCount; i++) {
                freeTypeNode(node->as.function.paramTypes[i]);
            }
            FREE_ARRAY(TypeNode*, node->as.function.paramTypes,
                       node->as.function.paramCount);
            freeTypeNode(node->as.function.returnType);
            break;
        case TYPE_NODE_OPTIONAL:
            freeTypeNode(node->as.optional.innerType);
            break;
        case TYPE_NODE_UNION:
            for (int i = 0; i < node->as.unionType.typeCount; i++) {
                freeTypeNode(node->as.unionType.types[i]);
            }
            FREE_ARRAY(TypeNode*, node->as.unionType.types,
                       node->as.unionType.typeCount);
            break;
        case TYPE_NODE_GENERIC:
            for (int i = 0; i < node->as.generic.typeArgCount; i++) {
                freeTypeNode(node->as.generic.typeArgs[i]);
            }
            FREE_ARRAY(TypeNode*, node->as.generic.typeArgs,
                       node->as.generic.typeArgCount);
            break;
    }
    FREE(TypeNode, node);
}

// ============================================================================
// Expression Construction
// ============================================================================

static Expr* allocateExpr(ExprKind kind, int line) {
    Expr* expr = ALLOCATE(Expr, 1);
    expr->kind = kind;
    expr->line = line;
    return expr;
}

Expr* newLiteralExpr(Token token, Value value) {
    Expr* expr = allocateExpr(EXPR_LITERAL, token.line);
    expr->as.literal.token = token;
    expr->as.literal.value = value;
    expr->as.literal.type = NULL;
    return expr;
}

Expr* newUnaryExpr(Token op, Expr* operand) {
    Expr* expr = allocateExpr(EXPR_UNARY, op.line);
    expr->as.unary.op = op;
    expr->as.unary.operand = operand;
    expr->as.unary.type = NULL;
    return expr;
}

Expr* newBinaryExpr(Expr* left, Token op, Expr* right) {
    Expr* expr = allocateExpr(EXPR_BINARY, op.line);
    expr->as.binary.left = left;
    expr->as.binary.op = op;
    expr->as.binary.right = right;
    expr->as.binary.type = NULL;
    return expr;
}

Expr* newGroupingExpr(Expr* expression) {
    Expr* expr = allocateExpr(EXPR_GROUPING, expression->line);
    expr->as.grouping.expression = expression;
    expr->as.grouping.type = NULL;
    return expr;
}

Expr* newVariableExpr(Token name) {
    Expr* expr = allocateExpr(EXPR_VARIABLE, name.line);
    expr->as.variable.name = name;
    expr->as.variable.type = NULL;
    return expr;
}

Expr* newAssignExpr(Token name, Expr* value) {
    Expr* expr = allocateExpr(EXPR_ASSIGN, name.line);
    expr->as.assign.name = name;
    expr->as.assign.value = value;
    expr->as.assign.type = NULL;
    return expr;
}

Expr* newLogicalExpr(Expr* left, Token op, Expr* right) {
    Expr* expr = allocateExpr(EXPR_LOGICAL, op.line);
    expr->as.logical.left = left;
    expr->as.logical.op = op;
    expr->as.logical.right = right;
    expr->as.logical.type = NULL;
    return expr;
}

Expr* newNullCoalesceExpr(Expr* left, Token op, Expr* right) {
    Expr* expr = allocateExpr(EXPR_NULL_COALESCE, op.line);
    expr->as.null_coalesce.left = left;
    expr->as.null_coalesce.op = op;
    expr->as.null_coalesce.right = right;
    expr->as.null_coalesce.type = NULL;
    return expr;
}

Expr* newCallExpr(Expr* callee, Token paren, Expr** arguments, int argCount,
                  TypeNode** explicitTypeArgs, int explicitTypeArgCount) {
    Expr* expr = allocateExpr(EXPR_CALL, paren.line);
    expr->as.call.callee = callee;
    expr->as.call.paren = paren;
    expr->as.call.arguments = arguments;
    expr->as.call.argCount = argCount;
    expr->as.call.type = NULL;
    expr->as.call.explicitTypeArgs = explicitTypeArgs;
    expr->as.call.explicitTypeArgCount = explicitTypeArgCount;
    return expr;
}

Expr* newLambdaExpr(Token arrow, Token* params, TypeNode** paramTypes,
                    int paramCount, TypeNode* returnType, Expr* body) {
    Expr* expr = allocateExpr(EXPR_LAMBDA, arrow.line);
    expr->as.lambda.arrow = arrow;
    expr->as.lambda.params = params;
    expr->as.lambda.paramTypes = paramTypes;
    expr->as.lambda.paramCount = paramCount;
    expr->as.lambda.returnType = returnType;
    expr->as.lambda.body = body;
    expr->as.lambda.blockBody = NULL;
    expr->as.lambda.isBlockBody = false;
    expr->as.lambda.type = NULL;
    return expr;
}

Expr* newLambdaBlockExpr(Token arrow, Token* params, TypeNode** paramTypes,
                         int paramCount, TypeNode* returnType, Stmt* blockBody) {
    Expr* expr = allocateExpr(EXPR_LAMBDA, arrow.line);
    expr->as.lambda.arrow = arrow;
    expr->as.lambda.params = params;
    expr->as.lambda.paramTypes = paramTypes;
    expr->as.lambda.paramCount = paramCount;
    expr->as.lambda.returnType = returnType;
    expr->as.lambda.body = NULL;
    expr->as.lambda.blockBody = blockBody;
    expr->as.lambda.isBlockBody = true;
    expr->as.lambda.type = NULL;
    return expr;
}

Expr* newArrayExpr(Token bracket, Expr** elements, int elementCount) {
    Expr* expr = allocateExpr(EXPR_ARRAY, bracket.line);
    expr->as.array.bracket = bracket;
    expr->as.array.elements = elements;
    expr->as.array.elementCount = elementCount;
    expr->as.array.type = NULL;
    return expr;
}

Expr* newSpreadExpr(Token dots, Expr* operand) {
    Expr* expr = allocateExpr(EXPR_SPREAD, dots.line);
    expr->as.spread.dots = dots;
    expr->as.spread.operand = operand;
    expr->as.spread.type = NULL;
    return expr;
}

Expr* newIndexExpr(Expr* object, Token bracket, Expr* index) {
    Expr* expr = allocateExpr(EXPR_INDEX, bracket.line);
    expr->as.index.object = object;
    expr->as.index.bracket = bracket;
    expr->as.index.index = index;
    expr->as.index.type = NULL;
    return expr;
}

Expr* newIndexSetExpr(Expr* object, Token bracket, Expr* index, Expr* value) {
    Expr* expr = allocateExpr(EXPR_INDEX_SET, bracket.line);
    expr->as.index_set.object = object;
    expr->as.index_set.bracket = bracket;
    expr->as.index_set.index = index;
    expr->as.index_set.value = value;
    expr->as.index_set.type = NULL;
    return expr;
}

Expr* newGetExpr(Expr* object, Token name, bool isOptional) {
    Expr* expr = allocateExpr(EXPR_GET, name.line);
    expr->as.get.object = object;
    expr->as.get.name = name;
    expr->as.get.isOptional = isOptional;
    expr->as.get.type = NULL;
    return expr;
}

Expr* newSetExpr(Expr* object, Token name, Expr* value) {
    Expr* expr = allocateExpr(EXPR_SET, name.line);
    expr->as.set.object = object;
    expr->as.set.name = name;
    expr->as.set.value = value;
    expr->as.set.type = NULL;
    return expr;
}

Expr* newThisExpr(Token keyword) {
    Expr* expr = allocateExpr(EXPR_THIS, keyword.line);
    expr->as.this_.keyword = keyword;
    expr->as.this_.type = NULL;
    return expr;
}

Expr* newSuperExpr(Token keyword, Token method) {
    Expr* expr = allocateExpr(EXPR_SUPER, keyword.line);
    expr->as.super_.keyword = keyword;
    expr->as.super_.method = method;
    expr->as.super_.type = NULL;
    return expr;
}

Expr* newMatchExpr(Token keyword, Expr* matchValue, ExprCaseClause* cases, int caseCount) {
    Expr* expr = allocateExpr(EXPR_MATCH, keyword.line);
    expr->as.match.keyword = keyword;
    expr->as.match.matchValue = matchValue;
    for (int i = 0; i < caseCount; i++) {
        cases[i].destructureParams = NULL;
        cases[i].destructureCount = 0;
    }
    expr->as.match.cases = cases;
    expr->as.match.caseCount = caseCount;
    expr->as.match.type = NULL;
    return expr;
}

void freeExpr(Expr* expr) {
    if (expr == NULL) return;

    switch (expr->kind) {
        case EXPR_LITERAL:
            break;
        case EXPR_UNARY:
            freeExpr(expr->as.unary.operand);
            break;
        case EXPR_BINARY:
            freeExpr(expr->as.binary.left);
            freeExpr(expr->as.binary.right);
            break;
        case EXPR_GROUPING:
            freeExpr(expr->as.grouping.expression);
            break;
        case EXPR_VARIABLE:
            break;
        case EXPR_ASSIGN:
            freeExpr(expr->as.assign.value);
            break;
        case EXPR_LOGICAL:
            freeExpr(expr->as.logical.left);
            freeExpr(expr->as.logical.right);
            break;
        case EXPR_NULL_COALESCE:
            freeExpr(expr->as.null_coalesce.left);
            freeExpr(expr->as.null_coalesce.right);
            break;
        case EXPR_CALL:
            freeExpr(expr->as.call.callee);
            for (int i = 0; i < expr->as.call.argCount; i++) {
                freeExpr(expr->as.call.arguments[i]);
            }
            FREE_ARRAY(Expr*, expr->as.call.arguments, expr->as.call.argCount);
            if (expr->as.call.explicitTypeArgs != NULL) {
                for (int i = 0; i < expr->as.call.explicitTypeArgCount; i++) {
                    freeTypeNode(expr->as.call.explicitTypeArgs[i]);
                }
                FREE_ARRAY(TypeNode*, expr->as.call.explicitTypeArgs,
                           expr->as.call.explicitTypeArgCount);
            }
            break;
        case EXPR_LAMBDA:
            FREE_ARRAY(Token, expr->as.lambda.params, expr->as.lambda.paramCount);
            for (int i = 0; i < expr->as.lambda.paramCount; i++) {
                freeTypeNode(expr->as.lambda.paramTypes[i]);
            }
            FREE_ARRAY(TypeNode*, expr->as.lambda.paramTypes, expr->as.lambda.paramCount);
            freeTypeNode(expr->as.lambda.returnType);
            if (expr->as.lambda.isBlockBody) {
                freeStmt(expr->as.lambda.blockBody);
            } else {
                freeExpr(expr->as.lambda.body);
            }
            break;
        case EXPR_ARRAY:
            for (int i = 0; i < expr->as.array.elementCount; i++) {
                freeExpr(expr->as.array.elements[i]);
            }
            FREE_ARRAY(Expr*, expr->as.array.elements, expr->as.array.elementCount);
            break;
        case EXPR_SPREAD:
            freeExpr(expr->as.spread.operand);
            break;
        case EXPR_INDEX:
            freeExpr(expr->as.index.object);
            freeExpr(expr->as.index.index);
            break;
        case EXPR_INDEX_SET:
            freeExpr(expr->as.index_set.object);
            freeExpr(expr->as.index_set.index);
            freeExpr(expr->as.index_set.value);
            break;
        case EXPR_GET:
            freeExpr(expr->as.get.object);
            break;
        case EXPR_SET:
            freeExpr(expr->as.set.object);
            freeExpr(expr->as.set.value);
            break;
        case EXPR_THIS:
        case EXPR_SUPER:
            break;
        case EXPR_MATCH:
            freeExpr(expr->as.match.matchValue);
            for (int i = 0; i < expr->as.match.caseCount; i++) {
                if (expr->as.match.cases[i].pattern != NULL) {
                    freeExpr(expr->as.match.cases[i].pattern);
                }
                if (expr->as.match.cases[i].destructureParams != NULL) {
                    FREE_ARRAY(Token, expr->as.match.cases[i].destructureParams,
                               expr->as.match.cases[i].destructureCount);
                }
                freeExpr(expr->as.match.cases[i].value);
            }
            FREE_ARRAY(ExprCaseClause, expr->as.match.cases, expr->as.match.caseCount);
            break;
    }
    FREE(Expr, expr);
}

// ============================================================================
// Statement Construction
// ============================================================================

static Stmt* allocateStmt(StmtKind kind, int line) {
    Stmt* stmt = ALLOCATE(Stmt, 1);
    stmt->kind = kind;
    stmt->line = line;
    return stmt;
}

Stmt* newExpressionStmt(Expr* expression) {
    if (expression == NULL) {
        // Parse error occurred, return NULL to indicate failure
        return NULL;
    }
    Stmt* stmt = allocateStmt(STMT_EXPRESSION, expression->line);
    stmt->as.expression.expression = expression;
    return stmt;
}

Stmt* newPrintStmt(Token keyword, Expr* expression) {
    Stmt* stmt = allocateStmt(STMT_PRINT, keyword.line);
    stmt->as.print.keyword = keyword;
    stmt->as.print.expression = expression;
    return stmt;
}

Stmt* newVarStmt(Token name, TypeNode* type, Expr* initializer, bool isConst) {
    Stmt* stmt = allocateStmt(STMT_VAR, name.line);
    stmt->as.var.name = name;
    stmt->as.var.typeAnnotation = type;
    stmt->as.var.initializer = initializer;
    stmt->as.var.isConst = isConst;
    stmt->as.var.type = NULL;
    stmt->as.var.destructureKind = DESTRUCTURE_NONE;
    stmt->as.var.destructureNames = NULL;
    stmt->as.var.destructureCount = 0;
    stmt->as.var.restIndex = -1;
    return stmt;
}

Stmt* newArrayDestructureStmt(Token* names, int count, int restIndex, TypeNode* type, Expr* initializer, bool isConst) {
    Stmt* stmt = allocateStmt(STMT_VAR, names[0].line);
    stmt->as.var.name = names[0]; // Use first name for line tracking
    stmt->as.var.typeAnnotation = type;
    stmt->as.var.initializer = initializer;
    stmt->as.var.isConst = isConst;
    stmt->as.var.type = NULL;
    stmt->as.var.destructureKind = DESTRUCTURE_ARRAY;
    stmt->as.var.destructureNames = names;
    stmt->as.var.destructureCount = count;
    stmt->as.var.restIndex = restIndex;
    return stmt;
}

Stmt* newObjectDestructureStmt(Token* names, int count, TypeNode* type, Expr* initializer, bool isConst) {
    Stmt* stmt = allocateStmt(STMT_VAR, names[0].line);
    stmt->as.var.name = names[0]; // Use first name for line tracking
    stmt->as.var.typeAnnotation = type;
    stmt->as.var.initializer = initializer;
    stmt->as.var.isConst = isConst;
    stmt->as.var.type = NULL;
    stmt->as.var.destructureKind = DESTRUCTURE_OBJECT;
    stmt->as.var.destructureNames = names;
    stmt->as.var.destructureCount = count;
    stmt->as.var.restIndex = -1; // No rest for objects yet
    return stmt;
}

Stmt* newBlockStmt(Stmt** statements, int count) {
    Stmt* stmt = allocateStmt(STMT_BLOCK, statements && count > 0 ? statements[0]->line : 0);
    stmt->as.block.statements = statements;
    stmt->as.block.count = count;
    return stmt;
}

Stmt* newIfStmt(Token keyword, Expr* condition, Stmt* thenBranch, Stmt* elseBranch) {
    Stmt* stmt = allocateStmt(STMT_IF, keyword.line);
    stmt->as.if_.keyword = keyword;
    stmt->as.if_.condition = condition;
    stmt->as.if_.thenBranch = thenBranch;
    stmt->as.if_.elseBranch = elseBranch;
    return stmt;
}

Stmt* newWhileStmt(Token keyword, Expr* condition, Stmt* body) {
    Stmt* stmt = allocateStmt(STMT_WHILE, keyword.line);
    stmt->as.while_.keyword = keyword;
    stmt->as.while_.condition = condition;
    stmt->as.while_.body = body;
    return stmt;
}

Stmt* newForStmt(Token keyword, Token variable, TypeNode* varType,
                 Expr* iterable, Stmt* body) {
    Stmt* stmt = allocateStmt(STMT_FOR, keyword.line);
    stmt->as.for_.keyword = keyword;
    stmt->as.for_.variable = variable;
    stmt->as.for_.varType = varType;
    stmt->as.for_.iterable = iterable;
    stmt->as.for_.body = body;
    return stmt;
}

Stmt* newFunctionStmt(Token name, Token* typeParams, int typeParamCount,
                       Token* params, TypeNode** paramTypes,
                       int paramCount, TypeNode* returnType, Stmt* body) {
    Stmt* stmt = allocateStmt(STMT_FUNCTION, name.line);
    stmt->as.function.name = name;
    stmt->as.function.typeParams = typeParams;
    stmt->as.function.typeParamCount = typeParamCount;
    stmt->as.function.params = params;
    stmt->as.function.paramTypes = paramTypes;
    stmt->as.function.paramCount = paramCount;
    stmt->as.function.returnType = returnType;
    stmt->as.function.body = body;
    stmt->as.function.type = NULL;
    return stmt;
}

Stmt* newReturnStmt(Token keyword, Expr* value) {
    Stmt* stmt = allocateStmt(STMT_RETURN, keyword.line);
    stmt->as.return_.keyword = keyword;
    stmt->as.return_.value = value;
    return stmt;
}

Stmt* newClassStmt(Token name, Token* typeParams, int typeParamCount,
                   TypeParamVariance* typeParamVariances,
                   Token* typeParamBounds,
                   TypeNode* superclassType,
                   Token* implementsNames, int implementsCount,
                   FieldDecl* fields, int fieldCount,
                   FunctionStmt* methods, int methodCount) {
    Stmt* stmt = allocateStmt(STMT_CLASS, name.line);
    stmt->as.class_.name = name;
    stmt->as.class_.typeParams = typeParams;
    stmt->as.class_.typeParamCount = typeParamCount;
    stmt->as.class_.typeParamVariances = typeParamVariances;
    stmt->as.class_.typeParamBounds = typeParamBounds;
    stmt->as.class_.superclassType = superclassType;
    stmt->as.class_.implementsNames = implementsNames;
    stmt->as.class_.implementsCount = implementsCount;
    stmt->as.class_.superclassResolved = NULL;
    stmt->as.class_.fields = fields;
    stmt->as.class_.fieldCount = fieldCount;
    stmt->as.class_.methods = methods;
    stmt->as.class_.methodCount = methodCount;
    stmt->as.class_.type = NULL;
    return stmt;
}

Stmt* newInterfaceStmt(Token name, InterfaceMethodDecl* methods, int methodCount) {
    Stmt* stmt = allocateStmt(STMT_INTERFACE, name.line);
    stmt->as.interface_.name = name;
    stmt->as.interface_.methods = methods;
    stmt->as.interface_.methodCount = methodCount;
    stmt->as.interface_.type = NULL;
    return stmt;
}

Stmt* newTypeAliasStmt(Token keyword, Token name, TypeNode* target) {
    Stmt* stmt = allocateStmt(STMT_TYPE_ALIAS, keyword.line);
    stmt->as.type_alias.keyword = keyword;
    stmt->as.type_alias.name = name;
    stmt->as.type_alias.target = target;
    return stmt;
}

Stmt* newEnumStmt(Token name, EnumVariant* variants, int variantCount) {
    Stmt* stmt = allocateStmt(STMT_ENUM, name.line);
    stmt->as.enum_.name = name;
    stmt->as.enum_.variants = variants;
    stmt->as.enum_.variantCount = variantCount;
    stmt->as.enum_.type = NULL;
    return stmt;
}

Stmt* newMatchStmt(Token keyword, Expr* value, CaseClause* cases, int caseCount) {
    Stmt* stmt = allocateStmt(STMT_MATCH, keyword.line);
    stmt->as.match.keyword = keyword;
    stmt->as.match.value = value;
    for (int i = 0; i < caseCount; i++) {
        cases[i].destructureParams = NULL;
        cases[i].destructureCount = 0;
    }
    stmt->as.match.cases = cases;
    stmt->as.match.caseCount = caseCount;
    return stmt;
}

Stmt* newTryStmt(Token keyword, Stmt* tryBody, Token catchVar, Stmt* catchBody, Stmt* finallyBody) {
    Stmt* stmt = allocateStmt(STMT_TRY, keyword.line);
    stmt->as.try_.keyword = keyword;
    stmt->as.try_.tryBody = tryBody;
    stmt->as.try_.catchVar = catchVar;
    stmt->as.try_.catchBody = catchBody;
    stmt->as.try_.finallyBody = finallyBody;
    return stmt;
}

Stmt* newThrowStmt(Token keyword, Expr* value) {
    Stmt* stmt = allocateStmt(STMT_THROW, keyword.line);
    stmt->as.throw_.keyword = keyword;
    stmt->as.throw_.value = value;
    return stmt;
}

Stmt* newImportStmt(Token keyword, Token path, Token* names, int nameCount) {
    Stmt* stmt = allocateStmt(STMT_IMPORT, keyword.line);
    stmt->as.import.keyword = keyword;
    stmt->as.import.path = path;
    stmt->as.import.names = names;
    stmt->as.import.nameCount = nameCount;
    return stmt;
}

void freeStmt(Stmt* stmt) {
    if (stmt == NULL) return;

    switch (stmt->kind) {
        case STMT_EXPRESSION:
            freeExpr(stmt->as.expression.expression);
            break;
        case STMT_PRINT:
            freeExpr(stmt->as.print.expression);
            break;
        case STMT_VAR:
            freeTypeNode(stmt->as.var.typeAnnotation);
            freeExpr(stmt->as.var.initializer);
            if (stmt->as.var.destructureKind != DESTRUCTURE_NONE && stmt->as.var.destructureNames != NULL) {
                FREE_ARRAY(Token, stmt->as.var.destructureNames, stmt->as.var.destructureCount);
            }
            break;
        case STMT_BLOCK:
            for (int i = 0; i < stmt->as.block.count; i++) {
                freeStmt(stmt->as.block.statements[i]);
            }
            FREE_ARRAY(Stmt*, stmt->as.block.statements, stmt->as.block.count);
            break;
        case STMT_IF:
            freeExpr(stmt->as.if_.condition);
            freeStmt(stmt->as.if_.thenBranch);
            freeStmt(stmt->as.if_.elseBranch);
            break;
        case STMT_WHILE:
            freeExpr(stmt->as.while_.condition);
            freeStmt(stmt->as.while_.body);
            break;
        case STMT_FOR:
            freeTypeNode(stmt->as.for_.varType);
            freeExpr(stmt->as.for_.iterable);
            freeStmt(stmt->as.for_.body);
            break;
        case STMT_FUNCTION:
            FREE_ARRAY(Token, stmt->as.function.typeParams, stmt->as.function.typeParamCount);
            FREE_ARRAY(Token, stmt->as.function.params, stmt->as.function.paramCount);
            for (int i = 0; i < stmt->as.function.paramCount; i++) {
                freeTypeNode(stmt->as.function.paramTypes[i]);
            }
            FREE_ARRAY(TypeNode*, stmt->as.function.paramTypes, stmt->as.function.paramCount);
            freeTypeNode(stmt->as.function.returnType);
            freeStmt(stmt->as.function.body);
            break;
        case STMT_RETURN:
            freeExpr(stmt->as.return_.value);
            break;
        case STMT_CLASS:
            if (stmt->as.class_.typeParams != NULL) {
                FREE_ARRAY(Token, stmt->as.class_.typeParams, stmt->as.class_.typeParamCount);
            }
            if (stmt->as.class_.typeParamVariances != NULL) {
                FREE_ARRAY(TypeParamVariance, stmt->as.class_.typeParamVariances,
                           stmt->as.class_.typeParamCount);
            }
            if (stmt->as.class_.typeParamBounds != NULL) {
                FREE_ARRAY(Token, stmt->as.class_.typeParamBounds, stmt->as.class_.typeParamCount);
            }
            freeTypeNode(stmt->as.class_.superclassType);
            if (stmt->as.class_.implementsNames != NULL) {
                FREE_ARRAY(Token, stmt->as.class_.implementsNames, stmt->as.class_.implementsCount);
            }
            for (int i = 0; i < stmt->as.class_.fieldCount; i++) {
                freeTypeNode(stmt->as.class_.fields[i].type);
            }
            FREE_ARRAY(FieldDecl, stmt->as.class_.fields, stmt->as.class_.fieldCount);
            // Methods are stored inline, free their internals
            for (int i = 0; i < stmt->as.class_.methodCount; i++) {
                FunctionStmt* m = &stmt->as.class_.methods[i];
                FREE_ARRAY(Token, m->typeParams, m->typeParamCount);
                FREE_ARRAY(Token, m->params, m->paramCount);
                for (int j = 0; j < m->paramCount; j++) {
                    freeTypeNode(m->paramTypes[j]);
                }
                FREE_ARRAY(TypeNode*, m->paramTypes, m->paramCount);
                freeTypeNode(m->returnType);
                freeStmt(m->body);
            }
            FREE_ARRAY(FunctionStmt, stmt->as.class_.methods, stmt->as.class_.methodCount);
            break;
        case STMT_INTERFACE:
            for (int i = 0; i < stmt->as.interface_.methodCount; i++) {
                InterfaceMethodDecl* m = &stmt->as.interface_.methods[i];
                FREE_ARRAY(Token, m->params, m->paramCount);
                for (int j = 0; j < m->paramCount; j++) {
                    freeTypeNode(m->paramTypes[j]);
                }
                FREE_ARRAY(TypeNode*, m->paramTypes, m->paramCount);
                freeTypeNode(m->returnType);
            }
            FREE_ARRAY(InterfaceMethodDecl, stmt->as.interface_.methods, stmt->as.interface_.methodCount);
            break;
        case STMT_TYPE_ALIAS:
            freeTypeNode(stmt->as.type_alias.target);
            break;
        case STMT_ENUM:
            for (int i = 0; i < stmt->as.enum_.variantCount; i++) {
                EnumVariant* v = &stmt->as.enum_.variants[i];
                for (int j = 0; j < v->fieldCount; j++) {
                    freeTypeNode(v->fieldTypes[j]);
                }
                FREE_ARRAY(TypeNode*, v->fieldTypes, v->fieldCount);
            }
            FREE_ARRAY(EnumVariant, stmt->as.enum_.variants, stmt->as.enum_.variantCount);
            break;
        case STMT_MATCH:
            freeExpr(stmt->as.match.value);
            for (int i = 0; i < stmt->as.match.caseCount; i++) {
                if (stmt->as.match.cases[i].pattern != NULL) {
                    freeExpr(stmt->as.match.cases[i].pattern);
                }
                if (stmt->as.match.cases[i].destructureParams != NULL) {
                    FREE_ARRAY(Token, stmt->as.match.cases[i].destructureParams,
                               stmt->as.match.cases[i].destructureCount);
                }
                freeStmt(stmt->as.match.cases[i].body);
            }
            FREE_ARRAY(CaseClause, stmt->as.match.cases, stmt->as.match.caseCount);
            break;
        case STMT_TRY:
            freeStmt(stmt->as.try_.tryBody);
            freeStmt(stmt->as.try_.catchBody);
            break;
        case STMT_THROW:
            freeExpr(stmt->as.throw_.value);
            break;
        case STMT_IMPORT:
            if (stmt->as.import.names != NULL) {
                FREE_ARRAY(Token, stmt->as.import.names, stmt->as.import.nameCount);
            }
            break;
    }
    FREE(Stmt, stmt);
}

// ============================================================================
// AST Printing
// ============================================================================

void printTypeNode(TypeNode* node) {
    if (node == NULL) {
        printf("<no type>");
        return;
    }

    switch (node->kind) {
        case TYPE_NODE_SIMPLE:
            printf("%.*s", node->as.simple.name.length, node->as.simple.name.start);
            break;
        case TYPE_NODE_ARRAY:
            printf("[");
            printTypeNode(node->as.array.elementType);
            printf("]");
            break;
        case TYPE_NODE_FUNCTION:
            printf("fn(");
            for (int i = 0; i < node->as.function.paramCount; i++) {
                if (i > 0) printf(", ");
                printTypeNode(node->as.function.paramTypes[i]);
            }
            printf(") -> ");
            printTypeNode(node->as.function.returnType);
            break;
        case TYPE_NODE_OPTIONAL:
            printTypeNode(node->as.optional.innerType);
            printf("?");
            break;
        case TYPE_NODE_GENERIC:
            printf("%.*s<", node->as.generic.name.length, node->as.generic.name.start);
            for (int i = 0; i < node->as.generic.typeArgCount; i++) {
                if (i > 0) printf(", ");
                printTypeNode(node->as.generic.typeArgs[i]);
            }
            printf(">");
            break;
        case TYPE_NODE_UNION:
            for (int i = 0; i < node->as.unionType.typeCount; i++) {
                if (i > 0) printf(" | ");
                printTypeNode(node->as.unionType.types[i]);
            }
            break;
    }
}

static void printIndent(int indent) {
    for (int i = 0; i < indent; i++) printf("  ");
}

static void printExprIndent(Expr* expr, int indent);
static void printStmtIndent(Stmt* stmt, int indent);

static void printExprIndent(Expr* expr, int indent) {
    if (expr == NULL) {
        printIndent(indent);
        printf("<null>\n");
        return;
    }

    printIndent(indent);
    switch (expr->kind) {
        case EXPR_LITERAL:
            printf("Literal: ");
            printValue(expr->as.literal.value);
            printf("\n");
            break;
        case EXPR_UNARY:
            printf("Unary: %.*s\n", expr->as.unary.op.length, expr->as.unary.op.start);
            printExprIndent(expr->as.unary.operand, indent + 1);
            break;
        case EXPR_BINARY:
            printf("Binary: %.*s\n", expr->as.binary.op.length, expr->as.binary.op.start);
            printExprIndent(expr->as.binary.left, indent + 1);
            printExprIndent(expr->as.binary.right, indent + 1);
            break;
        case EXPR_GROUPING:
            printf("Grouping:\n");
            printExprIndent(expr->as.grouping.expression, indent + 1);
            break;
        case EXPR_VARIABLE:
            printf("Variable: %.*s\n", expr->as.variable.name.length,
                   expr->as.variable.name.start);
            break;
        case EXPR_ASSIGN:
            printf("Assign: %.*s\n", expr->as.assign.name.length,
                   expr->as.assign.name.start);
            printExprIndent(expr->as.assign.value, indent + 1);
            break;
        case EXPR_LOGICAL:
            printf("Logical: %.*s\n", expr->as.logical.op.length,
                   expr->as.logical.op.start);
            printExprIndent(expr->as.logical.left, indent + 1);
            printExprIndent(expr->as.logical.right, indent + 1);
            break;
        case EXPR_NULL_COALESCE:
            printf("NullCoalesce: ??\n");
            printExprIndent(expr->as.null_coalesce.left, indent + 1);
            printExprIndent(expr->as.null_coalesce.right, indent + 1);
            break;
        case EXPR_CALL:
            printf("Call:\n");
            printIndent(indent + 1);
            printf("Callee:\n");
            printExprIndent(expr->as.call.callee, indent + 2);
            printIndent(indent + 1);
            printf("Args: %d\n", expr->as.call.argCount);
            for (int i = 0; i < expr->as.call.argCount; i++) {
                printExprIndent(expr->as.call.arguments[i], indent + 2);
            }
            break;
        case EXPR_LAMBDA:
            printf("Lambda: (");
            for (int i = 0; i < expr->as.lambda.paramCount; i++) {
                if (i > 0) printf(", ");
                printf("%.*s", expr->as.lambda.params[i].length,
                       expr->as.lambda.params[i].start);
            }
            printf(")\n");
            if (expr->as.lambda.isBlockBody) {
                printIndent(indent + 1);
                printf("Body (block):\n");
                printStmtIndent(expr->as.lambda.blockBody, indent + 2);
            } else {
                printExprIndent(expr->as.lambda.body, indent + 1);
            }
            break;
        case EXPR_ARRAY:
            printf("Array: %d elements\n", expr->as.array.elementCount);
            for (int i = 0; i < expr->as.array.elementCount; i++) {
                printExprIndent(expr->as.array.elements[i], indent + 1);
            }
            break;
        case EXPR_SPREAD:
            printf("Spread:\n");
            printExprIndent(expr->as.spread.operand, indent + 1);
            break;
        case EXPR_INDEX:
            printf("Index:\n");
            printExprIndent(expr->as.index.object, indent + 1);
            printIndent(indent + 1);
            printf("At:\n");
            printExprIndent(expr->as.index.index, indent + 2);
            break;
        case EXPR_INDEX_SET:
            printf("IndexSet:\n");
            printExprIndent(expr->as.index_set.object, indent + 1);
            printIndent(indent + 1);
            printf("At:\n");
            printExprIndent(expr->as.index_set.index, indent + 2);
            printIndent(indent + 1);
            printf("Value:\n");
            printExprIndent(expr->as.index_set.value, indent + 2);
            break;
        case EXPR_GET:
            printf("Get: .%.*s\n", expr->as.get.name.length, expr->as.get.name.start);
            printExprIndent(expr->as.get.object, indent + 1);
            break;
        case EXPR_SET:
            printf("Set: .%.*s\n", expr->as.set.name.length, expr->as.set.name.start);
            printExprIndent(expr->as.set.object, indent + 1);
            printIndent(indent + 1);
            printf("Value:\n");
            printExprIndent(expr->as.set.value, indent + 2);
            break;
        case EXPR_THIS:
            printf("This\n");
            break;
        case EXPR_SUPER:
            printf("Super: .%.*s\n", expr->as.super_.method.length,
                   expr->as.super_.method.start);
            break;
        case EXPR_MATCH:
            printf("Match:\n");
            printIndent(indent + 1);
            printf("Value:\n");
            printExprIndent(expr->as.match.matchValue, indent + 2);
            for (int i = 0; i < expr->as.match.caseCount; i++) {
                printIndent(indent + 1);
                if (expr->as.match.cases[i].isWildcard) {
                    printf("Case: _\n");
                } else {
                    printf("Case:\n");
                    printExprIndent(expr->as.match.cases[i].pattern, indent + 2);
                }
                printIndent(indent + 2);
                printf("Value:\n");
                printExprIndent(expr->as.match.cases[i].value, indent + 3);
            }
            break;
    }
}

static void printStmtIndent(Stmt* stmt, int indent) {
    if (stmt == NULL) {
        printIndent(indent);
        printf("<null>\n");
        return;
    }

    printIndent(indent);
    switch (stmt->kind) {
        case STMT_EXPRESSION:
            printf("ExprStmt:\n");
            printExprIndent(stmt->as.expression.expression, indent + 1);
            break;
        case STMT_PRINT:
            printf("Print:\n");
            printExprIndent(stmt->as.print.expression, indent + 1);
            break;
        case STMT_VAR:
            printf("%s %.*s: ", stmt->as.var.isConst ? "Const" : "Let",
                   stmt->as.var.name.length, stmt->as.var.name.start);
            printTypeNode(stmt->as.var.typeAnnotation);
            printf("\n");
            if (stmt->as.var.initializer) {
                printExprIndent(stmt->as.var.initializer, indent + 1);
            }
            break;
        case STMT_BLOCK:
            printf("Block: %d statements\n", stmt->as.block.count);
            for (int i = 0; i < stmt->as.block.count; i++) {
                printStmtIndent(stmt->as.block.statements[i], indent + 1);
            }
            break;
        case STMT_IF:
            printf("If:\n");
            printIndent(indent + 1);
            printf("Condition:\n");
            printExprIndent(stmt->as.if_.condition, indent + 2);
            printIndent(indent + 1);
            printf("Then:\n");
            printStmtIndent(stmt->as.if_.thenBranch, indent + 2);
            if (stmt->as.if_.elseBranch) {
                printIndent(indent + 1);
                printf("Else:\n");
                printStmtIndent(stmt->as.if_.elseBranch, indent + 2);
            }
            break;
        case STMT_WHILE:
            printf("While:\n");
            printIndent(indent + 1);
            printf("Condition:\n");
            printExprIndent(stmt->as.while_.condition, indent + 2);
            printIndent(indent + 1);
            printf("Body:\n");
            printStmtIndent(stmt->as.while_.body, indent + 2);
            break;
        case STMT_FOR:
            printf("For: %.*s in\n", stmt->as.for_.variable.length,
                   stmt->as.for_.variable.start);
            printIndent(indent + 1);
            printf("Iterable:\n");
            printExprIndent(stmt->as.for_.iterable, indent + 2);
            printIndent(indent + 1);
            printf("Body:\n");
            printStmtIndent(stmt->as.for_.body, indent + 2);
            break;
        case STMT_FUNCTION:
            printf("Function: %.*s(", stmt->as.function.name.length,
                   stmt->as.function.name.start);
            for (int i = 0; i < stmt->as.function.paramCount; i++) {
                if (i > 0) printf(", ");
                printf("%.*s: ", stmt->as.function.params[i].length,
                       stmt->as.function.params[i].start);
                printTypeNode(stmt->as.function.paramTypes[i]);
            }
            printf(") -> ");
            printTypeNode(stmt->as.function.returnType);
            printf("\n");
            printStmtIndent(stmt->as.function.body, indent + 1);
            break;
        case STMT_RETURN:
            printf("Return:\n");
            if (stmt->as.return_.value) {
                printExprIndent(stmt->as.return_.value, indent + 1);
            }
            break;
        case STMT_CLASS:
            printf("Class: %.*s", stmt->as.class_.name.length,
                   stmt->as.class_.name.start);
            if (stmt->as.class_.typeParamCount > 0) {
                printf("<");
                for (int i = 0; i < stmt->as.class_.typeParamCount; i++) {
                    if (i > 0) printf(", ");
                    printf("%.*s", stmt->as.class_.typeParams[i].length,
                           stmt->as.class_.typeParams[i].start);
                }
                printf(">");
            }
            if (stmt->as.class_.superclassType != NULL) {
                printf(" extends ");
                printTypeNode(stmt->as.class_.superclassType);
            }
            if (stmt->as.class_.implementsCount > 0) {
                printf(" implements ");
                for (int i = 0; i < stmt->as.class_.implementsCount; i++) {
                    if (i > 0) printf(", ");
                    printf("%.*s", stmt->as.class_.implementsNames[i].length,
                           stmt->as.class_.implementsNames[i].start);
                }
            }
            printf("\n");
            for (int i = 0; i < stmt->as.class_.fieldCount; i++) {
                printIndent(indent + 1);
                printf("Field: %.*s: ", stmt->as.class_.fields[i].name.length,
                       stmt->as.class_.fields[i].name.start);
                printTypeNode(stmt->as.class_.fields[i].type);
                printf("\n");
            }
            for (int i = 0; i < stmt->as.class_.methodCount; i++) {
                printIndent(indent + 1);
                printf("Method: %.*s\n", stmt->as.class_.methods[i].name.length,
                       stmt->as.class_.methods[i].name.start);
            }
            break;
        case STMT_INTERFACE:
            printf("Interface: %.*s\n", stmt->as.interface_.name.length,
                   stmt->as.interface_.name.start);
            for (int i = 0; i < stmt->as.interface_.methodCount; i++) {
                printIndent(indent + 1);
                printf("fn %.*s(...)\n", stmt->as.interface_.methods[i].name.length,
                       stmt->as.interface_.methods[i].name.start);
            }
            break;
        case STMT_TYPE_ALIAS:
            printf("Type alias: %.*s = ", stmt->as.type_alias.name.length,
                   stmt->as.type_alias.name.start);
            printTypeNode(stmt->as.type_alias.target);
            printf("\n");
            break;
        case STMT_ENUM:
            printf("Enum: %.*s\n", stmt->as.enum_.name.length,
                   stmt->as.enum_.name.start);
            for (int i = 0; i < stmt->as.enum_.variantCount; i++) {
                printIndent(indent + 1);
                printf("Variant: %.*s", stmt->as.enum_.variants[i].name.length,
                       stmt->as.enum_.variants[i].name.start);
                if (stmt->as.enum_.variants[i].fieldCount > 0) {
                    printf("(");
                    for (int j = 0; j < stmt->as.enum_.variants[i].fieldCount; j++) {
                        if (j > 0) printf(", ");
                        printTypeNode(stmt->as.enum_.variants[i].fieldTypes[j]);
                    }
                    printf(")");
                }
                printf("\n");
            }
            break;
        case STMT_MATCH:
            printf("Match:\n");
            printIndent(indent + 1);
            printf("Value:\n");
            printExprIndent(stmt->as.match.value, indent + 2);
            for (int i = 0; i < stmt->as.match.caseCount; i++) {
                printIndent(indent + 1);
                if (stmt->as.match.cases[i].isWildcard) {
                    printf("Case: _\n");
                } else {
                    printf("Case:\n");
                    printExprIndent(stmt->as.match.cases[i].pattern, indent + 2);
                }
                printIndent(indent + 2);
                printf("Body:\n");
                printStmtIndent(stmt->as.match.cases[i].body, indent + 3);
            }
            break;
        case STMT_TRY:
            printf("Try:\n");
            printIndent(indent + 1);
            printf("Body:\n");
            printStmtIndent(stmt->as.try_.tryBody, indent + 2);
            printIndent(indent + 1);
            printf("Catch (%.*s):\n", stmt->as.try_.catchVar.length,
                   stmt->as.try_.catchVar.start);
            printStmtIndent(stmt->as.try_.catchBody, indent + 2);
            break;
        case STMT_THROW:
            printf("Throw:\n");
            printExprIndent(stmt->as.throw_.value, indent + 1);
            break;
        case STMT_IMPORT:
            printf("Import: \"%.*s\"", stmt->as.import.path.length - 2,
                   stmt->as.import.path.start + 1);  // Strip quotes
            if (stmt->as.import.nameCount > 0) {
                printf(" { ");
                for (int i = 0; i < stmt->as.import.nameCount; i++) {
                    if (i > 0) printf(", ");
                    printf("%.*s", stmt->as.import.names[i].length,
                           stmt->as.import.names[i].start);
                }
                printf(" }");
            }
            printf("\n");
            break;
    }
}

void printExpr(Expr* expr) {
    printExprIndent(expr, 0);
}

void printStmt(Stmt* stmt) {
    printStmtIndent(stmt, 0);
}
