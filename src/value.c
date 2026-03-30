#include "value.h"
#include "memory.h"
#include "object.h"
#include <string.h>

void initValueArray(ValueArray* array) {
    array->values = NULL;
    array->capacity = 0;
    array->count = 0;
}

void writeValueArray(ValueArray* array, Value value) {
    if (array->capacity < array->count + 1) {
        int oldCapacity = array->capacity;
        array->capacity = GROW_CAPACITY(oldCapacity);
        array->values = GROW_ARRAY(Value, array->values,
                                   oldCapacity, array->capacity);
    }

    array->values[array->count] = value;
    array->count++;
}

void freeValueArray(ValueArray* array) {
    FREE_ARRAY(Value, array->values, array->capacity);
    initValueArray(array);
}

void printValue(Value value) {
    if (IS_NIL(value)) {
        printf("nil");
    } else if (IS_BOOL(value)) {
        printf(AS_BOOL(value) ? "true" : "false");
    } else if (IS_INT(value)) {
        printf("%lld", (long long)AS_INT(value));
    } else if (IS_OBJ(value)) {
        printObject(value);
    } else {
        // Must be a float (not a quiet NaN)
        printf("%g", AS_FLOAT(value));
    }
}

bool valuesEqual(Value a, Value b) {
    // Fast path: bit-identical values are equal
    // This handles nil, bool, and same-type int/obj comparisons
    if (a == b) return true;

    // NaN != NaN (IEEE 754 semantics)
    if (IS_FLOAT(a) && IS_FLOAT(b)) {
        double fa = AS_FLOAT(a);
        double fb = AS_FLOAT(b);
        // Handle NaN properly: NaN != NaN
        if (fa != fa && fb != fb) return false;  // Both NaN
        return fa == fb;
    }

    // Mixed int/float comparison
    if (IS_NUMBER(a) && IS_NUMBER(b)) {
        return AS_NUMBER(a) == AS_NUMBER(b);
    }

    if (IS_STRING(a) && IS_STRING(b)) {
        ObjString* sa = AS_STRING(a);
        ObjString* sb = AS_STRING(b);
        return sa->length == sb->length &&
               memcmp(sa->chars, sb->chars, (size_t)sa->length) == 0;
    }

    return false;
}
