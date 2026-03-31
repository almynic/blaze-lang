/* Opcode dispatch: legacy chunk and framed execution. */

#include "vm.h"
#include "vm_internal.h"
#include "chunk.h"
#include "object.h"
#include "memory.h"
#include "debug.h"
#include <stdio.h>
#include <string.h>

// ============================================================================
// Execution (Legacy - for simple scripts without functions)
// ============================================================================

InterpretResult vm_execute_legacy(VM* vm) {
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
                    vm_runtime_error(vm, "Division by zero.");
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
                    vm_runtime_error(vm, "Division by zero.");
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
                    vm_runtime_error(vm, "Invalid shift amount.");
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
                    vm_runtime_error(vm, "Invalid shift amount.");
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
                    if (!vm_call_value(vm, callee, argCount)) {
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
                        closure->upvalues[i] = vm_capture_upvalue(vm, vm->stack + index);
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
                vm_close_upvalues(vm, vm->stackTop - 1);
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
                vm_runtime_error(vm, "Unknown opcode %d at offset %ld.",
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

InterpretResult vm_execute_with_frames(VM* vm) {
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
            int line = vm_current_frame_line(frame);
            int frameDepth = vm->frameCount;
            ObjFunction* function = frame->closure->function;
            bool sourceLocationChanged =
                (line != vm->debuggerLastLine) ||
                (frameDepth != vm->debuggerLastFrameDepth) ||
                (function != vm->debuggerLastFunction);
            bool pause = false;

            if (sourceLocationChanged && vm_should_pause_for_breakpoint(vm, frame, line)) {
                pause = true;
            } else if (sourceLocationChanged && vm->debuggerStepMode == DEBUG_STEP_IN) {
                pause = true;
            } else if (sourceLocationChanged && vm->debuggerStepMode == DEBUG_STEP_NEXT &&
                       vm->frameCount <= vm->debuggerStepDepth) {
                pause = true;
            } else if (sourceLocationChanged && vm->debuggerStepMode == DEBUG_STEP_OUT &&
                       vm->frameCount < vm->debuggerStepDepth) {
                pause = true;
            }

            if (pause) {
                vm->debuggerLastLine = line;
                vm->debuggerLastFrameDepth = frameDepth;
                vm->debuggerLastFunction = function;
                vm_debugger_repl(vm, frame, line);
            } else if (sourceLocationChanged) {
                vm->debuggerLastLine = line;
                vm->debuggerLastFrameDepth = frameDepth;
                vm->debuggerLastFunction = function;
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
                    vm_runtime_error(vm, "Division by zero.");
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
                    vm_runtime_error(vm, "Division by zero.");
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
                    vm_runtime_error(vm, "Invalid shift amount.");
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
                    vm_runtime_error(vm, "Invalid shift amount.");
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
                if (!vm_call_value(vm, peek(vm, argCount), argCount)) {
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
                        closure->upvalues[i] = vm_capture_upvalue(vm, frame->slots + index);
                    } else {
                        closure->upvalues[i] = frame->closure->upvalues[index];
                    }
                }
                break;
            }

            case OP_CLOSE_UPVALUE:
                vm_close_upvalues(vm, vm->stackTop - 1);
                pop(vm);
                break;

            case OP_RETURN: {
                Value result = pop(vm);
                vm_close_upvalues(vm, frame->slots);
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
                    if (!vm_call_value(vm, callee, argCount)) {
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    Value result = pop(vm);
                    vm_close_upvalues(vm, frame->slots);
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
                    if (!vm_call_value(vm, callee, argCount)) {
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    Value result = pop(vm);
                    vm_close_upvalues(vm, frame->slots);
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
                    vm_runtime_error(vm, "Can only call functions.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                ObjClosure* closure = AS_CLOSURE(callee);

                // Validate argument count
                if (argCount != closure->function->arity) {
                    vm_runtime_error(vm, "Expected %d arguments but got %d.",
                                 closure->function->arity, argCount);
                    return INTERPRET_RUNTIME_ERROR;
                }

                // Close upvalues before reusing frame
                vm_close_upvalues(vm, frame->slots);

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

                    if (!vm_bind_method(vm, instance->klass, name)) {
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
                    vm_runtime_error(vm, "Undefined property '%s' on class.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }

                vm_runtime_error(vm, "Only instances and classes have properties.");
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

                vm_runtime_error(vm, "Only instances and classes have fields.");
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
                if (!vm_invoke(vm, method, argCount)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                frame = &vm->frames[vm->frameCount - 1];
                break;
            }

            case OP_INHERIT: {
                Value superclass = peek(vm, 1);
                if (!IS_CLASS(superclass)) {
                    vm_runtime_error(vm, "Superclass must be a class.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                ObjClass* subclass = AS_CLASS(peek(vm, 0));
                tableAddAll(&AS_CLASS(superclass)->methods, &subclass->methods);
                break;
            }

            case OP_GET_SUPER: {
                ObjString* name = AS_STRING(READ_CONSTANT());
                ObjClass* superclass = AS_CLASS(pop(vm));

                if (!vm_bind_method(vm, superclass, name)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }

            case OP_SUPER_INVOKE: {
                ObjString* method = AS_STRING(READ_CONSTANT());
                int argCount = READ_BYTE();
                ObjClass* superclass = AS_CLASS(pop(vm));
                if (!vm_invoke_from_class(vm, superclass, method, argCount)) {
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
                    vm_runtime_error(vm, "Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(vm, value);
                break;
            }

            case OP_SET_GLOBAL: {
                ObjString* name = AS_STRING(READ_CONSTANT());
                if (tableSet(&vm->globals, name, peek(vm, 0))) {
                    tableDelete(&vm->globals, name);
                    vm_runtime_error(vm, "Undefined variable '%s'.", name->chars);
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
                    vm_runtime_error(vm, "Can only index into arrays.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                if (!IS_INT(indexVal)) {
                    vm_runtime_error(vm, "Array index must be an integer.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                ObjArray* array = AS_ARRAY(arrayVal);
                int index = (int)AS_INT(indexVal);

                if (index < 0 || index >= array->count) {
                    vm_runtime_error(vm, "Array index %d out of bounds (length %d).", index, array->count);
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
                    vm_runtime_error(vm, "Can only index into arrays.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                if (!IS_INT(indexVal)) {
                    vm_runtime_error(vm, "Array index must be an integer.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                ObjArray* array = AS_ARRAY(arrayVal);
                int index = (int)AS_INT(indexVal);

                if (index < 0 || index >= array->count) {
                    vm_runtime_error(vm, "Array index %d out of bounds (length %d).", index, array->count);
                    return INTERPRET_RUNTIME_ERROR;
                }

                array->elements[index] = value;
                push(vm, value);
                break;
            }

            case OP_ARRAY_LENGTH: {
                Value arrayVal = pop(vm);
                if (!IS_ARRAY(arrayVal)) {
                    vm_runtime_error(vm, "Can only get length of arrays.");
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
                    vm_runtime_error(vm, "Range bounds must be integers.");
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
                    vm_runtime_error(vm, "Can only concatenate arrays.");
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
                    vm_runtime_error(vm, "Can only slice arrays.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                if (!IS_INT(startVal)) {
                    vm_runtime_error(vm, "Slice start index must be an integer.");
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
                    vm_runtime_error(vm, "Slice end index must be an integer or nil.");
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
                    vm_runtime_error(vm, "Exception handler stack overflow.");
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
                        vm_runtime_error(vm, "Uncaught exception: %s", AS_CSTRING(exception));
                    } else {
                        vm_runtime_error(vm, "Uncaught exception.");
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
                vm_runtime_error(vm, "Unknown opcode %d at offset %ld.",
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
