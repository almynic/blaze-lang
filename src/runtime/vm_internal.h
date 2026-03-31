#ifndef BLAZE_VM_INTERNAL_H
#define BLAZE_VM_INTERNAL_H

/* Shared declarations for VM implementation split across vm_*.c */

#include "vm.h"
#include "object.h"

void vm_runtime_error(VM* vm, const char* format, ...);
void vm_reset_stack(VM* vm);
int vm_current_frame_line(const CallFrame* frame);
int vm_current_frame_offset(const CallFrame* frame);

bool vm_call(VM* vm, ObjClosure* closure, int argCount);
bool vm_call_value(VM* vm, Value callee, int argCount);
bool vm_bind_method(VM* vm, ObjClass* klass, ObjString* name);
bool vm_invoke_from_class(VM* vm, ObjClass* klass, ObjString* name, int argCount);
bool vm_invoke(VM* vm, ObjString* name, int argCount);
ObjUpvalue* vm_capture_upvalue(VM* vm, Value* local);
void vm_close_upvalues(VM* vm, Value* last);

InterpretResult vm_execute_legacy(VM* vm);
InterpretResult vm_execute_with_frames(VM* vm);

void vm_define_native(VM* vm, const char* name, NativeFn function, int arity);
void vm_register_natives(VM* vm);

bool vm_should_pause_for_breakpoint(VM* vm, CallFrame* frame, int line);
void vm_debugger_repl(VM* vm, CallFrame* frame, int line);
void vm_load_breakpoints(VM* vm);
void vm_save_breakpoints(const VM* vm);

#endif
