/* Type constructors, copy/substitution, assignability, equality, and display.
 * Shared by the typechecker and compiler (e.g. return/slot types). */

#include "types.h"
#include "memory.h"
#include <stdio.h>
#include <string.h>

#define BLAZE_TYPE_RECURSION_MAX 64

// Built-in type singletons (no need to allocate these)
Type TYPE_NIL_INSTANCE = {TYPE_NIL, {0}};
Type TYPE_BOOL_INSTANCE = {TYPE_BOOL, {0}};
Type TYPE_INT_INSTANCE = {TYPE_INT, {0}};
Type TYPE_FLOAT_INSTANCE = {TYPE_FLOAT, {0}};
Type TYPE_STRING_INSTANCE = {TYPE_STRING, {0}};
Type TYPE_UNKNOWN_INSTANCE = {TYPE_UNKNOWN, {0}};
Type TYPE_ERROR_INSTANCE = {TYPE_ERROR, {0}};

Type* createNilType(void) {
    return &TYPE_NIL_INSTANCE;
}

Type* createBoolType(void) {
    return &TYPE_BOOL_INSTANCE;
}

Type* createIntType(void) {
    return &TYPE_INT_INSTANCE;
}

Type* createFloatType(void) {
    return &TYPE_FLOAT_INSTANCE;
}

Type* createStringType(void) {
    return &TYPE_STRING_INSTANCE;
}

Type* createUnknownType(void) {
    return &TYPE_UNKNOWN_INSTANCE;
}

Type* createErrorType(void) {
    return &TYPE_ERROR_INSTANCE;
}

Type* createTypeParamType(const char* name, int nameLength, int index) {
    Type* type = ALLOCATE(Type, 1);
    type->kind = TYPE_TYPE_PARAM;
    type->as.typeParam.name = name;
    type->as.typeParam.nameLength = nameLength;
    type->as.typeParam.index = index;
    return type;
}

Type* createGenericClassTemplateType(const char* name, int nameLength, void* classStmt) {
    GenericClassTemplate* g = ALLOCATE(GenericClassTemplate, 1);
    g->name = name;
    g->nameLength = nameLength;
    g->classStmt = classStmt;
    g->variances = NULL;
    g->varianceCount = 0;

    Type* type = ALLOCATE(Type, 1);
    type->kind = TYPE_GENERIC_CLASS_TEMPLATE;
    type->as.genericClassTemplate = g;
    return type;
}

void attachGenericTemplateVariances(Type* templateType, const TypeParamVariance* variances, int count) {
    if (templateType == NULL || templateType->kind != TYPE_GENERIC_CLASS_TEMPLATE) return;
    GenericClassTemplate* g = templateType->as.genericClassTemplate;
    if (g->variances != NULL) {
        FREE_ARRAY(TypeParamVariance, g->variances, g->varianceCount);
        g->variances = NULL;
        g->varianceCount = 0;
    }
    if (count <= 0 || variances == NULL) return;
    TypeParamVariance* copy = ALLOCATE(TypeParamVariance, count);
    memcpy(copy, variances, sizeof(TypeParamVariance) * (size_t)count);
    g->variances = copy;
    g->varianceCount = count;
}

Type* createGenericInstType(Type* templateType, Type** typeArgs, int typeArgCount) {
    if (templateType == NULL || templateType->kind != TYPE_GENERIC_CLASS_TEMPLATE) {
        return createErrorType();
    }
    Type** args = NULL;
    if (typeArgCount > 0) {
        args = ALLOCATE(Type*, typeArgCount);
        for (int i = 0; i < typeArgCount; i++) {
            args[i] = typeArgs[i];
        }
    }
    Type* type = ALLOCATE(Type, 1);
    type->kind = TYPE_GENERIC_INST;
    type->as.genericInst.genericType = templateType;
    type->as.genericInst.typeArgs = args;
    type->as.genericInst.typeArgCount = typeArgCount;
    return type;
}

static void appendMangledArg(char* buf, size_t bufSize, size_t* off, Type* t);

void mangleGenericInstType(Type* instType, char* buf, size_t bufSize) {
    if (bufSize == 0) return;
    buf[0] = '\0';
    if (instType == NULL || instType->kind != TYPE_GENERIC_INST) return;

    Type* tpl = instType->as.genericInst.genericType;
    if (tpl == NULL || tpl->kind != TYPE_GENERIC_CLASS_TEMPLATE) return;

    GenericClassTemplate* g = tpl->as.genericClassTemplate;
    size_t off = 0;
    int n = snprintf(buf + off, bufSize - off, "%.*s", g->nameLength, g->name);
    if (n < 0) return;
    off = strlen(buf);

    for (int i = 0; i < instType->as.genericInst.typeArgCount; i++) {
        if (off + 2 >= bufSize) break;
        buf[off++] = '_';
        buf[off++] = '_';
        buf[off] = '\0';
        appendMangledArg(buf, bufSize, &off, instType->as.genericInst.typeArgs[i]);
    }
}

static void appendMangledArg(char* buf, size_t bufSize, size_t* off, Type* t) {
    if (t == NULL || *off >= bufSize - 1) return;

    switch (t->kind) {
        case TYPE_NIL:
            snprintf(buf + *off, bufSize - *off, "nil");
            break;
        case TYPE_BOOL:
            snprintf(buf + *off, bufSize - *off, "bool");
            break;
        case TYPE_INT:
            snprintf(buf + *off, bufSize - *off, "int");
            break;
        case TYPE_FLOAT:
            snprintf(buf + *off, bufSize - *off, "float");
            break;
        case TYPE_STRING:
            snprintf(buf + *off, bufSize - *off, "string");
            break;
        case TYPE_GENERIC_INST: {
            char tmp[512];
            mangleGenericInstType(t, tmp, sizeof(tmp));
            snprintf(buf + *off, bufSize - *off, "%s", tmp);
            break;
        }
        default:
            snprintf(buf + *off, bufSize - *off, "unknown");
            break;
    }
    *off = strlen(buf);
}

static Type* substituteTypeInTypeDepth(Type* type, Type** replacements, int typeParamCount, int depth) {
    if (type == NULL) return NULL;
    if (depth <= 0) return type;

    switch (type->kind) {
        case TYPE_TYPE_PARAM:
            if (type->as.typeParam.index >= 0 &&
                type->as.typeParam.index < typeParamCount &&
                replacements[type->as.typeParam.index] != NULL) {
                return replacements[type->as.typeParam.index];
            }
            return type;

        case TYPE_ARRAY: {
            Type* inner = substituteTypeInTypeDepth(type->as.array.elementType, replacements,
                                                    typeParamCount, depth - 1);
            return createArrayType(inner);
        }

        case TYPE_FUNCTION: {
            FunctionType* fn = &type->as.function;
            Type** paramTypes = NULL;
            if (fn->paramCount > 0) {
                paramTypes = ALLOCATE(Type*, fn->paramCount);
                for (int i = 0; i < fn->paramCount; i++) {
                    paramTypes[i] = substituteTypeInTypeDepth(fn->paramTypes[i], replacements,
                                                              typeParamCount, depth - 1);
                }
            }
            Type* ret = substituteTypeInTypeDepth(fn->returnType, replacements, typeParamCount, depth - 1);
            return createFunctionType(paramTypes, fn->paramCount, ret);
        }

        case TYPE_OPTIONAL: {
            Type* inner = substituteTypeInTypeDepth(type->as.optional.innerType, replacements,
                                                    typeParamCount, depth - 1);
            return createOptionalType(inner);
        }

        case TYPE_UNION: {
            UnionType* u = &type->as.unionType;
            Type** types = ALLOCATE(Type*, u->typeCount);
            for (int i = 0; i < u->typeCount; i++) {
                types[i] = substituteTypeInTypeDepth(u->types[i], replacements, typeParamCount, depth - 1);
            }
            return createUnionType(types, u->typeCount);
        }

        case TYPE_GENERIC_INST: {
            GenericInst* gi = &type->as.genericInst;
            Type** args = ALLOCATE(Type*, gi->typeArgCount);
            for (int i = 0; i < gi->typeArgCount; i++) {
                args[i] = substituteTypeInTypeDepth(gi->typeArgs[i], replacements, typeParamCount, depth - 1);
            }
            return createGenericInstType(gi->genericType, args, gi->typeArgCount);
        }

        default:
            return type;
    }
}

Type* substituteTypeInType(Type* type, Type** replacements, int typeParamCount) {
    return substituteTypeInTypeDepth(type, replacements, typeParamCount, BLAZE_TYPE_RECURSION_MAX);
}

Type* createArrayType(Type* elementType) {
    Type* type = ALLOCATE(Type, 1);
    type->kind = TYPE_ARRAY;
    type->as.array.elementType = elementType;
    return type;
}

Type* createFunctionType(Type** paramTypes, int paramCount, Type* returnType) {
    Type* type = ALLOCATE(Type, 1);
    type->kind = TYPE_FUNCTION;
    type->as.function.paramTypes = paramTypes;
    type->as.function.paramCount = paramCount;
    type->as.function.returnType = returnType;
    return type;
}

Type* createClassType(const char* name, int nameLength) {
    Type* type = ALLOCATE(Type, 1);
    type->kind = TYPE_CLASS;
    type->as.classType = ALLOCATE(ClassType, 1);
    type->as.classType->name = name;
    type->as.classType->nameLength = nameLength;
    type->as.classType->superclass = NULL;
    type->as.classType->implementedInterfaces = NULL;
    type->as.classType->implementedInterfaceCount = 0;
    return type;
}

Type* createInterfaceType(const char* name, int nameLength,
                          const char** methodNameStarts, int* methodNameLengths,
                          Type** methodSignatures, int methodCount) {
    char* iname = ALLOCATE(char, (size_t)nameLength + 1);
    memcpy(iname, name, (size_t)nameLength);
    iname[nameLength] = '\0';

    const char** mstarts = ALLOCATE(const char*, methodCount);
    int* mlens = ALLOCATE(int, methodCount);
    for (int i = 0; i < methodCount; i++) {
        int len = methodNameLengths[i];
        char* mc = ALLOCATE(char, (size_t)len + 1);
        memcpy(mc, methodNameStarts[i], (size_t)len);
        mc[len] = '\0';
        mstarts[i] = mc;
        mlens[i] = len;
    }

    Type* type = ALLOCATE(Type, 1);
    type->kind = TYPE_INTERFACE;
    type->as.interfaceType = ALLOCATE(InterfaceType, 1);
    type->as.interfaceType->name = iname;
    type->as.interfaceType->nameLength = nameLength;
    type->as.interfaceType->methodNameStarts = mstarts;
    type->as.interfaceType->methodNameLengths = mlens;
    type->as.interfaceType->methodSignatures = methodSignatures;
    type->as.interfaceType->methodCount = methodCount;
    return type;
}

Type* createOptionalType(Type* innerType) {
    // Don't nest optionals: (T?)? should just be T?
    if (innerType->kind == TYPE_OPTIONAL) {
        return innerType;
    }
    Type* type = ALLOCATE(Type, 1);
    type->kind = TYPE_OPTIONAL;
    type->as.optional.innerType = innerType;
    return type;
}

Type* createUnionType(Type** types, int typeCount) {
    // Flatten nested unions and remove duplicates
    Type** flatTypes = ALLOCATE(Type*, typeCount * 2);  // Allocate extra space for flattening
    int flatCount = 0;

    for (int i = 0; i < typeCount; i++) {
        Type* t = types[i];

        // If this is a union, add its members
        if (t->kind == TYPE_UNION) {
            for (int j = 0; j < t->as.unionType.typeCount; j++) {
                Type* member = t->as.unionType.types[j];
                // Check if already in flatTypes
                bool found = false;
                for (int k = 0; k < flatCount; k++) {
                    if (typesEqual(flatTypes[k], member)) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    flatTypes[flatCount++] = member;
                }
            }
        } else {
            // Check if already in flatTypes
            bool found = false;
            for (int k = 0; k < flatCount; k++) {
                if (typesEqual(flatTypes[k], t)) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                flatTypes[flatCount++] = t;
            }
        }
    }

    // If only one type remains, return it directly
    if (flatCount == 1) {
        Type* result = flatTypes[0];
        FREE_ARRAY(Type*, flatTypes, typeCount * 2);
        return result;
    }

    // Allocate exact size array for final types
    Type** finalTypes = ALLOCATE(Type*, flatCount);
    for (int i = 0; i < flatCount; i++) {
        finalTypes[i] = flatTypes[i];
    }
    FREE_ARRAY(Type*, flatTypes, typeCount * 2);

    Type* type = ALLOCATE(Type, 1);
    type->kind = TYPE_UNION;
    type->as.unionType.types = finalTypes;
    type->as.unionType.typeCount = flatCount;
    return type;
}

Type* unwrapOptionalType(Type* type) {
    if (type->kind == TYPE_OPTIONAL) {
        return type->as.optional.innerType;
    }
    return type;
}

bool isOptionalType(Type* type) {
    return type->kind == TYPE_OPTIONAL;
}

bool isUnionType(Type* type) {
    return type->kind == TYPE_UNION;
}

bool typeIsInUnion(Type* type, Type* unionType) {
    if (unionType->kind != TYPE_UNION) return false;
    for (int i = 0; i < unionType->as.unionType.typeCount; i++) {
        if (typesEqual(type, unionType->as.unionType.types[i])) {
            return true;
        }
    }
    return false;
}

static bool typesEqualDepth(Type* a, Type* b, int depth) {
    if (a == b) return true;
    if (a == NULL || b == NULL) return false;
    if (depth <= 0) return false;
    if (a->kind != b->kind) return false;

    switch (a->kind) {
        case TYPE_NIL:
        case TYPE_BOOL:
        case TYPE_INT:
        case TYPE_FLOAT:
        case TYPE_STRING:
        case TYPE_UNKNOWN:
        case TYPE_ERROR:
            return true;

        case TYPE_ARRAY:
            return typesEqualDepth(a->as.array.elementType, b->as.array.elementType, depth - 1);

        case TYPE_FUNCTION: {
            FunctionType* fa = &a->as.function;
            FunctionType* fb = &b->as.function;
            if (fa->paramCount != fb->paramCount) return false;
            if (!typesEqualDepth(fa->returnType, fb->returnType, depth - 1)) return false;
            for (int i = 0; i < fa->paramCount; i++) {
                if (!typesEqualDepth(fa->paramTypes[i], fb->paramTypes[i], depth - 1)) {
                    return false;
                }
            }
            return true;
        }

        case TYPE_CLASS:
        case TYPE_INSTANCE:
            return a->as.classType == b->as.classType;

        case TYPE_GENERIC_CLASS_TEMPLATE:
            return a->as.genericClassTemplate == b->as.genericClassTemplate;

        case TYPE_OPTIONAL:
            return typesEqualDepth(a->as.optional.innerType, b->as.optional.innerType, depth - 1);

        case TYPE_UNION: {
            // Two unions are equal if they have the same members (order doesn't matter)
            if (a->as.unionType.typeCount != b->as.unionType.typeCount) return false;
            for (int i = 0; i < a->as.unionType.typeCount; i++) {
                bool found = false;
                for (int j = 0; j < b->as.unionType.typeCount; j++) {
                    if (typesEqualDepth(a->as.unionType.types[i], b->as.unionType.types[j], depth - 1)) {
                        found = true;
                        break;
                    }
                }
                if (!found) return false;
            }
            return true;
        }

        case TYPE_TYPE_PARAM:
            return a->as.typeParam.index == b->as.typeParam.index &&
                   a->as.typeParam.nameLength == b->as.typeParam.nameLength &&
                   memcmp(a->as.typeParam.name, b->as.typeParam.name,
                          a->as.typeParam.nameLength) == 0;

        case TYPE_GENERIC_INST: {
            GenericInst* ga = &a->as.genericInst;
            GenericInst* gb = &b->as.genericInst;
            if (ga->genericType != gb->genericType) return false;
            if (ga->typeArgCount != gb->typeArgCount) return false;
            for (int i = 0; i < ga->typeArgCount; i++) {
                if (!typesEqualDepth(ga->typeArgs[i], gb->typeArgs[i], depth - 1)) return false;
            }
            return true;
        }

        case TYPE_INTERFACE: {
            InterfaceType* ia = a->as.interfaceType;
            InterfaceType* ib = b->as.interfaceType;
            if (ia->nameLength != ib->nameLength ||
                memcmp(ia->name, ib->name, (size_t)ia->nameLength) != 0) {
                return false;
            }
            if (ia->methodCount != ib->methodCount) return false;
            for (int i = 0; i < ia->methodCount; i++) {
                if (ia->methodNameLengths[i] != ib->methodNameLengths[i] ||
                    memcmp(ia->methodNameStarts[i], ib->methodNameStarts[i],
                           (size_t)ia->methodNameLengths[i]) != 0) {
                    return false;
                }
                if (!typesEqualDepth(ia->methodSignatures[i], ib->methodSignatures[i], depth - 1)) {
                    return false;
                }
            }
            return true;
        }

        default:
            return false;
    }
}

bool typesEqual(Type* a, Type* b) {
    return typesEqualDepth(a, b, BLAZE_TYPE_RECURSION_MAX);
}

bool typeIsNumeric(Type* type) {
    return type->kind == TYPE_INT || type->kind == TYPE_FLOAT;
}

static bool typeIsAssignableToDepth(Type* source, Type* target, int depth) {
    if (depth <= 0) return false;

    // Same types are always assignable
    if (typesEqual(source, target)) return true;

    // UNKNOWN target accepts any source (wildcard for generic parameters)
    if (target->kind == TYPE_UNKNOWN) return true;

    // UNKNOWN source can be assigned to any target (result of untyped expressions)
    if (source->kind == TYPE_UNKNOWN) return true;

    // Type parameters behave like wildcard placeholders in current implementation.
    if (source->kind == TYPE_TYPE_PARAM || target->kind == TYPE_TYPE_PARAM) return true;

    // int can be assigned to float
    if (source->kind == TYPE_INT && target->kind == TYPE_FLOAT) return true;

    // Array with unknown element type (empty array) can be assigned to any array
    if (source->kind == TYPE_ARRAY && target->kind == TYPE_ARRAY) {
        if (source->as.array.elementType->kind == TYPE_UNKNOWN) return true;
    }

    // Function type compatibility (for lambdas with inferred parameter types)
    if (source->kind == TYPE_FUNCTION && target->kind == TYPE_FUNCTION) {
        FunctionType* srcFn = &source->as.function;
        FunctionType* tgtFn = &target->as.function;

        // Must have same number of parameters
        if (srcFn->paramCount != tgtFn->paramCount) return false;

        // Check parameter types (contravariant, but we allow UNKNOWN)
        for (int i = 0; i < srcFn->paramCount; i++) {
            Type* srcParam = srcFn->paramTypes[i];
            Type* tgtParam = tgtFn->paramTypes[i];
            // If source param is UNKNOWN, it can be inferred from target
            if (srcParam->kind == TYPE_UNKNOWN) continue;
            // Otherwise params should match
            if (!typesEqual(srcParam, tgtParam)) return false;
        }

        // Check return type (covariant, source return must be assignable to target return)
        return typeIsAssignableToDepth(srcFn->returnType, tgtFn->returnType, depth - 1);
    }

    // nil can be assigned to any optional type
    if (source->kind == TYPE_NIL && target->kind == TYPE_OPTIONAL) {
        return true;
    }

    // T can be assigned to T? (non-optional to optional)
    if (target->kind == TYPE_OPTIONAL) {
        return typeIsAssignableToDepth(source, target->as.optional.innerType, depth - 1);
    }

    if (source->kind == TYPE_GENERIC_INST && target->kind == TYPE_GENERIC_INST) {
        GenericInst* sa = &source->as.genericInst;
        GenericInst* ta = &target->as.genericInst;
        if (sa->genericType != ta->genericType) return false;
        if (sa->typeArgCount != ta->typeArgCount) return false;
        Type* tpl = sa->genericType;
        if (tpl == NULL || tpl->kind != TYPE_GENERIC_CLASS_TEMPLATE) return false;
        GenericClassTemplate* g = tpl->as.genericClassTemplate;
        for (int i = 0; i < sa->typeArgCount; i++) {
            TypeParamVariance v = TYPE_PARAM_VAR_INVARIANT;
            if (g->variances != NULL && i < g->varianceCount) {
                v = g->variances[i];
            }
            Type* s = sa->typeArgs[i];
            Type* t = ta->typeArgs[i];
            switch (v) {
                case TYPE_PARAM_VAR_OUT:
                    if (!typeIsAssignableToDepth(s, t, depth - 1)) return false;
                    break;
                case TYPE_PARAM_VAR_IN:
                    if (!typeIsAssignableToDepth(t, s, depth - 1)) return false;
                    break;
                default:
                    if (!typesEqual(s, t)) return false;
                    break;
            }
        }
        return true;
    }

    // Subclass can be assigned to superclass
    if (source->kind == TYPE_INSTANCE && target->kind == TYPE_INSTANCE) {
        ClassType* sourceClass = source->as.classType;
        ClassType* targetClass = target->as.classType;
        while (sourceClass != NULL) {
            if (sourceClass == targetClass) return true;
            sourceClass = sourceClass->superclass;
        }
    }

    // Instance (or class-as-instance) is assignable to interface if the class declared `implements`
    if ((source->kind == TYPE_INSTANCE || source->kind == TYPE_CLASS) &&
        target->kind == TYPE_INTERFACE) {
        ClassType* sourceClass = source->as.classType;
        if (sourceClass != NULL && sourceClass->implementedInterfaces != NULL) {
            for (int i = 0; i < sourceClass->implementedInterfaceCount; i++) {
                if (typesEqual(sourceClass->implementedInterfaces[i], target)) {
                    return true;
                }
            }
        }
        return false;
    }

    // Union type handling
    // T can be assigned to T | U (any type can be assigned to a union containing it)
    if (target->kind == TYPE_UNION) {
        return typeIsInUnion(source, target);
    }

    // T | U can be assigned to V if both T and U can be assigned to V
    if (source->kind == TYPE_UNION) {
        for (int i = 0; i < source->as.unionType.typeCount; i++) {
            if (!typeIsAssignableToDepth(source->as.unionType.types[i], target, depth - 1)) {
                return false;
            }
        }
        return true;
    }

    return false;
}

bool typeIsAssignableTo(Type* source, Type* target) {
    return typeIsAssignableToDepth(source, target, BLAZE_TYPE_RECURSION_MAX);
}

void printType(Type* type) {
    if (type == NULL) {
        printf("<null>");
        return;
    }

    switch (type->kind) {
        case TYPE_NIL:     printf("nil"); break;
        case TYPE_BOOL:    printf("bool"); break;
        case TYPE_INT:     printf("int"); break;
        case TYPE_FLOAT:   printf("float"); break;
        case TYPE_STRING:  printf("string"); break;
        case TYPE_UNKNOWN: printf("unknown"); break;
        case TYPE_ERROR:   printf("error"); break;
        case TYPE_TYPE_PARAM:
            printf("%.*s", type->as.typeParam.nameLength, type->as.typeParam.name);
            break;
        case TYPE_GENERIC_CLASS_TEMPLATE:
            printf("%.*s", type->as.genericClassTemplate->nameLength,
                   type->as.genericClassTemplate->name);
            break;

        case TYPE_GENERIC_INST: {
            Type* tpl = type->as.genericInst.genericType;
            if (tpl != NULL && tpl->kind == TYPE_GENERIC_CLASS_TEMPLATE) {
                printf("%.*s", tpl->as.genericClassTemplate->nameLength,
                       tpl->as.genericClassTemplate->name);
            } else {
                printf("?");
            }
            printf("<");
            for (int i = 0; i < type->as.genericInst.typeArgCount; i++) {
                if (i > 0) printf(", ");
                printType(type->as.genericInst.typeArgs[i]);
            }
            printf(">");
            break;
        }

        case TYPE_ARRAY:
            printf("[");
            printType(type->as.array.elementType);
            printf("]");
            break;

        case TYPE_FUNCTION:
            printf("fn(");
            for (int i = 0; i < type->as.function.paramCount; i++) {
                if (i > 0) printf(", ");
                printType(type->as.function.paramTypes[i]);
            }
            printf(") -> ");
            printType(type->as.function.returnType);
            break;

        case TYPE_CLASS:
        case TYPE_INSTANCE:
            printf("%.*s", type->as.classType->nameLength, type->as.classType->name);
            break;

        case TYPE_INTERFACE:
            printf("interface %.*s", type->as.interfaceType->nameLength,
                   type->as.interfaceType->name);
            break;

        case TYPE_OPTIONAL:
            printType(type->as.optional.innerType);
            printf("?");
            break;

        case TYPE_UNION:
            for (int i = 0; i < type->as.unionType.typeCount; i++) {
                if (i > 0) printf(" | ");
                printType(type->as.unionType.types[i]);
            }
            break;
    }
}

const char* typeKindToString(TypeKind kind) {
    switch (kind) {
        case TYPE_NIL:      return "nil";
        case TYPE_BOOL:     return "bool";
        case TYPE_INT:      return "int";
        case TYPE_FLOAT:    return "float";
        case TYPE_STRING:   return "string";
        case TYPE_ARRAY:    return "array";
        case TYPE_FUNCTION: return "function";
        case TYPE_CLASS:    return "class";
        case TYPE_INSTANCE: return "instance";
        case TYPE_OPTIONAL: return "optional";
        case TYPE_UNION:    return "union";
        case TYPE_UNKNOWN:  return "unknown";
        case TYPE_ERROR:    return "error";
        case TYPE_TYPE_PARAM: return "type_param";
        case TYPE_GENERIC_INST: return "generic_inst";
        case TYPE_GENERIC_CLASS_TEMPLATE: return "generic_class_template";
        case TYPE_INTERFACE: return "interface";
        default:            return "?";
    }
}

void freeType(Type* type) {
    if (type == NULL) return;

    // Don't free singleton types
    if (type == &TYPE_NIL_INSTANCE ||
        type == &TYPE_BOOL_INSTANCE ||
        type == &TYPE_INT_INSTANCE ||
        type == &TYPE_FLOAT_INSTANCE ||
        type == &TYPE_STRING_INSTANCE ||
        type == &TYPE_UNKNOWN_INSTANCE ||
        type == &TYPE_ERROR_INSTANCE) {
        return;
    }

    switch (type->kind) {
        case TYPE_ARRAY:
            // Don't free element type - it might be shared
            break;

        case TYPE_FUNCTION:
            FREE_ARRAY(Type*, type->as.function.paramTypes,
                       type->as.function.paramCount);
            break;

        case TYPE_CLASS:
        case TYPE_INSTANCE:
            if (type->as.classType->implementedInterfaces != NULL) {
                FREE_ARRAY(Type*, type->as.classType->implementedInterfaces,
                           type->as.classType->implementedInterfaceCount);
            }
            FREE(ClassType, type->as.classType);
            break;

        case TYPE_INTERFACE: {
            InterfaceType* it = type->as.interfaceType;
            FREE_ARRAY(char, (char*)(void*)it->name, (size_t)it->nameLength + 1);
            for (int i = 0; i < it->methodCount; i++) {
                freeType(it->methodSignatures[i]);
                FREE_ARRAY(char, (char*)(void*)it->methodNameStarts[i],
                           (size_t)it->methodNameLengths[i] + 1);
            }
            FREE_ARRAY(Type*, it->methodSignatures, it->methodCount);
            FREE_ARRAY(const char*, it->methodNameStarts, it->methodCount);
            FREE_ARRAY(int, it->methodNameLengths, it->methodCount);
            FREE(InterfaceType, it);
            break;
        }

        case TYPE_GENERIC_CLASS_TEMPLATE:
            if (type->as.genericClassTemplate->variances != NULL) {
                FREE_ARRAY(TypeParamVariance, type->as.genericClassTemplate->variances,
                           type->as.genericClassTemplate->varianceCount);
            }
            FREE(GenericClassTemplate, type->as.genericClassTemplate);
            break;

        case TYPE_UNION:
            FREE_ARRAY(Type*, type->as.unionType.types,
                       type->as.unionType.typeCount);
            break;
        case TYPE_GENERIC_INST:
            FREE_ARRAY(Type*, type->as.genericInst.typeArgs, type->as.genericInst.typeArgCount);
            break;

        default:
            break;
    }

    FREE(Type, type);
}

void initTypeSystem(void) {
    // Nothing to initialize for now
}

void freeTypeSystem(void) {
    // Nothing to free for now - types are mostly tied to AST lifetime
}
