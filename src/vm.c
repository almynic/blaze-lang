#include "vm.h"
#include "debug.h"
#include "compiler.h"
#include "parser.h"
#include "typechecker.h"
#include "memory.h"
#include "object.h"
#include "module.h"
#include <stdarg.h>
#include <time.h>
#include <math.h>

// Global module system
static ModuleSystem moduleSystem;
static bool moduleSystemInitialized = false;
static bool preludeLoaded = false;

// ============================================================================
// Stack Operations
// ============================================================================

void push(VM* vm, Value value) {
    *vm->stackTop = value;
    vm->stackTop++;
}

Value pop(VM* vm) {
    vm->stackTop--;
    return *vm->stackTop;
}

Value peek(VM* vm, int distance) {
    return vm->stackTop[-1 - distance];
}

static void resetStack(VM* vm) {
    vm->stackTop = vm->stack;
    vm->frameCount = 0;
    vm->openUpvalues = NULL;
    vm->exceptionHandlerCount = 0;
}

// ============================================================================
// Error Handling
// ============================================================================

static void runtimeError(VM* vm, const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    // Print stack trace
    for (int i = vm->frameCount - 1; i >= 0; i--) {
        CallFrame* frame = &vm->frames[i];
        ObjFunction* function = frame->closure->function;
        size_t instruction = frame->ip - function->chunk.code - 1;
        fprintf(stderr, "[line %d] in ", function->chunk.lines[instruction]);
        if (function->name == NULL) {
            fprintf(stderr, "script\n");
        } else {
            fprintf(stderr, "%s()\n", function->name->chars);
        }
    }

    resetStack(vm);
}

// ============================================================================
// Native Functions
// ============================================================================

// Helper to push objects for GC protection during native registration
static void defineNative(VM* vm, const char* name, NativeFn function, int arity) {
    push(vm, OBJ_VAL(copyString(name, (int)strlen(name))));
    push(vm, OBJ_VAL(newNative(function, arity)));
    tableSet(&vm->globals, AS_STRING(vm->stack[0]), vm->stack[1]);
    pop(vm);
    pop(vm);
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
// VM Lifecycle
// ============================================================================

void initVM(VM* vm) {
    resetStack(vm);
    vm->chunk = NULL;
    vm->ip = NULL;
    initTable(&vm->globals);

    // Register VM for garbage collection
    setGCVM(vm);

    // Time functions
    defineNative(vm, "clock", clockNative, 0);
    defineNative(vm, "time", timeNative, 0);

    // String functions
    defineNative(vm, "len", lenNative, 1);
    defineNative(vm, "substr", substrNative, 3);
    defineNative(vm, "concat", concatNative, 2);
    defineNative(vm, "charAt", charAtNative, 2);
    defineNative(vm, "indexOf", indexOfNative, 2);
    defineNative(vm, "toUpper", toUpperNative, 1);
    defineNative(vm, "toLower", toLowerNative, 1);
    defineNative(vm, "trim", trimNative, 1);

    // Array functions
    defineNative(vm, "push", pushNative, 2);
    defineNative(vm, "pop", popNative, 1);
    defineNative(vm, "first", firstNative, 1);
    defineNative(vm, "last", lastNative, 1);
    defineNative(vm, "contains", containsNative, 2);
    defineNative(vm, "reverse", reverseNative, 1);
    defineNative(vm, "clear", clearNative, 1);
    defineNative(vm, "slice", sliceNative, 3);
    defineNative(vm, "join", joinNative, 2);

    // Additional string functions
    defineNative(vm, "split", splitNative, 2);
    defineNative(vm, "replace", replaceNative, 3);
    defineNative(vm, "startsWith", startsWithNative, 2);
    defineNative(vm, "endsWith", endsWithNative, 2);
    defineNative(vm, "repeat", repeatNative, 2);

    // Array sorting
    defineNative(vm, "sort", sortNative, 1);

    // Math functions
    defineNative(vm, "abs", absNative, 1);
    defineNative(vm, "sqrt", sqrtNative, 1);
    defineNative(vm, "pow", powNative, 2);
    defineNative(vm, "sin", sinNative, 1);
    defineNative(vm, "cos", cosNative, 1);
    defineNative(vm, "tan", tanNative, 1);
    defineNative(vm, "log", logNative, 1);
    defineNative(vm, "log10", log10Native, 1);
    defineNative(vm, "exp", expNative, 1);
    defineNative(vm, "floor", floorNative, 1);
    defineNative(vm, "ceil", ceilNative, 1);
    defineNative(vm, "round", roundNative, 1);
    defineNative(vm, "min", minNative, 2);
    defineNative(vm, "max", maxNative, 2);
    defineNative(vm, "random", randomNative, 0);
    defineNative(vm, "randomInt", randomIntNative, 2);

    // Type conversion functions
    defineNative(vm, "toString", toStringNative, 1);
    defineNative(vm, "toInt", toIntNative, 1);
    defineNative(vm, "toFloat", toFloatNative, 1);

    // I/O functions
    defineNative(vm, "input", inputNative, 1);
    defineNative(vm, "type", typeNative, 1);

    // File I/O functions
    defineNative(vm, "readFile", readFileNative, 1);
    defineNative(vm, "writeFile", writeFileNative, 2);
    defineNative(vm, "appendFile", appendFileNative, 2);
    defineNative(vm, "fileExists", fileExistsNative, 1);
    defineNative(vm, "deleteFile", deleteFileNative, 1);
}

void freeVM(VM* vm) {
    freeTable(&vm->globals);
    setGCVM(NULL);  // Unregister VM from GC
    freeObjects();

    // Reset prelude flag so it's reloaded on next VM init
    preludeLoaded = false;
}

// ============================================================================
// Function Calls
// ============================================================================

static bool call(VM* vm, ObjClosure* closure, int argCount) {
    if (argCount != closure->function->arity) {
        runtimeError(vm, "Expected %d arguments but got %d.",
                     closure->function->arity, argCount);
        return false;
    }

    if (vm->frameCount == FRAMES_MAX) {
        runtimeError(vm, "Stack overflow.");
        return false;
    }

    CallFrame* frame = &vm->frames[vm->frameCount++];
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;
    frame->slots = vm->stackTop - argCount - 1;
    return true;
}

static bool callValue(VM* vm, Value callee, int argCount) {
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
            case OBJ_CLOSURE:
                return call(vm, AS_CLOSURE(callee), argCount);

            case OBJ_NATIVE: {
                ObjNative* native = AS_NATIVE_OBJ(callee);
                if (argCount != native->arity) {
                    runtimeError(vm, "Expected %d arguments but got %d.",
                                 native->arity, argCount);
                    return false;
                }
                NativeFn nativeFn = native->function;
                Value result = nativeFn(argCount, vm->stackTop - argCount);
                vm->stackTop -= argCount + 1;
                push(vm, result);
                return true;
            }

            case OBJ_CLASS: {
                ObjClass* klass = AS_CLASS(callee);
                vm->stackTop[-argCount - 1] = OBJ_VAL(newInstance(klass));

                // Look for init method
                Value initializer;
                if (tableGet(&klass->methods, copyString("init", 4), &initializer)) {
                    return call(vm, AS_CLOSURE(initializer), argCount);
                } else if (argCount != 0) {
                    runtimeError(vm, "Expected 0 arguments but got %d.", argCount);
                    return false;
                }
                return true;
            }

            case OBJ_BOUND_METHOD: {
                ObjBoundMethod* bound = AS_BOUND_METHOD(callee);
                vm->stackTop[-argCount - 1] = bound->receiver;
                return call(vm, bound->method, argCount);
            }

            default:
                break;
        }
    }
    runtimeError(vm, "Can only call functions and classes.");
    return false;
}

// ============================================================================
// Method Binding and Invocation
// ============================================================================

static bool bindMethod(VM* vm, ObjClass* klass, ObjString* name) {
    Value method;
    if (!tableGet(&klass->methods, name, &method)) {
        runtimeError(vm, "Undefined property '%s'.", name->chars);
        return false;
    }

    ObjBoundMethod* bound = newBoundMethod(peek(vm, 0), AS_CLOSURE(method));
    pop(vm);
    push(vm, OBJ_VAL(bound));
    return true;
}

static bool invokeFromClass(VM* vm, ObjClass* klass, ObjString* name, int argCount) {
    Value method;
    if (!tableGet(&klass->methods, name, &method)) {
        runtimeError(vm, "Undefined property '%s'.", name->chars);
        return false;
    }
    return call(vm, AS_CLOSURE(method), argCount);
}

static bool invoke(VM* vm, ObjString* name, int argCount) {
    Value receiver = peek(vm, argCount);

    if (!IS_INSTANCE(receiver)) {
        runtimeError(vm, "Only instances have methods.");
        return false;
    }

    ObjInstance* instance = AS_INSTANCE(receiver);

    // First check for a field with that name (could be a function stored in field)
    Value value;
    if (tableGet(&instance->fields, name, &value)) {
        vm->stackTop[-argCount - 1] = value;
        return callValue(vm, value, argCount);
    }

    return invokeFromClass(vm, instance->klass, name, argCount);
}

// ============================================================================
// Upvalues
// ============================================================================

static ObjUpvalue* captureUpvalue(VM* vm, Value* local) {
    ObjUpvalue* prevUpvalue = NULL;
    ObjUpvalue* upvalue = vm->openUpvalues;

    while (upvalue != NULL && upvalue->location > local) {
        prevUpvalue = upvalue;
        upvalue = upvalue->next;
    }

    if (upvalue != NULL && upvalue->location == local) {
        return upvalue;
    }

    ObjUpvalue* createdUpvalue = newUpvalue(local);
    createdUpvalue->next = upvalue;

    if (prevUpvalue == NULL) {
        vm->openUpvalues = createdUpvalue;
    } else {
        prevUpvalue->next = createdUpvalue;
    }

    return createdUpvalue;
}

static void closeUpvalues(VM* vm, Value* last) {
    while (vm->openUpvalues != NULL && vm->openUpvalues->location >= last) {
        ObjUpvalue* upvalue = vm->openUpvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        vm->openUpvalues = upvalue->next;
    }
}

// ============================================================================
// Execution (Legacy - for simple scripts without functions)
// ============================================================================

static InterpretResult executeLegacy(VM* vm) {
#define READ_BYTE() (*vm->ip++)
#define READ_SHORT() \
    (vm->ip += 2, (uint16_t)((vm->ip[-2] << 8) | vm->ip[-1]))
#define READ_CONSTANT() (vm->chunk->constants.values[READ_BYTE()])

#define BINARY_OP_INT(op) \
    do { \
        int64_t b = AS_INT(pop(vm)); \
        int64_t a = AS_INT(pop(vm)); \
        push(vm, INT_VAL(a op b)); \
    } while (false)

#define BINARY_OP_FLOAT(op) \
    do { \
        double b = AS_FLOAT(pop(vm)); \
        double a = AS_FLOAT(pop(vm)); \
        push(vm, FLOAT_VAL(a op b)); \
    } while (false)

#define COMPARE_OP(op) \
    do { \
        Value b = pop(vm); \
        Value a = pop(vm); \
        if (IS_INT(a) && IS_INT(b)) { \
            push(vm, BOOL_VAL(AS_INT(a) op AS_INT(b))); \
        } else { \
            push(vm, BOOL_VAL(AS_NUMBER(a) op AS_NUMBER(b))); \
        } \
    } while (false)

    for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
        // Print stack
        printf("          ");
        for (Value* slot = vm->stack; slot < vm->stackTop; slot++) {
            printf("[ ");
            printValue(*slot);
            printf(" ]");
        }
        printf("\n");
        disassembleInstruction(vm->chunk, (int)(vm->ip - vm->chunk->code));
#endif

        uint8_t instruction;
        switch (instruction = READ_BYTE()) {
            case OP_CONSTANT: {
                Value constant = READ_CONSTANT();
                push(vm, constant);
                break;
            }

            case OP_CONSTANT_LONG: {
                uint32_t index = READ_BYTE();
                index |= (uint32_t)READ_BYTE() << 8;
                index |= (uint32_t)READ_BYTE() << 16;
                push(vm, vm->chunk->constants.values[index]);
                break;
            }

            case OP_NIL:
                push(vm, NIL_VAL);
                break;

            case OP_TRUE:
                push(vm, BOOL_VAL(true));
                break;

            case OP_FALSE:
                push(vm, BOOL_VAL(false));
                break;

            case OP_POP:
                pop(vm);
                break;

            case OP_DUP:
                push(vm, peek(vm, 0));
                break;

            case OP_GET_LOCAL: {
                uint8_t slot = READ_BYTE();
                push(vm, vm->stack[slot]);
                break;
            }

            case OP_SET_LOCAL: {
                uint8_t slot = READ_BYTE();
                vm->stack[slot] = peek(vm, 0);
                break;
            }

            case OP_GET_GLOBAL: {
                READ_BYTE();
                push(vm, NIL_VAL);
                break;
            }

            case OP_DEFINE_GLOBAL: {
                READ_BYTE();
                pop(vm);
                break;
            }

            case OP_SET_GLOBAL: {
                READ_BYTE();
                break;
            }

            case OP_EQUAL: {
                Value b = pop(vm);
                Value a = pop(vm);
                push(vm, BOOL_VAL(valuesEqual(a, b)));
                break;
            }

            case OP_NOT_EQUAL: {
                Value b = pop(vm);
                Value a = pop(vm);
                push(vm, BOOL_VAL(!valuesEqual(a, b)));
                break;
            }

            case OP_GREATER:
                COMPARE_OP(>);
                break;

            case OP_GREATER_EQUAL:
                COMPARE_OP(>=);
                break;

            case OP_LESS:
                COMPARE_OP(<);
                break;

            case OP_LESS_EQUAL:
                COMPARE_OP(<=);
                break;

            case OP_ADD_INT:
                BINARY_OP_INT(+);
                break;

            case OP_SUBTRACT_INT:
                BINARY_OP_INT(-);
                break;

            case OP_MULTIPLY_INT:
                BINARY_OP_INT(*);
                break;

            case OP_DIVIDE_INT: {
                int64_t b = AS_INT(pop(vm));
                int64_t a = AS_INT(pop(vm));
                if (b == 0) {
                    runtimeError(vm, "Division by zero.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(vm, INT_VAL(a / b));
                break;
            }

            case OP_MODULO_INT: {
                int64_t b = AS_INT(pop(vm));
                int64_t a = AS_INT(pop(vm));
                if (b == 0) {
                    runtimeError(vm, "Division by zero.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(vm, INT_VAL(a % b));
                break;
            }

            case OP_NEGATE_INT: {
                push(vm, INT_VAL(-AS_INT(pop(vm))));
                break;
            }

            case OP_ADD_FLOAT:
                BINARY_OP_FLOAT(+);
                break;

            case OP_SUBTRACT_FLOAT:
                BINARY_OP_FLOAT(-);
                break;

            case OP_MULTIPLY_FLOAT:
                BINARY_OP_FLOAT(*);
                break;

            case OP_DIVIDE_FLOAT: {
                double b = AS_FLOAT(pop(vm));
                double a = AS_FLOAT(pop(vm));
                push(vm, FLOAT_VAL(a / b));
                break;
            }

            case OP_NEGATE_FLOAT: {
                push(vm, FLOAT_VAL(-AS_FLOAT(pop(vm))));
                break;
            }

            case OP_ADD_MIXED: {
                double b = AS_NUMBER(pop(vm));
                double a = AS_NUMBER(pop(vm));
                push(vm, FLOAT_VAL(a + b));
                break;
            }

            case OP_SUBTRACT_MIXED: {
                double b = AS_NUMBER(pop(vm));
                double a = AS_NUMBER(pop(vm));
                push(vm, FLOAT_VAL(a - b));
                break;
            }

            case OP_MULTIPLY_MIXED: {
                double b = AS_NUMBER(pop(vm));
                double a = AS_NUMBER(pop(vm));
                push(vm, FLOAT_VAL(a * b));
                break;
            }

            case OP_DIVIDE_MIXED: {
                double b = AS_NUMBER(pop(vm));
                double a = AS_NUMBER(pop(vm));
                push(vm, FLOAT_VAL(a / b));
                break;
            }

            case OP_INT_TO_FLOAT: {
                int64_t value = AS_INT(pop(vm));
                push(vm, FLOAT_VAL((double)value));
                break;
            }

            case OP_FLOAT_TO_INT: {
                double value = AS_FLOAT(pop(vm));
                push(vm, INT_VAL((int64_t)value));
                break;
            }

            case OP_NOT: {
                push(vm, BOOL_VAL(!AS_BOOL(pop(vm))));
                break;
            }

            case OP_CONCAT: {
                ObjString* b = AS_STRING(pop(vm));
                ObjString* a = AS_STRING(pop(vm));
                int length = a->length + b->length;
                char* chars = ALLOCATE(char, length + 1);
                memcpy(chars, a->chars, a->length);
                memcpy(chars + a->length, b->chars, b->length);
                chars[length] = '\0';
                ObjString* result = takeString(chars, length);
                push(vm, OBJ_VAL(result));
                break;
            }

            case OP_JUMP: {
                uint16_t offset = READ_SHORT();
                vm->ip += offset;
                break;
            }

            case OP_JUMP_IF_FALSE: {
                uint16_t offset = READ_SHORT();
                if (!AS_BOOL(peek(vm, 0))) {
                    vm->ip += offset;
                }
                break;
            }

            case OP_JUMP_IF_TRUE: {
                uint16_t offset = READ_SHORT();
                if (AS_BOOL(peek(vm, 0))) {
                    vm->ip += offset;
                }
                break;
            }

            case OP_LOOP: {
                uint16_t offset = READ_SHORT();
                vm->ip -= offset;
                break;
            }

            case OP_CALL: {
                uint8_t argCount = READ_BYTE();
                Value callee = peek(vm, argCount);
                if (IS_OBJ(callee) && (IS_CLOSURE(callee) || IS_NATIVE(callee))) {
                    if (!callValue(vm, callee, argCount)) {
                        return INTERPRET_RUNTIME_ERROR;
                    }
                } else {
                    // Legacy behavior for non-function calls
                    for (int i = 0; i < argCount; i++) {
                        pop(vm);
                    }
                    pop(vm);
                    push(vm, NIL_VAL);
                }
                break;
            }

            case OP_CLOSURE: {
                ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
                ObjClosure* closure = newClosure(function);
                push(vm, OBJ_VAL(closure));

                for (int i = 0; i < closure->upvalueCount; i++) {
                    uint8_t isLocal = READ_BYTE();
                    uint8_t index = READ_BYTE();
                    if (isLocal) {
                        closure->upvalues[i] = captureUpvalue(vm, vm->stack + index);
                    } else {
                        // Would need enclosing closure for non-local upvalues
                        closure->upvalues[i] = NULL;
                    }
                }
                break;
            }

            case OP_GET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                // Would need current frame's closure
                (void)slot;
                push(vm, NIL_VAL);
                break;
            }

            case OP_SET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                (void)slot;
                break;
            }

            case OP_CLOSE_UPVALUE:
                closeUpvalues(vm, vm->stackTop - 1);
                pop(vm);
                break;

            case OP_RETURN: {
                return INTERPRET_OK;
            }

            case OP_PRINT: {
                Value value = pop(vm);
                printValue(value);
                printf("\n");
                break;
            }

            case OP_ARRAY: {
                uint8_t count = READ_BYTE();
                for (int i = 0; i < count; i++) {
                    pop(vm);
                }
                push(vm, NIL_VAL);
                break;
            }

            case OP_INDEX_GET: {
                pop(vm);
                pop(vm);
                push(vm, NIL_VAL);
                break;
            }

            case OP_INDEX_SET: {
                pop(vm);
                pop(vm);
                pop(vm);
                push(vm, NIL_VAL);
                break;
            }

            case OP_CLASS: {
                READ_BYTE();
                push(vm, NIL_VAL);
                break;
            }

            case OP_GET_PROPERTY: {
                READ_BYTE();
                pop(vm);
                push(vm, NIL_VAL);
                break;
            }

            case OP_SET_PROPERTY: {
                READ_BYTE();
                pop(vm);
                pop(vm);
                push(vm, NIL_VAL);
                break;
            }

            default:
                runtimeError(vm, "Unknown opcode %d.", instruction);
                return INTERPRET_RUNTIME_ERROR;
        }
    }

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef BINARY_OP_INT
#undef BINARY_OP_FLOAT
#undef COMPARE_OP
}

// ============================================================================
// Execution (With call frames - for functions)
// ============================================================================

static InterpretResult executeWithFrames(VM* vm) {
    CallFrame* frame = &vm->frames[vm->frameCount - 1];

#define READ_BYTE() (*frame->ip++)
#define READ_SHORT() \
    (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT() \
    (frame->closure->function->chunk.constants.values[READ_BYTE()])

#define BINARY_OP_INT(op) \
    do { \
        int64_t b = AS_INT(pop(vm)); \
        int64_t a = AS_INT(pop(vm)); \
        push(vm, INT_VAL(a op b)); \
    } while (false)

#define BINARY_OP_FLOAT(op) \
    do { \
        double b = AS_FLOAT(pop(vm)); \
        double a = AS_FLOAT(pop(vm)); \
        push(vm, FLOAT_VAL(a op b)); \
    } while (false)

#define COMPARE_OP(op) \
    do { \
        Value b = pop(vm); \
        Value a = pop(vm); \
        if (IS_INT(a) && IS_INT(b)) { \
            push(vm, BOOL_VAL(AS_INT(a) op AS_INT(b))); \
        } else { \
            push(vm, BOOL_VAL(AS_NUMBER(a) op AS_NUMBER(b))); \
        } \
    } while (false)

    for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
        printf("          ");
        for (Value* slot = vm->stack; slot < vm->stackTop; slot++) {
            printf("[ ");
            printValue(*slot);
            printf(" ]");
        }
        printf("\n");
        disassembleInstruction(&frame->closure->function->chunk,
                              (int)(frame->ip - frame->closure->function->chunk.code));
#endif

        uint8_t instruction;
        switch (instruction = READ_BYTE()) {
            case OP_CONSTANT: {
                Value constant = READ_CONSTANT();
                push(vm, constant);
                break;
            }

            case OP_NIL:
                push(vm, NIL_VAL);
                break;

            case OP_TRUE:
                push(vm, BOOL_VAL(true));
                break;

            case OP_FALSE:
                push(vm, BOOL_VAL(false));
                break;

            case OP_POP:
                pop(vm);
                break;

            case OP_DUP:
                push(vm, peek(vm, 0));
                break;

            case OP_GET_LOCAL: {
                uint8_t slot = READ_BYTE();
                push(vm, frame->slots[slot]);
                break;
            }

            case OP_SET_LOCAL: {
                uint8_t slot = READ_BYTE();
                frame->slots[slot] = peek(vm, 0);
                break;
            }

            case OP_GET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                push(vm, *frame->closure->upvalues[slot]->location);
                break;
            }

            case OP_SET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                *frame->closure->upvalues[slot]->location = peek(vm, 0);
                break;
            }

            case OP_EQUAL: {
                Value b = pop(vm);
                Value a = pop(vm);
                push(vm, BOOL_VAL(valuesEqual(a, b)));
                break;
            }

            case OP_NOT_EQUAL: {
                Value b = pop(vm);
                Value a = pop(vm);
                push(vm, BOOL_VAL(!valuesEqual(a, b)));
                break;
            }

            case OP_GREATER:
                COMPARE_OP(>);
                break;

            case OP_GREATER_EQUAL:
                COMPARE_OP(>=);
                break;

            case OP_LESS:
                COMPARE_OP(<);
                break;

            case OP_LESS_EQUAL:
                COMPARE_OP(<=);
                break;

            case OP_ADD_INT:
                BINARY_OP_INT(+);
                break;

            case OP_SUBTRACT_INT:
                BINARY_OP_INT(-);
                break;

            case OP_MULTIPLY_INT:
                BINARY_OP_INT(*);
                break;

            case OP_DIVIDE_INT: {
                int64_t b = AS_INT(pop(vm));
                int64_t a = AS_INT(pop(vm));
                if (b == 0) {
                    runtimeError(vm, "Division by zero.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(vm, INT_VAL(a / b));
                break;
            }

            case OP_MODULO_INT: {
                int64_t b = AS_INT(pop(vm));
                int64_t a = AS_INT(pop(vm));
                if (b == 0) {
                    runtimeError(vm, "Division by zero.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(vm, INT_VAL(a % b));
                break;
            }

            case OP_NEGATE_INT:
                push(vm, INT_VAL(-AS_INT(pop(vm))));
                break;

            case OP_ADD_FLOAT:
                BINARY_OP_FLOAT(+);
                break;

            case OP_SUBTRACT_FLOAT:
                BINARY_OP_FLOAT(-);
                break;

            case OP_MULTIPLY_FLOAT:
                BINARY_OP_FLOAT(*);
                break;

            case OP_DIVIDE_FLOAT: {
                double b = AS_FLOAT(pop(vm));
                double a = AS_FLOAT(pop(vm));
                push(vm, FLOAT_VAL(a / b));
                break;
            }

            case OP_NEGATE_FLOAT:
                push(vm, FLOAT_VAL(-AS_FLOAT(pop(vm))));
                break;

            case OP_NOT:
                push(vm, BOOL_VAL(!AS_BOOL(pop(vm))));
                break;

            case OP_CONCAT: {
                ObjString* b = AS_STRING(pop(vm));
                ObjString* a = AS_STRING(pop(vm));
                int length = a->length + b->length;
                char* chars = ALLOCATE(char, length + 1);
                memcpy(chars, a->chars, a->length);
                memcpy(chars + a->length, b->chars, b->length);
                chars[length] = '\0';
                ObjString* result = takeString(chars, length);
                push(vm, OBJ_VAL(result));
                break;
            }

            case OP_JUMP: {
                uint16_t offset = READ_SHORT();
                frame->ip += offset;
                break;
            }

            case OP_JUMP_IF_FALSE: {
                uint16_t offset = READ_SHORT();
                if (!AS_BOOL(peek(vm, 0))) {
                    frame->ip += offset;
                }
                break;
            }

            case OP_LOOP: {
                uint16_t offset = READ_SHORT();
                frame->ip -= offset;
                break;
            }

            case OP_CALL: {
                int argCount = READ_BYTE();
                if (!callValue(vm, peek(vm, argCount), argCount)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                frame = &vm->frames[vm->frameCount - 1];
                break;
            }

            case OP_CLOSURE: {
                ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
                ObjClosure* closure = newClosure(function);
                push(vm, OBJ_VAL(closure));

                for (int i = 0; i < closure->upvalueCount; i++) {
                    uint8_t isLocal = READ_BYTE();
                    uint8_t index = READ_BYTE();
                    if (isLocal) {
                        closure->upvalues[i] = captureUpvalue(vm, frame->slots + index);
                    } else {
                        closure->upvalues[i] = frame->closure->upvalues[index];
                    }
                }
                break;
            }

            case OP_CLOSE_UPVALUE:
                closeUpvalues(vm, vm->stackTop - 1);
                pop(vm);
                break;

            case OP_RETURN: {
                Value result = pop(vm);
                closeUpvalues(vm, frame->slots);
                vm->frameCount--;

                if (vm->frameCount == 0) {
                    pop(vm);
                    return INTERPRET_OK;
                }

                vm->stackTop = frame->slots;
                push(vm, result);
                frame = &vm->frames[vm->frameCount - 1];
                break;
            }

            case OP_PRINT: {
                Value value = pop(vm);
                printValue(value);
                printf("\n");
                break;
            }

            case OP_CLASS: {
                ObjString* name = AS_STRING(READ_CONSTANT());
                push(vm, OBJ_VAL(newClass(name)));
                break;
            }

            case OP_GET_PROPERTY: {
                if (!IS_INSTANCE(peek(vm, 0))) {
                    runtimeError(vm, "Only instances have properties.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                ObjInstance* instance = AS_INSTANCE(peek(vm, 0));
                ObjString* name = AS_STRING(READ_CONSTANT());

                Value value;
                if (tableGet(&instance->fields, name, &value)) {
                    pop(vm);  // Instance
                    push(vm, value);
                    break;
                }

                if (!bindMethod(vm, instance->klass, name)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }

            case OP_SET_PROPERTY: {
                if (!IS_INSTANCE(peek(vm, 1))) {
                    runtimeError(vm, "Only instances have fields.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                ObjInstance* instance = AS_INSTANCE(peek(vm, 1));
                ObjString* name = AS_STRING(READ_CONSTANT());
                tableSet(&instance->fields, name, peek(vm, 0));
                Value value = pop(vm);
                pop(vm);  // Instance
                push(vm, value);
                break;
            }

            case OP_METHOD: {
                ObjString* name = AS_STRING(READ_CONSTANT());
                Value method = peek(vm, 0);
                ObjClass* klass = AS_CLASS(peek(vm, 1));
                tableSet(&klass->methods, name, method);
                pop(vm);  // Method
                break;
            }

            case OP_INVOKE: {
                ObjString* method = AS_STRING(READ_CONSTANT());
                int argCount = READ_BYTE();
                if (!invoke(vm, method, argCount)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                frame = &vm->frames[vm->frameCount - 1];
                break;
            }

            case OP_INHERIT: {
                Value superclass = peek(vm, 1);
                if (!IS_CLASS(superclass)) {
                    runtimeError(vm, "Superclass must be a class.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                ObjClass* subclass = AS_CLASS(peek(vm, 0));
                tableAddAll(&AS_CLASS(superclass)->methods, &subclass->methods);
                break;
            }

            case OP_GET_SUPER: {
                ObjString* name = AS_STRING(READ_CONSTANT());
                ObjClass* superclass = AS_CLASS(pop(vm));

                if (!bindMethod(vm, superclass, name)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }

            case OP_SUPER_INVOKE: {
                ObjString* method = AS_STRING(READ_CONSTANT());
                int argCount = READ_BYTE();
                ObjClass* superclass = AS_CLASS(pop(vm));
                if (!invokeFromClass(vm, superclass, method, argCount)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                frame = &vm->frames[vm->frameCount - 1];
                break;
            }

            case OP_DEFINE_GLOBAL: {
                ObjString* name = AS_STRING(READ_CONSTANT());
                tableSet(&vm->globals, name, peek(vm, 0));
                pop(vm);
                break;
            }

            case OP_GET_GLOBAL: {
                ObjString* name = AS_STRING(READ_CONSTANT());
                Value value;
                if (!tableGet(&vm->globals, name, &value)) {
                    runtimeError(vm, "Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(vm, value);
                break;
            }

            case OP_SET_GLOBAL: {
                ObjString* name = AS_STRING(READ_CONSTANT());
                if (tableSet(&vm->globals, name, peek(vm, 0))) {
                    tableDelete(&vm->globals, name);
                    runtimeError(vm, "Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }

            case OP_ARRAY: {
                int count = READ_BYTE();
                ObjArray* array = newArray();
                // Elements are on stack in reverse order, so we need to add them correctly
                // Stack: [elem0, elem1, ..., elemN-1] with elemN-1 at top
                // First, reserve space
                for (int i = 0; i < count; i++) {
                    arrayPush(array, NIL_VAL);
                }
                // Now fill in reverse order (pop from top)
                for (int i = count - 1; i >= 0; i--) {
                    array->elements[i] = pop(vm);
                }
                push(vm, OBJ_VAL(array));
                break;
            }

            case OP_INDEX_GET: {
                Value indexVal = pop(vm);
                Value arrayVal = pop(vm);

                if (!IS_ARRAY(arrayVal)) {
                    runtimeError(vm, "Can only index into arrays.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                if (!IS_INT(indexVal)) {
                    runtimeError(vm, "Array index must be an integer.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                ObjArray* array = AS_ARRAY(arrayVal);
                int index = (int)AS_INT(indexVal);

                if (index < 0 || index >= array->count) {
                    runtimeError(vm, "Array index %d out of bounds (length %d).", index, array->count);
                    return INTERPRET_RUNTIME_ERROR;
                }

                push(vm, array->elements[index]);
                break;
            }

            case OP_INDEX_SET: {
                Value value = pop(vm);
                Value indexVal = pop(vm);
                Value arrayVal = pop(vm);

                if (!IS_ARRAY(arrayVal)) {
                    runtimeError(vm, "Can only index into arrays.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                if (!IS_INT(indexVal)) {
                    runtimeError(vm, "Array index must be an integer.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                ObjArray* array = AS_ARRAY(arrayVal);
                int index = (int)AS_INT(indexVal);

                if (index < 0 || index >= array->count) {
                    runtimeError(vm, "Array index %d out of bounds (length %d).", index, array->count);
                    return INTERPRET_RUNTIME_ERROR;
                }

                array->elements[index] = value;
                push(vm, value);
                break;
            }

            case OP_ARRAY_LENGTH: {
                Value arrayVal = pop(vm);
                if (!IS_ARRAY(arrayVal)) {
                    runtimeError(vm, "Can only get length of arrays.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                ObjArray* array = AS_ARRAY(arrayVal);
                push(vm, INT_VAL(array->count));
                break;
            }

            case OP_RANGE: {
                Value endVal = pop(vm);
                Value startVal = pop(vm);
                if (!IS_INT(startVal) || !IS_INT(endVal)) {
                    runtimeError(vm, "Range bounds must be integers.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                int64_t start = AS_INT(startVal);
                int64_t end = AS_INT(endVal);
                ObjArray* array = newArray();
                if (start <= end) {
                    for (int64_t i = start; i < end; i++) {
                        arrayPush(array, INT_VAL(i));
                    }
                } else {
                    for (int64_t i = start; i > end; i--) {
                        arrayPush(array, INT_VAL(i));
                    }
                }
                push(vm, OBJ_VAL(array));
                break;
            }

            case OP_TRY: {
                // Read catch offset
                uint16_t catchOffset = (uint16_t)(frame->ip[0] << 8);
                catchOffset |= frame->ip[1];
                frame->ip += 2;

                // Push exception handler
                if (vm->exceptionHandlerCount >= EXCEPTION_HANDLERS_MAX) {
                    runtimeError(vm, "Exception handler stack overflow.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                ExceptionHandler* handler = &vm->exceptionHandlers[vm->exceptionHandlerCount++];
                handler->frameIndex = vm->frameCount - 1;
                handler->catchIp = frame->closure->function->chunk.code + catchOffset;
                handler->stackTop = vm->stackTop;
                break;
            }

            case OP_TRY_END: {
                // Pop exception handler (no exception occurred)
                if (vm->exceptionHandlerCount > 0) {
                    vm->exceptionHandlerCount--;
                }
                break;
            }

            case OP_THROW: {
                Value exception = pop(vm);

                // Find a handler
                if (vm->exceptionHandlerCount == 0) {
                    // No handler, convert to runtime error
                    if (IS_STRING(exception)) {
                        runtimeError(vm, "Uncaught exception: %s", AS_CSTRING(exception));
                    } else {
                        runtimeError(vm, "Uncaught exception.");
                    }
                    return INTERPRET_RUNTIME_ERROR;
                }

                // Get the handler
                ExceptionHandler* handler = &vm->exceptionHandlers[--vm->exceptionHandlerCount];

                // Unwind stack to the handler's frame
                while (vm->frameCount - 1 > handler->frameIndex) {
                    vm->frameCount--;
                }
                frame = &vm->frames[vm->frameCount - 1];

                // Restore stack and jump to catch
                vm->stackTop = handler->stackTop;
                frame->ip = handler->catchIp;

                // Push exception value for catch block
                push(vm, exception);
                break;
            }

            default:
                runtimeError(vm, "Unknown opcode %d.", instruction);
                return INTERPRET_RUNTIME_ERROR;
        }
    }

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef BINARY_OP_INT
#undef BINARY_OP_FLOAT
#undef COMPARE_OP
}

// ============================================================================
// Public API
// ============================================================================

InterpretResult run(VM* vm, Chunk* chunk) {
    vm->chunk = chunk;
    vm->ip = chunk->code;
    return executeLegacy(vm);
}

// Load the prelude module (std/prelude) which imports common functions
// Forward declaration for processImports
static bool processImports(VM* vm, Stmt** statements, int count);

// Load the prelude module (std/prelude) which imports common functions
static bool loadPrelude(VM* vm) {
    if (preludeLoaded) {
        return true;
    }

    // Initialize module system if needed
    if (!moduleSystemInitialized) {
        initModuleSystem(&moduleSystem);
        moduleSystemInitialized = true;
    }

    // Try to resolve the prelude path
    char* preludePath = resolveModulePath(&moduleSystem, "std/prelude");
    if (preludePath == NULL) {
        // Prelude not found - this is okay, just means no std library available
        // fprintf(stderr, "[DEBUG] Prelude not found\n");
        preludeLoaded = true;
        return true;
    }
    // fprintf(stderr, "[DEBUG] Loading prelude from: %s\n", preludePath);

    // Read the prelude source
    char* source = readFile(preludePath);
    free(preludePath);
    if (source == NULL) {
        preludeLoaded = true;
        return true;
    }

    // Parse the prelude to extract its import statements
    Parser parser;
    initParser(&parser, source);
    int count = 0;
    Stmt** statements = parse(&parser, &count);

    if (statements == NULL) {
        free(source);
        preludeLoaded = true;
        return true;
    }

    // Process the prelude's imports - this loads the standard library modules
    // and adds their exported functions to the VM's globals
    bool success = processImports(vm, statements, count);
    freeStatements(statements, count);
    free(source);  // Free source AFTER processing (tokens point to source memory)

    preludeLoaded = true;
    return success;
}

// Helper to check if a name is in the selective import list
static bool isNameInImportList(ImportStmt* import, const char* name, int nameLen) {
    for (int i = 0; i < import->nameCount; i++) {
        if (import->names[i].length == nameLen &&
            memcmp(import->names[i].start, name, nameLen) == 0) {
            return true;
        }
    }
    return false;
}

// Process imports from statements and execute imported modules
static bool processImports(VM* vm, Stmt** statements, int count) {
    // Initialize module system if needed
    if (!moduleSystemInitialized) {
        initModuleSystem(&moduleSystem);
        moduleSystemInitialized = true;
    }

    for (int i = 0; i < count; i++) {
        if (statements[i]->kind == STMT_IMPORT) {
            ImportStmt* import = &statements[i]->as.import;

            // Extract path from string token (remove quotes)
            int pathLen = import->path.length - 2;
            char* path = (char*)malloc(pathLen + 1);
            memcpy(path, import->path.start + 1, pathLen);
            path[pathLen] = '\0';

            // Load the module
            Module* module = loadModule(&moduleSystem, path);
            free(path);

            if (module == NULL) {
                return false;
            }

            // For selective imports, track existing globals before module execution
            ObjString** existingKeys = NULL;
            int existingCount = 0;

            if (import->nameCount > 0) {
                // Collect existing global keys
                existingKeys = (ObjString**)malloc(sizeof(ObjString*) * vm->globals.capacity);
                for (int j = 0; j < vm->globals.capacity; j++) {
                    Entry* entry = &vm->globals.entries[j];
                    if (entry->key != NULL) {
                        existingKeys[existingCount++] = entry->key;
                    }
                }
            }

            // Execute the module to define its symbols
            if (module->function != NULL) {
                push(vm, OBJ_VAL(module->function));
                ObjClosure* closure = newClosure(module->function);
                pop(vm);
                push(vm, OBJ_VAL(closure));
                call(vm, closure, 0);

                InterpretResult result = executeWithFrames(vm);
                if (result != INTERPRET_OK) {
                    if (existingKeys != NULL) free(existingKeys);
                    return false;
                }
            }

            // For selective imports, remove unwanted symbols
            if (import->nameCount > 0) {
                // Collect keys to delete (new symbols not in import list)
                ObjString** keysToDelete = (ObjString**)malloc(sizeof(ObjString*) * vm->globals.capacity);
                int deleteCount = 0;

                for (int j = 0; j < vm->globals.capacity; j++) {
                    Entry* entry = &vm->globals.entries[j];
                    if (entry->key == NULL) continue;

                    // Check if this key existed before module execution
                    bool existed = false;
                    for (int k = 0; k < existingCount; k++) {
                        if (existingKeys[k] == entry->key) {
                            existed = true;
                            break;
                        }
                    }

                    // If it's a new symbol and not in the import list, mark for deletion
                    if (!existed && !isNameInImportList(import, entry->key->chars, entry->key->length)) {
                        keysToDelete[deleteCount++] = entry->key;
                    }
                }

                // Delete the unwanted symbols
                for (int j = 0; j < deleteCount; j++) {
                    tableDelete(&vm->globals, keysToDelete[j]);
                }

                free(keysToDelete);
                free(existingKeys);
            }
        }
    }
    return true;
}

static InterpretResult interpretInternal(VM* vm, const char* source, bool replMode) {
    // Load the prelude on first execution (adds common functions to globals)
    if (!loadPrelude(vm)) {
        return INTERPRET_COMPILE_ERROR;
    }

    // Parse
    Parser parser;
    initParser(&parser, source);

    int stmtCount = 0;
    Stmt** statements = parse(&parser, &stmtCount);

    if (statements == NULL) {
        return INTERPRET_COMPILE_ERROR;
    }

    // Process imports first (load and execute imported modules)
    // This adds imported functions to the VM's globals
    if (!processImports(vm, statements, stmtCount)) {
        freeStatements(statements, stmtCount);
        return INTERPRET_COMPILE_ERROR;
    }

    // Type check - skip import statements and trust imported functions
    // (imported modules are already type-checked when loaded)
    TypeChecker checker;
    initTypeChecker(&checker);

    // Add existing globals to the type checker's symbol table
    // This enables REPL persistence - variables/functions from previous inputs
    for (int i = 0; i < vm->globals.capacity; i++) {
        Entry* entry = &vm->globals.entries[i];
        if (entry->key == NULL) continue;

        Type* type = NULL;
        if (IS_CLOSURE(entry->value)) {
            ObjClosure* closure = AS_CLOSURE(entry->value);
            ObjFunction* func = closure->function;
            // Create a function type for the function
            Type** paramTypes = NULL;
            if (func->arity > 0) {
                paramTypes = ALLOCATE(Type*, func->arity);
                for (int j = 0; j < func->arity; j++) {
                    paramTypes[j] = createUnknownType();
                }
            }
            type = createFunctionType(paramTypes, func->arity, createUnknownType());
        } else if (IS_CLASS(entry->value)) {
            type = createClassType(entry->key->chars, entry->key->length);
        } else if (IS_INT(entry->value)) {
            type = createIntType();
        } else if (IS_FLOAT(entry->value)) {
            type = createFloatType();
        } else if (IS_BOOL(entry->value)) {
            type = createBoolType();
        } else if (IS_STRING(entry->value)) {
            type = createStringType();
        } else if (IS_NIL(entry->value)) {
            type = createNilType();
        } else {
            type = createUnknownType();
        }

        defineSymbol(&checker, entry->key->chars, entry->key->length, type, false);
    }

    bool typeOk = typeCheck(&checker, statements, stmtCount);
    freeTypeChecker(&checker);

    if (!typeOk) {
        freeStatements(statements, stmtCount);
        return INTERPRET_COMPILE_ERROR;
    }

    // Compile - use compileRepl for REPL mode so globals persist
    ObjFunction* function = replMode
        ? compileRepl(statements, stmtCount)
        : compile(statements, stmtCount);
    freeStatements(statements, stmtCount);

    if (function == NULL) {
        return INTERPRET_COMPILE_ERROR;
    }

    // Set up execution with call frame
    push(vm, OBJ_VAL(function));
    ObjClosure* closure = newClosure(function);
    pop(vm);
    push(vm, OBJ_VAL(closure));
    call(vm, closure, 0);

    return executeWithFrames(vm);
}

InterpretResult interpret(VM* vm, const char* source) {
    return interpretInternal(vm, source, false);
}

InterpretResult interpretRepl(VM* vm, const char* source) {
    return interpretInternal(vm, source, true);
}
