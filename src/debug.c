#include "debug.h"
#include "value.h"

void disassembleChunk(Chunk* chunk, const char* name) {
    printf("== %s ==\n", name);

    for (int offset = 0; offset < chunk->count;) {
        offset = disassembleInstruction(chunk, offset);
    }
}

static int simpleInstruction(const char* name, int offset) {
    printf("%s\n", name);
    return offset + 1;
}

static int byteInstruction(const char* name, Chunk* chunk, int offset) {
    uint8_t slot = chunk->code[offset + 1];
    printf("%-16s %4d\n", name, slot);
    return offset + 2;
}

static int jumpInstruction(const char* name, int sign, Chunk* chunk, int offset) {
    uint16_t jump = (uint16_t)(chunk->code[offset + 1] << 8);
    jump |= chunk->code[offset + 2];
    printf("%-16s %4d -> %d\n", name, offset, offset + 3 + sign * jump);
    return offset + 3;
}

static int constantInstruction(const char* name, Chunk* chunk, int offset) {
    uint8_t constant = chunk->code[offset + 1];
    printf("%-16s %4d '", name, constant);
    printValue(chunk->constants.values[constant]);
    printf("'\n");
    return offset + 2;
}

static int constantLongInstruction(const char* name, Chunk* chunk, int offset) {
    uint32_t constant = chunk->code[offset + 1];
    constant |= (uint32_t)chunk->code[offset + 2] << 8;
    constant |= (uint32_t)chunk->code[offset + 3] << 16;
    printf("%-16s %4d '", name, constant);
    printValue(chunk->constants.values[constant]);
    printf("'\n");
    return offset + 4;
}

int disassembleInstruction(Chunk* chunk, int offset) {
    printf("%04d ", offset);

    if (offset > 0 && chunk->lines[offset] == chunk->lines[offset - 1]) {
        printf("   | ");
    } else {
        printf("%4d ", chunk->lines[offset]);
    }

    uint8_t instruction = chunk->code[offset];
    switch (instruction) {
        case OP_CONSTANT:
            return constantInstruction("OP_CONSTANT", chunk, offset);
        case OP_CONSTANT_LONG:
            return constantLongInstruction("OP_CONSTANT_LONG", chunk, offset);
        case OP_NIL:
            return simpleInstruction("OP_NIL", offset);
        case OP_TRUE:
            return simpleInstruction("OP_TRUE", offset);
        case OP_FALSE:
            return simpleInstruction("OP_FALSE", offset);
        case OP_POP:
            return simpleInstruction("OP_POP", offset);
        case OP_DUP:
            return simpleInstruction("OP_DUP", offset);
        case OP_GET_LOCAL:
            return byteInstruction("OP_GET_LOCAL", chunk, offset);
        case OP_SET_LOCAL:
            return byteInstruction("OP_SET_LOCAL", chunk, offset);
        case OP_GET_GLOBAL:
            return constantInstruction("OP_GET_GLOBAL", chunk, offset);
        case OP_DEFINE_GLOBAL:
            return constantInstruction("OP_DEFINE_GLOBAL", chunk, offset);
        case OP_SET_GLOBAL:
            return constantInstruction("OP_SET_GLOBAL", chunk, offset);
        case OP_GET_UPVALUE:
            return byteInstruction("OP_GET_UPVALUE", chunk, offset);
        case OP_SET_UPVALUE:
            return byteInstruction("OP_SET_UPVALUE", chunk, offset);
        case OP_EQUAL:
            return simpleInstruction("OP_EQUAL", offset);
        case OP_NOT_EQUAL:
            return simpleInstruction("OP_NOT_EQUAL", offset);
        case OP_GREATER:
            return simpleInstruction("OP_GREATER", offset);
        case OP_GREATER_EQUAL:
            return simpleInstruction("OP_GREATER_EQUAL", offset);
        case OP_LESS:
            return simpleInstruction("OP_LESS", offset);
        case OP_LESS_EQUAL:
            return simpleInstruction("OP_LESS_EQUAL", offset);
        case OP_ADD_INT:
            return simpleInstruction("OP_ADD_INT", offset);
        case OP_SUBTRACT_INT:
            return simpleInstruction("OP_SUBTRACT_INT", offset);
        case OP_MULTIPLY_INT:
            return simpleInstruction("OP_MULTIPLY_INT", offset);
        case OP_DIVIDE_INT:
            return simpleInstruction("OP_DIVIDE_INT", offset);
        case OP_MODULO_INT:
            return simpleInstruction("OP_MODULO_INT", offset);
        case OP_NEGATE_INT:
            return simpleInstruction("OP_NEGATE_INT", offset);
        case OP_ADD_FLOAT:
            return simpleInstruction("OP_ADD_FLOAT", offset);
        case OP_SUBTRACT_FLOAT:
            return simpleInstruction("OP_SUBTRACT_FLOAT", offset);
        case OP_MULTIPLY_FLOAT:
            return simpleInstruction("OP_MULTIPLY_FLOAT", offset);
        case OP_DIVIDE_FLOAT:
            return simpleInstruction("OP_DIVIDE_FLOAT", offset);
        case OP_NEGATE_FLOAT:
            return simpleInstruction("OP_NEGATE_FLOAT", offset);
        case OP_ADD_MIXED:
            return simpleInstruction("OP_ADD_MIXED", offset);
        case OP_SUBTRACT_MIXED:
            return simpleInstruction("OP_SUBTRACT_MIXED", offset);
        case OP_MULTIPLY_MIXED:
            return simpleInstruction("OP_MULTIPLY_MIXED", offset);
        case OP_DIVIDE_MIXED:
            return simpleInstruction("OP_DIVIDE_MIXED", offset);
        case OP_INT_TO_FLOAT:
            return simpleInstruction("OP_INT_TO_FLOAT", offset);
        case OP_FLOAT_TO_INT:
            return simpleInstruction("OP_FLOAT_TO_INT", offset);
        case OP_NOT:
            return simpleInstruction("OP_NOT", offset);
        case OP_CONCAT:
            return simpleInstruction("OP_CONCAT", offset);
        case OP_JUMP:
            return jumpInstruction("OP_JUMP", 1, chunk, offset);
        case OP_JUMP_IF_FALSE:
            return jumpInstruction("OP_JUMP_IF_FALSE", 1, chunk, offset);
        case OP_JUMP_IF_TRUE:
            return jumpInstruction("OP_JUMP_IF_TRUE", 1, chunk, offset);
        case OP_LOOP:
            return jumpInstruction("OP_LOOP", -1, chunk, offset);
        case OP_CALL:
            return byteInstruction("OP_CALL", chunk, offset);
        case OP_CLOSURE: {
            offset++;
            uint8_t constant = chunk->code[offset++];
            printf("%-16s %4d ", "OP_CLOSURE", constant);
            printValue(chunk->constants.values[constant]);
            printf("\n");
            // TODO: Print upvalue info
            return offset;
        }
        case OP_CLOSE_UPVALUE:
            return simpleInstruction("OP_CLOSE_UPVALUE", offset);
        case OP_RETURN:
            return simpleInstruction("OP_RETURN", offset);
        case OP_TAIL_CALL:
            return byteInstruction("OP_TAIL_CALL", chunk, offset);
        case OP_CLASS:
            return constantInstruction("OP_CLASS", chunk, offset);
        case OP_GET_PROPERTY:
            return constantInstruction("OP_GET_PROPERTY", chunk, offset);
        case OP_SET_PROPERTY:
            return constantInstruction("OP_SET_PROPERTY", chunk, offset);
        case OP_METHOD:
            return constantInstruction("OP_METHOD", chunk, offset);
        case OP_INVOKE: {
            uint8_t constant = chunk->code[offset + 1];
            uint8_t argCount = chunk->code[offset + 2];
            printf("%-16s (%d args) %4d '", "OP_INVOKE", argCount, constant);
            printValue(chunk->constants.values[constant]);
            printf("'\n");
            return offset + 3;
        }
        case OP_INHERIT:
            return simpleInstruction("OP_INHERIT", offset);
        case OP_GET_SUPER:
            return constantInstruction("OP_GET_SUPER", chunk, offset);
        case OP_SUPER_INVOKE: {
            uint8_t constant = chunk->code[offset + 1];
            uint8_t argCount = chunk->code[offset + 2];
            printf("%-16s (%d args) %4d '", "OP_SUPER_INVOKE", argCount, constant);
            printValue(chunk->constants.values[constant]);
            printf("'\n");
            return offset + 3;
        }
        case OP_ARRAY:
            return byteInstruction("OP_ARRAY", chunk, offset);
        case OP_INDEX_GET:
            return simpleInstruction("OP_INDEX_GET", offset);
        case OP_INDEX_SET:
            return simpleInstruction("OP_INDEX_SET", offset);
        case OP_ARRAY_LENGTH:
            return simpleInstruction("OP_ARRAY_LENGTH", offset);
        case OP_RANGE:
            return simpleInstruction("OP_RANGE", offset);
        case OP_ARRAY_CONCAT:
            return simpleInstruction("OP_ARRAY_CONCAT", offset);
        case OP_ARRAY_SLICE:
            return simpleInstruction("OP_ARRAY_SLICE", offset);
        case OP_PRINT:
            return simpleInstruction("OP_PRINT", offset);
        case OP_TRY:
            return jumpInstruction("OP_TRY", 1, chunk, offset);
        case OP_TRY_END:
            return simpleInstruction("OP_TRY_END", offset);
        case OP_THROW:
            return simpleInstruction("OP_THROW", offset);
        default:
            printf("Unknown opcode %d\n", instruction);
            return offset + 1;
    }
}
