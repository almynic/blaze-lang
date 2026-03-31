/* Calls, method binding, upvalues. */

#include "vm.h"
#include "vm_internal.h"
#include "object.h"
#include "memory.h"

// ============================================================================
// Function Calls
// ============================================================================

bool vm_call(VM* vm, ObjClosure* closure, int argCount) {
    if (argCount != closure->function->arity) {
        vm_runtime_error(vm, "Expected %d arguments but got %d.",
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
            vm_runtime_error(vm,
                "Stack overflow in recursive call to '%s'. "
                "Hint: convert recursion to tail-recursive form to benefit from tail-call optimization.",
                fnName);
        } else {
            vm_runtime_error(vm, "Stack overflow.");
        }
        return false;
    }

    CallFrame* frame = &vm->frames[vm->frameCount++];
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;
    frame->slots = vm->stackTop - argCount - 1;
    return true;
}

bool vm_call_value(VM* vm, Value callee, int argCount) {
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
            case OBJ_CLOSURE:
                return vm_call(vm, AS_CLOSURE(callee), argCount);

            case OBJ_NATIVE: {
                ObjNative* native = AS_NATIVE_OBJ(callee);
                if (argCount != native->arity) {
                    vm_runtime_error(vm, "Expected %d arguments but got %d.",
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
                    return vm_call(vm, AS_CLOSURE(initializer), argCount);
                } else if (argCount != 0) {
                    vm_runtime_error(vm, "Expected 0 arguments but got %d.", argCount);
                    return false;
                }
                return true;
            }

            case OBJ_BOUND_METHOD: {
                ObjBoundMethod* bound = AS_BOUND_METHOD(callee);
                vm->stackTop[-argCount - 1] = bound->receiver;
                return vm_call(vm, bound->method, argCount);
            }

            default:
                break;
        }
    }
    vm_runtime_error(vm, "Can only call functions and classes.");
    return false;
}

// ============================================================================
// Method Binding and Invocation
// ============================================================================

bool vm_bind_method(VM* vm, ObjClass* klass, ObjString* name) {
    Value method;
    if (!tableGet(&klass->methods, name, &method)) {
        vm_runtime_error(vm, "Undefined property '%s'.", name->chars);
        return false;
    }

    ObjBoundMethod* bound = newBoundMethod(peek(vm, 0), AS_CLOSURE(method));
    pop(vm);
    push(vm, OBJ_VAL(bound));
    return true;
}

bool vm_invoke_from_class(VM* vm, ObjClass* klass, ObjString* name, int argCount) {
    Value method;
    if (!tableGet(&klass->methods, name, &method)) {
        vm_runtime_error(vm, "Undefined property '%s'.", name->chars);
        return false;
    }
    return vm_call(vm, AS_CLOSURE(method), argCount);
}

bool vm_invoke(VM* vm, ObjString* name, int argCount) {
    Value receiver = peek(vm, argCount);

    if (!IS_INSTANCE(receiver)) {
        vm_runtime_error(vm, "Only instances have methods.");
        return false;
    }

    ObjInstance* instance = AS_INSTANCE(receiver);

    // First check for a field with that name (could be a function stored in field)
    Value value;
    if (tableGet(&instance->fields, name, &value)) {
        vm->stackTop[-argCount - 1] = value;
        return vm_call_value(vm, value, argCount);
    }

    return vm_invoke_from_class(vm, instance->klass, name, argCount);
}

// ============================================================================
// Upvalues
// ============================================================================

ObjUpvalue* vm_capture_upvalue(VM* vm, Value* local) {
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

void vm_close_upvalues(VM* vm, Value* last) {
    while (vm->openUpvalues != NULL && vm->openUpvalues->location >= last) {
        ObjUpvalue* upvalue = vm->openUpvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        vm->openUpvalues = upvalue->next;
    }
}
