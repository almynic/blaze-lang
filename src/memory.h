#ifndef blaze_memory_h
#define blaze_memory_h

#include "common.h"

// Forward declaration for Obj (actual definition in object.h)
typedef struct Obj Obj;

// Macro to grow capacity for dynamic arrays
#define GROW_CAPACITY(capacity) \
    ((capacity) < 8 ? 8 : (capacity) * 2)

// Macro to grow an array
#define GROW_ARRAY(type, pointer, oldCount, newCount) \
    (type*)reallocate(pointer, sizeof(type) * (oldCount), \
        sizeof(type) * (newCount))

// Macro to free an array
#define FREE_ARRAY(type, pointer, oldCount) \
    reallocate(pointer, sizeof(type) * (oldCount), 0)

// Macro to allocate a single object
#define ALLOCATE(type, count) \
    (type*)reallocate(NULL, 0, sizeof(type) * (count))

// Macro to free a single object
#define FREE(type, pointer) \
    reallocate(pointer, sizeof(type), 0)

// Core memory reallocation function
// If newSize is 0, frees the memory
// If oldSize is 0, allocates new memory
// Otherwise, reallocates
void* reallocate(void* pointer, size_t oldSize, size_t newSize);

// ============================================================================
// Garbage Collection
// ============================================================================

// Forward declare Value
#include "value.h"

// Mark an object as reachable
void markObject(Obj* object);

// Mark a value (marks the object if it's an object value)
void markValue(Value value);

// Forward declare VM for GC functions
struct VM;

// Collect garbage - called automatically or manually
void collectGarbage(struct VM* vm);

// Check if GC should run based on memory threshold
bool shouldCollectGarbage(void);

// Get memory stats
size_t getBytesAllocated(void);

// Set the VM for GC to use when collecting
void setGCVM(struct VM* vm);

#endif
