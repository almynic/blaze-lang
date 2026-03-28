#ifndef blaze_value_h
#define blaze_value_h

#include "common.h"

// Forward declaration for heap objects
typedef struct Obj Obj;
typedef struct ObjString ObjString;

// Value types at runtime
typedef enum {
    VAL_NIL,
    VAL_BOOL,
    VAL_INT,
    VAL_FLOAT,
    VAL_OBJ,    // Heap-allocated objects (strings, functions, etc.)
} ValueType;

// Tagged union for runtime values
typedef struct {
    ValueType type;
    union {
        bool boolean;
        int64_t integer;
        double floating;
        Obj* obj;
    } as;
} Value;

// Type checking macros
#define IS_NIL(value)     ((value).type == VAL_NIL)
#define IS_BOOL(value)    ((value).type == VAL_BOOL)
#define IS_INT(value)     ((value).type == VAL_INT)
#define IS_FLOAT(value)   ((value).type == VAL_FLOAT)
#define IS_OBJ(value)     ((value).type == VAL_OBJ)
#define IS_NUMBER(value)  (IS_INT(value) || IS_FLOAT(value))

// Value extraction macros
#define AS_BOOL(value)    ((value).as.boolean)
#define AS_INT(value)     ((value).as.integer)
#define AS_FLOAT(value)   ((value).as.floating)
#define AS_OBJ(value)     ((value).as.obj)

// Convert to float for mixed arithmetic
#define AS_NUMBER(value)  (IS_INT(value) ? (double)AS_INT(value) : AS_FLOAT(value))

// Value creation macros
#define NIL_VAL           ((Value){VAL_NIL, {.integer = 0}})
#define BOOL_VAL(value)   ((Value){VAL_BOOL, {.boolean = value}})
#define INT_VAL(value)    ((Value){VAL_INT, {.integer = value}})
#define FLOAT_VAL(value)  ((Value){VAL_FLOAT, {.floating = value}})
#define OBJ_VAL(object)   ((Value){VAL_OBJ, {.obj = (Obj*)object}})

// Store arbitrary pointers using integer field (for internal use)
#define POINTER_VAL(ptr)  ((Value){VAL_INT, {.integer = (int64_t)(intptr_t)(ptr)}})
#define AS_POINTER(value) ((void*)(intptr_t)AS_INT(value))

// Dynamic array of values (for constants pool)
typedef struct {
    int capacity;
    int count;
    Value* values;
} ValueArray;

// ValueArray functions
void initValueArray(ValueArray* array);
void writeValueArray(ValueArray* array, Value value);
void freeValueArray(ValueArray* array);

// Print a value for debugging
void printValue(Value value);

// Check if two values are equal
bool valuesEqual(Value a, Value b);

#endif
