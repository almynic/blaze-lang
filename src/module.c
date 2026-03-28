#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <libgen.h>

#include "module.h"
#include "memory.h"
#include "parser.h"
#include "typechecker.h"
#include "compiler.h"

// ============================================================================
// Module System Initialization
// ============================================================================

void initModuleSystem(ModuleSystem* system) {
    initTable(&system->modules);
    system->searchPathCount = 0;
    system->basePath = NULL;

    // Add default search paths
    addModuleSearchPath(system, ".");
    addModuleSearchPath(system, "./std");
}

void freeModuleSystem(ModuleSystem* system) {
    // Free all cached modules
    for (int i = 0; i < system->modules.capacity; i++) {
        Entry* entry = &system->modules.entries[i];
        if (entry->key != NULL) {
            Module* module = (Module*)AS_POINTER(entry->value);
            if (module != NULL) {
                free(module->path);
                free(module->name);
                freeTable(&module->exports);
                free(module);
            }
        }
    }
    freeTable(&system->modules);

    // Free search paths
    for (int i = 0; i < system->searchPathCount; i++) {
        free(system->searchPaths[i]);
    }

    if (system->basePath != NULL) {
        free(system->basePath);
    }
}

void addModuleSearchPath(ModuleSystem* system, const char* path) {
    if (system->searchPathCount >= 16) {
        fprintf(stderr, "Too many module search paths.\n");
        return;
    }
    system->searchPaths[system->searchPathCount++] = strdup(path);
}

void setModuleBasePath(ModuleSystem* system, const char* path) {
    if (system->basePath != NULL) {
        free(system->basePath);
    }

    // Extract directory from path
    char* pathCopy = strdup(path);
    char* dir = dirname(pathCopy);
    system->basePath = strdup(dir);
    free(pathCopy);
}

// ============================================================================
// File Utilities
// ============================================================================

static bool fileExists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0;
}

char* readFile(const char* path) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);

    char* buffer = (char*)malloc(fileSize + 1);
    if (buffer == NULL) {
        fclose(file);
        return NULL;
    }

    size_t bytesRead = fread(buffer, 1, fileSize, file);
    buffer[bytesRead] = '\0';

    fclose(file);
    return buffer;
}

// ============================================================================
// Module Resolution
// ============================================================================

char* resolveModulePath(ModuleSystem* system, const char* importPath) {
    char fullPath[1024];

    // If it's an absolute path, use it directly
    if (importPath[0] == '/') {
        if (fileExists(importPath)) {
            return strdup(importPath);
        }
        // Try adding .blaze extension
        snprintf(fullPath, sizeof(fullPath), "%s.blaze", importPath);
        if (fileExists(fullPath)) {
            return strdup(fullPath);
        }
        return NULL;
    }

    // Try relative to base path first
    if (system->basePath != NULL) {
        snprintf(fullPath, sizeof(fullPath), "%s/%s", system->basePath, importPath);
        if (fileExists(fullPath)) {
            return strdup(fullPath);
        }
        snprintf(fullPath, sizeof(fullPath), "%s/%s.blaze", system->basePath, importPath);
        if (fileExists(fullPath)) {
            return strdup(fullPath);
        }
    }

    // Try each search path
    for (int i = 0; i < system->searchPathCount; i++) {
        snprintf(fullPath, sizeof(fullPath), "%s/%s", system->searchPaths[i], importPath);
        if (fileExists(fullPath)) {
            return strdup(fullPath);
        }
        snprintf(fullPath, sizeof(fullPath), "%s/%s.blaze", system->searchPaths[i], importPath);
        if (fileExists(fullPath)) {
            return strdup(fullPath);
        }
    }

    return NULL;
}

// ============================================================================
// Module Loading
// ============================================================================

Module* getModule(ModuleSystem* system, const char* path) {
    ObjString* key = copyString(path, (int)strlen(path));
    Value value;
    if (tableGet(&system->modules, key, &value)) {
        return (Module*)AS_POINTER(value);
    }
    return NULL;
}

static char* extractModuleName(const char* path) {
    // Get filename without extension
    const char* lastSlash = strrchr(path, '/');
    const char* filename = lastSlash ? lastSlash + 1 : path;

    const char* dot = strrchr(filename, '.');
    size_t nameLen = dot ? (size_t)(dot - filename) : strlen(filename);

    char* name = (char*)malloc(nameLen + 1);
    memcpy(name, filename, nameLen);
    name[nameLen] = '\0';

    return name;
}

Module* loadModule(ModuleSystem* system, const char* importPath) {
    // Resolve the path
    char* resolvedPath = resolveModulePath(system, importPath);
    if (resolvedPath == NULL) {
        fprintf(stderr, "Module not found: %s\n", importPath);
        return NULL;
    }

    // Check if already loaded
    Module* existing = getModule(system, resolvedPath);
    if (existing != NULL) {
        if (existing->state == MODULE_LOADING) {
            fprintf(stderr, "Circular import detected: %s\n", resolvedPath);
            free(resolvedPath);
            return NULL;
        }
        free(resolvedPath);
        return existing;
    }

    // Create new module entry
    Module* module = (Module*)malloc(sizeof(Module));
    module->path = resolvedPath;
    module->name = extractModuleName(resolvedPath);
    module->state = MODULE_LOADING;
    module->function = NULL;
    initTable(&module->exports);

    // Cache it immediately (for circular import detection)
    ObjString* key = copyString(resolvedPath, (int)strlen(resolvedPath));
    tableSet(&system->modules, key, POINTER_VAL(module));

    // Read the source file
    char* source = readFile(resolvedPath);
    if (source == NULL) {
        fprintf(stderr, "Could not read module: %s\n", resolvedPath);
        module->state = MODULE_UNLOADED;
        return NULL;
    }

    // Parse the module
    Parser parser;
    initParser(&parser, source);

    int count = 0;
    Stmt** statements = parse(&parser, &count);

    if (parser.hadError || statements == NULL) {
        fprintf(stderr, "Parse error in module: %s\n", resolvedPath);
        free(source);
        module->state = MODULE_UNLOADED;
        return NULL;
    }

    // Type check
    TypeChecker checker;
    initTypeChecker(&checker);

    if (!typeCheck(&checker, statements, count)) {
        fprintf(stderr, "Type error in module: %s\n", resolvedPath);
        freeTypeChecker(&checker);
        freeStatements(statements, count);
        free(source);
        module->state = MODULE_UNLOADED;
        return NULL;
    }

    freeTypeChecker(&checker);

    // Compile the module
    ObjFunction* function = compile(statements, count);

    if (function == NULL) {
        fprintf(stderr, "Compile error in module: %s\n", resolvedPath);
        free(source);
        module->state = MODULE_UNLOADED;
        return NULL;
    }

    module->function = function;
    module->state = MODULE_LOADED;

    // Clean up
    freeStatements(statements, count);
    free(source);

    return module;
}
