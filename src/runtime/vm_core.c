/* VM core: stack, runtime errors, frame line/offset helpers. */

#include "vm.h"
#include "vm_internal.h"
#include "object.h"
#include "memory.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

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

void vm_reset_stack(VM* vm) {
    vm->stackTop = vm->stack;
    vm->frameCount = 0;
    vm->openUpvalues = NULL;
    vm->exceptionHandlerCount = 0;
}

// ============================================================================
// Error Handling
// ============================================================================

void vm_runtime_error(VM* vm, const char* format, ...) {
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

    vm_reset_stack(vm);
}

int vm_current_frame_line(const CallFrame* frame) {
    ObjFunction* function = frame->closure->function;
    ptrdiff_t rawInstruction = (frame->ip - function->chunk.code) - 1;
    if (rawInstruction < 0) rawInstruction = 0;
    if (function->chunk.count <= 0) return 0;
    if (rawInstruction >= function->chunk.count) {
        rawInstruction = function->chunk.count - 1;
    }
    return function->chunk.lines[rawInstruction];
}

int vm_current_frame_offset(const CallFrame* frame) {
    ObjFunction* function = frame->closure->function;
    ptrdiff_t rawInstruction = (frame->ip - function->chunk.code) - 1;
    if (rawInstruction < 0) rawInstruction = 0;
    if (function->chunk.count <= 0) return 0;
    if (rawInstruction >= function->chunk.count) {
        rawInstruction = function->chunk.count - 1;
    }
    return (int)rawInstruction;
}
