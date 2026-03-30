/* `reallocate` implementation: used for all dynamic storage; frees when
 * newSize is 0. GC is not invoked here—allocations that trigger collection
 * go through object.c. */

#include "memory.h"

void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
    (void)oldSize; // Unused for now, will be used by GC

    if (newSize == 0) {
        free(pointer);
        return NULL;
    }

    // Hold realloc's result in a temporary: on failure it returns NULL but the
    // original block is still valid; we must not lose `pointer` before we know.
    void* newPointer = realloc(pointer, newSize);
    if (newPointer == NULL) {
        fprintf(stderr, "Out of memory!\n");
        exit(1);
    }
    return newPointer;
}

