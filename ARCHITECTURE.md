# Blaze Language Architecture

> A comprehensive guide to the Blaze compiler and virtual machine architecture

---

## Table of Contents

- [Overview](#overview)
- [Compilation Pipeline](#compilation-pipeline)
- [Virtual Machine](#virtual-machine)
- [Type System](#type-system)
- [Memory Management](#memory-management)
- [Module System](#module-system)
- [Standard Library](#standard-library)
- [Optimizations](#optimizations)
- [File Structure](#file-structure)

---

## Overview

Blaze is a statically-typed, bytecode-interpreted programming language written in C (C23). The compiler follows a traditional pipeline architecture:

```
Source Code → Scanner → Parser → AST → Type Checker → Compiler → Bytecode → VM
```

**Key Characteristics**:
- **Language**: C23
- **Compilation Model**: Bytecode interpretation
- **Type System**: Static with inference
- **Memory Management**: Mark-and-sweep garbage collection
- **Execution**: Stack-based virtual machine
- **Codebase Size**: ~12,000+ lines of C; standard library spread across `std/*.blaze` (including generic-style `std/array.blaze`)

---

## Compilation Pipeline

### 1. Scanner (Lexical Analysis)
**File**: `src/scanner.c`, `src/scanner.h`

The scanner converts source code into a stream of tokens.

**Features**:
- Single-pass scanning
- Keyword recognition
- Identifier and literal parsing
- Multi-character operators (`==`, `!=`, `<=`, `>=`, `&&`, `||`, `??`, `?.`, `...`)
- Comment handling (single-line: `//`)
- String literals with escape sequences
- Newline as statement terminator

**Token Types**: 86 token types including:
- Literals: `TOKEN_INT`, `TOKEN_FLOAT`, `TOKEN_STRING`
- Keywords: `TOKEN_LET`, `TOKEN_FN`, `TOKEN_CLASS`, `TOKEN_MATCH`, etc.
- Operators: `TOKEN_PLUS`, `TOKEN_MINUS`, `TOKEN_STAR`, `TOKEN_SLASH`, etc.
- Type keywords: `TOKEN_TYPE_INT`, `TOKEN_TYPE_FLOAT`, `TOKEN_TYPE_BOOL`, `TOKEN_TYPE_STRING`

---

### 2. Parser (Syntax Analysis)
**File**: `src/parser.c`, `src/parser.h`

The parser constructs an Abstract Syntax Tree (AST) from tokens using Pratt parsing for expressions.

**Parsing Techniques**:
- **Pratt Parsing**: For expressions with operator precedence
- **Recursive Descent**: For statements
- **Precedence Climbing**: For binary operators

**Precedence Levels** (lowest to highest):
1. `PREC_NONE`
2. `PREC_ASSIGNMENT` - `=`
3. `PREC_NULL_COALESCE` - `??`
4. `PREC_OR` - `||`
5. `PREC_AND` - `&&`
6. `PREC_EQUALITY` - `==`, `!=`
7. `PREC_COMPARISON` - `<`, `>`, `<=`, `>=`
8. `PREC_TERM` - `+`, `-`
9. `PREC_FACTOR` - `*`, `/`, `%`
10. `PREC_UNARY` - `!`, `-`
11. `PREC_CALL` - `.`, `?.`, `()`, `[]`
12. `PREC_PRIMARY`

**AST Node Types**:
- **Expressions**: 20 types (literals, binary, unary, call, lambda, match, etc.)
- **Statements**: 14 types (var, function, class, if, while, for, try, etc.)
- **Type Nodes**: 6 kinds — simple, array, function, optional, union, **generic** (`Name<Args...>`)

**Lambdas**:
- Parsed speculatively after `(`: parameter list may include `name: Type`, optional `-> ReturnType` before `=>`, and body as **expression** or **`{` block `}`** (block body uses `newLambdaBlockExpr`).
- Explicit **generic call** form `callee<TypeArgs>(args)` is recognized when `<` begins a type list (not comparison).

---

### 3. AST (Abstract Syntax Tree)
**File**: `src/ast.c`, `src/ast.h`

The AST represents the program structure as a tree of nodes.

**Expression Kinds**:
- `EXPR_LITERAL` - Constants (int, float, string, bool, nil)
- `EXPR_UNARY` - Unary operations (`-x`, `!x`)
- `EXPR_BINARY` - Binary operations (`a + b`, `a * b`)
- `EXPR_GROUPING` - Parenthesized expressions
- `EXPR_VARIABLE` - Variable references
- `EXPR_ASSIGN` - Variable assignment
- `EXPR_LOGICAL` - Logical operations (`&&`, `||`)
- `EXPR_NULL_COALESCE` - Null coalescing (`a ?? b`)
- `EXPR_CALL` - Function calls
- `EXPR_LAMBDA` - Lambda expressions (expression or block body; optional param/return types)
- `EXPR_ARRAY` - Array literals
- `EXPR_SPREAD` - Spread operator (`...arr`)
- `EXPR_INDEX` - Array indexing
- `EXPR_INDEX_SET` - Array element assignment
- `EXPR_GET` - Property access
- `EXPR_SET` - Property assignment
- `EXPR_THIS` - This reference
- `EXPR_SUPER` - Super method calls
- `EXPR_MATCH` - Match expressions

**Statement Kinds**:
- `STMT_EXPRESSION` - Expression statements
- `STMT_PRINT` - Print statements
- `STMT_VAR` - Variable declarations
- `STMT_BLOCK` - Block statements
- `STMT_IF` - If statements
- `STMT_WHILE` - While loops
- `STMT_FOR` - For-in loops
- `STMT_FUNCTION` - Function declarations
- `STMT_RETURN` - Return statements
- `STMT_CLASS` - Class declarations
- `STMT_MATCH` - Match statements
- `STMT_TRY` - Try-catch-finally
- `STMT_THROW` - Throw statements
- `STMT_IMPORT` - Import statements

---

### 4. Type Checker
**File**: `src/typechecker.c`, `src/typechecker.h`, `src/types.c`, `src/types.h`

The type checker performs semantic analysis and type inference.

**Type System Features**:
- **Primitive Types**: `int`, `float`, `bool`, `string`, `nil`
- **Composite Types**: Arrays, functions, classes, unions, optionals
- **Type parameters** (functions): `TYPE_TYPE_PARAM` — placeholder types for `T` in `fn f<T>(...)`, resolved in scope during checking; used with conservative rules for arithmetic and comparisons inside generic bodies
- **Generic annotations**: `TYPE_GENERIC_INST` exists for future instantiated types; generic type nodes in the AST may still resolve to `unknown` where full checking is not implemented yet
- **Type Inference**: Bidirectional type inference for variables and functions
- **Type Narrowing**: Automatic refinement in conditionals
- **Union Types**: `int | string`
- **Optional Types**: `int?` (syntactic sugar for `int | nil`)

**Type Checking Rules**:
- Assignment compatibility
- Function argument type checking
- Return type verification
- Array element type consistency
- Union type member checking
- Optional type null safety

**Type Narrowing**:
- `type(x) == "typename"` - Narrows union types
- `x == nil` / `x != nil` - Narrows optional types
- Applies to both then and else branches

---

### 5. Compiler (Bytecode Generation)
**File**: `src/compiler.c`, `src/compiler.h`

The compiler generates bytecode for the virtual machine.

**Compilation Strategies**:
- **Single-pass compilation**: Direct bytecode emission
- **Local variable resolution**: Stack-based locals
- **Upvalue capture**: For closures
- **Constant folding**: Compile-time evaluation
- **Dead code elimination**: Removes unreachable code
- **Type-specialized opcodes**: Separate opcodes for int/float arithmetic

**Optimizations**:
- Constant folding for arithmetic, logical, and string operations
- Dead code elimination after return/throw
- Partial evaluation of logical expressions (`false && x` → `false`)
- String interning for literals

---

## Virtual Machine

**File**: `src/vm.c`, `src/vm.h`

### Architecture

The Blaze VM is a **stack-based bytecode interpreter** with **67** opcodes (see `OpCode` in `src/chunk.h`).

**VM Components**:
- **Value Stack**: 256 slots for operands and temporaries
- **Call Stack**: 64 frames for function calls
- **Globals Table**: Hash table for global variables
- **Strings Table**: Hash table for string interning
- **Garbage Collector**: Mark-and-sweep collector

### Bytecode Opcodes

#### Stack Management (6 opcodes)
- `OP_CONSTANT` - Push constant from constant pool
- `OP_NIL` - Push nil
- `OP_TRUE` - Push true
- `OP_FALSE` - Push false
- `OP_POP` - Pop value from stack
- `OP_DUP` - Duplicate top of stack

#### Variables (7 opcodes)
- `OP_GET_LOCAL` - Get local variable
- `OP_SET_LOCAL` - Set local variable
- `OP_GET_GLOBAL` - Get global variable
- `OP_SET_GLOBAL` - Set global variable
- `OP_DEFINE_GLOBAL` - Define global variable
- `OP_GET_UPVALUE` - Get upvalue (closure variable)
- `OP_SET_UPVALUE` - Set upvalue

#### Arithmetic (15 opcodes)
Type-specialized for performance:
- **Integer**: `OP_ADD_INT`, `OP_SUBTRACT_INT`, `OP_MULTIPLY_INT`, `OP_DIVIDE_INT`, `OP_MODULO_INT`, `OP_NEGATE_INT`
- **Float**: `OP_ADD_FLOAT`, `OP_SUBTRACT_FLOAT`, `OP_MULTIPLY_FLOAT`, `OP_DIVIDE_FLOAT`, `OP_NEGATE_FLOAT`
- **Mixed**: `OP_ADD_MIXED`, `OP_SUBTRACT_MIXED`, `OP_MULTIPLY_MIXED`, `OP_DIVIDE_MIXED`
- **Conversion**: `OP_INT_TO_FLOAT`, `OP_FLOAT_TO_INT`

#### Comparison (6 opcodes)
- `OP_EQUAL` - Equality test
- `OP_NOT_EQUAL` - Inequality test
- `OP_GREATER` - Greater than
- `OP_GREATER_EQUAL` - Greater or equal
- `OP_LESS` - Less than
- `OP_LESS_EQUAL` - Less or equal

#### Logic (1 opcode)
- `OP_NOT` - Logical NOT

#### Strings (1 opcode)
- `OP_CONCAT` - String concatenation

#### Control Flow (4 opcodes)
- `OP_JUMP` - Unconditional jump
- `OP_JUMP_IF_FALSE` - Jump if false
- `OP_JUMP_IF_TRUE` - Jump if true
- `OP_LOOP` - Loop back

#### Functions (5 opcodes)
- `OP_CALL` - Call function
- `OP_TAIL_CALL` - Tail call (reuse frame)
- `OP_CLOSURE` - Create closure
- `OP_CLOSE_UPVALUE` - Close upvalue
- `OP_RETURN` - Return from function

#### Classes (8 opcodes)
- `OP_CLASS` - Define class
- `OP_GET_PROPERTY` - Get property
- `OP_SET_PROPERTY` - Set property
- `OP_METHOD` - Define method
- `OP_INVOKE` - Optimized method call
- `OP_INHERIT` - Inherit from superclass
- `OP_GET_SUPER` - Get superclass method
- `OP_SUPER_INVOKE` - Call superclass method

#### Arrays (7 opcodes)
- `OP_ARRAY` - Create array with N elements
- `OP_INDEX_GET` - Get array element (`arr[i]`)
- `OP_INDEX_SET` - Set array element (`arr[i] = x`)
- `OP_ARRAY_LENGTH` - Get array length
- `OP_RANGE` - Create range array (`start..end`)
- `OP_ARRAY_CONCAT` - Concatenate two arrays
- `OP_ARRAY_SLICE` - Slice array (`slice` / rest destructuring)

#### Exceptions (3 opcodes)
- `OP_TRY` - Begin try block
- `OP_TRY_END` - End try block
- `OP_THROW` - Throw exception

#### I/O (1 opcode)
- `OP_PRINT` - Print value to stdout

---

## Type System

### Primitive Types

```blaze
int     // 64-bit signed integer
float   // 64-bit floating point
bool    // Boolean (true/false)
string  // UTF-8 string
nil     // Null value
```

### Composite Types

**Arrays**:
```blaze
[int]           // Array of integers
[string]        // Array of strings
[[int]]         // 2D array
```

**Functions**:
```blaze
(int, int) -> int           // Function type
() -> string                // No parameters
(int) -> (int) -> int       // Curried function
```

**Union Types**:
```blaze
int | string                // Can be int or string
int | float | nil           // Three-way union
```

**Optional Types**:
```blaze
int?            // Shorthand for int | nil
string?         // Shorthand for string | nil
```

### Type Inference

Blaze uses bidirectional type inference:

```blaze
// Variable inference
let x = 42              // Inferred as int
let y = 3.14            // Inferred as float
let z = [1, 2, 3]       // Inferred as [int]

// Function inference
fn double(x) {          // Parameter type inferred from usage
    return x * 2
}

// Lambda inference
let add = (a, b) => a + b   // Fully inferred
```

### Type Narrowing

```blaze
let value: int | string = getValue()

if type(value) == "int" {
    // value is narrowed to int here
    let doubled = value * 2  // OK
}

let maybe: int? = getMaybe()

if maybe != nil {
    // maybe is narrowed to int here
    let result = maybe + 10  // OK
}
```

---

## Memory Management

### Garbage Collection

Blaze uses a **mark-and-sweep garbage collector**.

**GC Phases**:
1. **Mark**: Traverse reachable objects from roots
2. **Sweep**: Free unmarked objects

**GC Roots**:
- Value stack
- Call frames
- Global variables
- **Active compiler chain** (`markCompilerRoots`) while a function or script is being compiled
- Open upvalues

**Compile-time boundary**: When a top-level script finishes compiling, the active compiler is torn down (`current` is cleared). The compiled `ObjFunction` is not yet reachable from globals. Before the AST is freed, the VM **must** push that function onto the value stack so a GC triggered during AST teardown cannot collect the bytecode chunk while it is still needed for execution (`interpretInternal` in `src/vm.c`).

**GC Triggers**:
- After allocating certain number of bytes
- Threshold grows/shrinks based on heap size
- Manual trigger via `collectGarbage()`

### Value Representation

**NaN boxing** — implementation in [`src/value.h`](src/value.h). `Value` is a single **`uint64_t`** (8 bytes on all supported platforms).

- **IEEE doubles**: Non–quiet-NaN `double` values are stored as their raw bit patterns (the common case for float-heavy code).
- **Tagged values**: A quiet-NaN bit pattern plus a 3-bit type tag and 48-bit payload encode nil, booleans, integers, and object pointers without a separate runtime `ValueType` discriminant.

**Payload kinds** (see macros in `value.h`):

| Kind | Detection | Notes |
|------|-----------|--------|
| Nil | `IS_NIL` | Single `NIL_VAL` |
| Bool | `IS_BOOL` | `FALSE_VAL` / `TRUE_VAL` |
| Int | `IS_INT` | 48-bit signed range (±2^47) |
| Float | `IS_FLOAT` | Any other valid double bits |
| Object | `IS_OBJ` | 48-bit pointer payload |

This matches the README and CHANGELOG: stack slots and constant pools use 8 bytes per value instead of a larger tagged struct.

### Object Types

All heap-allocated values are objects:

- `OBJ_STRING` - Interned strings
- `OBJ_ARRAY` - Dynamic arrays
- `OBJ_FUNCTION` - Function objects
- `OBJ_CLOSURE` - Closures (function + upvalues)
- `OBJ_NATIVE` - Native C functions
- `OBJ_CLASS` - Class objects
- `OBJ_INSTANCE` - Class instances
- `OBJ_BOUND_METHOD` - Method bound to instance
- `OBJ_UPVALUE` - Closed-over variables

---

## Module System

### Import Syntax

**Import All**:
```blaze
import "std/math"
// All exports available globally
```

**Selective Import**:
```blaze
import { map, filter, reduce } from "std/array"
// Only specified exports available
```

### Module Resolution

1. Check if path is absolute
2. If relative, resolve from current file directory
3. Check standard library paths
4. Load and parse module
5. Execute module in isolated scope
6. Export symbols to global scope

### Prelude

`std/prelude.blaze` is automatically imported in all files:
- Common utility functions
- Type conversion functions (`toString`, `toInt`, `toFloat`)
- Array utilities (`len`, `push`, `pop`)

---

## Standard Library

### Architecture

**Native Primitives** (C functions):
- Low-level operations that can't be implemented in Blaze
- Math: `_sin`, `_cos`, `_sqrt`, `_log`, `_exp`, `_random`
- String: `_charAt`, `_substr`
- Array: internal operations
- Type operations: `type`, `toString`

**Standard Library** (Blaze code):
- Built on native primitives
- User-extensible
- Better testability

### Modules

**std/math.blaze**:
- `sign`, `signf`, `isEven`, `isOdd`
- `gcd`, `lcm`, `factorial`, `fib`
- `isPrime`, `sumRange`
- `square`, `cube`, `divmod`
- `lerp`, `clampf`, `minf`, `maxf`

**std/string.blaze**:
- `contains`, `isEmpty`, `isBlank`
- `padLeft`, `padRight`, `center`
- `countOccurrences`, `reverseString`
- `capitalize`

**std/array.blaze**:
- Higher-order: `map`, `filter`, `reduce`, `forEach`
- Search: `find`, `findIndex`, `indexOf`, `any`, `all`
- Aggregate: `sum`, `product`, `average`
- Transform: `copy`, `concatArrays`, `take`, `drop`
- Utility: `range`, `fill`, `isEmpty`, `countValue`

**std/prelude.blaze**:
- Auto-loaded common functions
- `len`, `push`, `pop`
- `toString`, `toInt`, `toFloat`

---

## Optimizations

### String Interning

All strings with identical content share the same memory:

```blaze
"hello" == "hello"  // true (pointer equality)
```

**Benefits**:
- O(1) string comparison
- Reduced memory usage
- Match expressions work with strings

### Constant Folding

Compile-time evaluation of constant expressions:

```blaze
let x = 2 + 3           // Compiled as: let x = 5
let y = 10 * 5 - 3      // Compiled as: let y = 47
let z = true && false   // Compiled as: let z = false
```

**Supported Operations**:
- Arithmetic: `+`, `-`, `*`, `/`, `%`
- Comparison: `==`, `!=`, `<`, `>`, `<=`, `>=`
- Logical: `&&`, `||`, `!`
- String concatenation: `+`

### Dead Code Elimination

Removes unreachable code after terminators:

```blaze
fn example() -> int {
    return 42
    print("never runs")  // Not compiled
}
```

**Benefits**:
- Smaller bytecode
- Faster compilation
- Helps catch logic errors

---

## File Structure

```
blaze-lang/
├── main.c                  # REPL and file execution
├── src/
│   ├── scanner.c/h         # Lexical analysis
│   ├── parser.c/h          # Syntax analysis
│   ├── ast.c/h             # AST nodes
│   ├── typechecker.c/h     # Type checking
│   ├── types.c/h           # Type system
│   ├── compiler.c/h        # Bytecode generation
│   ├── vm.c/h              # Virtual machine
│   ├── chunk.c/h           # Bytecode chunks
│   ├── value.c/h           # Value representation
│   ├── object.c/h          # Heap objects
│   ├── memory.c/h          # Memory management
│   ├── table.c/h           # Hash tables
│   ├── module.c/h          # Module system
│   ├── colors.c/h          # Terminal colors
│   ├── debug.c/h           # Debugging utilities
│   └── common.h            # Common definitions
├── std/
│   ├── prelude.blaze       # Auto-loaded functions
│   ├── math.blaze          # Math functions
│   ├── string.blaze        # String utilities
│   └── array.blaze         # Array utilities
├── tests/                  # Test files
├── examples/               # Example programs
├── CMakeLists.txt          # Build configuration
├── CHANGELOG.md            # Version history
├── ROADMAP.md              # Future plans
├── ARCHITECTURE.md         # This file
└── .cursor/rules/          # Optional Cursor IDE rules for contributors
```

---

## Performance Characteristics

### Time Complexity

- **Variable lookup**: O(1) for locals/globals
- **Function call**: O(1) overhead
- **Array access**: O(1)
- **String comparison**: O(1) after interning
- **GC mark phase**: O(live objects)
- **GC sweep phase**: O(total heap)

### Space Complexity

- **Value**: 8 bytes (NaN-boxed `uint64_t`; see [`src/value.h`](src/value.h) and CHANGELOG)
- **Call frame**: ~40 bytes
- **Stack**: 256 values max (~4KB)
- **Heap objects**: Varies by type

---

## Design Principles

1. **Simplicity**: Clean, readable C code
2. **Performance**: Type-specialized opcodes, string interning
3. **Safety**: Garbage collection, type checking
4. **Extensibility**: Easy to add new features
5. **Developer Experience**: Great error messages, REPL, colors

---

**Last Updated**: March 30, 2026
