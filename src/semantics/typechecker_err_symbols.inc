// ============================================================================
// Error Reporting
// ============================================================================

static void typeError(TypeChecker* checker, int line, const char* message) {
    checker->hadError = true;

    if (colorsEnabled()) {
        fprintf(stderr, "%s[line %d]%s %sType error:%s %s\n",
                COLOR_BOLD, line, COLOR_RESET,
                COLOR_BRIGHT_RED, COLOR_RESET, message);
    } else {
        fprintf(stderr, "[line %d] Type error: %s\n", line, message);
    }
}

static void typeErrorFormat(TypeChecker* checker, int line, const char* format, ...) {
    checker->hadError = true;

    if (colorsEnabled()) {
        fprintf(stderr, "%s[line %d]%s %sType error:%s ",
                COLOR_BOLD, line, COLOR_RESET,
                COLOR_BRIGHT_RED, COLOR_RESET);
    } else {
        fprintf(stderr, "[line %d] Type error: ", line);
    }

    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);

    fprintf(stderr, "\n");
}

// ============================================================================
// Symbol Table
// ============================================================================

static void initSymbolTable(SymbolTable* table) {
    table->symbols = NULL;
    table->count = 0;
    table->capacity = 0;
    table->scopeDepth = 0;
}

static void freeSymbolTable(SymbolTable* table) {
    FREE_ARRAY(Symbol, table->symbols, table->capacity);
    initSymbolTable(table);
}

static void beginScope(TypeChecker* checker) {
    checker->symbols.scopeDepth++;
}

static void endScope(TypeChecker* checker) {
    // Remove symbols from the current scope
    while (checker->symbols.count > 0 &&
           checker->symbols.symbols[checker->symbols.count - 1].depth ==
               checker->symbols.scopeDepth) {
        checker->symbols.count--;
    }
    checker->symbols.scopeDepth--;
}

void defineSymbol(TypeChecker* checker, const char* name, int length,
                  Type* type, bool isConst) {
    SymbolTable* table = &checker->symbols;

    // Check for redefinition in same scope
    for (int i = table->count - 1; i >= 0; i--) {
        Symbol* sym = &table->symbols[i];
        if (sym->depth < table->scopeDepth) break;

        if (sym->nameLength == length &&
            memcmp(sym->name, name, length) == 0) {
            // Already defined in this scope - error will be reported elsewhere
            return;
        }
    }

    if (table->count >= table->capacity) {
        int oldCapacity = table->capacity;
        table->capacity = GROW_CAPACITY(oldCapacity);
        table->symbols = GROW_ARRAY(Symbol, table->symbols, oldCapacity, table->capacity);
    }

    Symbol* symbol = &table->symbols[table->count++];
    symbol->name = name;
    symbol->nameLength = length;
    symbol->type = type;
    symbol->isConst = isConst;
    symbol->depth = table->scopeDepth;
}

static Symbol* lookupSymbol(TypeChecker* checker, const char* name, int length) {
    SymbolTable* table = &checker->symbols;

    for (int i = table->count - 1; i >= 0; i--) {
        Symbol* sym = &table->symbols[i];
        if (sym->nameLength == length &&
            memcmp(sym->name, name, length) == 0) {
            return sym;
        }
    }

    return NULL;
}

