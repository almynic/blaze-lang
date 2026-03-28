#ifndef blaze_types_h
#define blaze_types_h

#include "common.h"

// Static type kinds (compile-time)
typedef enum {
    TYPE_NIL,
    TYPE_BOOL,
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_STRING,
    TYPE_ARRAY,
    TYPE_FUNCTION,
    TYPE_CLASS,
    TYPE_INSTANCE,
    TYPE_OPTIONAL,  // Nullable type: T?
    TYPE_UNION,     // Union type: T | U
    TYPE_UNKNOWN,   // For type inference
    TYPE_ERROR,     // Type error occurred
} TypeKind;

// Forward declarations
typedef struct Type Type;
typedef struct FunctionType FunctionType;
typedef struct ArrayType ArrayType;
typedef struct ClassType ClassType;
typedef struct OptionalType OptionalType;
typedef struct UnionType UnionType;

// Function type: fn(param1, param2) -> returnType
struct FunctionType {
    Type** paramTypes;
    int paramCount;
    Type* returnType;
};

// Array type: [elementType]
struct ArrayType {
    Type* elementType;
};

// Class type
struct ClassType {
    const char* name;
    int nameLength;
    struct ClassType* superclass;
    // Fields and methods will be added later
};

// Optional type: T?
struct OptionalType {
    Type* innerType;
};

// Union type: T | U | V ...
struct UnionType {
    Type** types;
    int typeCount;
};

// The main Type struct
struct Type {
    TypeKind kind;
    union {
        FunctionType function;
        ArrayType array;
        ClassType* classType;
        OptionalType optional;
        UnionType unionType;
    } as;
};

// Built-in type singletons
extern Type TYPE_NIL_INSTANCE;
extern Type TYPE_BOOL_INSTANCE;
extern Type TYPE_INT_INSTANCE;
extern Type TYPE_FLOAT_INSTANCE;
extern Type TYPE_STRING_INSTANCE;
extern Type TYPE_UNKNOWN_INSTANCE;
extern Type TYPE_ERROR_INSTANCE;

// Type creation functions
Type* createNilType(void);
Type* createBoolType(void);
Type* createIntType(void);
Type* createFloatType(void);
Type* createStringType(void);
Type* createArrayType(Type* elementType);
Type* createFunctionType(Type** paramTypes, int paramCount, Type* returnType);
Type* createClassType(const char* name, int nameLength);
Type* createOptionalType(Type* innerType);
Type* createUnionType(Type** types, int typeCount);
Type* createUnknownType(void);
Type* createErrorType(void);

// Optional type utilities
Type* unwrapOptionalType(Type* type);  // Get inner type from optional
bool isOptionalType(Type* type);

// Union type utilities
bool isUnionType(Type* type);
bool typeIsInUnion(Type* type, Type* unionType);  // Check if type is a member of union

// Type utilities
bool typesEqual(Type* a, Type* b);
bool typeIsNumeric(Type* type);
bool typeIsAssignableTo(Type* source, Type* target);
void printType(Type* type);
const char* typeKindToString(TypeKind kind);

// Type memory management
void freeType(Type* type);
void initTypeSystem(void);
void freeTypeSystem(void);

#endif
