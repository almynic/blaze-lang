#ifndef blaze_vm_h
#define blaze_vm_h

#include "common.h"
#include "chunk.h"
#include "value.h"
#include "object.h"

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR,
} InterpretResult;

// Call frame - tracks a single ongoing function call
typedef struct {
    ObjClosure* closure;    // The function being called
    uint8_t* ip;            // Instruction pointer within this function
    Value* slots;           // First slot this function can use
} CallFrame;

// Exception handler - tracks a try block
#define EXCEPTION_HANDLERS_MAX 64
typedef struct {
    int frameIndex;         // Which call frame this handler belongs to
    uint8_t* catchIp;       // Where to jump on exception
    Value* stackTop;        // Stack top when try was entered
} ExceptionHandler;

typedef struct VM {
    CallFrame frames[FRAMES_MAX];
    int frameCount;

    Value stack[STACK_MAX];
    Value* stackTop;

    ObjUpvalue* openUpvalues;  // Linked list of open upvalues

    Table globals;             // Global variables and native functions

    // Exception handling
    ExceptionHandler exceptionHandlers[EXCEPTION_HANDLERS_MAX];
    int exceptionHandlerCount;

    // Legacy fields for simple execution (will be removed)
    Chunk* chunk;
    uint8_t* ip;
} VM;

// Initialize the VM
void initVM(VM* vm);

// Free VM resources
void freeVM(VM* vm);

// Run a chunk of bytecode (legacy)
InterpretResult run(VM* vm, Chunk* chunk);

// Interpret source code (full pipeline)
InterpretResult interpret(VM* vm, const char* source);

// Interpret source for REPL (globals persist between calls)
InterpretResult interpretRepl(VM* vm, const char* source);

// Stack operations
void push(VM* vm, Value value);
Value pop(VM* vm);
Value peek(VM* vm, int distance);

#endif
