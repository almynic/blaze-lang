#ifndef blaze_types_h
#define blaze_types_h

/* Compile-time type system: primitives, functions, classes, generics,
 * interfaces, unions/optionals, type parameters and variance. */

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
    TYPE_TYPE_PARAM,    // Type parameter (T, K, V, etc.)
    TYPE_GENERIC_INST,  // Instantiated generic type (e.g., Box<int>)
    TYPE_GENERIC_CLASS_TEMPLATE, // User-defined generic class template (class Box<T> { ... })
    TYPE_INTERFACE,       // Protocol: set of method signatures (compile-time only)
} TypeKind;

// Forward declarations
typedef struct Type Type;
typedef struct FunctionType FunctionType;
typedef struct ArrayType ArrayType;
typedef struct ClassType ClassType;
typedef struct OptionalType OptionalType;
typedef struct UnionType UnionType;
typedef struct TypeParam TypeParam;
typedef struct GenericInst GenericInst;
typedef struct GenericClassTemplate GenericClassTemplate;
typedef struct InterfaceType InterfaceType;

// Variance for generic class type parameters (class Box<out T> / in T>).
typedef enum {
    TYPE_PARAM_VAR_INVARIANT,
    TYPE_PARAM_VAR_OUT,   // covariant: use typeIsAssignableTo(argS, argT)
    TYPE_PARAM_VAR_IN,    // contravariant: use typeIsAssignableTo(argT, argS)
} TypeParamVariance;

// Type parameter: T, K, V, etc.
struct TypeParam {
    int index;          // Position in type parameter list
    const char* name;   // Name for debugging
    int nameLength;
};

// Generic type instantiation: Box<int>, etc.
struct GenericInst {
    Type* genericType;  // TYPE_GENERIC_CLASS_TEMPLATE for user generics
    Type** typeArgs;    // Concrete type arguments (e.g., [int])
    int typeArgCount;
};

// Template for a user-defined generic class (class Box<T> { ... })
struct GenericClassTemplate {
    const char* name;
    int nameLength;
    void* classStmt;    // Stmt* — ClassStmt AST (not owned by Type)
    TypeParamVariance* variances; // Parallel to type params; NULL = all invariant
    int varianceCount;
};

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
    Type** implementedInterfaces;  // TYPE_INTERFACE pointers (not owned)
    int implementedInterfaceCount;
};

// Interface / protocol: named set of function signatures
struct InterfaceType {
    const char* name;
    int nameLength;
    const char** methodNameStarts;
    int* methodNameLengths;
    Type** methodSignatures;  // TYPE_FUNCTION each
    int methodCount;
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
        TypeParam typeParam;    // For type parameters
        GenericInst genericInst; // For instantiated generics
        GenericClassTemplate* genericClassTemplate; // Template metadata (heap)
        InterfaceType* interfaceType;
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
Type* createTypeParamType(const char* name, int nameLength, int index);
Type* createGenericClassTemplateType(const char* name, int nameLength, void* classStmt);
void attachGenericTemplateVariances(Type* templateType, const TypeParamVariance* variances, int count);
Type* createGenericInstType(Type* templateType, Type** typeArgs, int typeArgCount);
Type* createInterfaceType(const char* name, int nameLength,
                          const char** methodNameStarts, int* methodNameLengths,
                          Type** methodSignatures, int methodCount);
Type* substituteTypeInType(Type* type, Type** replacements, int typeParamCount);
void mangleGenericInstType(Type* instType, char* buf, size_t bufSize);

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
