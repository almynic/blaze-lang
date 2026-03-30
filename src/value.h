#ifndef blaze_value_h
#define blaze_value_h

/* Runtime `Value`: NaN-boxed scalars, bool, and 48-bit object pointers.
 * Macros IS_*, AS_*, valueToString, etc. are the public surface. */

#include "common.h"
#include <stdint.h>

// Forward declaration for heap objects
typedef struct Obj Obj;
typedef struct ObjString ObjString;

// ============================================================================
// NaN Boxing Value Representation
// ============================================================================
// Values are encoded in 64 bits using IEEE 754 NaN boxing.
// This reduces memory usage from 16 bytes to 8 bytes per value.
//
// Encoding scheme:
// - Real doubles: Any non-NaN double value (most common case)
// - Tagged values: Quiet NaN base (0x7FF8...) + 3-bit type tag + 48-bit payload
//
// Bit layout for tagged values:
// [Sign:1][Exp:11][QNAN:1][Type:3][Payload:48]
//    0     7FF       1      TTT   48-bit data
//
// Type tags:
// 000 = NIL
// 001 = FALSE
// 010 = TRUE
// 011 = INT (48-bit signed integer)
// 100 = PTR (48-bit object pointer)
//
// Integer range: -140,737,488,355,328 to +140,737,488,355,327 (±2^47)
// Pointer range: 48-bit (sufficient for modern x86-64 and ARM64)
// ============================================================================

// NaN boxing constants
#define QNAN         0x7FF8000000000000ULL
#define SIGN_BIT     0x8000000000000000ULL
#define TAG_MASK     0x0007000000000000ULL
#define PAYLOAD_MASK 0x0000FFFFFFFFFFFFULL

// Type tags (3 bits in positions [50:48])
#define TAG_NIL      0x0
#define TAG_INT      0x1
#define TAG_FALSE    0x2
#define TAG_TRUE     0x3
#define TAG_PTR      0x4

// Helper to create NaN-boxed value with type tag
#define MAKE_QNAN(tag) (QNAN | ((uint64_t)(tag) << 48))

// Value type - single 64-bit unsigned integer
typedef uint64_t Value;

// Singleton constants
#define NIL_VAL      MAKE_QNAN(TAG_NIL)
#define FALSE_VAL    MAKE_QNAN(TAG_FALSE)
#define TRUE_VAL     MAKE_QNAN(TAG_TRUE)

// Type checking macros
#define IS_NIL(v)    ((v) == NIL_VAL)
#define IS_BOOL(v)   (((v) & ~(1ULL << 48)) == FALSE_VAL)  // Matches both FALSE and TRUE
#define IS_FALSE(v)  ((v) == FALSE_VAL)
#define IS_TRUE(v)   ((v) == TRUE_VAL)
#define IS_INT(v)    (((v) & 0xFFFF000000000000ULL) == (QNAN | ((uint64_t)TAG_INT << 48)))
#define IS_OBJ(v)    (((v) & 0xFFFF000000000000ULL) == (QNAN | ((uint64_t)TAG_PTR << 48)))
#define IS_FLOAT(v)  (((v) & 0x7FF8000000000000ULL) != 0x7FF8000000000000ULL)
#define IS_NUMBER(v) (IS_INT(v) || IS_FLOAT(v))

// Value creation macros
#define BOOL_VAL(b)  ((b) ? TRUE_VAL : FALSE_VAL)
#define INT_VAL(i)   (MAKE_QNAN(TAG_INT) | ((uint64_t)(i) & PAYLOAD_MASK))
#define FLOAT_VAL(f) (floatToValue(f))
#define OBJ_VAL(obj) (MAKE_QNAN(TAG_PTR) | (uint64_t)(uintptr_t)(obj))

// Value extraction macros
// Note: AS_INT sign-extends 48-bit value to 64-bit int64_t
#define AS_BOOL(v)   ((v) == TRUE_VAL)
#define AS_INT(v)    ((int64_t)((v) & PAYLOAD_MASK) | \
                      (((v) & 0x0000800000000000ULL) ? 0xFFFF000000000000ULL : 0))
#define AS_FLOAT(v)  (valueToFloat(v))
#define AS_OBJ(v)    ((Obj*)(uintptr_t)((v) & PAYLOAD_MASK))

// Float conversion helpers (need functions due to union usage)
static inline Value floatToValue(double num) {
    union { uint64_t bits; double num; } data;
    data.num = num;
    return data.bits;
}

static inline double valueToFloat(Value val) {
    union { uint64_t bits; double num; } data;
    data.bits = val;
    return data.num;
}

// Convert to double for mixed int/float arithmetic
static inline double AS_NUMBER(Value v) {
    if (IS_INT(v)) {
        return (double)AS_INT(v);
    } else {
        return AS_FLOAT(v);
    }
}

// For compatibility (may be used in some places)
#define POINTER_VAL(ptr) OBJ_VAL(ptr)
#define AS_POINTER(v)    AS_OBJ(v)

// Dynamic array of values (for constants pool)
typedef struct {
    int capacity;
    int count;
    Value* values;  // Now 8 bytes per value instead of 16
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
