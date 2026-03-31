/* Debugger: breakpoints, REPL, JSON protocol. */

#include "vm.h"
#include "vm_internal.h"
#include "object.h"
#include "memory.h"
#include "value.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

void vm_save_breakpoints(const VM* vm) {
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

void vm_load_breakpoints(VM* vm) {
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
        int line = vm_current_frame_line(frame);
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

static void printJsonEscaped(const char* s) {
    if (s == NULL) return;
    for (const char* p = s; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;
        switch (c) {
            case '\"': printf("\\\""); break;
            case '\\': printf("\\\\"); break;
            case '\n': printf("\\n"); break;
            case '\r': printf("\\r"); break;
            case '\t': printf("\\t"); break;
            default:
                if (c < 0x20) {
                    printf("\\u%04x", (unsigned)c);
                } else {
                    putchar((char)c);
                }
                break;
        }
    }
}

static void emitProtocolStopped(VM* vm, CallFrame* frame, int line, const char* reason) {
    const char* name = "script";
    if (frame->closure->function->name != NULL) {
        name = frame->closure->function->name->chars;
    }
    int offset = vm_current_frame_offset(frame);
    printf("{\"event\":\"stopped\",\"reason\":\"");
    printJsonEscaped(reason);
    printf("\",\"line\":%d,\"offset\":%d,\"frameDepth\":%d,\"function\":\"", line, offset, vm->frameCount);
    printJsonEscaped(name);
    printf("\"}\n");
    fflush(stdout);
}

static void emitProtocolContinued(void) {
    printf("{\"event\":\"continued\"}\n");
    fflush(stdout);
}

static void emitProtocolStack(VM* vm) {
    printf("{\"event\":\"stack\",\"frames\":[");
    for (int i = vm->frameCount - 1; i >= 0; i--) {
        CallFrame* frame = &vm->frames[i];
        ObjFunction* function = frame->closure->function;
        const char* name = function->name == NULL ? "script" : function->name->chars;
        int line = vm_current_frame_line(frame);
        int offset = vm_current_frame_offset(frame);
        if (i != vm->frameCount - 1) printf(",");
        printf("{\"index\":%d,\"line\":%d,\"offset\":%d,\"function\":\"",
               vm->frameCount - 1 - i, line, offset);
        printJsonEscaped(name);
        printf("\"}");
    }
    printf("]}\n");
    fflush(stdout);
}

static void emitProtocolLocals(VM* vm, CallFrame* frame) {
    int count = 0;
    if (vm->stackTop > frame->slots) {
        count = (int)(vm->stackTop - frame->slots);
    }
    printf("{\"event\":\"locals\",\"count\":%d,\"locals\":[", count);
    for (int i = 0; i < count; i++) {
        if (i > 0) printf(",");
        printf("{\"index\":%d,\"value\":\"", i);
        Value v = frame->slots[i];
        if (IS_NIL(v)) {
            printJsonEscaped("nil");
        } else if (IS_BOOL(v)) {
            printJsonEscaped(AS_BOOL(v) ? "true" : "false");
        } else if (IS_INT(v)) {
            char buf[64];
            snprintf(buf, sizeof(buf), "%lld", (long long)AS_INT(v));
            printJsonEscaped(buf);
        } else if (IS_FLOAT(v)) {
            char buf[64];
            snprintf(buf, sizeof(buf), "%g", AS_FLOAT(v));
            printJsonEscaped(buf);
        } else if (IS_STRING(v)) {
            printJsonEscaped(AS_CSTRING(v));
        } else {
            printJsonEscaped("<object>");
        }
        printf("\"}");
    }
    printf("]}\n");
    fflush(stdout);
}

static bool extractJsonStringField(const char* input, const char* field, char* out, size_t outSize) {
    char needle[32];
    snprintf(needle, sizeof(needle), "\"%s\"", field);
    const char* p = strstr(input, needle);
    if (p == NULL) return false;
    p = strchr(p, ':');
    if (p == NULL) return false;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '\"') return false;
    p++;
    size_t n = 0;
    while (*p != '\0' && *p != '\"') {
        if (n + 1 < outSize) out[n++] = *p;
        p++;
    }
    if (*p != '\"') return false;
    out[n] = '\0';
    return true;
}

static bool extractJsonIntField(const char* input, const char* field, int* out) {
    char needle[32];
    snprintf(needle, sizeof(needle), "\"%s\"", field);
    const char* p = strstr(input, needle);
    if (p == NULL) return false;
    p = strchr(p, ':');
    if (p == NULL) return false;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (!isdigit((unsigned char)*p) && *p != '-') return false;
    *out = atoi(p);
    return true;
}

bool vm_should_pause_for_breakpoint(VM* vm, CallFrame* frame, int line) {
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

void vm_debugger_repl(VM* vm, CallFrame* frame, int line) {
    vm->debuggerPaused = true;
    if (vm->debuggerProtocolMode) {
        emitProtocolStopped(vm, frame, line, "pause");
    } else {
        printf("\n[debug] paused at line %d", line);
        if (frame->closure->function->name != NULL) {
            printf(" in %s()", frame->closure->function->name->chars);
        }
        printf("\n");
    }

    char input[256];
    while (!vm->debuggerAutoContinue) {
        if (!vm->debuggerProtocolMode) {
            printf("(blaze-debug) ");
            fflush(stdout);
        }
        if (fgets(input, sizeof(input), stdin) == NULL) {
            vm->debuggerAutoContinue = true;
            vm->debuggerStepMode = DEBUG_STEP_NONE;
            break;
        }
        char* nl = strchr(input, '\n');
        if (nl) *nl = '\0';

        if (vm->debuggerProtocolMode) {
            char cmd[64];
            char cond[128];
            int bpLine = 0;
            bool haveCmd = extractJsonStringField(input, "command", cmd, sizeof(cmd)) ||
                           extractJsonStringField(input, "cmd", cmd, sizeof(cmd));
            if (!haveCmd) {
                printf("{\"event\":\"error\",\"message\":\"missing command\"}\n");
                fflush(stdout);
                continue;
            }

            if (strcmp(cmd, "continue") == 0) {
                vm->debuggerStepMode = DEBUG_STEP_NONE;
                vm->debuggerAutoContinue = true;
                emitProtocolContinued();
            } else if (strcmp(cmd, "step") == 0) {
                vm->debuggerStepMode = DEBUG_STEP_IN;
                vm->debuggerAutoContinue = true;
                emitProtocolContinued();
            } else if (strcmp(cmd, "next") == 0) {
                vm->debuggerStepMode = DEBUG_STEP_NEXT;
                vm->debuggerStepDepth = vm->frameCount;
                vm->debuggerAutoContinue = true;
                emitProtocolContinued();
            } else if (strcmp(cmd, "out") == 0) {
                vm->debuggerStepMode = DEBUG_STEP_OUT;
                vm->debuggerStepDepth = vm->frameCount;
                vm->debuggerAutoContinue = true;
                emitProtocolContinued();
            } else if (strcmp(cmd, "stack") == 0 || strcmp(cmd, "bt") == 0) {
                emitProtocolStack(vm);
            } else if (strcmp(cmd, "locals") == 0) {
                emitProtocolLocals(vm, frame);
            } else if (strcmp(cmd, "setBreakpoint") == 0) {
                if (!extractJsonIntField(input, "line", &bpLine)) {
                    printf("{\"event\":\"error\",\"message\":\"setBreakpoint requires line\"}\n");
                    fflush(stdout);
                    continue;
                }
                bool hasCond = extractJsonStringField(input, "condition", cond, sizeof(cond));
                bool ok = debuggerAddBreakpoint(vm, bpLine, hasCond ? cond : NULL);
                if (ok) vm_save_breakpoints(vm);
                printf("{\"event\":\"breakpoint\",\"action\":\"set\",\"ok\":%s,\"line\":%d}\n",
                       ok ? "true" : "false", bpLine);
                fflush(stdout);
            } else if (strcmp(cmd, "removeBreakpoint") == 0) {
                if (!extractJsonIntField(input, "line", &bpLine)) {
                    printf("{\"event\":\"error\",\"message\":\"removeBreakpoint requires line\"}\n");
                    fflush(stdout);
                    continue;
                }
                bool ok = debuggerRemoveBreakpoint(vm, bpLine);
                if (ok) vm_save_breakpoints(vm);
                printf("{\"event\":\"breakpoint\",\"action\":\"remove\",\"ok\":%s,\"line\":%d}\n",
                       ok ? "true" : "false", bpLine);
                fflush(stdout);
            } else {
                printf("{\"event\":\"error\",\"message\":\"unknown command\"}\n");
                fflush(stdout);
            }
            continue;
        }

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
            vm_save_breakpoints(vm);
        } else if (strncmp(input, "delete ", 7) == 0) {
            int bpLine = atoi(input + 7);
            bool ok = debuggerRemoveBreakpoint(vm, bpLine);
            printf(ok ? "deleted breakpoint\n" : "breakpoint not found\n");
            vm_save_breakpoints(vm);
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
void setDebuggerEnabled(VM* vm, bool enabled) {
    vm->debuggerEnabled = enabled;
    vm->debuggerLastLine = -1;
    vm->debuggerLastFrameDepth = 0;
    vm->debuggerLastFunction = NULL;
}

void setDebuggerProtocolMode(VM* vm, bool enabled) {
    vm->debuggerProtocolMode = enabled;
}

void setDebuggerBreakpointsPath(VM* vm, const char* path) {
    if (path == NULL) {
        vm->debuggerBreakpointsPath[0] = '\0';
        return;
    }
    snprintf(vm->debuggerBreakpointsPath, sizeof(vm->debuggerBreakpointsPath), "%s", path);
    vm_load_breakpoints(vm);
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
