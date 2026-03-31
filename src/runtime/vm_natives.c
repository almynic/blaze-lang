/* Built-in native functions and registration. */

#include "vm.h"
#include "vm_internal.h"
#include "object.h"
#include "memory.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// ============================================================================
// Native Functions
// ============================================================================

// Helper to push objects for GC protection during native registration
void vm_define_native(VM* vm, const char* name, NativeFn function, int arity) {
    push(vm, OBJ_VAL(copyString(name, (int)strlen(name))));
    push(vm, OBJ_VAL(newNative(function, arity)));
    tableSet(&vm->globals, AS_STRING(vm->stack[0]), vm->stack[1]);
    pop(vm);
    pop(vm);
}

static bool isHashMapInstance(Value value) {
    return IS_HASH_MAP(value);
}

static bool isHashSetInstance(Value value) {
    return IS_HASH_SET(value);
}

// ============================================================================
// Time Functions
// ============================================================================

static Value clockNative(int argCount, Value* args) {
    (void)argCount;
    (void)args;
    return FLOAT_VAL((double)clock() / CLOCKS_PER_SEC);
}

static Value timeNative(int argCount, Value* args) {
    (void)argCount;
    (void)args;
    return INT_VAL((int64_t)time(NULL));
}

// formatTime(fmt, unixSeconds) -> string — strftime in local timezone
static Value formatTimeNative(int argCount, Value* args) {
    (void)argCount;
    if (!IS_STRING(args[0]) || !IS_INT(args[1])) {
        return OBJ_VAL(copyString("", 0));
    }
    const char* fmt = AS_CSTRING(args[0]);
    time_t t = (time_t)AS_INT(args[1]);
    struct tm* tm = localtime(&t);
    if (tm == NULL) {
        return OBJ_VAL(copyString("", 0));
    }
    char buf[512];
    size_t n = strftime(buf, sizeof(buf), fmt, tm);
    if (n == 0) {
        return OBJ_VAL(copyString("", 0));
    }
    return OBJ_VAL(copyString(buf, (int)n));
}

// formatTimeUtc(fmt, unixSeconds) -> string — strftime in UTC
static Value formatTimeUtcNative(int argCount, Value* args) {
    (void)argCount;
    if (!IS_STRING(args[0]) || !IS_INT(args[1])) {
        return OBJ_VAL(copyString("", 0));
    }
    const char* fmt = AS_CSTRING(args[0]);
    time_t t = (time_t)AS_INT(args[1]);
    struct tm* tm = gmtime(&t);
    if (tm == NULL) {
        return OBJ_VAL(copyString("", 0));
    }
    char buf[512];
    size_t n = strftime(buf, sizeof(buf), fmt, tm);
    if (n == 0) {
        return OBJ_VAL(copyString("", 0));
    }
    return OBJ_VAL(copyString(buf, (int)n));
}

// ============================================================================
// String Functions
// ============================================================================

// len(string|array) -> int - returns length of string or array
static Value lenNative(int argCount, Value* args) {
    (void)argCount;
    if (!IS_OBJ(args[0])) {
        return INT_VAL(0);
    }
    if (IS_STRING(args[0])) {
        return INT_VAL(AS_STRING(args[0])->length);
    }
    if (IS_ARRAY(args[0])) {
        return INT_VAL(AS_ARRAY(args[0])->count);
    }
    return INT_VAL(0);
}

// ============================================================================
// Array Functions
// ============================================================================

// push(array, value) -> array - appends value to array, returns array
static Value pushNative(int argCount, Value* args) {
    (void)argCount;
    if (!IS_ARRAY(args[0])) {
        return NIL_VAL;
    }
    ObjArray* array = AS_ARRAY(args[0]);
    arrayPush(array, args[1]);
    return args[0];  // Return the array for chaining
}

// pop(array) -> value - removes and returns last element
static Value popNative(int argCount, Value* args) {
    (void)argCount;
    if (!IS_ARRAY(args[0])) {
        return NIL_VAL;
    }
    ObjArray* array = AS_ARRAY(args[0]);
    if (array->count == 0) {
        return NIL_VAL;
    }
    return arrayPop(array);
}

// first(array) -> value - returns first element without removing
static Value firstNative(int argCount, Value* args) {
    (void)argCount;
    if (!IS_ARRAY(args[0])) {
        return NIL_VAL;
    }
    ObjArray* array = AS_ARRAY(args[0]);
    if (array->count == 0) {
        return NIL_VAL;
    }
    return array->elements[0];
}

// last(array) -> value - returns last element without removing
static Value lastNative(int argCount, Value* args) {
    (void)argCount;
    if (!IS_ARRAY(args[0])) {
        return NIL_VAL;
    }
    ObjArray* array = AS_ARRAY(args[0]);
    if (array->count == 0) {
        return NIL_VAL;
    }
    return array->elements[array->count - 1];
}

// contains(array, value) -> bool - checks if array contains value
static Value containsNative(int argCount, Value* args) {
    (void)argCount;
    if (!IS_ARRAY(args[0])) {
        return BOOL_VAL(false);
    }
    ObjArray* array = AS_ARRAY(args[0]);
    Value target = args[1];
    for (int i = 0; i < array->count; i++) {
        if (valuesEqual(array->elements[i], target)) {
            return BOOL_VAL(true);
        }
    }
    return BOOL_VAL(false);
}

// reverse(array) -> array - reverses array in place, returns it
static Value reverseNative(int argCount, Value* args) {
    (void)argCount;
    if (!IS_ARRAY(args[0])) {
        return NIL_VAL;
    }
    ObjArray* array = AS_ARRAY(args[0]);
    int left = 0;
    int right = array->count - 1;
    while (left < right) {
        Value temp = array->elements[left];
        array->elements[left] = array->elements[right];
        array->elements[right] = temp;
        left++;
        right--;
    }
    return args[0];
}

// clear(array) -> array - clears array, returns it
static Value clearNative(int argCount, Value* args) {
    (void)argCount;
    if (!IS_ARRAY(args[0])) {
        return NIL_VAL;
    }
    ObjArray* array = AS_ARRAY(args[0]);
    array->count = 0;
    return args[0];
}

// sort(array) -> array - sorts array in place (ascending), returns it
static Value sortNative(int argCount, Value* args) {
    (void)argCount;
    if (!IS_ARRAY(args[0])) {
        return NIL_VAL;
    }
    ObjArray* array = AS_ARRAY(args[0]);

    // Simple insertion sort (works for any comparable values)
    for (int i = 1; i < array->count; i++) {
        Value key = array->elements[i];
        int j = i - 1;

        // Compare and shift
        while (j >= 0) {
            Value a = array->elements[j];
            bool shouldSwap = false;

            // Compare based on types
            if (IS_INT(a) && IS_INT(key)) {
                shouldSwap = AS_INT(a) > AS_INT(key);
            } else if (IS_FLOAT(a) && IS_FLOAT(key)) {
                shouldSwap = AS_FLOAT(a) > AS_FLOAT(key);
            } else if ((IS_INT(a) || IS_FLOAT(a)) && (IS_INT(key) || IS_FLOAT(key))) {
                double aVal = IS_INT(a) ? (double)AS_INT(a) : AS_FLOAT(a);
                double keyVal = IS_INT(key) ? (double)AS_INT(key) : AS_FLOAT(key);
                shouldSwap = aVal > keyVal;
            } else if (IS_STRING(a) && IS_STRING(key)) {
                shouldSwap = strcmp(AS_CSTRING(a), AS_CSTRING(key)) > 0;
            }

            if (!shouldSwap) break;

            array->elements[j + 1] = array->elements[j];
            j--;
        }
        array->elements[j + 1] = key;
    }

    return args[0];
}

// slice(array, start, end) -> array - returns new array with elements from start to end
static Value sliceNative(int argCount, Value* args) {
    (void)argCount;
    if (!IS_ARRAY(args[0])) {
        return NIL_VAL;
    }
    ObjArray* array = AS_ARRAY(args[0]);
    int start = (int)AS_INT(args[1]);
    int end = (int)AS_INT(args[2]);

    // Handle negative indices
    if (start < 0) start = array->count + start;
    if (end < 0) end = array->count + end;

    // Clamp to bounds
    if (start < 0) start = 0;
    if (end > array->count) end = array->count;
    if (start > end) start = end;

    ObjArray* result = newArray();
    for (int i = start; i < end; i++) {
        arrayPush(result, array->elements[i]);
    }
    return OBJ_VAL(result);
}

// join(array, separator) -> string - joins array elements into string
static Value joinNative(int argCount, Value* args) {
    (void)argCount;
    if (!IS_ARRAY(args[0]) || !IS_STRING(args[1])) {
        return OBJ_VAL(copyString("", 0));
    }
    ObjArray* array = AS_ARRAY(args[0]);
    ObjString* sep = AS_STRING(args[1]);

    if (array->count == 0) {
        return OBJ_VAL(copyString("", 0));
    }

    // Calculate total length
    int totalLen = 0;
    for (int i = 0; i < array->count; i++) {
        if (IS_STRING(array->elements[i])) {
            totalLen += AS_STRING(array->elements[i])->length;
        } else if (IS_INT(array->elements[i])) {
            totalLen += 20;  // max int length estimate
        } else if (IS_FLOAT(array->elements[i])) {
            totalLen += 30;  // max float length estimate
        } else if (IS_BOOL(array->elements[i])) {
            totalLen += 5;   // "true" or "false"
        }
        if (i < array->count - 1) {
            totalLen += sep->length;
        }
    }

    char* buffer = ALLOCATE(char, totalLen + 1);
    int pos = 0;

    for (int i = 0; i < array->count; i++) {
        if (IS_STRING(array->elements[i])) {
            ObjString* s = AS_STRING(array->elements[i]);
            memcpy(buffer + pos, s->chars, s->length);
            pos += s->length;
        } else if (IS_INT(array->elements[i])) {
            pos += sprintf(buffer + pos, "%lld", (long long)AS_INT(array->elements[i]));
        } else if (IS_FLOAT(array->elements[i])) {
            pos += sprintf(buffer + pos, "%g", AS_FLOAT(array->elements[i]));
        } else if (IS_BOOL(array->elements[i])) {
            const char* boolStr = AS_BOOL(array->elements[i]) ? "true" : "false";
            int len = (int)strlen(boolStr);
            memcpy(buffer + pos, boolStr, len);
            pos += len;
        }

        if (i < array->count - 1) {
            memcpy(buffer + pos, sep->chars, sep->length);
            pos += sep->length;
        }
    }
    buffer[pos] = '\0';

    return OBJ_VAL(takeString(buffer, pos));
}

// split(string, separator) -> array - splits string into array
static Value splitNative(int argCount, Value* args) {
    (void)argCount;
    if (!IS_STRING(args[0]) || !IS_STRING(args[1])) {
        return OBJ_VAL(newArray());
    }
    ObjString* str = AS_STRING(args[0]);
    ObjString* sep = AS_STRING(args[1]);

    ObjArray* result = newArray();

    if (sep->length == 0) {
        // Split into individual characters
        for (int i = 0; i < str->length; i++) {
            arrayPush(result, OBJ_VAL(copyString(str->chars + i, 1)));
        }
        return OBJ_VAL(result);
    }

    int start = 0;
    for (int i = 0; i <= str->length - sep->length; i++) {
        if (memcmp(str->chars + i, sep->chars, sep->length) == 0) {
            arrayPush(result, OBJ_VAL(copyString(str->chars + start, i - start)));
            start = i + sep->length;
            i = start - 1;  // -1 because loop will increment
        }
    }
    // Add the last part
    arrayPush(result, OBJ_VAL(copyString(str->chars + start, str->length - start)));

    return OBJ_VAL(result);
}

// startsWith(string, prefix) -> bool - checks if string starts with prefix
static Value startsWithNative(int argCount, Value* args) {
    (void)argCount;
    if (!IS_STRING(args[0]) || !IS_STRING(args[1])) {
        return BOOL_VAL(false);
    }
    ObjString* str = AS_STRING(args[0]);
    ObjString* prefix = AS_STRING(args[1]);

    if (prefix->length > str->length) {
        return BOOL_VAL(false);
    }

    return BOOL_VAL(memcmp(str->chars, prefix->chars, prefix->length) == 0);
}

// endsWith(string, suffix) -> bool - checks if string ends with suffix
static Value endsWithNative(int argCount, Value* args) {
    (void)argCount;
    if (!IS_STRING(args[0]) || !IS_STRING(args[1])) {
        return BOOL_VAL(false);
    }
    ObjString* str = AS_STRING(args[0]);
    ObjString* suffix = AS_STRING(args[1]);

    if (suffix->length > str->length) {
        return BOOL_VAL(false);
    }

    int offset = str->length - suffix->length;
    return BOOL_VAL(memcmp(str->chars + offset, suffix->chars, suffix->length) == 0);
}

// repeat(string, count) -> string - repeats string count times
static Value repeatNative(int argCount, Value* args) {
    (void)argCount;
    if (!IS_STRING(args[0]) || !IS_INT(args[1])) {
        return OBJ_VAL(copyString("", 0));
    }
    ObjString* str = AS_STRING(args[0]);
    int count = (int)AS_INT(args[1]);

    if (count <= 0 || str->length == 0) {
        return OBJ_VAL(copyString("", 0));
    }

    int totalLen = str->length * count;
    char* buffer = ALLOCATE(char, totalLen + 1);

    for (int i = 0; i < count; i++) {
        memcpy(buffer + i * str->length, str->chars, str->length);
    }
    buffer[totalLen] = '\0';

    return OBJ_VAL(takeString(buffer, totalLen));
}

// replace(string, old, new) -> string - replaces all occurrences
static Value replaceNative(int argCount, Value* args) {
    (void)argCount;
    if (!IS_STRING(args[0]) || !IS_STRING(args[1]) || !IS_STRING(args[2])) {
        return args[0];
    }
    ObjString* str = AS_STRING(args[0]);
    ObjString* old = AS_STRING(args[1]);
    ObjString* new = AS_STRING(args[2]);

    if (old->length == 0) {
        return args[0];
    }

    // Count occurrences
    int count = 0;
    for (int i = 0; i <= str->length - old->length; i++) {
        if (memcmp(str->chars + i, old->chars, old->length) == 0) {
            count++;
            i += old->length - 1;
        }
    }

    if (count == 0) {
        return args[0];
    }

    int newLen = str->length + count * (new->length - old->length);
    char* buffer = ALLOCATE(char, newLen + 1);
    int pos = 0;
    int start = 0;

    for (int i = 0; i <= str->length - old->length; i++) {
        if (memcmp(str->chars + i, old->chars, old->length) == 0) {
            // Copy part before match
            memcpy(buffer + pos, str->chars + start, i - start);
            pos += i - start;
            // Copy replacement
            memcpy(buffer + pos, new->chars, new->length);
            pos += new->length;
            start = i + old->length;
            i = start - 1;
        }
    }
    // Copy remaining
    memcpy(buffer + pos, str->chars + start, str->length - start);
    pos += str->length - start;
    buffer[pos] = '\0';

    return OBJ_VAL(takeString(buffer, pos));
}

// substr(string, start, length) -> string - returns substring
static Value substrNative(int argCount, Value* args) {
    (void)argCount;
    if (!IS_STRING(args[0])) return OBJ_VAL(copyString("", 0));

    ObjString* str = AS_STRING(args[0]);
    int start = (int)AS_INT(args[1]);
    int length = (int)AS_INT(args[2]);

    // Bounds checking
    if (start < 0) start = 0;
    if (start >= str->length) return OBJ_VAL(copyString("", 0));
    if (length < 0) length = 0;
    if (start + length > str->length) length = str->length - start;

    return OBJ_VAL(copyString(str->chars + start, length));
}

// concat(string, string) -> string - concatenates two strings
static Value concatNative(int argCount, Value* args) {
    (void)argCount;
    if (!IS_STRING(args[0]) || !IS_STRING(args[1])) {
        return OBJ_VAL(copyString("", 0));
    }

    ObjString* a = AS_STRING(args[0]);
    ObjString* b = AS_STRING(args[1]);

    int length = a->length + b->length;
    char* chars = ALLOCATE(char, length + 1);
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';

    return OBJ_VAL(takeString(chars, length));
}

// charAt(string, index) -> string - returns character at index
static Value charAtNative(int argCount, Value* args) {
    (void)argCount;
    if (!IS_STRING(args[0])) return OBJ_VAL(copyString("", 0));

    ObjString* str = AS_STRING(args[0]);
    int index = (int)AS_INT(args[1]);

    if (index < 0 || index >= str->length) {
        return OBJ_VAL(copyString("", 0));
    }

    return OBJ_VAL(copyString(str->chars + index, 1));
}

// indexOf(string, substring) -> int - returns index of substring or -1
static Value indexOfNative(int argCount, Value* args) {
    (void)argCount;
    if (!IS_STRING(args[0]) || !IS_STRING(args[1])) return INT_VAL(-1);

    ObjString* str = AS_STRING(args[0]);
    ObjString* sub = AS_STRING(args[1]);

    if (sub->length == 0) return INT_VAL(0);
    if (sub->length > str->length) return INT_VAL(-1);

    for (int i = 0; i <= str->length - sub->length; i++) {
        if (memcmp(str->chars + i, sub->chars, sub->length) == 0) {
            return INT_VAL(i);
        }
    }
    return INT_VAL(-1);
}

// toUpper(string) -> string - converts to uppercase
static Value toUpperNative(int argCount, Value* args) {
    (void)argCount;
    if (!IS_STRING(args[0])) return OBJ_VAL(copyString("", 0));

    ObjString* str = AS_STRING(args[0]);
    char* chars = ALLOCATE(char, str->length + 1);

    for (int i = 0; i < str->length; i++) {
        char c = str->chars[i];
        if (c >= 'a' && c <= 'z') {
            chars[i] = c - 32;
        } else {
            chars[i] = c;
        }
    }
    chars[str->length] = '\0';

    return OBJ_VAL(takeString(chars, str->length));
}

// toLower(string) -> string - converts to lowercase
static Value toLowerNative(int argCount, Value* args) {
    (void)argCount;
    if (!IS_STRING(args[0])) return OBJ_VAL(copyString("", 0));

    ObjString* str = AS_STRING(args[0]);
    char* chars = ALLOCATE(char, str->length + 1);

    for (int i = 0; i < str->length; i++) {
        char c = str->chars[i];
        if (c >= 'A' && c <= 'Z') {
            chars[i] = c + 32;
        } else {
            chars[i] = c;
        }
    }
    chars[str->length] = '\0';

    return OBJ_VAL(takeString(chars, str->length));
}

// trim(string) -> string - removes leading and trailing whitespace
static Value trimNative(int argCount, Value* args) {
    (void)argCount;
    if (!IS_STRING(args[0])) return OBJ_VAL(copyString("", 0));

    ObjString* str = AS_STRING(args[0]);
    int start = 0;
    int end = str->length;

    // Find first non-whitespace
    while (start < str->length &&
           (str->chars[start] == ' ' || str->chars[start] == '\t' ||
            str->chars[start] == '\n' || str->chars[start] == '\r')) {
        start++;
    }

    // Find last non-whitespace
    while (end > start &&
           (str->chars[end - 1] == ' ' || str->chars[end - 1] == '\t' ||
            str->chars[end - 1] == '\n' || str->chars[end - 1] == '\r')) {
        end--;
    }

    return OBJ_VAL(copyString(str->chars + start, end - start));
}

// ============================================================================
// Math Functions
// ============================================================================

static Value absNative(int argCount, Value* args) {
    (void)argCount;
    if (IS_INT(args[0])) {
        int64_t val = AS_INT(args[0]);
        return INT_VAL(val < 0 ? -val : val);
    } else if (IS_FLOAT(args[0])) {
        return FLOAT_VAL(fabs(AS_FLOAT(args[0])));
    }
    return INT_VAL(0);
}

static Value sqrtNative(int argCount, Value* args) {
    (void)argCount;
    double val = IS_INT(args[0]) ? (double)AS_INT(args[0]) : AS_FLOAT(args[0]);
    return FLOAT_VAL(sqrt(val));
}

static Value powNative(int argCount, Value* args) {
    (void)argCount;
    double base = IS_INT(args[0]) ? (double)AS_INT(args[0]) : AS_FLOAT(args[0]);
    double exp = IS_INT(args[1]) ? (double)AS_INT(args[1]) : AS_FLOAT(args[1]);
    return FLOAT_VAL(pow(base, exp));
}

static Value sinNative(int argCount, Value* args) {
    (void)argCount;
    double val = IS_INT(args[0]) ? (double)AS_INT(args[0]) : AS_FLOAT(args[0]);
    return FLOAT_VAL(sin(val));
}

static Value cosNative(int argCount, Value* args) {
    (void)argCount;
    double val = IS_INT(args[0]) ? (double)AS_INT(args[0]) : AS_FLOAT(args[0]);
    return FLOAT_VAL(cos(val));
}

static Value tanNative(int argCount, Value* args) {
    (void)argCount;
    double val = IS_INT(args[0]) ? (double)AS_INT(args[0]) : AS_FLOAT(args[0]);
    return FLOAT_VAL(tan(val));
}

static Value logNative(int argCount, Value* args) {
    (void)argCount;
    double val = IS_INT(args[0]) ? (double)AS_INT(args[0]) : AS_FLOAT(args[0]);
    return FLOAT_VAL(log(val));
}

static Value log10Native(int argCount, Value* args) {
    (void)argCount;
    double val = IS_INT(args[0]) ? (double)AS_INT(args[0]) : AS_FLOAT(args[0]);
    return FLOAT_VAL(log10(val));
}

static Value expNative(int argCount, Value* args) {
    (void)argCount;
    double val = IS_INT(args[0]) ? (double)AS_INT(args[0]) : AS_FLOAT(args[0]);
    return FLOAT_VAL(exp(val));
}

static Value floorNative(int argCount, Value* args) {
    (void)argCount;
    double val = IS_INT(args[0]) ? (double)AS_INT(args[0]) : AS_FLOAT(args[0]);
    return INT_VAL((int64_t)floor(val));
}

static Value ceilNative(int argCount, Value* args) {
    (void)argCount;
    double val = IS_INT(args[0]) ? (double)AS_INT(args[0]) : AS_FLOAT(args[0]);
    return INT_VAL((int64_t)ceil(val));
}

static Value roundNative(int argCount, Value* args) {
    (void)argCount;
    double val = IS_INT(args[0]) ? (double)AS_INT(args[0]) : AS_FLOAT(args[0]);
    return INT_VAL((int64_t)round(val));
}

static Value minNative(int argCount, Value* args) {
    (void)argCount;
    if (IS_INT(args[0]) && IS_INT(args[1])) {
        int64_t a = AS_INT(args[0]);
        int64_t b = AS_INT(args[1]);
        return INT_VAL(a < b ? a : b);
    }
    double a = IS_INT(args[0]) ? (double)AS_INT(args[0]) : AS_FLOAT(args[0]);
    double b = IS_INT(args[1]) ? (double)AS_INT(args[1]) : AS_FLOAT(args[1]);
    return FLOAT_VAL(a < b ? a : b);
}

static Value maxNative(int argCount, Value* args) {
    (void)argCount;
    if (IS_INT(args[0]) && IS_INT(args[1])) {
        int64_t a = AS_INT(args[0]);
        int64_t b = AS_INT(args[1]);
        return INT_VAL(a > b ? a : b);
    }
    double a = IS_INT(args[0]) ? (double)AS_INT(args[0]) : AS_FLOAT(args[0]);
    double b = IS_INT(args[1]) ? (double)AS_INT(args[1]) : AS_FLOAT(args[1]);
    return FLOAT_VAL(a > b ? a : b);
}

// random() -> float - returns random float between 0 and 1
static Value randomNative(int argCount, Value* args) {
    (void)argCount;
    (void)args;
    static bool seeded = false;
    if (!seeded) {
        srand((unsigned int)time(NULL));
        seeded = true;
    }
    return FLOAT_VAL((double)rand() / RAND_MAX);
}

// randomInt(min, max) -> int - returns random integer in range [min, max)
static Value randomIntNative(int argCount, Value* args) {
    (void)argCount;
    static bool seeded = false;
    if (!seeded) {
        srand((unsigned int)time(NULL));
        seeded = true;
    }
    int64_t minVal = AS_INT(args[0]);
    int64_t maxVal = AS_INT(args[1]);
    if (minVal >= maxVal) {
        return INT_VAL(minVal);
    }
    int64_t range = maxVal - minVal;
    return INT_VAL(minVal + (rand() % range));
}

// ============================================================================
// Type Conversion Functions
// ============================================================================

// toString(value) -> string - converts any value to string
static Value toStringNative(int argCount, Value* args) {
    (void)argCount;
    char buffer[64];
    int len;

    if (IS_NIL(args[0])) {
        return OBJ_VAL(copyString("nil", 3));
    } else if (IS_BOOL(args[0])) {
        return OBJ_VAL(copyString(AS_BOOL(args[0]) ? "true" : "false",
                                  AS_BOOL(args[0]) ? 4 : 5));
    } else if (IS_INT(args[0])) {
        len = snprintf(buffer, sizeof(buffer), "%lld", (long long)AS_INT(args[0]));
        return OBJ_VAL(copyString(buffer, len));
    } else if (IS_FLOAT(args[0])) {
        len = snprintf(buffer, sizeof(buffer), "%g", AS_FLOAT(args[0]));
        return OBJ_VAL(copyString(buffer, len));
    } else if (IS_STRING(args[0])) {
        return args[0];
    }
    return OBJ_VAL(copyString("<object>", 8));
}

// toInt(value) -> int - converts to integer
static Value toIntNative(int argCount, Value* args) {
    (void)argCount;
    if (IS_INT(args[0])) return args[0];
    if (IS_FLOAT(args[0])) return INT_VAL((int64_t)AS_FLOAT(args[0]));
    if (IS_BOOL(args[0])) return INT_VAL(AS_BOOL(args[0]) ? 1 : 0);
    if (IS_STRING(args[0])) {
        ObjString* str = AS_STRING(args[0]);
        return INT_VAL(strtoll(str->chars, NULL, 10));
    }
    return INT_VAL(0);
}

// toFloat(value) -> float - converts to float
static Value toFloatNative(int argCount, Value* args) {
    (void)argCount;
    if (IS_FLOAT(args[0])) return args[0];
    if (IS_INT(args[0])) return FLOAT_VAL((double)AS_INT(args[0]));
    if (IS_BOOL(args[0])) return FLOAT_VAL(AS_BOOL(args[0]) ? 1.0 : 0.0);
    if (IS_STRING(args[0])) {
        ObjString* str = AS_STRING(args[0]);
        return FLOAT_VAL(strtod(str->chars, NULL));
    }
    return FLOAT_VAL(0.0);
}

// ============================================================================
// I/O Functions
// ============================================================================

// input(prompt) -> string - reads a line from stdin
static Value inputNative(int argCount, Value* args) {
    (void)argCount;
    if (IS_STRING(args[0])) {
        printf("%s", AS_CSTRING(args[0]));
        fflush(stdout);
    }

    char buffer[1024];
    if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
        return OBJ_VAL(copyString("", 0));
    }

    // Remove trailing newline
    int len = (int)strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
        buffer[len - 1] = '\0';
        len--;
    }

    return OBJ_VAL(copyString(buffer, len));
}

// writeStr(s) -> writes string to stdout without newline
static Value writeStrNative(int argCount, Value* args) {
    (void)argCount;
    if (!IS_STRING(args[0])) return NIL_VAL;
    printf("%s", AS_CSTRING(args[0]));
    fflush(stdout);
    return args[0];
}

// writeLine(s) -> writes string to stdout with newline
static Value writeLineNative(int argCount, Value* args) {
    (void)argCount;
    if (!IS_STRING(args[0])) return NIL_VAL;
    printf("%s\n", AS_CSTRING(args[0]));
    fflush(stdout);
    return args[0];
}

// flushOut() -> fflush(stdout)
static Value flushOutNative(int argCount, Value* args) {
    (void)argCount;
    (void)args;
    fflush(stdout);
    return NIL_VAL;
}

// type(value) -> string - returns type name
static Value typeNative(int argCount, Value* args) {
    (void)argCount;
    if (IS_NIL(args[0])) return OBJ_VAL(copyString("nil", 3));
    if (IS_BOOL(args[0])) return OBJ_VAL(copyString("bool", 4));
    if (IS_INT(args[0])) return OBJ_VAL(copyString("int", 3));
    if (IS_FLOAT(args[0])) return OBJ_VAL(copyString("float", 5));
    if (IS_STRING(args[0])) return OBJ_VAL(copyString("string", 6));
    if (IS_FUNCTION(args[0])) return OBJ_VAL(copyString("function", 8));
    if (IS_CLOSURE(args[0])) return OBJ_VAL(copyString("function", 8));
    if (IS_NATIVE(args[0])) return OBJ_VAL(copyString("native", 6));
    if (IS_CLASS(args[0])) return OBJ_VAL(copyString("class", 5));
    if (IS_HASH_MAP(args[0])) return OBJ_VAL(copyString("hashmap", 7));
    if (IS_HASH_SET(args[0])) return OBJ_VAL(copyString("hashset", 7));
    if (IS_INSTANCE(args[0])) return OBJ_VAL(copyString("instance", 8));
    return OBJ_VAL(copyString("unknown", 7));
}

// ============================================================================
// File I/O Functions
// ============================================================================

// readFile(path) -> string - reads entire file contents
static Value readFileNative(int argCount, Value* args) {
    (void)argCount;
    if (!IS_STRING(args[0])) {
        return NIL_VAL;
    }

    const char* path = AS_CSTRING(args[0]);
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        return NIL_VAL;
    }

    // Get file size
    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    rewind(file);

    // Read file
    char* buffer = ALLOCATE(char, fileSize + 1);
    size_t bytesRead = fread(buffer, 1, fileSize, file);
    buffer[bytesRead] = '\0';
    fclose(file);

    return OBJ_VAL(takeString(buffer, (int)bytesRead));
}

// writeFile(path, content) -> bool - writes content to file
static Value writeFileNative(int argCount, Value* args) {
    (void)argCount;
    if (!IS_STRING(args[0]) || !IS_STRING(args[1])) {
        return BOOL_VAL(false);
    }

    const char* path = AS_CSTRING(args[0]);
    ObjString* content = AS_STRING(args[1]);

    FILE* file = fopen(path, "wb");
    if (file == NULL) {
        return BOOL_VAL(false);
    }

    size_t written = fwrite(content->chars, 1, content->length, file);
    fclose(file);

    return BOOL_VAL(written == (size_t)content->length);
}

// appendFile(path, content) -> bool - appends content to file
static Value appendFileNative(int argCount, Value* args) {
    (void)argCount;
    if (!IS_STRING(args[0]) || !IS_STRING(args[1])) {
        return BOOL_VAL(false);
    }

    const char* path = AS_CSTRING(args[0]);
    ObjString* content = AS_STRING(args[1]);

    FILE* file = fopen(path, "ab");
    if (file == NULL) {
        return BOOL_VAL(false);
    }

    size_t written = fwrite(content->chars, 1, content->length, file);
    fclose(file);

    return BOOL_VAL(written == (size_t)content->length);
}

// fileExists(path) -> bool - checks if file exists
static Value fileExistsNative(int argCount, Value* args) {
    (void)argCount;
    if (!IS_STRING(args[0])) {
        return BOOL_VAL(false);
    }

    const char* path = AS_CSTRING(args[0]);
    FILE* file = fopen(path, "r");
    if (file != NULL) {
        fclose(file);
        return BOOL_VAL(true);
    }
    return BOOL_VAL(false);
}

// deleteFile(path) -> bool - deletes a file
static Value deleteFileNative(int argCount, Value* args) {
    (void)argCount;
    if (!IS_STRING(args[0])) {
        return BOOL_VAL(false);
    }

    const char* path = AS_CSTRING(args[0]);
    return BOOL_VAL(remove(path) == 0);
}

// ============================================================================
// Hash Map / Hash Set Functions (builtin, Table-backed)
// ============================================================================

static Value hashMapNative(int argCount, Value* args) {
    (void)argCount;
    (void)args;
    return OBJ_VAL(newHashMapObj());
}

static Value hashMapWithCapacityNative(int argCount, Value* args) {
    (void)argCount;
    ObjHashMap* map = newHashMapObj();
    int64_t requested = AS_INT(args[0]);
    if (requested > 0 && requested < INT32_MAX) {
        valueTableReserve(&map->table, (int)requested);
    }
    return OBJ_VAL(map);
}

static Value hashSetNative(int argCount, Value* args) {
    (void)argCount;
    (void)args;
    return OBJ_VAL(newHashSetObj());
}

static Value hashSetWithCapacityNative(int argCount, Value* args) {
    (void)argCount;
    ObjHashSet* set = newHashSetObj();
    int64_t requested = AS_INT(args[0]);
    if (requested > 0 && requested < INT32_MAX) {
        valueTableReserve(&set->table, (int)requested);
    }
    return OBJ_VAL(set);
}

static Value hashMapSetNative(int argCount, Value* args) {
    (void)argCount;
    if (!isHashMapInstance(args[0])) return NIL_VAL;
    valueTableSet(&AS_HASH_MAP(args[0])->table, args[1], args[2]);
    return args[0];
}

static Value hashMapSetIntNative(int argCount, Value* args) {
    (void)argCount;
    if (!isHashMapInstance(args[0]) || !IS_INT(args[1]) || !IS_INT(args[2])) return NIL_VAL;
    valueTableSet(&AS_HASH_MAP(args[0])->table, args[1], args[2]);
    return args[0];
}

static Value hashMapGetNative(int argCount, Value* args) {
    (void)argCount;
    if (!isHashMapInstance(args[0])) return NIL_VAL;
    Value value;
    if (valueTableGet(&AS_HASH_MAP(args[0])->table, args[1], &value)) {
        return value;
    }
    return NIL_VAL;
}

static Value hashMapHasNative(int argCount, Value* args) {
    (void)argCount;
    if (!isHashMapInstance(args[0])) return BOOL_VAL(false);
    Value value;
    return BOOL_VAL(valueTableGet(&AS_HASH_MAP(args[0])->table, args[1], &value));
}

static Value hashMapHasIntNative(int argCount, Value* args) {
    (void)argCount;
    if (!isHashMapInstance(args[0]) || !IS_INT(args[1])) return BOOL_VAL(false);
    Value value;
    return BOOL_VAL(valueTableGet(&AS_HASH_MAP(args[0])->table, args[1], &value));
}

static Value hashMapDeleteNative(int argCount, Value* args) {
    (void)argCount;
    if (!isHashMapInstance(args[0])) return BOOL_VAL(false);
    return BOOL_VAL(valueTableDelete(&AS_HASH_MAP(args[0])->table, args[1]));
}

static Value hashMapSizeNative(int argCount, Value* args) {
    (void)argCount;
    if (!isHashMapInstance(args[0])) return INT_VAL(0);
    return INT_VAL(AS_HASH_MAP(args[0])->table.count);
}

static Value hashMapClearNative(int argCount, Value* args) {
    (void)argCount;
    if (!isHashMapInstance(args[0])) return NIL_VAL;
    ValueTable* table = &AS_HASH_MAP(args[0])->table;
    freeValueTable(table);
    initValueTable(table);
    return args[0];
}

static Value hashMapKeysNative(int argCount, Value* args) {
    (void)argCount;
    if (!isHashMapInstance(args[0])) return NIL_VAL;
    ObjArray* keys = newArray();
    ValueTable* table = &AS_HASH_MAP(args[0])->table;
    for (int i = 0; i < table->capacity; i++) {
        ValueEntry* entry = &table->entries[i];
        if (entry->state != 2) continue;
        arrayPush(keys, entry->key);
    }
    return OBJ_VAL(keys);
}

static Value hashMapValuesNative(int argCount, Value* args) {
    (void)argCount;
    if (!isHashMapInstance(args[0])) return NIL_VAL;
    ObjArray* values = newArray();
    ValueTable* table = &AS_HASH_MAP(args[0])->table;
    for (int i = 0; i < table->capacity; i++) {
        ValueEntry* entry = &table->entries[i];
        if (entry->state != 2) continue;
        arrayPush(values, entry->value);
    }
    return OBJ_VAL(values);
}

static Value hashSetAddNative(int argCount, Value* args) {
    (void)argCount;
    if (!isHashSetInstance(args[0])) return NIL_VAL;
    valueTableSet(&AS_HASH_SET(args[0])->table, args[1], TRUE_VAL);
    return args[0];
}

static Value hashSetAddIntNative(int argCount, Value* args) {
    (void)argCount;
    if (!isHashSetInstance(args[0]) || !IS_INT(args[1])) return NIL_VAL;
    valueTableSet(&AS_HASH_SET(args[0])->table, args[1], TRUE_VAL);
    return args[0];
}

static Value hashSetHasNative(int argCount, Value* args) {
    (void)argCount;
    if (!isHashSetInstance(args[0])) return BOOL_VAL(false);
    Value value;
    return BOOL_VAL(valueTableGet(&AS_HASH_SET(args[0])->table, args[1], &value));
}

static Value hashSetDeleteNative(int argCount, Value* args) {
    (void)argCount;
    if (!isHashSetInstance(args[0])) return BOOL_VAL(false);
    return BOOL_VAL(valueTableDelete(&AS_HASH_SET(args[0])->table, args[1]));
}

static Value hashSetSizeNative(int argCount, Value* args) {
    (void)argCount;
    if (!isHashSetInstance(args[0])) return INT_VAL(0);
    return INT_VAL(AS_HASH_SET(args[0])->table.count);
}

static Value hashSetClearNative(int argCount, Value* args) {
    (void)argCount;
    if (!isHashSetInstance(args[0])) return NIL_VAL;
    ValueTable* table = &AS_HASH_SET(args[0])->table;
    freeValueTable(table);
    initValueTable(table);
    return args[0];
}

static Value hashSetValuesNative(int argCount, Value* args) {
    (void)argCount;
    if (!isHashSetInstance(args[0])) return NIL_VAL;
    ObjArray* values = newArray();
    ValueTable* table = &AS_HASH_SET(args[0])->table;
    for (int i = 0; i < table->capacity; i++) {
        ValueEntry* entry = &table->entries[i];
        if (entry->state != 2) continue;
        arrayPush(values, entry->key);
    }
    return OBJ_VAL(values);
}

// Fused bulk operations to reduce native-call overhead in hot loops.
static Value hashMapFillRangeNative(int argCount, Value* args) {
    (void)argCount;
    if (!isHashMapInstance(args[0]) || !IS_INT(args[1]) || !IS_INT(args[2])) return NIL_VAL;
    int64_t start = AS_INT(args[1]);
    int64_t end = AS_INT(args[2]);
    if (end <= start) return args[0];

    ValueTable* table = &AS_HASH_MAP(args[0])->table;
    int64_t span = end - start;
    if (span > 0 && span < INT32_MAX) {
        valueTableReserve(table, (int)span);
    }
    for (int64_t i = start; i < end; i++) {
        Value key = INT_VAL(i);
        valueTableSet(table, key, INT_VAL(i + 1));
    }
    return args[0];
}

static Value hashMapCountPresentRangeNative(int argCount, Value* args) {
    (void)argCount;
    if (!isHashMapInstance(args[0]) || !IS_INT(args[1]) || !IS_INT(args[2])) return INT_VAL(0);
    int64_t start = AS_INT(args[1]);
    int64_t end = AS_INT(args[2]);
    if (end <= start) return INT_VAL(0);

    ValueTable* table = &AS_HASH_MAP(args[0])->table;
    int64_t count = 0;
    for (int64_t i = start; i < end; i++) {
        Value val;
        if (valueTableGet(table, INT_VAL(i), &val)) count++;
    }
    return INT_VAL(count);
}

// Same key encoding as toString(int): decimal ASCII, fused to avoid per-iteration native calls.
static ObjString* int64ToDecimalString(int64_t n) {
    char buffer[64];
    int len = snprintf(buffer, sizeof(buffer), "%lld", (long long)n);
    if (len < 0) len = 0;
    return copyString(buffer, len);
}

static Value hashMapFillStringKeysRangeNative(int argCount, Value* args) {
    (void)argCount;
    if (!isHashMapInstance(args[0]) || !IS_INT(args[1]) || !IS_INT(args[2])) return NIL_VAL;
    int64_t start = AS_INT(args[1]);
    int64_t end = AS_INT(args[2]);
    if (end <= start) return args[0];

    ValueTable* table = &AS_HASH_MAP(args[0])->table;
    int64_t span = end - start;
    if (span > 0 && span < INT32_MAX) {
        valueTableReserve(table, (int)span);
    }
    for (int64_t i = start; i < end; i++) {
        Value key = OBJ_VAL(int64ToDecimalString(i));
        valueTableSet(table, key, INT_VAL(i));
    }
    return args[0];
}

static Value hashMapCountPresentStringKeysRangeNative(int argCount, Value* args) {
    (void)argCount;
    if (!isHashMapInstance(args[0]) || !IS_INT(args[1]) || !IS_INT(args[2])) return INT_VAL(0);
    int64_t start = AS_INT(args[1]);
    int64_t end = AS_INT(args[2]);
    if (end <= start) return INT_VAL(0);

    ValueTable* table = &AS_HASH_MAP(args[0])->table;
    int64_t count = 0;
    for (int64_t i = start; i < end; i++) {
        Value key = OBJ_VAL(int64ToDecimalString(i));
        Value val;
        if (valueTableGet(table, key, &val)) count++;
    }
    return INT_VAL(count);
}

// Bulk probe for arbitrary key arrays (including class instances).
static Value hashMapCountPresentKeysNative(int argCount, Value* args) {
    (void)argCount;
    if (!isHashMapInstance(args[0]) || !IS_ARRAY(args[1])) return INT_VAL(0);

    ValueTable* table = &AS_HASH_MAP(args[0])->table;
    ObjArray* keys = AS_ARRAY(args[1]);
    int64_t count = 0;

    for (int i = 0; i < keys->count; i++) {
        Value found;
        if (valueTableGet(table, keys->elements[i], &found)) count++;
    }
    return INT_VAL(count);
}

// Parallel key/value arrays (same length). Reduces per-entry native-call overhead.
static Value hashMapSetBulkNative(int argCount, Value* args) {
    (void)argCount;
    if (!isHashMapInstance(args[0]) || !IS_ARRAY(args[1]) || !IS_ARRAY(args[2])) return NIL_VAL;
    ObjArray* keys = AS_ARRAY(args[1]);
    ObjArray* vals = AS_ARRAY(args[2]);
    if (keys->count != vals->count) return NIL_VAL;

    ValueTable* table = &AS_HASH_MAP(args[0])->table;
    int n = keys->count;
    if (n > 0 && n < INT32_MAX) {
        valueTableReserve(table, n);
    }
    for (int i = 0; i < n; i++) {
        valueTableSet(table, keys->elements[i], vals->elements[i]);
    }
    return args[0];
}

static Value hashSetAddModRangeNative(int argCount, Value* args) {
    (void)argCount;
    if (!isHashSetInstance(args[0]) || !IS_INT(args[1]) || !IS_INT(args[2])) return NIL_VAL;
    int64_t count = AS_INT(args[1]);
    int64_t mod = AS_INT(args[2]);
    if (count <= 0 || mod <= 0) return args[0];

    ValueTable* table = &AS_HASH_SET(args[0])->table;
    int64_t reserve = mod < count ? mod : count;
    if (reserve > 0 && reserve < INT32_MAX) {
        valueTableReserve(table, (int)reserve);
    }
    for (int64_t i = 0; i < count; i++) {
        valueTableSet(table, INT_VAL(i % mod), TRUE_VAL);
    }
    return args[0];
}

// ============================================================================
// VM Lifecycle
// ============================================================================


void vm_register_natives(VM* vm) {
    vm_define_native(vm, "clock", clockNative, 0);
    vm_define_native(vm, "time", timeNative, 0);
    vm_define_native(vm, "formatTime", formatTimeNative, 2);
    vm_define_native(vm, "formatTimeUtc", formatTimeUtcNative, 2);
    vm_define_native(vm, "len", lenNative, 1);
    vm_define_native(vm, "substr", substrNative, 3);
    vm_define_native(vm, "concat", concatNative, 2);
    vm_define_native(vm, "charAt", charAtNative, 2);
    vm_define_native(vm, "indexOf", indexOfNative, 2);
    vm_define_native(vm, "toUpper", toUpperNative, 1);
    vm_define_native(vm, "toLower", toLowerNative, 1);
    vm_define_native(vm, "trim", trimNative, 1);
    vm_define_native(vm, "push", pushNative, 2);
    vm_define_native(vm, "pop", popNative, 1);
    vm_define_native(vm, "first", firstNative, 1);
    vm_define_native(vm, "last", lastNative, 1);
    vm_define_native(vm, "contains", containsNative, 2);
    vm_define_native(vm, "reverse", reverseNative, 1);
    vm_define_native(vm, "clear", clearNative, 1);
    vm_define_native(vm, "slice", sliceNative, 3);
    vm_define_native(vm, "join", joinNative, 2);
    vm_define_native(vm, "split", splitNative, 2);
    vm_define_native(vm, "replace", replaceNative, 3);
    vm_define_native(vm, "startsWith", startsWithNative, 2);
    vm_define_native(vm, "endsWith", endsWithNative, 2);
    vm_define_native(vm, "repeat", repeatNative, 2);
    vm_define_native(vm, "sort", sortNative, 1);
    vm_define_native(vm, "abs", absNative, 1);
    vm_define_native(vm, "sqrt", sqrtNative, 1);
    vm_define_native(vm, "pow", powNative, 2);
    vm_define_native(vm, "sin", sinNative, 1);
    vm_define_native(vm, "cos", cosNative, 1);
    vm_define_native(vm, "tan", tanNative, 1);
    vm_define_native(vm, "log", logNative, 1);
    vm_define_native(vm, "log10", log10Native, 1);
    vm_define_native(vm, "exp", expNative, 1);
    vm_define_native(vm, "floor", floorNative, 1);
    vm_define_native(vm, "ceil", ceilNative, 1);
    vm_define_native(vm, "round", roundNative, 1);
    vm_define_native(vm, "min", minNative, 2);
    vm_define_native(vm, "max", maxNative, 2);
    vm_define_native(vm, "random", randomNative, 0);
    vm_define_native(vm, "randomInt", randomIntNative, 2);
    vm_define_native(vm, "toString", toStringNative, 1);
    vm_define_native(vm, "toInt", toIntNative, 1);
    vm_define_native(vm, "toFloat", toFloatNative, 1);
    vm_define_native(vm, "input", inputNative, 1);
    vm_define_native(vm, "writeStr", writeStrNative, 1);
    vm_define_native(vm, "writeLine", writeLineNative, 1);
    vm_define_native(vm, "flushOut", flushOutNative, 0);
    vm_define_native(vm, "type", typeNative, 1);
    vm_define_native(vm, "readFile", readFileNative, 1);
    vm_define_native(vm, "writeFile", writeFileNative, 2);
    vm_define_native(vm, "appendFile", appendFileNative, 2);
    vm_define_native(vm, "fileExists", fileExistsNative, 1);
    vm_define_native(vm, "deleteFile", deleteFileNative, 1);
    vm_define_native(vm, "hashMap", hashMapNative, 0);
    vm_define_native(vm, "hashMapWithCapacity", hashMapWithCapacityNative, 1);
    vm_define_native(vm, "hashSet", hashSetNative, 0);
    vm_define_native(vm, "hashSetWithCapacity", hashSetWithCapacityNative, 1);
    vm_define_native(vm, "hashMapSet", hashMapSetNative, 3);
    vm_define_native(vm, "hashMapSetInt", hashMapSetIntNative, 3);
    vm_define_native(vm, "hashMapGet", hashMapGetNative, 2);
    vm_define_native(vm, "hashMapHas", hashMapHasNative, 2);
    vm_define_native(vm, "hashMapHasInt", hashMapHasIntNative, 2);
    vm_define_native(vm, "hashMapDelete", hashMapDeleteNative, 2);
    vm_define_native(vm, "hashMapSize", hashMapSizeNative, 1);
    vm_define_native(vm, "hashMapClear", hashMapClearNative, 1);
    vm_define_native(vm, "hashMapKeys", hashMapKeysNative, 1);
    vm_define_native(vm, "hashMapValues", hashMapValuesNative, 1);
    vm_define_native(vm, "hashMapFillRange", hashMapFillRangeNative, 3);
    vm_define_native(vm, "hashMapCountPresentRange", hashMapCountPresentRangeNative, 3);
    vm_define_native(vm, "hashMapFillStringKeysRange", hashMapFillStringKeysRangeNative, 3);
    vm_define_native(vm, "hashMapCountPresentStringKeysRange", hashMapCountPresentStringKeysRangeNative, 3);
    vm_define_native(vm, "hashMapCountPresentKeys", hashMapCountPresentKeysNative, 2);
    vm_define_native(vm, "hashMapSetBulk", hashMapSetBulkNative, 3);
    vm_define_native(vm, "hashSetAdd", hashSetAddNative, 2);
    vm_define_native(vm, "hashSetAddInt", hashSetAddIntNative, 2);
    vm_define_native(vm, "hashSetHas", hashSetHasNative, 2);
    vm_define_native(vm, "hashSetDelete", hashSetDeleteNative, 2);
    vm_define_native(vm, "hashSetSize", hashSetSizeNative, 1);
    vm_define_native(vm, "hashSetClear", hashSetClearNative, 1);
    vm_define_native(vm, "hashSetValues", hashSetValuesNative, 1);
    vm_define_native(vm, "hashSetAddModRange", hashSetAddModRangeNative, 3);
}
