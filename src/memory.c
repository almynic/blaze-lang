#include "memory.h"

void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
    (void)oldSize; // Unused for now, will be used by GC

    if (newSize == 0) {
        free(pointer);
        return NULL;
    }

    void* result = realloc(pointer, newSize);
    if (result == NULL) {
        fprintf(stderr, "Out of memory!\n");
        exit(1);
    }
    return result;
}

