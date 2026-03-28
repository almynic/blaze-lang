#ifndef blaze_debug_h
#define blaze_debug_h

#include "chunk.h"

// Disassemble an entire chunk
void disassembleChunk(Chunk* chunk, const char* name);

// Disassemble a single instruction and return the offset of the next instruction
int disassembleInstruction(Chunk* chunk, int offset);

#endif
