/* Builds synthetic AST for each generic instantiation (type args → TypeNodes,
 * duplicated method bodies referencing the monomorph name). */

#include "generic.h"
#include "memory.h"
#include "types.h"
#include <string.h>

static Token identToken(const char* s, int line) {
    Token t;
    t.type = TOKEN_IDENTIFIER;
    t.start = s;
    t.length = (int)strlen(s);
    t.line = line;
    return t;
}

static TypeNode* typeToTypeNode(Type* t, int line) {
    if (t == NULL) return NULL;
    switch (t->kind) {
        case TYPE_NIL:
            return newSimpleTypeNode(identToken("nil", line));
        case TYPE_BOOL:
            return newSimpleTypeNode(identToken("bool", line));
        case TYPE_INT:
            return newSimpleTypeNode(identToken("int", line));
        case TYPE_FLOAT:
            return newSimpleTypeNode(identToken("float", line));
        case TYPE_STRING:
            return newSimpleTypeNode(identToken("string", line));
        case TYPE_GENERIC_INST: {
            Type* tpl = t->as.genericInst.genericType;
            if (tpl == NULL || tpl->kind != TYPE_GENERIC_CLASS_TEMPLATE) {
                return NULL;
            }
            GenericClassTemplate* g = tpl->as.genericClassTemplate;
            Token nameTok;
            nameTok.type = TOKEN_IDENTIFIER;
            nameTok.start = g->name;
            nameTok.length = g->nameLength;
            nameTok.line = line;
            TypeNode** args = ALLOCATE(TypeNode*, t->as.genericInst.typeArgCount);
            for (int i = 0; i < t->as.genericInst.typeArgCount; i++) {
                args[i] = typeToTypeNode(t->as.genericInst.typeArgs[i], line);
            }
            return newGenericTypeNode(nameTok, args, t->as.genericInst.typeArgCount);
        }
        default:
            return NULL;
    }
}

static TypeNode* cloneTypeNode(TypeNode* node) {
    if (node == NULL) return NULL;
    switch (node->kind) {
        case TYPE_NODE_SIMPLE:
            return newSimpleTypeNode(node->as.simple.name);
        case TYPE_NODE_ARRAY:
            return newArrayTypeNode(cloneTypeNode(node->as.array.elementType), node->token);
        case TYPE_NODE_FUNCTION: {
            int pc = node->as.function.paramCount;
            TypeNode** pts = NULL;
            if (pc > 0) {
                pts = ALLOCATE(TypeNode*, pc);
                for (int i = 0; i < pc; i++) {
                    pts[i] = cloneTypeNode(node->as.function.paramTypes[i]);
                }
            }
            return newFunctionTypeNode(pts, pc, cloneTypeNode(node->as.function.returnType),
                                       node->token);
        }
        case TYPE_NODE_OPTIONAL:
            return newOptionalTypeNode(cloneTypeNode(node->as.optional.innerType), node->token);
        case TYPE_NODE_UNION: {
            int tc = node->as.unionType.typeCount;
            TypeNode** ts = ALLOCATE(TypeNode*, tc);
            for (int i = 0; i < tc; i++) {
                ts[i] = cloneTypeNode(node->as.unionType.types[i]);
            }
            return newUnionTypeNode(ts, tc, node->token);
        }
        case TYPE_NODE_GENERIC: {
            int ac = node->as.generic.typeArgCount;
            TypeNode** ta = ALLOCATE(TypeNode*, ac);
            for (int i = 0; i < ac; i++) {
                ta[i] = cloneTypeNode(node->as.generic.typeArgs[i]);
            }
            return newGenericTypeNode(node->as.generic.name, ta, ac);
        }
    }
    return NULL;
}

static TypeNode* substituteTypeNode(TypeNode* node, ClassStmt* cs, Type** repl, int line) {
    if (node == NULL) return NULL;
    switch (node->kind) {
        case TYPE_NODE_SIMPLE: {
            Token name = node->as.simple.name;
            for (int i = 0; i < cs->typeParamCount; i++) {
                if (name.length == cs->typeParams[i].length &&
                    memcmp(name.start, cs->typeParams[i].start, name.length) == 0) {
                    TypeNode* tn = typeToTypeNode(repl[i], line);
                    if (tn != NULL) return tn;
                    return cloneTypeNode(node);
                }
            }
            return cloneTypeNode(node);
        }
        case TYPE_NODE_ARRAY:
            return newArrayTypeNode(
                substituteTypeNode(node->as.array.elementType, cs, repl, line), node->token);
        case TYPE_NODE_FUNCTION: {
            int pc = node->as.function.paramCount;
            TypeNode** pts = NULL;
            if (pc > 0) {
                pts = ALLOCATE(TypeNode*, pc);
                for (int i = 0; i < pc; i++) {
                    pts[i] = substituteTypeNode(node->as.function.paramTypes[i], cs, repl, line);
                }
            }
            return newFunctionTypeNode(pts, pc,
                                       substituteTypeNode(node->as.function.returnType, cs, repl, line),
                                       node->token);
        }
        case TYPE_NODE_OPTIONAL:
            return newOptionalTypeNode(
                substituteTypeNode(node->as.optional.innerType, cs, repl, line), node->token);
        case TYPE_NODE_UNION: {
            int tc = node->as.unionType.typeCount;
            TypeNode** ts = ALLOCATE(TypeNode*, tc);
            for (int i = 0; i < tc; i++) {
                ts[i] = substituteTypeNode(node->as.unionType.types[i], cs, repl, line);
            }
            return newUnionTypeNode(ts, tc, node->token);
        }
        case TYPE_NODE_GENERIC: {
            int ac = node->as.generic.typeArgCount;
            TypeNode** ta = ALLOCATE(TypeNode*, ac);
            for (int i = 0; i < ac; i++) {
                ta[i] = substituteTypeNode(node->as.generic.typeArgs[i], cs, repl, line);
            }
            return newGenericTypeNode(node->as.generic.name, ta, ac);
        }
    }
    return NULL;
}

static Stmt* lowerOneInstance(TypeChecker* checker, Type* inst, Stmt* templateStmt) {
    ClassStmt* cs = &templateStmt->as.class_;
    char* nameBuf = ALLOCATE(char, 512);
    mangleGenericInstType(inst, nameBuf, 512);
    int nameLen = (int)strlen(nameBuf);
    Token className;
    className.type = TOKEN_IDENTIFIER;
    className.start = nameBuf;
    className.length = nameLen;
    className.line = templateStmt->line;

    Type** repl = inst->as.genericInst.typeArgs;

    FieldDecl* fields = NULL;
    if (cs->fieldCount > 0) {
        fields = ALLOCATE(FieldDecl, cs->fieldCount);
        for (int i = 0; i < cs->fieldCount; i++) {
            fields[i].name = cs->fields[i].name;
            fields[i].type = substituteTypeNode(cs->fields[i].type, cs, repl, templateStmt->line);
        }
    }

    FunctionStmt* methods = NULL;
    if (cs->methodCount > 0) {
        methods = ALLOCATE(FunctionStmt, cs->methodCount);
        for (int i = 0; i < cs->methodCount; i++) {
            FunctionStmt* m = &cs->methods[i];
            methods[i].name = m->name;
            methods[i].typeParams = NULL;
            methods[i].typeParamCount = 0;
            methods[i].params = m->params;
            methods[i].paramCount = m->paramCount;
            if (m->type != NULL) {
                FunctionType* ft = &m->type->as.function;
                Type** subParams = NULL;
                if (m->paramCount > 0) {
                    subParams = ALLOCATE(Type*, m->paramCount);
                    for (int j = 0; j < m->paramCount; j++) {
                        subParams[j] = substituteTypeInType(ft->paramTypes[j], repl,
                                                            cs->typeParamCount);
                    }
                }
                Type* subRet = substituteTypeInType(ft->returnType, repl, cs->typeParamCount);
                methods[i].type = createFunctionType(subParams, m->paramCount, subRet);
            } else {
                methods[i].type = NULL;
            }
            if (m->paramCount > 0) {
                methods[i].paramTypes = ALLOCATE(TypeNode*, m->paramCount);
                for (int j = 0; j < m->paramCount; j++) {
                    methods[i].paramTypes[j] = substituteTypeNode(m->paramTypes[j], cs, repl,
                                                                    templateStmt->line);
                }
            } else {
                methods[i].paramTypes = NULL;
            }
            methods[i].returnType = m->returnType == NULL
                ? NULL
                : substituteTypeNode(m->returnType, cs, repl, templateStmt->line);
            methods[i].body = m->body;
        }
    }

    Type* superResolved = NULL;
    if (cs->superclassType != NULL) {
        TypeNode* subSuper = substituteTypeNode(cs->superclassType, cs, repl, templateStmt->line);
        if (subSuper != NULL) {
            superResolved = resolveTypeNode(checker, subSuper);
            freeTypeNode(subSuper);
        }
    }

    Stmt* stmt = newClassStmt(className, NULL, 0, NULL, NULL, NULL, NULL, 0, fields, cs->fieldCount,
                              methods, cs->methodCount);
    stmt->as.class_.type = createClassType(className.start, className.length);
    stmt->as.class_.superclassResolved = superResolved;
    return stmt;
}

void freeLoweredMonomorphClassStmt(Stmt* stmt) {
    if (stmt == NULL || stmt->kind != STMT_CLASS) return;
    ClassStmt* c = &stmt->as.class_;

    if (c->name.start != NULL) {
        FREE_ARRAY(char, (char*)c->name.start, 512);
    }
    freeType(c->type);

    for (int i = 0; i < c->fieldCount; i++) {
        freeTypeNode(c->fields[i].type);
    }
    FREE_ARRAY(FieldDecl, c->fields, c->fieldCount);

    for (int i = 0; i < c->methodCount; i++) {
        FunctionStmt* m = &c->methods[i];
        FREE_ARRAY(Token, m->typeParams, m->typeParamCount);
        FREE_ARRAY(Token, m->params, m->paramCount);
        for (int j = 0; j < m->paramCount; j++) {
            freeTypeNode(m->paramTypes[j]);
        }
        FREE_ARRAY(TypeNode*, m->paramTypes, m->paramCount);
        freeTypeNode(m->returnType);
        freeType(m->type);
        // m->body is shared with the generic template — do not free
    }
    FREE_ARRAY(FunctionStmt, c->methods, c->methodCount);
    FREE(Stmt, stmt);
}

bool lowerGenericClasses(TypeChecker* checker, Stmt*** outLowered, int* outCount) {
    *outLowered = NULL;
    *outCount = 0;
    if (checker->genericInstCount == 0) {
        return true;
    }

    Stmt** arr = ALLOCATE(Stmt*, checker->genericInstCount);
    for (int i = 0; i < checker->genericInstCount; i++) {
        arr[i] = lowerOneInstance(checker, checker->genericInsts[i].instType,
                                  checker->genericInsts[i].templateStmt);
        if (arr[i] == NULL) {
            for (int j = 0; j < i; j++) {
                freeStmt(arr[j]);
            }
            FREE_ARRAY(Stmt*, arr, checker->genericInstCount);
            return false;
        }
    }
    *outLowered = arr;
    *outCount = checker->genericInstCount;
    return true;
}
