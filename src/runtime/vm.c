/* Main interpreter loop: decodes opcodes, dispatches natives, handles
 * closures/upvalues, classes, try/catch, and module loading via ModuleSystem. */

#include "vm.h"
#include "debug.h"
#include "compiler.h"
#include "parser.h"
#include "typechecker.h"
#include "memory.h"
#include "object.h"
#include "module.h"
#include "generic.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <ctype.h>

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

    // Print stack trace with line/offset details.
    for (int i = vm->frameCount - 1; i >= 0; i--) {
        CallFrame* frame = &vm->frames[i];
        ObjFunction* function = frame->closure->function;
        ptrdiff_t rawInstruction = (frame->ip - function->chunk.code) - 1;
        if (rawInstruction < 0) rawInstruction = 0;
        size_t instruction = (size_t)rawInstruction;
        int line = 0;
        if (function->chunk.count > 0 && instruction < (size_t)function->chunk.count) {
            line = function->chunk.lines[instruction];
        }
        fprintf(stderr, "[line %d, offset %zu] in ", line, instruction);
        if (function->name == NULL) {
            fprintf(stderr, "script\n");
        } else {
            fprintf(stderr, "%s()\n", function->name->chars);
        }
    }

    resetStack(vm);
}

static int currentFrameLine(const CallFrame* frame) {
    ObjFunction* function = frame->closure->function;
    ptrdiff_t rawInstruction = (frame->ip - function->chunk.code) - 1;
    if (rawInstruction < 0) rawInstruction = 0;
    if (function->chunk.count <= 0) return 0;
    if (rawInstruction >= function->chunk.count) {
        rawInstruction = function->chunk.count - 1;
    }
    return function->chunk.lines[rawInstruction];
}

typedef enum {
    BP_OP_EQ,
    BP_OP_NE,
    BP_OP_LT,
    BP_OP_LE,
    BP_OP_GT,
    BP_OP_GE,
} BreakpointOp;

static void skipSpaces(const char** p) {
    while (**p == ' ' || **p == '\t') (*p)++;
}

static bool parseBreakpointOp(const char** p, BreakpointOp* outOp) {
    if (strncmp(*p, "==", 2) == 0) {
        *outOp = BP_OP_EQ;
        *p += 2;
        return true;
    }
    if (strncmp(*p, "!=", 2) == 0) {
        *outOp = BP_OP_NE;
        *p += 2;
        return true;
    }
    if (strncmp(*p, ">=", 2) == 0) {
        *outOp = BP_OP_GE;
        *p += 2;
        return true;
    }
    if (strncmp(*p, "<=", 2) == 0) {
        *outOp = BP_OP_LE;
        *p += 2;
        return true;
    }
    if (**p == '>') {
        *outOp = BP_OP_GT;
        (*p)++;
        return true;
    }
    if (**p == '<') {
        *outOp = BP_OP_LT;
        (*p)++;
        return true;
    }
    return false;
}

static bool compareNumbers(double lhs, BreakpointOp op, double rhs) {
    switch (op) {
        case BP_OP_EQ: return lhs == rhs;
        case BP_OP_NE: return lhs != rhs;
        case BP_OP_LT: return lhs < rhs;
        case BP_OP_LE: return lhs <= rhs;
        case BP_OP_GT: return lhs > rhs;
        case BP_OP_GE: return lhs >= rhs;
    }
    return false;
}

static bool compareValues(Value lhs, BreakpointOp op, Value rhs) {
    if (op == BP_OP_EQ) return valuesEqual(lhs, rhs);
    if (op == BP_OP_NE) return !valuesEqual(lhs, rhs);
    if (IS_NUMBER(lhs) && IS_NUMBER(rhs)) {
        return compareNumbers(AS_NUMBER(lhs), op, AS_NUMBER(rhs));
    }
    if (IS_STRING(lhs) && IS_STRING(rhs)) {
        int cmp = strcmp(AS_CSTRING(lhs), AS_CSTRING(rhs));
        switch (op) {
            case BP_OP_LT: return cmp < 0;
            case BP_OP_LE: return cmp <= 0;
            case BP_OP_GT: return cmp > 0;
            case BP_OP_GE: return cmp >= 0;
            case BP_OP_EQ:
            case BP_OP_NE:
                break;
        }
    }
    return false;
}

static bool parseConditionRhsValue(const char** p, Value* out) {
    skipSpaces(p);
    if (**p == '"') {
        const char* start = ++(*p);
        while (**p != '\0' && **p != '"') (*p)++;
        if (**p != '"') return false;
        int len = (int)(*p - start);
        *out = OBJ_VAL(copyString(start, len));
        (*p)++;
        return true;
    }
    if (strncmp(*p, "true", 4) == 0 && !isalnum((unsigned char)(*p)[4]) && (*p)[4] != '_') {
        *out = TRUE_VAL;
        *p += 4;
        return true;
    }
    if (strncmp(*p, "false", 5) == 0 && !isalnum((unsigned char)(*p)[5]) && (*p)[5] != '_') {
        *out = FALSE_VAL;
        *p += 5;
        return true;
    }
    if (strncmp(*p, "nil", 3) == 0 && !isalnum((unsigned char)(*p)[3]) && (*p)[3] != '_') {
        *out = NIL_VAL;
        *p += 3;
        return true;
    }

    char* end = NULL;
    double asDouble = strtod(*p, &end);
    if (end == *p) return false;

    bool isFloat = false;
    for (const char* s = *p; s < end; s++) {
        if (*s == '.' || *s == 'e' || *s == 'E') {
            isFloat = true;
            break;
        }
    }
    if (isFloat) {
        *out = FLOAT_VAL(asDouble);
    } else {
        long long asInt = strtoll(*p, NULL, 10);
        *out = INT_VAL((int64_t)asInt);
    }
    *p = end;
    return true;
}

static bool evaluateBreakpointClause(VM* vm, CallFrame* frame, int line,
                                     DebugBreakpoint* bp, const char** p) {
    (void)vm;
    skipSpaces(p);

    Value lhs;
    bool hasLhs = true;
    if (strncmp(*p, "hit", 3) == 0 && !isalnum((unsigned char)(*p)[3]) && (*p)[3] != '_') {
        lhs = INT_VAL(bp->hitCount);
        *p += 3;
    } else if (strncmp(*p, "line", 4) == 0 && !isalnum((unsigned char)(*p)[4]) && (*p)[4] != '_') {
        lhs = INT_VAL(line);
        *p += 4;
    } else if (strncmp(*p, "depth", 5) == 0 && !isalnum((unsigned char)(*p)[5]) && (*p)[5] != '_') {
        lhs = INT_VAL(vm->frameCount);
        *p += 5;
    } else if (strncmp(*p, "local[", 6) == 0) {
        *p += 6;
        skipSpaces(p);
        if (!isdigit((unsigned char)**p)) return true;
        char* end = NULL;
        long idx = strtol(*p, &end, 10);
        *p = end;
        skipSpaces(p);
        if (**p != ']') return true;
        (*p)++;
        int localCount = (int)(vm->stackTop - frame->slots);
        if (idx < 0 || idx >= localCount) return false;
        lhs = frame->slots[idx];
    } else {
        hasLhs = false;
    }

    if (!hasLhs) return true;  // Keep backward-compatible permissive behavior.
    skipSpaces(p);

    BreakpointOp op;
    if (!parseBreakpointOp(p, &op)) return true;

    Value rhs;
    if (!parseConditionRhsValue(p, &rhs)) return true;

    return compareValues(lhs, op, rhs);
}

static bool evaluateBreakpointCondition(VM* vm, CallFrame* frame, int line,
                                        DebugBreakpoint* bp) {
    if (!bp->hasCondition) return true;
    const char* p = bp->condition;

    // Support conjunctions: "hit>=2 && local[0]==42".
    while (true) {
        if (!evaluateBreakpointClause(vm, frame, line, bp, &p)) {
            return false;
        }
        skipSpaces(&p);
        if (strncmp(p, "&&", 2) == 0) {
            p += 2;
            continue;
        }
        break;
    }
    return true;
}

static void saveBreakpoints(const VM* vm) {
    if (vm->debuggerBreakpointsPath[0] == '\0') return;
    FILE* f = fopen(vm->debuggerBreakpointsPath, "w");
    if (f == NULL) return;
    for (int i = 0; i < vm->breakpointCount; i++) {
        const DebugBreakpoint* bp = &vm->breakpoints[i];
        if (bp->hasCondition) {
            fprintf(f, "%d|%s\n", bp->line, bp->condition);
        } else {
            fprintf(f, "%d\n", bp->line);
        }
    }
    fclose(f);
}

static void loadBreakpoints(VM* vm) {
    if (vm->debuggerBreakpointsPath[0] == '\0') return;
    FILE* f = fopen(vm->debuggerBreakpointsPath, "r");
    if (f == NULL) return;
    debuggerClearBreakpoints(vm);

    char line[256];
    while (fgets(line, sizeof(line), f) != NULL) {
        char* newline = strchr(line, '\n');
        if (newline) *newline = '\0';
        if (line[0] == '\0') continue;

        char* sep = strchr(line, '|');
        if (sep != NULL) {
            *sep = '\0';
            int lineNo = atoi(line);
            debuggerAddBreakpoint(vm, lineNo, sep + 1);
        } else {
            int lineNo = atoi(line);
            debuggerAddBreakpoint(vm, lineNo, NULL);
        }
    }
    fclose(f);
}

static void printDebuggerStackTrace(VM* vm) {
    for (int i = vm->frameCount - 1; i >= 0; i--) {
        CallFrame* frame = &vm->frames[i];
        ObjFunction* function = frame->closure->function;
        int line = currentFrameLine(frame);
        const char* name = function->name == NULL ? "script" : function->name->chars;
        printf("#%d line %d in %s\n", vm->frameCount - 1 - i, line, name);
    }
}

static void printDebuggerLocals(VM* vm, CallFrame* frame) {
    printf("locals:\n");
    if (vm->stackTop <= frame->slots) {
        printf("  (none)\n");
        return;
    }
    int count = (int)(vm->stackTop - frame->slots);
    for (int i = 0; i < count; i++) {
        printf("  [%d] = ", i);
        printValue(frame->slots[i]);
        printf("\n");
    }
}

static bool shouldPauseForBreakpoint(VM* vm, CallFrame* frame, int line) {
    for (int i = 0; i < vm->breakpointCount; i++) {
        DebugBreakpoint* bp = &vm->breakpoints[i];
        if (bp->line != line) continue;
        bp->hitCount++;
        if (evaluateBreakpointCondition(vm, frame, line, bp)) {
            return true;
        }
    }
    return false;
}

static void debuggerRepl(VM* vm, CallFrame* frame, int line) {
    vm->debuggerPaused = true;
    printf("\n[debug] paused at line %d", line);
    if (frame->closure->function->name != NULL) {
        printf(" in %s()", frame->closure->function->name->chars);
    }
    printf("\n");

    char input[256];
    while (!vm->debuggerAutoContinue) {
        printf("(blaze-debug) ");
        fflush(stdout);
        if (fgets(input, sizeof(input), stdin) == NULL) {
            vm->debuggerAutoContinue = true;
            vm->debuggerStepMode = DEBUG_STEP_NONE;
            break;
        }
        char* nl = strchr(input, '\n');
        if (nl) *nl = '\0';

        if (strcmp(input, "c") == 0 || strcmp(input, "continue") == 0) {
            vm->debuggerStepMode = DEBUG_STEP_NONE;
            vm->debuggerAutoContinue = true;
        } else if (strcmp(input, "s") == 0 || strcmp(input, "step") == 0) {
            vm->debuggerStepMode = DEBUG_STEP_IN;
            vm->debuggerAutoContinue = true;
        } else if (strcmp(input, "n") == 0 || strcmp(input, "next") == 0) {
            vm->debuggerStepMode = DEBUG_STEP_NEXT;
            vm->debuggerStepDepth = vm->frameCount;
            vm->debuggerAutoContinue = true;
        } else if (strcmp(input, "o") == 0 || strcmp(input, "out") == 0 ||
                   strcmp(input, "stepout") == 0 || strcmp(input, "step-out") == 0) {
            vm->debuggerStepMode = DEBUG_STEP_OUT;
            vm->debuggerStepDepth = vm->frameCount;
            vm->debuggerAutoContinue = true;
        } else if (strcmp(input, "bt") == 0) {
            printDebuggerStackTrace(vm);
        } else if (strcmp(input, "locals") == 0) {
            printDebuggerLocals(vm, frame);
        } else if (strncmp(input, "break ", 6) == 0) {
            char* arg = input + 6;
            while (*arg == ' ') arg++;
            char* condSep = strstr(arg, " if ");
            bool ok;
            if (condSep != NULL) {
                *condSep = '\0';
                int bpLine = atoi(arg);
                ok = debuggerAddBreakpoint(vm, bpLine, condSep + 4);
            } else {
                int bpLine = atoi(arg);
                ok = debuggerAddBreakpoint(vm, bpLine, NULL);
            }
            printf(ok ? "added breakpoint\n" : "failed to add breakpoint\n");
            saveBreakpoints(vm);
        } else if (strncmp(input, "delete ", 7) == 0) {
            int bpLine = atoi(input + 7);
            bool ok = debuggerRemoveBreakpoint(vm, bpLine);
            printf(ok ? "deleted breakpoint\n" : "breakpoint not found\n");
            saveBreakpoints(vm);
        } else if (strcmp(input, "breakpoints") == 0) {
            if (vm->breakpointCount == 0) {
                printf("(no breakpoints)\n");
            }
            for (int i = 0; i < vm->breakpointCount; i++) {
                DebugBreakpoint* bp = &vm->breakpoints[i];
                if (bp->hasCondition) {
                    printf("%d if %s (hits=%d)\n", bp->line, bp->condition, bp->hitCount);
                } else {
                    printf("%d (hits=%d)\n", bp->line, bp->hitCount);
                }
            }
        } else if (strcmp(input, "help") == 0 || strcmp(input, "h") == 0) {
            printf("commands: break <line> [if cond], delete <line>, breakpoints, step|s, next|n, out|o, continue|c, bt, locals, help\n");
            printf("  cond vars: hit, line, depth, local[N]  (ops: == != < <= > >=, join with &&)\n");
        } else {
            printf("unknown command. try: help\n");
        }
    }
    vm->debuggerAutoContinue = false;
    vm->debuggerPaused = false;
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

void initVM(VM* vm) {
    resetStack(vm);
    vm->chunk = NULL;
    vm->ip = NULL;
    vm->debuggerEnabled = false;
    vm->debuggerPaused = false;
    vm->debuggerStepping = false;
    vm->debuggerStepMode = DEBUG_STEP_NONE;
    vm->debuggerStepDepth = 0;
    vm->debuggerLastLine = -1;
    vm->debuggerAutoContinue = false;
    vm->debuggerBreakpointsPath[0] = '\0';
    vm->breakpointCount = 0;
    initTable(&vm->globals);
    initTable(&vm->strings);

    // Register VM for garbage collection
    setGCVM(vm);

    // Time functions
    defineNative(vm, "clock", clockNative, 0);
    defineNative(vm, "time", timeNative, 0);
    defineNative(vm, "formatTime", formatTimeNative, 2);
    defineNative(vm, "formatTimeUtc", formatTimeUtcNative, 2);

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
    defineNative(vm, "writeStr", writeStrNative, 1);
    defineNative(vm, "writeLine", writeLineNative, 1);
    defineNative(vm, "flushOut", flushOutNative, 0);
    defineNative(vm, "type", typeNative, 1);

    // File I/O functions
    defineNative(vm, "readFile", readFileNative, 1);
    defineNative(vm, "writeFile", writeFileNative, 2);
    defineNative(vm, "appendFile", appendFileNative, 2);
    defineNative(vm, "fileExists", fileExistsNative, 1);
    defineNative(vm, "deleteFile", deleteFileNative, 1);

    // Hash map / hash set builtins
    defineNative(vm, "hashMap", hashMapNative, 0);
    defineNative(vm, "hashMapWithCapacity", hashMapWithCapacityNative, 1);
    defineNative(vm, "hashSet", hashSetNative, 0);
    defineNative(vm, "hashSetWithCapacity", hashSetWithCapacityNative, 1);
    defineNative(vm, "hashMapSet", hashMapSetNative, 3);
    defineNative(vm, "hashMapSetInt", hashMapSetIntNative, 3);
    defineNative(vm, "hashMapGet", hashMapGetNative, 2);
    defineNative(vm, "hashMapHas", hashMapHasNative, 2);
    defineNative(vm, "hashMapHasInt", hashMapHasIntNative, 2);
    defineNative(vm, "hashMapDelete", hashMapDeleteNative, 2);
    defineNative(vm, "hashMapSize", hashMapSizeNative, 1);
    defineNative(vm, "hashMapClear", hashMapClearNative, 1);
    defineNative(vm, "hashMapKeys", hashMapKeysNative, 1);
    defineNative(vm, "hashMapValues", hashMapValuesNative, 1);
    defineNative(vm, "hashMapFillRange", hashMapFillRangeNative, 3);
    defineNative(vm, "hashMapCountPresentRange", hashMapCountPresentRangeNative, 3);
    defineNative(vm, "hashMapFillStringKeysRange", hashMapFillStringKeysRangeNative, 3);
    defineNative(vm, "hashMapCountPresentStringKeysRange", hashMapCountPresentStringKeysRangeNative, 3);
    defineNative(vm, "hashMapCountPresentKeys", hashMapCountPresentKeysNative, 2);
    defineNative(vm, "hashMapSetBulk", hashMapSetBulkNative, 3);
    defineNative(vm, "hashSetAdd", hashSetAddNative, 2);
    defineNative(vm, "hashSetAddInt", hashSetAddIntNative, 2);
    defineNative(vm, "hashSetHas", hashSetHasNative, 2);
    defineNative(vm, "hashSetDelete", hashSetDeleteNative, 2);
    defineNative(vm, "hashSetSize", hashSetSizeNative, 1);
    defineNative(vm, "hashSetClear", hashSetClearNative, 1);
    defineNative(vm, "hashSetValues", hashSetValuesNative, 1);
    defineNative(vm, "hashSetAddModRange", hashSetAddModRangeNative, 3);
}

void freeVM(VM* vm) {
    if (moduleSystemInitialized) {
        freeModuleSystem(&moduleSystem);
        moduleSystemInitialized = false;
    }
    freeTable(&vm->strings);
    freeTable(&vm->globals);
    setGCVM(NULL);  // Unregister VM from GC
    freeObjects();

    // Reset prelude flag so it's reloaded on next VM init
    preludeLoaded = false;
}

void setDebuggerEnabled(VM* vm, bool enabled) {
    vm->debuggerEnabled = enabled;
}

void setDebuggerBreakpointsPath(VM* vm, const char* path) {
    if (path == NULL) {
        vm->debuggerBreakpointsPath[0] = '\0';
        return;
    }
    snprintf(vm->debuggerBreakpointsPath, sizeof(vm->debuggerBreakpointsPath), "%s", path);
    loadBreakpoints(vm);
}

bool debuggerAddBreakpoint(VM* vm, int line, const char* condition) {
    if (line <= 0) return false;
    for (int i = 0; i < vm->breakpointCount; i++) {
        if (vm->breakpoints[i].line == line) {
            vm->breakpoints[i].hasCondition = (condition != NULL && condition[0] != '\0');
            vm->breakpoints[i].hitCount = 0;
            if (vm->breakpoints[i].hasCondition) {
                snprintf(vm->breakpoints[i].condition, sizeof(vm->breakpoints[i].condition), "%s", condition);
            } else {
                vm->breakpoints[i].condition[0] = '\0';
            }
            return true;
        }
    }
    if (vm->breakpointCount >= DEBUG_BREAKPOINTS_MAX) return false;
    DebugBreakpoint* bp = &vm->breakpoints[vm->breakpointCount++];
    bp->line = line;
    bp->hitCount = 0;
    bp->hasCondition = (condition != NULL && condition[0] != '\0');
    if (bp->hasCondition) {
        snprintf(bp->condition, sizeof(bp->condition), "%s", condition);
    } else {
        bp->condition[0] = '\0';
    }
    return true;
}

bool debuggerRemoveBreakpoint(VM* vm, int line) {
    for (int i = 0; i < vm->breakpointCount; i++) {
        if (vm->breakpoints[i].line == line) {
            for (int j = i; j < vm->breakpointCount - 1; j++) {
                vm->breakpoints[j] = vm->breakpoints[j + 1];
            }
            vm->breakpointCount--;
            return true;
        }
    }
    return false;
}

void debuggerClearBreakpoints(VM* vm) {
    vm->breakpointCount = 0;
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
        // Smarter hint: suggest tail-recursive style only when this looks like
        // recursive overflow (same function already active in call stack).
        bool isRecursiveOverflow = false;
        for (int i = vm->frameCount - 1; i >= 0; i--) {
            if (vm->frames[i].closure->function == closure->function) {
                isRecursiveOverflow = true;
                break;
            }
        }

        if (isRecursiveOverflow) {
            const char* fnName = closure->function->name != NULL
                               ? closure->function->name->chars
                               : "<script>";
            runtimeError(
                vm,
                "Stack overflow in recursive call to '%s'. "
                "Hint: convert recursion to tail-recursive form to benefit from tail-call optimization.",
                fnName);
        } else {
            runtimeError(vm, "Stack overflow.");
        }
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
        Value _vb = pop(vm); \
        Value _va = pop(vm); \
        int64_t b = AS_INT(_vb); \
        int64_t a = AS_INT(_va); \
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
                Value bVal = pop(vm);
                Value aVal = pop(vm);
                int64_t b = AS_INT(bVal);
                int64_t a = AS_INT(aVal);
                if (b == 0) {
                    runtimeError(vm, "Division by zero.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(vm, INT_VAL(a / b));
                break;
            }

            case OP_MODULO_INT: {
                Value bVal = pop(vm);
                Value aVal = pop(vm);
                int64_t b = AS_INT(bVal);
                int64_t a = AS_INT(aVal);
                if (b == 0) {
                    runtimeError(vm, "Division by zero.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(vm, INT_VAL(a % b));
                break;
            }

            case OP_NEGATE_INT: {
                Value value = pop(vm);
                push(vm, INT_VAL(-AS_INT(value)));
                break;
            }

            case OP_BITWISE_NOT_INT: {
                Value value = pop(vm);
                push(vm, INT_VAL(~AS_INT(value)));
                break;
            }

            case OP_BITWISE_AND_INT:
                BINARY_OP_INT(&);
                break;

            case OP_BITWISE_OR_INT:
                BINARY_OP_INT(|);
                break;

            case OP_BITWISE_XOR_INT:
                BINARY_OP_INT(^);
                break;

            case OP_SHIFT_LEFT_INT: {
                Value shiftV = pop(vm);
                Value valueV = pop(vm);
                int64_t shift = AS_INT(shiftV);
                int64_t value = AS_INT(valueV);
                if (shift < 0 || shift >= 64) {
                    runtimeError(vm, "Invalid shift amount.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(vm, INT_VAL(value << shift));
                break;
            }

            case OP_SHIFT_RIGHT_INT: {
                Value shiftVal = pop(vm);
                Value valueVal = pop(vm);
                int64_t shift = AS_INT(shiftVal);
                int64_t value = AS_INT(valueVal);
                if (shift < 0 || shift >= 64) {
                    runtimeError(vm, "Invalid shift amount.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                // Signed right shift: arithmetic shift on two's complement machines.
                push(vm, INT_VAL(value >> shift));
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
                Value valueVal = pop(vm);
                int64_t value = AS_INT(valueVal);
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
                runtimeError(vm, "Unknown opcode %d at offset %ld.",
                             instruction,
                             (long)((vm->ip - vm->chunk->code) - 1));
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
        Value _vb = pop(vm); \
        Value _va = pop(vm); \
        int64_t b = AS_INT(_vb); \
        int64_t a = AS_INT(_va); \
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

        if (vm->debuggerEnabled && vm->frameCount > 0) {
            int line = currentFrameLine(frame);
            bool lineChanged = (line != vm->debuggerLastLine);
            bool pause = false;

            if (lineChanged && shouldPauseForBreakpoint(vm, frame, line)) {
                pause = true;
            } else if (lineChanged && vm->debuggerStepMode == DEBUG_STEP_IN) {
                pause = true;
            } else if (lineChanged && vm->debuggerStepMode == DEBUG_STEP_NEXT &&
                       vm->frameCount <= vm->debuggerStepDepth) {
                pause = true;
            } else if (lineChanged && vm->debuggerStepMode == DEBUG_STEP_OUT &&
                       vm->frameCount < vm->debuggerStepDepth) {
                pause = true;
            }

            if (pause) {
                vm->debuggerLastLine = line;
                debuggerRepl(vm, frame, line);
            } else if (lineChanged) {
                vm->debuggerLastLine = line;
            }
        }

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
                push(vm, frame->closure->function->chunk.constants.values[index]);
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
                Value bVal = pop(vm);
                Value aVal = pop(vm);
                int64_t b = AS_INT(bVal);
                int64_t a = AS_INT(aVal);
                if (b == 0) {
                    runtimeError(vm, "Division by zero.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(vm, INT_VAL(a / b));
                break;
            }

            case OP_MODULO_INT: {
                Value bVal = pop(vm);
                Value aVal = pop(vm);
                int64_t b = AS_INT(bVal);
                int64_t a = AS_INT(aVal);
                if (b == 0) {
                    runtimeError(vm, "Division by zero.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(vm, INT_VAL(a % b));
                break;
            }

            case OP_NEGATE_INT:
                {
                    Value value = pop(vm);
                    push(vm, INT_VAL(-AS_INT(value)));
                }
                break;

            case OP_BITWISE_NOT_INT:
                {
                    Value value = pop(vm);
                    push(vm, INT_VAL(~AS_INT(value)));
                }
                break;

            case OP_BITWISE_AND_INT:
                BINARY_OP_INT(&);
                break;

            case OP_BITWISE_OR_INT:
                BINARY_OP_INT(|);
                break;

            case OP_BITWISE_XOR_INT:
                BINARY_OP_INT(^);
                break;

            case OP_SHIFT_LEFT_INT: {
                Value shiftV = pop(vm);
                Value valueV = pop(vm);
                int64_t shift = AS_INT(shiftV);
                int64_t value = AS_INT(valueV);
                if (shift < 0 || shift >= 64) {
                    runtimeError(vm, "Invalid shift amount.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(vm, INT_VAL(value << shift));
                break;
            }

            case OP_SHIFT_RIGHT_INT: {
                Value shiftV = pop(vm);
                Value valueV = pop(vm);
                int64_t shift = AS_INT(shiftV);
                int64_t value = AS_INT(valueV);
                if (shift < 0 || shift >= 64) {
                    runtimeError(vm, "Invalid shift amount.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                // Signed right shift: arithmetic shift on two's complement machines.
                push(vm, INT_VAL(value >> shift));
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

            case OP_TAIL_CALL: {
                int argCount = READ_BYTE();
                Value callee = peek(vm, argCount);

                // Handle native functions - fall back to normal call + return
                if (IS_OBJ(callee) && OBJ_TYPE(callee) == OBJ_NATIVE) {
                    if (!callValue(vm, callee, argCount)) {
                        return INTERPRET_RUNTIME_ERROR;
                    }
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

                // Handle class instantiation - fall back to normal call + return
                if (IS_OBJ(callee) && OBJ_TYPE(callee) == OBJ_CLASS) {
                    if (!callValue(vm, callee, argCount)) {
                        return INTERPRET_RUNTIME_ERROR;
                    }
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

                // Handle bound methods
                if (IS_OBJ(callee) && OBJ_TYPE(callee) == OBJ_BOUND_METHOD) {
                    ObjBoundMethod* bound = AS_BOUND_METHOD(callee);
                    vm->stackTop[-argCount - 1] = bound->receiver;
                    callee = OBJ_VAL(bound->method);
                }

                // Must be a closure
                if (!IS_OBJ(callee) || OBJ_TYPE(callee) != OBJ_CLOSURE) {
                    runtimeError(vm, "Can only call functions.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                ObjClosure* closure = AS_CLOSURE(callee);

                // Validate argument count
                if (argCount != closure->function->arity) {
                    runtimeError(vm, "Expected %d arguments but got %d.",
                                 closure->function->arity, argCount);
                    return INTERPRET_RUNTIME_ERROR;
                }

                // Close upvalues before reusing frame
                closeUpvalues(vm, frame->slots);

                // Move new call data to frame start
                Value* src = vm->stackTop - argCount - 1;
                Value* dst = frame->slots;
                for (int i = 0; i <= argCount; i++) {
                    dst[i] = src[i];
                }

                // Reset stack top
                vm->stackTop = frame->slots + argCount + 1;

                // Update frame to new function
                frame->closure = closure;
                frame->ip = closure->function->chunk.code;

                // frame->slots and vm->frameCount stay the same (frame reuse!)

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
                Value receiver = peek(vm, 0);
                ObjString* name = AS_STRING(READ_CONSTANT());

                if (IS_INSTANCE(receiver)) {
                    ObjInstance* instance = AS_INSTANCE(receiver);
                    Value value;
                    if (tableGet(&instance->fields, name, &value)) {
                        pop(vm);
                        push(vm, value);
                        break;
                    }

                    if (!bindMethod(vm, instance->klass, name)) {
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    break;
                }

                /* Class as receiver: read enum variant values / class fields from
                 * klass->fields (e.g. Opt.Some after SET_PROPERTY on the class). */
                if (IS_CLASS(receiver)) {
                    ObjClass* klass = AS_CLASS(receiver);
                    Value value;
                    if (tableGet(&klass->fields, name, &value)) {
                        pop(vm);
                        push(vm, value);
                        break;
                    }
                    runtimeError(vm, "Undefined property '%s' on class.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }

                runtimeError(vm, "Only instances and classes have properties.");
                return INTERPRET_RUNTIME_ERROR;
            }

            case OP_SET_PROPERTY: {
                Value valueToSet = peek(vm, 0);
                Value receiver = peek(vm, 1);
                ObjString* name = AS_STRING(READ_CONSTANT());

                if (IS_INSTANCE(receiver)) {
                    ObjInstance* instance = AS_INSTANCE(receiver);
                    tableSet(&instance->fields, name, valueToSet);
                    Value value = pop(vm);
                    pop(vm);
                    push(vm, value);
                    break;
                }

                if (IS_CLASS(receiver)) {
                    ObjClass* klass = AS_CLASS(receiver);
                    tableSet(&klass->fields, name, valueToSet);
                    Value value = pop(vm);
                    pop(vm);
                    push(vm, value);
                    break;
                }

                runtimeError(vm, "Only instances and classes have fields.");
                return INTERPRET_RUNTIME_ERROR;
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

            case OP_ARRAY_CONCAT: {
                // Pop two arrays from stack and concatenate them
                Value bVal = pop(vm);
                Value aVal = pop(vm);
                if (!IS_ARRAY(aVal) || !IS_ARRAY(bVal)) {
                    runtimeError(vm, "Can only concatenate arrays.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                ObjArray* a = AS_ARRAY(aVal);
                ObjArray* b = AS_ARRAY(bVal);
                ObjArray* result = newArray();
                // Copy elements from first array
                for (int i = 0; i < a->count; i++) {
                    arrayPush(result, a->elements[i]);
                }
                // Copy elements from second array
                for (int i = 0; i < b->count; i++) {
                    arrayPush(result, b->elements[i]);
                }
                push(vm, OBJ_VAL(result));
                break;
            }

            case OP_ARRAY_SLICE: {
                // Stack: [array, startIndex, endIndex]
                // endIndex can be nil (meaning "to end")
                Value endVal = pop(vm);
                Value startVal = pop(vm);
                Value arrayVal = pop(vm);

                if (!IS_ARRAY(arrayVal)) {
                    runtimeError(vm, "Can only slice arrays.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                if (!IS_INT(startVal)) {
                    runtimeError(vm, "Slice start index must be an integer.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                ObjArray* array = AS_ARRAY(arrayVal);
                int start = AS_INT(startVal);

                // Handle negative start index
                if (start < 0) {
                    start = array->count + start;
                }
                if (start < 0) start = 0;
                if (start > array->count) start = array->count;

                int end;
                if (IS_NIL(endVal)) {
                    // nil means "to end"
                    end = array->count;
                } else if (IS_INT(endVal)) {
                    end = AS_INT(endVal);
                    // Handle negative end index
                    if (end < 0) {
                        end = array->count + end;
                    }
                    if (end < 0) end = 0;
                    if (end > array->count) end = array->count;
                } else {
                    runtimeError(vm, "Slice end index must be an integer or nil.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                // Create sliced array
                ObjArray* result = newArray();
                for (int i = start; i < end; i++) {
                    arrayPush(result, array->elements[i]);
                }
                push(vm, OBJ_VAL(result));
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
                runtimeError(vm, "Unknown opcode %d at offset %ld.",
                             instruction,
                             (long)((vm->ip - vm->chunk->code) - 1));
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
            Value* existingValues = NULL;
            int existingCount = 0;

            if (import->nameCount > 0) {
                // Collect existing global keys
                existingKeys = (ObjString**)malloc(sizeof(ObjString*) * vm->globals.capacity);
                existingValues = (Value*)malloc(sizeof(Value) * vm->globals.capacity);
                for (int j = 0; j < vm->globals.capacity; j++) {
                    Entry* entry = &vm->globals.entries[j];
                    if (entry->key != NULL) {
                        existingKeys[existingCount++] = entry->key;
                        existingValues[existingCount - 1] = entry->value;
                    }
                }
            }

            // Execute the module to define its symbols
            if (module->function != NULL) {
                push(vm, OBJ_VAL(module->function));
                ObjClosure* closure = newClosure(module->function);
                pop(vm);
                push(vm, OBJ_VAL(closure));
                if (!call(vm, closure, 0)) {
                    return false;
                }

                InterpretResult result = executeWithFrames(vm);
                if (result != INTERPRET_OK) {
                    if (existingKeys != NULL) free(existingKeys);
                    if (existingValues != NULL) free(existingValues);
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
                    int existedIndex = -1;
                    for (int k = 0; k < existingCount; k++) {
                        if (existingKeys[k] == entry->key) {
                            existed = true;
                            existedIndex = k;
                            break;
                        }
                    }

                    bool keepImported = isNameInImportList(import, entry->key->chars, entry->key->length);

                    // New symbol not imported: remove it.
                    if (!existed && !keepImported) {
                        keysToDelete[deleteCount++] = entry->key;
                    }

                    // Existing symbol not imported: restore the previous value in case
                    // module execution overwrote a builtin/global with same name.
                    if (existed && !keepImported && existedIndex >= 0) {
                        tableSet(&vm->globals, entry->key, existingValues[existedIndex]);
                    }
                }

                // Delete the unwanted symbols
                for (int j = 0; j < deleteCount; j++) {
                    tableDelete(&vm->globals, keysToDelete[j]);
                }

                free(keysToDelete);
                free(existingKeys);
                free(existingValues);
            }
        }
    }
    return true;
}

static InterpretResult interpretInternal(VM* vm, const char* source, bool replMode) {
    bool debuggerWasEnabled = vm->debuggerEnabled;

    // Load the prelude on first execution (adds common functions to globals)
    vm->debuggerEnabled = false;
    if (!loadPrelude(vm)) {
        vm->debuggerEnabled = debuggerWasEnabled;
        return INTERPRET_COMPILE_ERROR;
    }

    // Parse
    Parser parser;
    initParser(&parser, source);

    int stmtCount = 0;
    Stmt** statements = parse(&parser, &stmtCount);

    if (statements == NULL) {
        vm->debuggerEnabled = debuggerWasEnabled;
        return INTERPRET_COMPILE_ERROR;
    }

    // Process imports first (load and execute imported modules)
    // This adds imported functions to the VM's globals
    if (!processImports(vm, statements, stmtCount)) {
        freeStatements(statements, stmtCount);
        vm->debuggerEnabled = debuggerWasEnabled;
        return INTERPRET_COMPILE_ERROR;
    }

    // Type check - skip import statements and trust imported functions
    // (imported modules are already type-checked when loaded)
    TypeChecker checker;
    initTypeChecker(&checker);

    // Add existing globals to the type checker's symbol table
    // This enables REPL persistence - variables/functions from previous inputs
    // Store them at an outer depth so current top-level declarations can shadow
    // names like prelude `sum`/`range`.
    checker.symbols.scopeDepth = -1;
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
    checker.symbols.scopeDepth = 0;

    bool typeOk = typeCheck(&checker, statements, stmtCount);

    Stmt** lowered = NULL;
    int loweredCount = 0;
    if (!lowerGenericClasses(&checker, &lowered, &loweredCount)) {
        freeTypeChecker(&checker);
        freeStatements(statements, stmtCount);
        vm->debuggerEnabled = debuggerWasEnabled;
        return INTERPRET_COMPILE_ERROR;
    }
    freeTypeChecker(&checker);

    if (!typeOk) {
        if (lowered != NULL) {
            for (int i = 0; i < loweredCount; i++) {
                freeLoweredMonomorphClassStmt(lowered[i]);
            }
            FREE_ARRAY(Stmt*, lowered, loweredCount);
        }
        freeStatements(statements, stmtCount);
        vm->debuggerEnabled = debuggerWasEnabled;
        return INTERPRET_COMPILE_ERROR;
    }

    // Compile - use compileRepl for REPL mode so globals persist
    ObjFunction* function = replMode
        ? compileReplWithPrependedClasses(lowered, loweredCount, statements, stmtCount)
        : compileWithPrependedClasses(lowered, loweredCount, statements, stmtCount);

    if (function == NULL) {
        if (lowered != NULL) {
            for (int i = 0; i < loweredCount; i++) {
                freeLoweredMonomorphClassStmt(lowered[i]);
            }
            FREE_ARRAY(Stmt*, lowered, loweredCount);
        }
        freeStatements(statements, stmtCount);
        vm->debuggerEnabled = debuggerWasEnabled;
        return INTERPRET_COMPILE_ERROR;
    }

    // Optional: disassemble compiled bytecode for debugging.
    // Enable via `BLAZE_DISASM=1 blaze <script.blaze>`
    const char* disasmEnv = getenv("BLAZE_DISASM");
    if (disasmEnv != NULL && disasmEnv[0] != '\0' && disasmEnv[0] != '0') {
        disassembleChunk(&function->chunk, "script");
        fflush(stdout);
    }

    // Root the compiled function before freeing the AST: GC may run during
    // freeStatements, and after compile ends the compiler no longer marks this
    // function (current is NULL), so it would otherwise be collected.
    push(vm, OBJ_VAL(function));

    if (lowered != NULL) {
        for (int i = 0; i < loweredCount; i++) {
            freeLoweredMonomorphClassStmt(lowered[i]);
        }
        FREE_ARRAY(Stmt*, lowered, loweredCount);
    }
    freeStatements(statements, stmtCount);

    // Set up execution with call frame
    ObjClosure* closure = newClosure(function);
    pop(vm);
    push(vm, OBJ_VAL(closure));
    if (!call(vm, closure, 0)) {
        vm->debuggerEnabled = debuggerWasEnabled;
        return INTERPRET_RUNTIME_ERROR;
    }
    vm->debuggerEnabled = debuggerWasEnabled;

    return executeWithFrames(vm);
}

InterpretResult interpret(VM* vm, const char* source) {
    return interpretInternal(vm, source, false);
}

InterpretResult interpretRepl(VM* vm, const char* source) {
    return interpretInternal(vm, source, true);
}
