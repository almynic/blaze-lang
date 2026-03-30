#ifndef blaze_object_h
#define blaze_object_h

#include "common.h"
#include "value.h"
#include "chunk.h"

// Object types
typedef enum {
    OBJ_STRING,
    OBJ_ARRAY,
    OBJ_FUNCTION,
    OBJ_NATIVE,
    OBJ_CLOSURE,
    OBJ_UPVALUE,
    OBJ_CLASS,
    OBJ_INSTANCE,
    OBJ_BOUND_METHOD,
} ObjType;

// Forward declarations for class types
typedef struct ObjClass ObjClass;
typedef struct ObjInstance ObjInstance;

// Base object struct - all objects start with this (Obj is forward declared in value.h)
struct Obj {
    ObjType type;
    bool isMarked;     // For GC mark phase
    struct Obj* next;  // For GC linked list
};

// String object (ObjString is forward declared in value.h)
struct ObjString {
    Obj obj;
    int length;
    char* chars;
    uint32_t hash;
};

// Array object - dynamic array of values
typedef struct {
    Obj obj;
    int count;
    int capacity;
    Value* elements;
} ObjArray;

// Function object
typedef struct {
    Obj obj;
    int arity;              // Number of parameters
    int upvalueCount;       // Number of captured variables
    Chunk chunk;            // Bytecode for this function
    ObjString* name;        // Function name (NULL for scripts)
} ObjFunction;

// Native function type
typedef Value (*NativeFn)(int argCount, Value* args);

// Native function object
typedef struct {
    Obj obj;
    NativeFn function;
    int arity;
} ObjNative;

// Upvalue object - captures a variable from enclosing scope
typedef struct ObjUpvalue {
    Obj obj;
    Value* location;        // Points to the captured variable
    Value closed;           // Stores value when variable goes out of scope
    struct ObjUpvalue* next;
} ObjUpvalue;

// Closure object - function + captured upvalues
typedef struct {
    Obj obj;
    ObjFunction* function;
    ObjUpvalue** upvalues;
    int upvalueCount;
} ObjClosure;

// ============================================================================
// Simple Hash Table for fields/methods
// ============================================================================

typedef struct {
    ObjString* key;
    Value value;
} Entry;

typedef struct {
    int count;
    int capacity;
    Entry* entries;
} Table;

void initTable(Table* table);
void freeTable(Table* table);
bool tableGet(Table* table, ObjString* key, Value* value);
bool tableSet(Table* table, ObjString* key, Value value);
bool tableDelete(Table* table, ObjString* key);
void tableAddAll(Table* from, Table* to);
ObjString* tableFindString(Table* table, const char* chars, int length, uint32_t hash);

// ============================================================================
// Class and Instance Objects
// ============================================================================

// Class object
struct ObjClass {
    Obj obj;
    ObjString* name;
    Table fields;   // Enum variants (and future class-level values); methods stay separate
    Table methods;
};

// Instance object
struct ObjInstance {
    Obj obj;
    ObjClass* klass;
    Table fields;
};

// Bound method - method closure + instance receiver
typedef struct {
    Obj obj;
    Value receiver;
    ObjClosure* method;
} ObjBoundMethod;

// ============================================================================
// Type checking macros
// ============================================================================

#define OBJ_TYPE(value)     (AS_OBJ(value)->type)

#define IS_STRING(value)       isObjType(value, OBJ_STRING)
#define IS_ARRAY(value)        isObjType(value, OBJ_ARRAY)
#define IS_FUNCTION(value)     isObjType(value, OBJ_FUNCTION)
#define IS_NATIVE(value)       isObjType(value, OBJ_NATIVE)
#define IS_CLOSURE(value)      isObjType(value, OBJ_CLOSURE)
#define IS_CLASS(value)        isObjType(value, OBJ_CLASS)
#define IS_INSTANCE(value)     isObjType(value, OBJ_INSTANCE)
#define IS_BOUND_METHOD(value) isObjType(value, OBJ_BOUND_METHOD)

#define AS_STRING(value)       ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)      (((ObjString*)AS_OBJ(value))->chars)
#define AS_ARRAY(value)        ((ObjArray*)AS_OBJ(value))
#define AS_FUNCTION(value)     ((ObjFunction*)AS_OBJ(value))
#define AS_NATIVE(value)       (((ObjNative*)AS_OBJ(value))->function)
#define AS_NATIVE_OBJ(value)   ((ObjNative*)AS_OBJ(value))
#define AS_CLOSURE(value)      ((ObjClosure*)AS_OBJ(value))
#define AS_CLASS(value)        ((ObjClass*)AS_OBJ(value))
#define AS_INSTANCE(value)     ((ObjInstance*)AS_OBJ(value))
#define AS_BOUND_METHOD(value) ((ObjBoundMethod*)AS_OBJ(value))

static inline bool isObjType(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

// ============================================================================
// Object creation functions
// ============================================================================

ObjString* copyString(const char* chars, int length);
ObjString* takeString(char* chars, int length);
ObjArray* newArray(void);
void arrayPush(ObjArray* array, Value value);
Value arrayPop(ObjArray* array);
ObjFunction* newFunction(void);
ObjNative* newNative(NativeFn function, int arity);
ObjClosure* newClosure(ObjFunction* function);
ObjUpvalue* newUpvalue(Value* slot);
ObjClass* newClass(ObjString* name);
ObjInstance* newInstance(ObjClass* klass);
ObjBoundMethod* newBoundMethod(Value receiver, ObjClosure* method);

// ============================================================================
// Object utilities
// ============================================================================

void printObject(Value value);
void freeObjects(void);

#endif
