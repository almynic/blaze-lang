#ifndef blaze_module_h
#define blaze_module_h

#include "common.h"
#include "object.h"

// Module state
typedef enum {
    MODULE_UNLOADED,
    MODULE_LOADING,     // Currently being compiled (for circular import detection)
    MODULE_LOADED,
} ModuleState;

// Represents a loaded module
typedef struct {
    char* path;             // Absolute path to the module file
    char* name;             // Module name (derived from path)
    ModuleState state;
    ObjFunction* function;  // Compiled module code
    Table exports;          // Exported symbols
} Module;

// Module system state
typedef struct {
    Table modules;          // Cache of loaded modules (path -> Module*)
    char* searchPaths[16];  // Paths to search for modules
    int searchPathCount;
    char* basePath;         // Base path for relative imports
} ModuleSystem;

// Initialize the module system
void initModuleSystem(ModuleSystem* system);

// Free module system resources
void freeModuleSystem(ModuleSystem* system);

// Add a search path for modules
void addModuleSearchPath(ModuleSystem* system, const char* path);

// Set the base path (usually the directory of the main script)
void setModuleBasePath(ModuleSystem* system, const char* path);

// Resolve a module path (handles relative paths and search paths)
// Returns allocated string or NULL if not found
char* resolveModulePath(ModuleSystem* system, const char* importPath);

// Load and compile a module
// Returns the compiled function or NULL on error
Module* loadModule(ModuleSystem* system, const char* importPath);

// Get a cached module (returns NULL if not loaded)
Module* getModule(ModuleSystem* system, const char* path);

// Read a file's contents (utility function)
char* readFile(const char* path);

#endif
