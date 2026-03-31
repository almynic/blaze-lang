/* Object allocation, string interning, table/map support, GC mark/sweep,
 * and constructors for core runtime types (functions, classes, etc.). */

#include "vm.h"       // Must be first to get full VM definition
#include "object.h"
#include "memory.h"
#include "compiler.h"  // For markCompilerRoots
#include <string.h>
#include <stdio.h>

// Global linked list of all objects (for GC)
static Obj* objects = NULL;

// Memory tracking for GC
static size_t bytesAllocated = 0;
static size_t nextGC = 1024 * 1024;  // Initial threshold: 1MB

// Global VM pointer for GC (set by setGCVM)
static struct VM* gcVM = NULL;

// Debug flag for GC logging
#ifdef DEBUG_LOG_GC
#include "debug.h"
#endif

// ============================================================================
// Object Allocation
// ============================================================================

#define ALLOCATE_OBJ(type, objectType) \
    (type*)allocateObject(sizeof(type), objectType)

static Obj* allocateObject(size_t size, ObjType type) {
    bytesAllocated += size;

#ifdef DEBUG_STRESS_GC
    // Stress test: collect garbage on every allocation
    if (gcVM != NULL) {
        collectGarbage(gcVM);
    }
#else
    // Normal mode: collect when threshold exceeded
    if (gcVM != NULL && bytesAllocated > nextGC) {
        collectGarbage(gcVM);
    }
#endif

    Obj* object = (Obj*)reallocate(NULL, 0, size);

    // NaN boxing: Verify pointer fits in 48 bits
    // Modern x86-64 and ARM64 use 48-bit virtual addressing, so this should always pass
    uintptr_t ptr = (uintptr_t)object;
    if (ptr > 0x0000FFFFFFFFFFFFULL) {
        fprintf(stderr, "Fatal: Object pointer exceeds 48 bits: %p\n", object);
        fprintf(stderr, "This system's pointer addressing exceeds NaN boxing limitations.\n");
        exit(1);
    }

    object->type = type;
    object->isMarked = false;
    object->next = objects;
    objects = object;

#ifdef DEBUG_LOG_GC
    printf("%p allocate %zu for %d\n", (void*)object, size, type);
#endif

    return object;
}

// ============================================================================
// String Objects
// ============================================================================

static uint32_t hashString(const char* key, int length) {
    // FNV-1a hash
    uint32_t hash = 2166136261u;
    for (int i = 0; i < length; i++) {
        hash ^= (uint8_t)key[i];
        hash *= 16777619;
    }
    return hash;
}

static ObjString* allocateString(char* chars, int length, uint32_t hash) {
    ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
    string->length = length;
    string->chars = chars;
    string->hash = hash;
    return string;
}

ObjString* copyString(const char* chars, int length) {
    uint32_t hash = hashString(chars, length);

    // Check if the string already exists in the intern table
    if (gcVM != NULL) {
        ObjString* interned = tableFindString(&gcVM->strings, chars, length, hash);
        if (interned != NULL) {
            return interned;
        }
    }

    // String doesn't exist, create a new one
    char* heapChars = ALLOCATE(char, length + 1);
    memcpy(heapChars, chars, length);
    heapChars[length] = '\0';

    ObjString* string = allocateString(heapChars, length, hash);

    // Add to intern table
    if (gcVM != NULL) {
        tableSet(&gcVM->strings, string, NIL_VAL);
    }

    return string;
}

ObjString* takeString(char* chars, int length) {
    uint32_t hash = hashString(chars, length);

    // Check if the string already exists in the intern table
    if (gcVM != NULL) {
        ObjString* interned = tableFindString(&gcVM->strings, chars, length, hash);
        if (interned != NULL) {
            // String already exists, free the one we were given and return the interned one
            FREE_ARRAY(char, chars, length + 1);
            return interned;
        }
    }

    // String doesn't exist, create a new one with the given chars
    ObjString* string = allocateString(chars, length, hash);

    // Add to intern table
    if (gcVM != NULL) {
        tableSet(&gcVM->strings, string, NIL_VAL);
    }

    return string;
}

// ============================================================================
// Array Objects
// ============================================================================

ObjArray* newArray(void) {
    ObjArray* array = ALLOCATE_OBJ(ObjArray, OBJ_ARRAY);
    array->count = 0;
    array->capacity = 0;
    array->elements = NULL;
    return array;
}

ObjHashMap* newHashMapObj(void) {
    ObjHashMap* map = ALLOCATE_OBJ(ObjHashMap, OBJ_HASH_MAP);
    initValueTable(&map->table);
    return map;
}

ObjHashSet* newHashSetObj(void) {
    ObjHashSet* set = ALLOCATE_OBJ(ObjHashSet, OBJ_HASH_SET);
    initValueTable(&set->table);
    return set;
}

void arrayPush(ObjArray* array, Value value) {
    if (array->capacity < array->count + 1) {
        int oldCapacity = array->capacity;
        array->capacity = GROW_CAPACITY(oldCapacity);
        array->elements = GROW_ARRAY(Value, array->elements, oldCapacity, array->capacity);
    }
    array->elements[array->count] = value;
    array->count++;
}

Value arrayPop(ObjArray* array) {
    if (array->count == 0) {
        return NIL_VAL;
    }
    array->count--;
    return array->elements[array->count];
}

// ============================================================================
// Function Objects
// ============================================================================

ObjFunction* newFunction(void) {
    ObjFunction* function = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);
    function->arity = 0;
    function->upvalueCount = 0;
    function->name = NULL;
    initChunk(&function->chunk);
    return function;
}

// ============================================================================
// Native Function Objects
// ============================================================================

ObjNative* newNative(NativeFn function, int arity) {
    ObjNative* native = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
    native->function = function;
    native->arity = arity;
    return native;
}

// ============================================================================
// Closure Objects
// ============================================================================

ObjClosure* newClosure(ObjFunction* function) {
    ObjUpvalue** upvalues = ALLOCATE(ObjUpvalue*, function->upvalueCount);
    for (int i = 0; i < function->upvalueCount; i++) {
        upvalues[i] = NULL;
    }

    ObjClosure* closure = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
    closure->function = function;
    closure->upvalues = upvalues;
    closure->upvalueCount = function->upvalueCount;
    return closure;
}

// ============================================================================
// Upvalue Objects
// ============================================================================

ObjUpvalue* newUpvalue(Value* slot) {
    ObjUpvalue* upvalue = ALLOCATE_OBJ(ObjUpvalue, OBJ_UPVALUE);
    upvalue->location = slot;
    upvalue->closed = NIL_VAL;
    upvalue->next = NULL;
    return upvalue;
}

// ============================================================================
// Hash Table Implementation
// ============================================================================

#define TABLE_MAX_LOAD 0.75

void initTable(Table* table) {
    table->count = 0;
    table->capacity = 0;
    table->entries = NULL;
}

void freeTable(Table* table) {
    FREE_ARRAY(Entry, table->entries, table->capacity);
    initTable(table);
}

static Entry* findEntry(Entry* entries, int capacity, ObjString* key) {
    uint32_t index = key->hash & (capacity - 1);
    Entry* tombstone = NULL;

    for (;;) {
        Entry* entry = &entries[index];
        if (entry->key == NULL) {
            if (IS_NIL(entry->value)) {
                return tombstone != NULL ? tombstone : entry;
            } else {
                if (tombstone == NULL) tombstone = entry;
            }
        } else if (entry->key == key) {
            return entry;
        } else if (entry->key->hash == key->hash &&
                   entry->key->length == key->length &&
                   memcmp(entry->key->chars, key->chars, key->length) == 0) {
            // Same content, different pointer (no string interning yet)
            return entry;
        }

        index = (index + 1) & (capacity - 1);
    }
}

static void adjustCapacity(Table* table, int capacity) {
    Entry* entries = ALLOCATE(Entry, capacity);
    for (int i = 0; i < capacity; i++) {
        entries[i].key = NULL;
        entries[i].value = NIL_VAL;
    }

    table->count = 0;
    for (int i = 0; i < table->capacity; i++) {
        Entry* entry = &table->entries[i];
        if (entry->key == NULL) continue;

        Entry* dest = findEntry(entries, capacity, entry->key);
        dest->key = entry->key;
        dest->value = entry->value;
        table->count++;
    }

    FREE_ARRAY(Entry, table->entries, table->capacity);
    table->entries = entries;
    table->capacity = capacity;
}

bool tableGet(Table* table, ObjString* key, Value* value) {
    if (table->count == 0) return false;

    Entry* entry = findEntry(table->entries, table->capacity, key);
    if (entry->key == NULL) return false;

    *value = entry->value;
    return true;
}

bool tableSet(Table* table, ObjString* key, Value value) {
    if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
        int capacity = GROW_CAPACITY(table->capacity);
        adjustCapacity(table, capacity);
    }

    Entry* entry = findEntry(table->entries, table->capacity, key);
    bool isNewKey = entry->key == NULL;
    if (isNewKey && IS_NIL(entry->value)) table->count++;

    entry->key = key;
    entry->value = value;
    return isNewKey;
}

bool tableDelete(Table* table, ObjString* key) {
    if (table->count == 0) return false;

    Entry* entry = findEntry(table->entries, table->capacity, key);
    if (entry->key == NULL) return false;

    entry->key = NULL;
    entry->value = BOOL_VAL(true);  // Tombstone
    return true;
}

void tableAddAll(Table* from, Table* to) {
    for (int i = 0; i < from->capacity; i++) {
        Entry* entry = &from->entries[i];
        if (entry->key != NULL) {
            tableSet(to, entry->key, entry->value);
        }
    }
}

ObjString* tableFindString(Table* table, const char* chars, int length, uint32_t hash) {
    if (table->count == 0) return NULL;

    uint32_t index = hash & (table->capacity - 1);
    for (;;) {
        Entry* entry = &table->entries[index];
        if (entry->key == NULL) {
            if (IS_NIL(entry->value)) return NULL;
        } else if (entry->key->length == length &&
                   entry->key->hash == hash &&
                   memcmp(entry->key->chars, chars, length) == 0) {
            return entry->key;
        }
        index = (index + 1) & (table->capacity - 1);
    }
}

// ============================================================================
// Value-keyed Hash Table (for hash map/set builtins)
// ============================================================================

static uint32_t hashBits(uint64_t x) {
    // SplitMix64 finalizer (32-bit output)
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return (uint32_t)(x ^ (x >> 32));
}

static uint32_t hashValue(Value value) {
    if (IS_NIL(value)) return 0x9e3779b9u;
    if (IS_BOOL(value)) return AS_BOOL(value) ? 0x85ebca6bu : 0xc2b2ae35u;
    if (IS_INT(value)) return hashBits((uint64_t)AS_INT(value));
    if (IS_FLOAT(value)) {
        union { uint64_t bits; double num; } d;
        d.num = AS_FLOAT(value);
        return hashBits(d.bits);
    }
    if (IS_STRING(value)) {
        return AS_STRING(value)->hash;
    }
    if (IS_OBJ(value)) {
        return hashBits((uint64_t)(uintptr_t)AS_OBJ(value));
    }
    return hashBits((uint64_t)value);
}

static bool valueKeyEquals(Value a, Value b) {
    if (a == b) return true;
    if (IS_NIL(a) || IS_NIL(b)) return false;
    if (IS_BOOL(a) || IS_BOOL(b)) return false;
    if (IS_INT(a) && IS_INT(b)) return AS_INT(a) == AS_INT(b);
    if (IS_FLOAT(a) && IS_FLOAT(b)) return AS_FLOAT(a) == AS_FLOAT(b);
    if (IS_INT(a) || IS_INT(b) || IS_FLOAT(a) || IS_FLOAT(b)) return false;
    if (IS_STRING(a) && IS_STRING(b)) {
        ObjString* sa = AS_STRING(a);
        ObjString* sb = AS_STRING(b);
        return sa->hash == sb->hash &&
               sa->length == sb->length &&
               memcmp(sa->chars, sb->chars, (size_t)sa->length) == 0;
    }
    if (IS_OBJ(a) && IS_OBJ(b)) return AS_OBJ(a) == AS_OBJ(b);
    return false;
}

// Forward declaration for generic probe path.
static ValueEntry* findValueEntry(ValueEntry* entries, int capacity, Value key);

static ValueEntry* findValueEntryString(ObjString* key, ValueEntry* entries, int capacity) {
    uint32_t index = key->hash & (capacity - 1);
    ValueEntry* tombstone = NULL;

    for (;;) {
        ValueEntry* entry = &entries[index];
        if (entry->state == 0) {
            return tombstone != NULL ? tombstone : entry;
        }
        if (entry->state == 1) {
            if (tombstone == NULL) tombstone = entry;
        } else if (IS_STRING(entry->key)) {
            ObjString* ek = AS_STRING(entry->key);
            if (ek == key) return entry;
            if (ek->hash == key->hash &&
                ek->length == key->length &&
                memcmp(ek->chars, key->chars, (size_t)key->length) == 0) {
                return entry;
            }
        }
        index = (index + 1) & (capacity - 1);
    }
}

static ValueEntry* findValueEntryObj(Obj* key, ValueEntry* entries, int capacity) {
    uint32_t index = hashBits((uint64_t)(uintptr_t)key) & (capacity - 1);
    ValueEntry* tombstone = NULL;

    for (;;) {
        ValueEntry* entry = &entries[index];
        if (entry->state == 0) {
            return tombstone != NULL ? tombstone : entry;
        }
        if (entry->state == 1) {
            if (tombstone == NULL) tombstone = entry;
        } else if (IS_OBJ(entry->key) && AS_OBJ(entry->key) == key) {
            return entry;
        }
        index = (index + 1) & (capacity - 1);
    }
}

static ValueEntry* findValueEntryInt(int64_t key, ValueEntry* entries, int capacity) {
    uint32_t index = hashBits((uint64_t)key) & (capacity - 1);
    ValueEntry* tombstone = NULL;

    for (;;) {
        ValueEntry* entry = &entries[index];
        if (entry->state == 0) {
            return tombstone != NULL ? tombstone : entry;
        }
        if (entry->state == 1) {
            if (tombstone == NULL) tombstone = entry;
        } else if (IS_INT(entry->key) && (int64_t)AS_INT(entry->key) == key) {
            return entry;
        }
        index = (index + 1) & (capacity - 1);
    }
}

static ValueEntry* findValueEntryForKey(Value key, ValueEntry* entries, int capacity) {
    if (IS_STRING(key)) return findValueEntryString(AS_STRING(key), entries, capacity);
    if (IS_OBJ(key)) return findValueEntryObj(AS_OBJ(key), entries, capacity);
    if (IS_INT(key)) return findValueEntryInt(AS_INT(key), entries, capacity);
    return findValueEntry(entries, capacity, key);
}

void initValueTable(ValueTable* table) {
    table->count = 0;
    table->capacity = 0;
    table->entries = NULL;
}

void freeValueTable(ValueTable* table) {
    FREE_ARRAY(ValueEntry, table->entries, table->capacity);
    initValueTable(table);
}

static ValueEntry* findValueEntry(ValueEntry* entries, int capacity, Value key) {
    uint32_t index = hashValue(key) & (capacity - 1);
    ValueEntry* tombstone = NULL;

    for (;;) {
        ValueEntry* entry = &entries[index];
        if (entry->state == 0) {  // empty
            return tombstone != NULL ? tombstone : entry;
        }
        if (entry->state == 1) {  // tombstone
            if (tombstone == NULL) tombstone = entry;
        } else if (valueKeyEquals(entry->key, key)) {
            return entry;
        }
        index = (index + 1) & (capacity - 1);
    }
}

static void adjustValueTableCapacity(ValueTable* table, int capacity) {
    ValueEntry* entries = ALLOCATE(ValueEntry, capacity);
    for (int i = 0; i < capacity; i++) {
        entries[i].state = 0;
        entries[i].key = NIL_VAL;
        entries[i].value = NIL_VAL;
    }

    table->count = 0;
    for (int i = 0; i < table->capacity; i++) {
        ValueEntry* src = &table->entries[i];
        if (src->state != 2) continue;

        ValueEntry* dst = findValueEntry(entries, capacity, src->key);
        dst->state = 2;
        dst->key = src->key;
        dst->value = src->value;
        table->count++;
    }

    FREE_ARRAY(ValueEntry, table->entries, table->capacity);
    table->entries = entries;
    table->capacity = capacity;
}

bool valueTableGet(ValueTable* table, Value key, Value* value) {
    if (table->count == 0) return false;
    ValueEntry* entry = findValueEntryForKey(key, table->entries, table->capacity);
    if (entry->state != 2) return false;
    *value = entry->value;
    return true;
}

bool valueTableSet(ValueTable* table, Value key, Value value) {
    if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
        int capacity = GROW_CAPACITY(table->capacity);
        adjustValueTableCapacity(table, capacity);
    }

    ValueEntry* entry = findValueEntryForKey(key, table->entries, table->capacity);
    bool isNewKey = entry->state != 2;
    if (isNewKey) table->count++;

    entry->state = 2;
    entry->key = key;
    entry->value = value;
    return isNewKey;
}

void valueTableReserve(ValueTable* table, int minEntries) {
    if (minEntries <= 0) return;

    int needed = (int)((double)minEntries / TABLE_MAX_LOAD) + 1;
    int capacity = 8;
    while (capacity < needed) capacity <<= 1;
    if (capacity <= table->capacity) return;
    adjustValueTableCapacity(table, capacity);
}

bool valueTableDelete(ValueTable* table, Value key) {
    if (table->count == 0) return false;
    ValueEntry* entry = findValueEntryForKey(key, table->entries, table->capacity);
    if (entry->state != 2) return false;

    entry->state = 1;  // tombstone
    entry->key = NIL_VAL;
    entry->value = NIL_VAL;
    return true;
}

// ============================================================================
// Class Objects
// ============================================================================

ObjClass* newClass(ObjString* name) {
    ObjClass* klass = ALLOCATE_OBJ(ObjClass, OBJ_CLASS);
    klass->name = name;
    initTable(&klass->fields);
    initTable(&klass->methods);
    return klass;
}

// ============================================================================
// Instance Objects
// ============================================================================

ObjInstance* newInstance(ObjClass* klass) {
    ObjInstance* instance = ALLOCATE_OBJ(ObjInstance, OBJ_INSTANCE);
    instance->klass = klass;
    initTable(&instance->fields);
    return instance;
}

// ============================================================================
// Bound Method Objects
// ============================================================================

ObjBoundMethod* newBoundMethod(Value receiver, ObjClosure* method) {
    ObjBoundMethod* bound = ALLOCATE_OBJ(ObjBoundMethod, OBJ_BOUND_METHOD);
    bound->receiver = receiver;
    bound->method = method;
    return bound;
}

// ============================================================================
// Object Utilities
// ============================================================================

static void printFunction(ObjFunction* function) {
    if (function->name == NULL) {
        printf("<script>");
        return;
    }
    printf("<fn %s>", function->name->chars);
}

void printObject(Value value) {
    switch (OBJ_TYPE(value)) {
        case OBJ_STRING:
            printf("%s", AS_CSTRING(value));
            break;
        case OBJ_ARRAY: {
            ObjArray* array = AS_ARRAY(value);
            printf("[");
            for (int i = 0; i < array->count; i++) {
                if (i > 0) printf(", ");
                printValue(array->elements[i]);
            }
            printf("]");
            break;
        }
        case OBJ_HASH_MAP:
            printf("<hashmap>");
            break;
        case OBJ_HASH_SET:
            printf("<hashset>");
            break;
        case OBJ_FUNCTION:
            printFunction(AS_FUNCTION(value));
            break;
        case OBJ_NATIVE:
            printf("<native fn>");
            break;
        case OBJ_CLOSURE:
            printFunction(AS_CLOSURE(value)->function);
            break;
        case OBJ_UPVALUE:
            printf("upvalue");
            break;
        case OBJ_CLASS:
            printf("%s", AS_CLASS(value)->name->chars);
            break;
        case OBJ_INSTANCE:
            printf("%s instance", AS_INSTANCE(value)->klass->name->chars);
            break;
        case OBJ_BOUND_METHOD:
            printFunction(AS_BOUND_METHOD(value)->method->function);
            break;
    }
}

// ============================================================================
// Memory Management
// ============================================================================

static void freeObject(Obj* object) {
    switch (object->type) {
        case OBJ_STRING: {
            ObjString* string = (ObjString*)object;
            FREE_ARRAY(char, string->chars, string->length + 1);
            FREE(ObjString, object);
            break;
        }
        case OBJ_ARRAY: {
            ObjArray* array = (ObjArray*)object;
            FREE_ARRAY(Value, array->elements, array->capacity);
            FREE(ObjArray, object);
            break;
        }
        case OBJ_HASH_MAP: {
            ObjHashMap* map = (ObjHashMap*)object;
            freeValueTable(&map->table);
            FREE(ObjHashMap, object);
            break;
        }
        case OBJ_HASH_SET: {
            ObjHashSet* set = (ObjHashSet*)object;
            freeValueTable(&set->table);
            FREE(ObjHashSet, object);
            break;
        }
        case OBJ_FUNCTION: {
            ObjFunction* function = (ObjFunction*)object;
            freeChunk(&function->chunk);
            FREE(ObjFunction, object);
            break;
        }
        case OBJ_NATIVE: {
            FREE(ObjNative, object);
            break;
        }
        case OBJ_CLOSURE: {
            ObjClosure* closure = (ObjClosure*)object;
            FREE_ARRAY(ObjUpvalue*, closure->upvalues, closure->upvalueCount);
            FREE(ObjClosure, object);
            break;
        }
        case OBJ_UPVALUE: {
            FREE(ObjUpvalue, object);
            break;
        }
        case OBJ_CLASS: {
            ObjClass* klass = (ObjClass*)object;
            freeTable(&klass->fields);
            freeTable(&klass->methods);
            FREE(ObjClass, object);
            break;
        }
        case OBJ_INSTANCE: {
            ObjInstance* instance = (ObjInstance*)object;
            freeTable(&instance->fields);
            FREE(ObjInstance, object);
            break;
        }
        case OBJ_BOUND_METHOD: {
            FREE(ObjBoundMethod, object);
            break;
        }
    }
}

void freeObjects(void) {
    Obj* object = objects;
    while (object != NULL) {
        Obj* next = object->next;
        freeObject(object);
        object = next;
    }
    objects = NULL;
}

// ============================================================================
// Garbage Collection
// ============================================================================

// Gray stack for objects to be processed
static Obj** grayStack = NULL;
static int grayCount = 0;
static int grayCapacity = 0;

void markObject(Obj* object) {
    if (object == NULL) return;
    if (object->isMarked) return;

#ifdef DEBUG_LOG_GC
    printf("%p mark ", (void*)object);
    printValue(OBJ_VAL(object));
    printf("\n");
#endif

    object->isMarked = true;

    // Add to gray stack for processing
    if (grayCapacity < grayCount + 1) {
        grayCapacity = GROW_CAPACITY(grayCapacity);
        grayStack = (Obj**)realloc(grayStack, sizeof(Obj*) * grayCapacity);
        if (grayStack == NULL) exit(1);
    }
    grayStack[grayCount++] = object;
}

void markValue(Value value) {
    if (IS_OBJ(value)) markObject(AS_OBJ(value));
}

static void markArray(ValueArray* array) {
    for (int i = 0; i < array->count; i++) {
        markValue(array->values[i]);
    }
}

static void markTable(Table* table) {
    for (int i = 0; i < table->capacity; i++) {
        Entry* entry = &table->entries[i];
        markObject((Obj*)entry->key);
        markValue(entry->value);
    }
}

static void blackenObject(Obj* object) {
#ifdef DEBUG_LOG_GC
    printf("%p blacken ", (void*)object);
    printValue(OBJ_VAL(object));
    printf("\n");
#endif

    switch (object->type) {
        case OBJ_STRING:
        case OBJ_NATIVE:
            // No references
            break;
        case OBJ_ARRAY: {
            ObjArray* array = (ObjArray*)object;
            for (int i = 0; i < array->count; i++) {
                markValue(array->elements[i]);
            }
            break;
        }
        case OBJ_HASH_MAP: {
            ObjHashMap* map = (ObjHashMap*)object;
            for (int i = 0; i < map->table.capacity; i++) {
                ValueEntry* entry = &map->table.entries[i];
                if (entry->state != 2) continue;
                markValue(entry->key);
                markValue(entry->value);
            }
            break;
        }
        case OBJ_HASH_SET: {
            ObjHashSet* set = (ObjHashSet*)object;
            for (int i = 0; i < set->table.capacity; i++) {
                ValueEntry* entry = &set->table.entries[i];
                if (entry->state != 2) continue;
                markValue(entry->key);
            }
            break;
        }
        case OBJ_UPVALUE:
            markValue(((ObjUpvalue*)object)->closed);
            break;
        case OBJ_FUNCTION: {
            ObjFunction* function = (ObjFunction*)object;
            markObject((Obj*)function->name);
            markArray(&function->chunk.constants);
            break;
        }
        case OBJ_CLOSURE: {
            ObjClosure* closure = (ObjClosure*)object;
            markObject((Obj*)closure->function);
            for (int i = 0; i < closure->upvalueCount; i++) {
                markObject((Obj*)closure->upvalues[i]);
            }
            break;
        }
        case OBJ_CLASS: {
            ObjClass* klass = (ObjClass*)object;
            markObject((Obj*)klass->name);
            markTable(&klass->fields);
            markTable(&klass->methods);
            break;
        }
        case OBJ_INSTANCE: {
            ObjInstance* instance = (ObjInstance*)object;
            markObject((Obj*)instance->klass);
            markTable(&instance->fields);
            break;
        }
        case OBJ_BOUND_METHOD: {
            ObjBoundMethod* bound = (ObjBoundMethod*)object;
            markValue(bound->receiver);
            markObject((Obj*)bound->method);
            break;
        }
    }
}

static void traceReferences(void) {
    while (grayCount > 0) {
        Obj* object = grayStack[--grayCount];
        blackenObject(object);
    }
}

static void sweep(void) {
    Obj* previous = NULL;
    Obj* object = objects;
    while (object != NULL) {
        if (object->isMarked) {
            object->isMarked = false;
            previous = object;
            object = object->next;
        } else {
            Obj* unreached = object;
            object = object->next;
            if (previous != NULL) {
                previous->next = object;
            } else {
                objects = object;
            }

#ifdef DEBUG_LOG_GC
            printf("%p free type %d\n", (void*)unreached, unreached->type);
#endif
            freeObject(unreached);
        }
    }
}

size_t getBytesAllocated(void) {
    return bytesAllocated;
}

static void markRoots(struct VM* vm) {
    // Mark stack
    for (Value* slot = vm->stack; slot < vm->stackTop; slot++) {
        markValue(*slot);
    }

    // Mark call frames
    for (int i = 0; i < vm->frameCount; i++) {
        markObject((Obj*)vm->frames[i].closure);
    }

    // Mark open upvalues
    for (ObjUpvalue* upvalue = vm->openUpvalues;
         upvalue != NULL;
         upvalue = upvalue->next) {
        markObject((Obj*)upvalue);
    }

    // Mark globals table (native functions and global variables)
    markTable(&vm->globals);

    // Mark compiler roots (functions being compiled)
    markCompilerRoots();
}

void collectGarbage(struct VM* vm) {
#ifdef DEBUG_LOG_GC
    printf("-- gc begin\n");
    size_t before = bytesAllocated;
#endif

    markRoots(vm);
    traceReferences();
    sweep();

    nextGC = bytesAllocated * 2;

#ifdef DEBUG_LOG_GC
    printf("-- gc end\n");
    printf("   collected %zu bytes (from %zu to %zu) next at %zu\n",
           before - bytesAllocated, before, bytesAllocated, nextGC);
#endif
}

// Check if GC should run (used by reallocate)
bool shouldCollectGarbage(void) {
    return bytesAllocated > nextGC;
}

// Set the VM for GC to use
void setGCVM(struct VM* vm) {
    gcVM = vm;
}
