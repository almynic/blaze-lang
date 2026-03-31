/* VM: module loading, init/free, interpret pipeline, run(). */

#include "vm.h"
#include "vm_internal.h"
#include "compiler.h"
#include "debug.h"
#include "generic.h"
#include "memory.h"
#include "module.h"
#include "object.h"
#include "parser.h"
#include "typechecker.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Global module system
// Global module system
static ModuleSystem moduleSystem;
static bool moduleSystemInitialized = false;
static bool preludeLoaded = false;

// ============================================================================
// VM Lifecycle
// ============================================================================

void initVM(VM* vm) {
    vm_reset_stack(vm);
    vm->chunk = NULL;
    vm->ip = NULL;
    vm->debuggerEnabled = false;
    vm->debuggerPaused = false;
    vm->debuggerStepping = false;
    vm->debuggerStepMode = DEBUG_STEP_NONE;
    vm->debuggerStepDepth = 0;
    vm->debuggerLastLine = -1;
    vm->debuggerLastFrameDepth = 0;
    vm->debuggerLastFunction = NULL;
    vm->debuggerProtocolMode = false;
    vm->debuggerAutoContinue = false;
    vm->debuggerBreakpointsPath[0] = '\0';
    vm->breakpointCount = 0;
    initTable(&vm->globals);
    initTable(&vm->strings);

    // Register VM for garbage collection
    setGCVM(vm);

    vm_register_natives(vm);
}

void freeVM(VM* vm) {
    if (moduleSystemInitialized) {
        freeModuleSystem(&moduleSystem);
        moduleSystemInitialized = false;
    }
    freeTable(&vm->strings);
    freeTable(&vm->globals);
    setGCVM(NULL);  // Unregister VM from GC
    freeObjects();

    // Reset prelude flag so it's reloaded on next VM init
    preludeLoaded = false;
}

InterpretResult run(VM* vm, Chunk* chunk) {
    vm->chunk = chunk;
    vm->ip = chunk->code;
    return vm_execute_legacy(vm);
}

// Load the prelude module (std/prelude) which imports common functions
// Forward declaration for processImports
static bool processImports(VM* vm, Stmt** statements, int count);

// Load the prelude module (std/prelude) which imports common functions
static bool loadPrelude(VM* vm) {
    if (preludeLoaded) {
        return true;
    }

    // Initialize module system if needed
    if (!moduleSystemInitialized) {
        initModuleSystem(&moduleSystem);
        moduleSystemInitialized = true;
    }

    // Try to resolve the prelude path
    char* preludePath = resolveModulePath(&moduleSystem, "std/prelude");
    if (preludePath == NULL) {
        // Prelude not found - this is okay, just means no std library available
        // fprintf(stderr, "[DEBUG] Prelude not found\n");
        preludeLoaded = true;
        return true;
    }
    // fprintf(stderr, "[DEBUG] Loading prelude from: %s\n", preludePath);

    // Read the prelude source
    char* source = readFile(preludePath);
    free(preludePath);
    if (source == NULL) {
        preludeLoaded = true;
        return true;
    }

    // Parse the prelude to extract its import statements
    Parser parser;
    initParser(&parser, source);
    int count = 0;
    Stmt** statements = parse(&parser, &count);

    if (statements == NULL) {
        free(source);
        preludeLoaded = true;
        return true;
    }

    // Process the prelude's imports - this loads the standard library modules
    // and adds their exported functions to the VM's globals
    bool success = processImports(vm, statements, count);
    freeStatements(statements, count);
    free(source);  // Free source AFTER processing (tokens point to source memory)

    preludeLoaded = true;
    return success;
}

// Helper to check if a name is in the selective import list
static bool isNameInImportList(ImportStmt* import, const char* name, int nameLen) {
    for (int i = 0; i < import->nameCount; i++) {
        if (import->names[i].length == nameLen &&
            memcmp(import->names[i].start, name, nameLen) == 0) {
            return true;
        }
    }
    return false;
}

// Process imports from statements and execute imported modules
static bool processImports(VM* vm, Stmt** statements, int count) {
    // Initialize module system if needed
    if (!moduleSystemInitialized) {
        initModuleSystem(&moduleSystem);
        moduleSystemInitialized = true;
    }

    for (int i = 0; i < count; i++) {
        if (statements[i]->kind == STMT_IMPORT) {
            ImportStmt* import = &statements[i]->as.import;

            // Extract path from string token (remove quotes)
            int pathLen = import->path.length - 2;
            char* path = (char*)malloc(pathLen + 1);
            memcpy(path, import->path.start + 1, pathLen);
            path[pathLen] = '\0';

            // Load the module
            Module* module = loadModule(&moduleSystem, path);
            free(path);

            if (module == NULL) {
                return false;
            }

            // For selective imports, track existing globals before module execution
            ObjString** existingKeys = NULL;
            Value* existingValues = NULL;
            int existingCount = 0;

            if (import->nameCount > 0) {
                // Collect existing global keys
                existingKeys = (ObjString**)malloc(sizeof(ObjString*) * vm->globals.capacity);
                existingValues = (Value*)malloc(sizeof(Value) * vm->globals.capacity);
                for (int j = 0; j < vm->globals.capacity; j++) {
                    Entry* entry = &vm->globals.entries[j];
                    if (entry->key != NULL) {
                        existingKeys[existingCount++] = entry->key;
                        existingValues[existingCount - 1] = entry->value;
                    }
                }
            }

            // Execute the module to define its symbols
            if (module->function != NULL) {
                push(vm, OBJ_VAL(module->function));
                ObjClosure* closure = newClosure(module->function);
                pop(vm);
                push(vm, OBJ_VAL(closure));
                if (!vm_call(vm, closure, 0)) {
                    return false;
                }

                InterpretResult result = vm_execute_with_frames(vm);
                if (result != INTERPRET_OK) {
                    if (existingKeys != NULL) free(existingKeys);
                    if (existingValues != NULL) free(existingValues);
                    return false;
                }
            }

            // For selective imports, remove unwanted symbols
            if (import->nameCount > 0) {
                // Collect keys to delete (new symbols not in import list)
                ObjString** keysToDelete = (ObjString**)malloc(sizeof(ObjString*) * vm->globals.capacity);
                int deleteCount = 0;

                for (int j = 0; j < vm->globals.capacity; j++) {
                    Entry* entry = &vm->globals.entries[j];
                    if (entry->key == NULL) continue;

                    // Check if this key existed before module execution
                    bool existed = false;
                    int existedIndex = -1;
                    for (int k = 0; k < existingCount; k++) {
                        if (existingKeys[k] == entry->key) {
                            existed = true;
                            existedIndex = k;
                            break;
                        }
                    }

                    bool keepImported = isNameInImportList(import, entry->key->chars, entry->key->length);

                    // New symbol not imported: remove it.
                    if (!existed && !keepImported) {
                        keysToDelete[deleteCount++] = entry->key;
                    }

                    // Existing symbol not imported: restore the previous value in case
                    // module execution overwrote a builtin/global with same name.
                    if (existed && !keepImported && existedIndex >= 0) {
                        tableSet(&vm->globals, entry->key, existingValues[existedIndex]);
                    }
                }

                // Delete the unwanted symbols
                for (int j = 0; j < deleteCount; j++) {
                    tableDelete(&vm->globals, keysToDelete[j]);
                }

                free(keysToDelete);
                free(existingKeys);
                free(existingValues);
            }
        }
    }
    return true;
}

static InterpretResult interpretInternal(VM* vm, const char* source, bool replMode) {
    bool debuggerWasEnabled = vm->debuggerEnabled;

    // Load the prelude on first execution (adds common functions to globals)
    vm->debuggerEnabled = false;
    if (!loadPrelude(vm)) {
        vm->debuggerEnabled = debuggerWasEnabled;
        return INTERPRET_COMPILE_ERROR;
    }

    // Parse
    Parser parser;
    initParser(&parser, source);

    int stmtCount = 0;
    Stmt** statements = parse(&parser, &stmtCount);

    if (statements == NULL) {
        vm->debuggerEnabled = debuggerWasEnabled;
        return INTERPRET_COMPILE_ERROR;
    }

    // Process imports first (load and execute imported modules)
    // This adds imported functions to the VM's globals
    if (!processImports(vm, statements, stmtCount)) {
        freeStatements(statements, stmtCount);
        vm->debuggerEnabled = debuggerWasEnabled;
        return INTERPRET_COMPILE_ERROR;
    }

    // Type check - skip import statements and trust imported functions
    // (imported modules are already type-checked when loaded)
    TypeChecker checker;
    initTypeChecker(&checker);

    // Add existing globals to the type checker's symbol table
    // This enables REPL persistence - variables/functions from previous inputs
    // Store them at an outer depth so current top-level declarations can shadow
    // names like prelude `sum`/`range`.
    checker.symbols.scopeDepth = -1;
    for (int i = 0; i < vm->globals.capacity; i++) {
        Entry* entry = &vm->globals.entries[i];
        if (entry->key == NULL) continue;

        Type* type = NULL;
        if (IS_CLOSURE(entry->value)) {
            ObjClosure* closure = AS_CLOSURE(entry->value);
            ObjFunction* func = closure->function;
            // Create a function type for the function
            Type** paramTypes = NULL;
            if (func->arity > 0) {
                paramTypes = ALLOCATE(Type*, func->arity);
                for (int j = 0; j < func->arity; j++) {
                    paramTypes[j] = createUnknownType();
                }
            }
            type = createFunctionType(paramTypes, func->arity, createUnknownType());
        } else if (IS_CLASS(entry->value)) {
            type = createClassType(entry->key->chars, entry->key->length);
        } else if (IS_INT(entry->value)) {
            type = createIntType();
        } else if (IS_FLOAT(entry->value)) {
            type = createFloatType();
        } else if (IS_BOOL(entry->value)) {
            type = createBoolType();
        } else if (IS_STRING(entry->value)) {
            type = createStringType();
        } else if (IS_NIL(entry->value)) {
            type = createNilType();
        } else {
            type = createUnknownType();
        }

        defineSymbol(&checker, entry->key->chars, entry->key->length, type, false);
    }
    checker.symbols.scopeDepth = 0;

    bool typeOk = typeCheck(&checker, statements, stmtCount);

    Stmt** lowered = NULL;
    int loweredCount = 0;
    if (!lowerGenericClasses(&checker, &lowered, &loweredCount)) {
        freeTypeChecker(&checker);
        freeStatements(statements, stmtCount);
        vm->debuggerEnabled = debuggerWasEnabled;
        return INTERPRET_COMPILE_ERROR;
    }
    freeTypeChecker(&checker);

    if (!typeOk) {
        if (lowered != NULL) {
            for (int i = 0; i < loweredCount; i++) {
                freeLoweredMonomorphClassStmt(lowered[i]);
            }
            FREE_ARRAY(Stmt*, lowered, loweredCount);
        }
        freeStatements(statements, stmtCount);
        vm->debuggerEnabled = debuggerWasEnabled;
        return INTERPRET_COMPILE_ERROR;
    }

    // Compile - use compileRepl for REPL mode so globals persist
    ObjFunction* function = replMode
        ? compileReplWithPrependedClasses(lowered, loweredCount, statements, stmtCount)
        : compileWithPrependedClasses(lowered, loweredCount, statements, stmtCount);

    if (function == NULL) {
        if (lowered != NULL) {
            for (int i = 0; i < loweredCount; i++) {
                freeLoweredMonomorphClassStmt(lowered[i]);
            }
            FREE_ARRAY(Stmt*, lowered, loweredCount);
        }
        freeStatements(statements, stmtCount);
        vm->debuggerEnabled = debuggerWasEnabled;
        return INTERPRET_COMPILE_ERROR;
    }

    // Optional: disassemble compiled bytecode for debugging.
    // Enable via `BLAZE_DISASM=1 blaze <script.blaze>`
    const char* disasmEnv = getenv("BLAZE_DISASM");
    if (disasmEnv != NULL && disasmEnv[0] != '\0' && disasmEnv[0] != '0') {
        disassembleChunk(&function->chunk, "script");
        fflush(stdout);
    }

    // Root the compiled function before freeing the AST: GC may run during
    // freeStatements, and after compile ends the compiler no longer marks this
    // function (current is NULL), so it would otherwise be collected.
    push(vm, OBJ_VAL(function));

    if (lowered != NULL) {
        for (int i = 0; i < loweredCount; i++) {
            freeLoweredMonomorphClassStmt(lowered[i]);
        }
        FREE_ARRAY(Stmt*, lowered, loweredCount);
    }
    freeStatements(statements, stmtCount);

    // Set up execution with call frame
    ObjClosure* closure = newClosure(function);
    pop(vm);
    push(vm, OBJ_VAL(closure));
    if (!vm_call(vm, closure, 0)) {
        vm->debuggerEnabled = debuggerWasEnabled;
        return INTERPRET_RUNTIME_ERROR;
    }
    vm->debuggerEnabled = debuggerWasEnabled;

    return vm_execute_with_frames(vm);
}

InterpretResult interpret(VM* vm, const char* source) {
    return interpretInternal(vm, source, false);
}

InterpretResult interpretRepl(VM* vm, const char* source) {
    return interpretInternal(vm, source, true);
}
