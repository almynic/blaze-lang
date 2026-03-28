#ifndef blaze_chunk_h
#define blaze_chunk_h

#include "common.h"
#include "value.h"

// Bytecode operation codes
typedef enum {
    // Constants
    OP_CONSTANT,        // Push constant onto stack
    OP_CONSTANT_LONG,   // Push constant with 24-bit index

    // Literals
    OP_NIL,
    OP_TRUE,
    OP_FALSE,

    // Stack operations
    OP_POP,
    OP_DUP,             // Duplicate top of stack

    // Variables
    OP_GET_LOCAL,
    OP_SET_LOCAL,
    OP_GET_GLOBAL,
    OP_DEFINE_GLOBAL,
    OP_SET_GLOBAL,
    OP_GET_UPVALUE,
    OP_SET_UPVALUE,

    // Comparison
    OP_EQUAL,
    OP_NOT_EQUAL,
    OP_GREATER,
    OP_GREATER_EQUAL,
    OP_LESS,
    OP_LESS_EQUAL,

    // Arithmetic (integers)
    OP_ADD_INT,
    OP_SUBTRACT_INT,
    OP_MULTIPLY_INT,
    OP_DIVIDE_INT,
    OP_MODULO_INT,
    OP_NEGATE_INT,

    // Arithmetic (floats)
    OP_ADD_FLOAT,
    OP_SUBTRACT_FLOAT,
    OP_MULTIPLY_FLOAT,
    OP_DIVIDE_FLOAT,
    OP_NEGATE_FLOAT,

    // Mixed arithmetic (int + float -> float)
    OP_ADD_MIXED,
    OP_SUBTRACT_MIXED,
    OP_MULTIPLY_MIXED,
    OP_DIVIDE_MIXED,

    // Type conversions
    OP_INT_TO_FLOAT,
    OP_FLOAT_TO_INT,

    // Logical
    OP_NOT,

    // String operations
    OP_CONCAT,          // String concatenation

    // Control flow
    OP_JUMP,
    OP_JUMP_IF_FALSE,
    OP_JUMP_IF_TRUE,
    OP_LOOP,

    // Functions
    OP_CALL,
    OP_CLOSURE,
    OP_CLOSE_UPVALUE,
    OP_RETURN,

    // Classes
    OP_CLASS,
    OP_GET_PROPERTY,
    OP_SET_PROPERTY,
    OP_METHOD,
    OP_INVOKE,
    OP_INHERIT,
    OP_GET_SUPER,
    OP_SUPER_INVOKE,

    // Arrays
    OP_ARRAY,           // Create array with N elements
    OP_INDEX_GET,       // array[index]
    OP_INDEX_SET,       // array[index] = value
    OP_ARRAY_LENGTH,    // Get array length
    OP_RANGE,           // Create array from range (start..end)

    // Built-in
    OP_PRINT,

    // Exception handling
    OP_TRY,             // Begin try block (operand: catch offset)
    OP_TRY_END,         // End try block (no exception)
    OP_THROW,           // Throw exception
} OpCode;

// A chunk of bytecode
typedef struct {
    int count;
    int capacity;
    uint8_t* code;
    int* lines;         // Line numbers for error reporting
    ValueArray constants;
} Chunk;

// Chunk functions
void initChunk(Chunk* chunk);
void freeChunk(Chunk* chunk);
void writeChunk(Chunk* chunk, uint8_t byte, int line);

// Add a constant and return its index
int addConstant(Chunk* chunk, Value value);

// Write a constant instruction (handles long constants automatically)
void writeConstant(Chunk* chunk, Value value, int line);

#endif
