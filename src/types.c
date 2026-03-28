#include "types.h"
#include "memory.h"

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

bool typesEqual(Type* a, Type* b) {
    if (a == b) return true;
    if (a == NULL || b == NULL) return false;
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
            return typesEqual(a->as.array.elementType, b->as.array.elementType);

        case TYPE_FUNCTION: {
            FunctionType* fa = &a->as.function;
            FunctionType* fb = &b->as.function;
            if (fa->paramCount != fb->paramCount) return false;
            if (!typesEqual(fa->returnType, fb->returnType)) return false;
            for (int i = 0; i < fa->paramCount; i++) {
                if (!typesEqual(fa->paramTypes[i], fb->paramTypes[i])) {
                    return false;
                }
            }
            return true;
        }

        case TYPE_CLASS:
        case TYPE_INSTANCE:
            return a->as.classType == b->as.classType;

        case TYPE_OPTIONAL:
            return typesEqual(a->as.optional.innerType, b->as.optional.innerType);

        case TYPE_UNION: {
            // Two unions are equal if they have the same members (order doesn't matter)
            if (a->as.unionType.typeCount != b->as.unionType.typeCount) return false;
            for (int i = 0; i < a->as.unionType.typeCount; i++) {
                bool found = false;
                for (int j = 0; j < b->as.unionType.typeCount; j++) {
                    if (typesEqual(a->as.unionType.types[i], b->as.unionType.types[j])) {
                        found = true;
                        break;
                    }
                }
                if (!found) return false;
            }
            return true;
        }

        default:
            return false;
    }
}

bool typeIsNumeric(Type* type) {
    return type->kind == TYPE_INT || type->kind == TYPE_FLOAT;
}

bool typeIsAssignableTo(Type* source, Type* target) {
    // Same types are always assignable
    if (typesEqual(source, target)) return true;

    // UNKNOWN target accepts any source (wildcard for generic parameters)
    if (target->kind == TYPE_UNKNOWN) return true;

    // UNKNOWN source can be assigned to any target (result of untyped expressions)
    if (source->kind == TYPE_UNKNOWN) return true;

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
        return typeIsAssignableTo(srcFn->returnType, tgtFn->returnType);
    }

    // nil can be assigned to any optional type
    if (source->kind == TYPE_NIL && target->kind == TYPE_OPTIONAL) {
        return true;
    }

    // T can be assigned to T? (non-optional to optional)
    if (target->kind == TYPE_OPTIONAL) {
        return typeIsAssignableTo(source, target->as.optional.innerType);
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

    // Union type handling
    // T can be assigned to T | U (any type can be assigned to a union containing it)
    if (target->kind == TYPE_UNION) {
        return typeIsInUnion(source, target);
    }

    // T | U can be assigned to V if both T and U can be assigned to V
    if (source->kind == TYPE_UNION) {
        for (int i = 0; i < source->as.unionType.typeCount; i++) {
            if (!typeIsAssignableTo(source->as.unionType.types[i], target)) {
                return false;
            }
        }
        return true;
    }

    return false;
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
            FREE(ClassType, type->as.classType);
            break;

        case TYPE_UNION:
            FREE_ARRAY(Type*, type->as.unionType.types,
                       type->as.unionType.typeCount);
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
