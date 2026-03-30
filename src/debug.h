#ifndef blaze_debug_h
#define blaze_debug_h

/* Bytecode disassembler: maps opcodes to readable lines (used with
 * DEBUG_PRINT_CODE and for debugging the compiler/VM). */

#include "chunk.h"

// Disassemble an entire chunk
void disassembleChunk(Chunk* chunk, const char* name);

// Disassemble a single instruction and return the offset of the next instruction
int disassembleInstruction(Chunk* chunk, int offset);

#endif
