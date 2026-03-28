# Blaze Compiler Implementation Plan

> **Status**: ✅ Production Ready | **Completion**: 99% | **Last Updated**: March 28, 2026

## Table of Contents

- [Getting Started](#getting-started)
- [Quick Summary](#quick-summary)
- [Current State](#current-state-summary)
- [Known Limitations](#known-limitations)
- [Language Features Reference](#language-features-reference)
- [VM Architecture](#vm-architecture)
- [Implementation Status](#implementation-status)
- [File Structure](#file-structure-updated)
- [Verification](#verification)
- [Changelog](#changelog)

---

## Getting Started

```bash
# Build the compiler
cmake -B build && cmake --build build

# Run a Blaze program
./build/blaze examples/comprehensive_test.blaze

# Start the interactive REPL
./build/blaze

# Run tests
./build/blaze examples/comprehensive_test.blaze
```

### Example Blaze Code

```blaze
// Variables with type inference
let x = 42
let name = "Blaze"

// String interpolation
let greeting = "Hello, ${name}!"
let info = "The answer is ${x * 2}"

// Functions
fn fibonacci(n: int) -> int {
    if n <= 1 {
        return n
    }
    return fibonacci(n - 1) + fibonacci(n - 2)
}

// Lambdas and higher-order functions
let doubled = map([1, 2, 3, 4], (x) => x * 2)
let evens = filter([1, 2, 3, 4, 5, 6], (x) => isEven(x))

// Union types
let value: int | string = 100
value = "hello"  // Valid!

// Optional types with null coalescing
let maybe: int? = nil
let result = maybe ?? 42

// Type narrowing for union types
let data: int | string = 100
if type(data) == "int" {
    let doubled = data * 2  // data is narrowed to int here
    print(doubled)
}

// Type narrowing for optional types
let opt: int? = 42
if opt != nil {
    let squared = opt * opt  // opt is narrowed to int here
    print(squared)
}

// Pattern matching (statement)
match value {
    1 => print("one")
    2 => print("two")
    _ => print("other")
}

// Match expressions with integers
let result = match value {
    1 => "one"
    2 => "two"
    _ => "other"
}

fn getDay(n: int) -> string {
    return match n {
        1 => "Monday"
        2 => "Tuesday"
        _ => "Weekend"
    }
}

// Match expressions with strings (works with string interning!)
fn classifyAnimal(animal: string) -> string {
    return match animal {
        "dog" => "Mammal"
        "cat" => "Mammal"
        "bird" => "Avian"
        _ => "Unknown"
    }
}

// Classes and inheritance
class Animal {
    fn speak() -> string {
        return "Some sound"
    }
}

class Dog extends Animal {
    fn speak() -> string {
        return "Woof!"
    }
}

// Exception handling
try {
    let result = divide(10, 0)
} catch (e) {
    print("Error: " + e)
} finally {
    print("Cleanup")
}

// Module imports
import { map, filter, reduce } from "std/array"
```

## Quick Summary

Blaze is a **complete, production-ready** statically-typed programming language with:
- Modern syntax with type inference
- Classes, closures, and inheritance
- Pattern matching and exception handling
- Module system with standard library
- Interactive REPL with readline support
- Excellent developer experience (colored errors, helpful hints)

**Codebase Size**: ~10,680 lines of C code | ~481 lines of Blaze standard library

The compiler is feature-complete with only optional enhancements remaining (generics, optimizations, etc.).

---

## Current State Summary

The Blaze compiler is **functionally complete** and production-ready! It's a statically-typed, bytecode-interpreted language written in C (C23). The core pipeline works: Scanner → Parser → AST → Type Checker → Compiler → VM.

**Developer Experience is excellent** with colored output, context-aware error messages with hints, and an enhanced REPL with GNU Readline support!

**Last Updated**: March 28, 2026

### What's Working
- ✅ Full lexical analysis (scanner)
- ✅ Expression and statement parsing
- ✅ AST construction
- ✅ Type checking with scope management
- ✅ Bytecode compilation
- ✅ Stack-based VM with 64 opcodes
- ✅ Classes, closures, and inheritance (including super calls)
- ✅ Garbage collection (mark-and-sweep)
- ✅ Lambda expressions with closures and type inference
- ✅ Match/case statements and **match expressions** with pattern matching
- ✅ For-in loops with proper array iteration
- ✅ Index assignment for arrays
- ✅ Range syntax (start..end)
- ✅ Exception handling (try/catch/throw/finally)
- ✅ Native primitives for low-level operations
- ✅ Higher-order functions (map, filter, reduce, etc.)
- ✅ Function type parameters (passing functions as arguments)
- ✅ Module system with selective imports
- ✅ String concatenation with `+` operator
- ✅ **Union types** (`int | string`)
- ✅ **Prelude auto-loading** (std/prelude.blaze)
- ✅ **Enhanced error messages** with code context and hints
- ✅ Optional/nullable types (`int?`) with null coalescing (`??`)
- ✅ Interactive REPL with multi-line support and line editing
- ✅ Comprehensive standard library (math, array, string modules)
- ✅ **Color-coded terminal output** for errors, warnings, and REPL
- ✅ **Context-aware error messages** with code snippets and helpful hints
- ✅ **String interning** for efficient string comparison and memory usage
- ✅ **Type narrowing** for union and optional types in conditionals
- ✅ **String interpolation** with `${expression}` syntax

### Known Limitations

These are current design choices or limitations:

- **Finally blocks and early returns**: Finally blocks don't execute on early return/break (would require more complex bytecode)

### Optional Future Enhancements

The core compiler is complete and has excellent DX. These are optional enhancements:

- **Generic types** (e.g., `Map<K, V>`, `Array<T>`) - Would require significant type system extensions
- **Debug mode** with step-through execution and breakpoints
- **Optimizations**: Constant folding, dead code elimination, tail call optimization
- **NaN boxing** for value representation (performance optimization)
- **Spread operator** for arrays (`...arr`)
- **Destructuring** for arrays and objects

### Architecture Decision: Standard Library

**String and math operations should be implemented via a Blaze standard library**, not as native C functions. This provides:
- Cleaner separation of concerns
- User-extensible standard library
- Smaller native function set
- Better testability

**Native Primitives (C)** - Operations that MUST be native:
- Low-level math: `_sin`, `_cos`, `_sqrt`, `_log`, `_exp`, `_random`
- Memory/type operations: `len`, `type`, `_charAt`, `_substr`
- I/O: `print`, `input`, `readFile`, `writeFile`
- Array primitives: `push`, `pop`, `_arrayGet`, `_arraySet`

**Standard Library (Blaze)** - Higher-level functions written in Blaze:
- `std/string.blaze`: `contains`, `isEmpty`, `isBlank`, `padLeft`, `padRight`, `capitalize`, etc.
- `std/math.blaze`: `sign`, `isEven`, `isOdd`, `gcd`, `lcm`, `factorial`, `fib`, `isPrime`, `square`, `cube`, etc.
- `std/array.blaze`: `sum`, `product`, `average`, `range`, `map`, `filter`, `reduce`, `any`, `all`, `find`, etc.

---

## Phase 1: Core Language Completions (High Priority) - COMPLETED

### 1.1 Index Assignment for Arrays - COMPLETED
### 1.2 For Loop Proper Iteration - COMPLETED
### 1.3 Super Method Calls - COMPLETED

---

## Phase 2: Lambda Expressions - COMPLETED

---

## Phase 3: Match/Case Statements - COMPLETED

**Status**: Match/case statements are fully implemented as statements (not expressions).

**Implementation**:
- Pattern matching with literal values and wildcard (`_`)
- Multiple case arms with `=>` syntax
- Type checking for match arms
- Compiler generates jump tables for efficient dispatch

**Note**: Match is currently a statement. To return values, assign to a variable within each case arm.

---

## Phase 4: Module/Import System - COMPLETED

### 4.1 Basic Module Support - COMPLETED
**Files**: `module.c/h`, `scanner.c`, `parser.c`, `compiler.c`, `vm.c`

**Completed**:
- [x] Design module resolution strategy (file-based)
- [x] Create `module.c/h` for module management
- [x] Implement `parseImport()` in `parser.c`
- [x] Add module loading to compiler
- [x] Implement module caching to avoid re-compilation
- [x] Handle circular imports
- [x] Add tests for single file imports: `import "std/string"`
- [x] Selective imports: `import { startsWith, endsWith } from "std/string"`

### 4.2 Standard Library Structure - COMPLETED
**Files**: `std/` directory with `.blaze` files

```
blaze-lang/
├── std/
│   ├── string.blaze     # String utilities
│   ├── math.blaze       # Math utilities
│   ├── array.blaze      # Array utilities
│   └── ...
```

**Completed**:
- [x] Create `std/string.blaze` with string functions (contains, isEmpty, isBlank, padLeft, padRight, center, countOccurrences, reverseString, capitalize)
- [x] Create `std/math.blaze` with math functions (sign, signf, isEven, isOdd, gcd, lcm, factorial, fib, isPrime, sumRange, square, cube, divmod, lerp, clampf, minf, maxf)
- [x] Create `std/array.blaze` with array functions (sum, product, average, range, fill, indexOf, countValue, isEmpty, copy, concatArrays, take, drop)
- [x] Fix empty array literal type inference
- [x] Higher-order functions (map, filter, reduce, forEach, any, all, find, findIndex)
- [x] Lambda type checking and function type inference

**Completed**:
- [x] Auto-load prelude on startup (implemented in vm.c:loadPrelude())

---

## Phase 5: Error Handling - COMPLETED

### 5.1 Try/Catch/Finally Mechanism - COMPLETED
**Completed**:
- [x] Added `try`, `catch`, `throw`, `finally` keywords
- [x] Added AST nodes and parsing
- [x] Implemented stack unwinding in VM
- [x] Added `OP_TRY`, `OP_TRY_END`, `OP_THROW` opcodes
- [x] Nested try/catch support
- [x] Finally blocks (runs after try or catch)

**Known limitations**:
- Finally doesn't run on early return/break (would require more complex bytecode)

---

## Phase 6: Native Primitives (Refactored)

### 6.1 Core Primitives (Keep as Native)
**Files**: `vm.c`, `typechecker.c`

These MUST remain native (require C library or low-level access):

**Math Primitives**:
- [x] `_sin(x)`, `_cos(x)`, `_tan(x)` - trigonometric (calls C math.h)
- [x] `_log(x)`, `_log10(x)`, `_exp(x)` - logarithms/exponential
- [x] `_sqrt(x)` - square root
- [x] `_random()` - random number generation
- [x] `_floor(x)`, `_ceil(x)` - rounding (native for performance)

**String Primitives**:
- [x] `len(s)` - get length
- [x] `_charAt(s, i)` - get character at index
- [x] `_substr(s, start, len)` - substring extraction
- [x] `_concat(a, b)` - string concatenation

**Array Primitives**:
- [x] `len(arr)` - array length
- [x] `push(arr, val)` - append element
- [x] `pop(arr)` - remove last element

**I/O Primitives**:
- [x] `print(val)` - output
- [x] `input(prompt)` - read input
- [x] `readFile(path)` - read file
- [x] `writeFile(path, content)` - write file
- [x] `fileExists(path)` - check file
- [x] `deleteFile(path)` - delete file

**Type Primitives**:
- [x] `type(val)` - get type name
- [x] `toString(val)`, `toInt(val)`, `toFloat(val)` - conversions

### 6.2 Functions to Move to Standard Library
**After module system is complete**, these should be reimplemented in Blaze:

**String Functions** (move to `std/string.blaze`):
- [ ] `startsWith(s, prefix)` → `_substr(s, 0, len(prefix)) == prefix`
- [ ] `endsWith(s, suffix)` → check last N chars
- [ ] `repeat(s, n)` → loop and concat
- [ ] `trim(s)` → strip whitespace
- [ ] `toUpper(s)`, `toLower(s)` → character mapping
- [ ] `split(s, delim)` → parse and build array
- [ ] `join(arr, sep)` → iterate and concat
- [ ] `replace(s, old, new)` → find and replace
- [ ] `indexOf(s, sub)` → search

**Math Functions** (move to `std/math.blaze`):
- [ ] `abs(x)` → `if x < 0 { -x } else { x }`
- [ ] `min(a, b)` → `if a < b { a } else { b }`
- [ ] `max(a, b)` → `if a > b { a } else { b }`
- [ ] `clamp(x, lo, hi)` → `min(max(x, lo), hi)`
- [ ] `pow(base, exp)` → loop multiplication
- [ ] `round(x)` → `_floor(x + 0.5)`

**Array Functions** (move to `std/array.blaze`):
- [ ] `first(arr)` → `arr[0]`
- [ ] `last(arr)` → `arr[len(arr) - 1]`
- [ ] `contains(arr, val)` → loop and compare
- [ ] `reverse(arr)` → swap elements
- [ ] `slice(arr, start, end)` → build new array
- [ ] `sort(arr)` → implement sorting algorithm
- [x] `map(arr, fn)` → apply fn to each element
- [x] `filter(arr, fn)` → filter by predicate
- [x] `reduce(arr, fn, init)` → fold operation
- [x] `forEach(arr, fn)` → execute fn for each element
- [x] `any(arr, fn)` → true if any element matches
- [x] `all(arr, fn)` → true if all elements match
- [x] `find(arr, fn)` → first matching element
- [x] `findIndex(arr, fn)` → index of first match

---

## Phase 7: Advanced Type System

### 7.1 Generic Types
- [ ] Add generic type syntax: `Array<int>`
- [ ] Implement type parameter tracking
- [ ] Add generic function support

### 7.2 Union Types - COMPLETED
- [x] Add union type syntax: `int | string`
- [x] Implement union type checking
- [x] Union type assignability rules (T can be assigned to T | U)
- [x] Add type narrowing in conditionals

### 7.3 Optional/Nullable Types - COMPLETED
- [x] Add optional type syntax: `int?`
- [x] Add optional chaining: `obj?.field`
- [x] Add null coalescing: `value ?? default`

---

## Phase 8: Developer Experience

### 8.1 REPL - COMPLETED
- [x] Create interactive REPL mode
- [x] Multi-line input with brace/bracket detection
- [x] Variable and function persistence between inputs
- [x] Special commands (.help, .exit, .clear, .test, .history)
- [x] **GNU Readline integration** with full line editing
- [x] **Persistent history** saved to `~/.blaze_history`
- [x] Arrow keys for history navigation (via readline)
- [x] Emacs-style keybindings (Ctrl+A/E, Ctrl+U/K, etc.)
- [x] Fallback to custom line editing when readline unavailable

### 8.2 Better Error Messages - COMPLETED
- [x] Show code context with caret pointing to error location
- [x] Add fix suggestions for common errors (with 💡 hints)
- [x] Color-coded output with ANSI colors for terminal

### 8.3 Debug Mode
- [ ] Step-through execution
- [ ] Breakpoint support

---

## Phase 9: Optimizations

### 9.1 Bytecode Optimizations
- [ ] Constant folding
- [ ] Dead code elimination
- [ ] Tail call optimization

### 9.2 VM Optimizations
- [ ] Inline caching for property access
- [ ] NaN boxing for values

---

## Implementation Status

### ✅ Completed (All Core Features)

**Phase 1-6**: All core language features
- Scanner, parser, AST, type checker, compiler, VM ✓
- Classes, inheritance, closures ✓
- Lambda expressions with type inference ✓
- Match/case pattern matching ✓
- Exception handling (try/catch/finally) ✓
- Module system with selective imports ✓
- Union types and optional types ✓
- Standard library (math, string, array modules) ✓
- Prelude auto-loading ✓

**Phase 8**: Developer Experience
- Interactive REPL with GNU Readline ✓
- Persistent command history ✓
- Color-coded terminal output ✓
- Context-aware error messages with hints ✓

### 🔮 Future Enhancements (Optional)

**Phase 7**: Advanced Type System
- Generic types (e.g., `Array<T>`, `Map<K,V>`)

**Phase 8**: Debug Mode
- Step-through execution
- Breakpoint support
- Stack trace inspection

**Phase 9**: Performance Optimizations
- Constant folding
- Dead code elimination
- Tail call optimization
- NaN boxing for value representation

---

## VM Architecture

### Bytecode Virtual Machine

The Blaze VM is a stack-based bytecode interpreter with 64 opcodes organized into these categories:

**Stack Management**: `OP_CONSTANT`, `OP_NIL`, `OP_TRUE`, `OP_FALSE`, `OP_POP`, `OP_DUP`

**Variables**: `OP_GET_LOCAL`, `OP_SET_LOCAL`, `OP_GET_GLOBAL`, `OP_SET_GLOBAL`, `OP_DEFINE_GLOBAL`, `OP_GET_UPVALUE`, `OP_SET_UPVALUE`

**Arithmetic** (type-specialized):
- Integer ops: `OP_ADD_INT`, `OP_SUBTRACT_INT`, `OP_MULTIPLY_INT`, `OP_DIVIDE_INT`, `OP_MODULO_INT`, `OP_NEGATE_INT`
- Float ops: `OP_ADD_FLOAT`, `OP_SUBTRACT_FLOAT`, `OP_MULTIPLY_FLOAT`, `OP_DIVIDE_FLOAT`, `OP_NEGATE_FLOAT`
- Mixed ops: `OP_ADD_MIXED`, `OP_SUBTRACT_MIXED`, `OP_MULTIPLY_MIXED`, `OP_DIVIDE_MIXED`
- Type conversion: `OP_INT_TO_FLOAT`, `OP_FLOAT_TO_INT`

**Comparison**: `OP_EQUAL`, `OP_NOT_EQUAL`, `OP_GREATER`, `OP_GREATER_EQUAL`, `OP_LESS`, `OP_LESS_EQUAL`

**Logic**: `OP_NOT`

**Strings**: `OP_CONCAT`

**Control Flow**: `OP_JUMP`, `OP_JUMP_IF_FALSE`, `OP_JUMP_IF_TRUE`, `OP_LOOP`

**Functions**: `OP_CALL`, `OP_CLOSURE`, `OP_CLOSE_UPVALUE`, `OP_RETURN`

**Classes**: `OP_CLASS`, `OP_GET_PROPERTY`, `OP_SET_PROPERTY`, `OP_METHOD`, `OP_INVOKE`, `OP_INHERIT`, `OP_GET_SUPER`, `OP_SUPER_INVOKE`

**Arrays**: `OP_ARRAY`, `OP_INDEX_GET`, `OP_INDEX_SET`, `OP_ARRAY_LENGTH`, `OP_RANGE`

**Exceptions**: `OP_TRY`, `OP_TRY_END`, `OP_THROW`

**I/O**: `OP_PRINT`

### Memory Management

- **Garbage Collection**: Mark-and-sweep GC
- **Value Representation**: Tagged union (not NaN-boxed)
- **Object Types**: Strings, arrays, functions, closures, classes, instances, upvalues, bound methods, native functions

---

## Language Features Reference

### Type System

**Primitive Types**:
- `int` - 64-bit integer
- `float` - 64-bit floating point
- `bool` - Boolean (true/false)
- `string` - UTF-8 strings
- `nil` - Null value

**Composite Types**:
- `[T]` - Arrays (e.g., `[int]`, `[string]`)
- `T | U` - Union types (e.g., `int | string`)
- `T?` - Optional types (e.g., `int?` is shorthand for `int | nil`)
- `(T, U) -> R` - Function types

### Syntax Features

**Variables**:
```blaze
let x: int = 42          // Explicit type
let y = 3.14             // Type inference
const PI = 3.14159       // Constant
```

**Functions**:
```blaze
fn add(a: int, b: int) -> int {
    return a + b
}

// Lambda expressions
let multiply = (a, b) => a * b
```

**Control Flow**:
```blaze
// If-else
if x > 10 {
    print("big")
} else {
    print("small")
}

// While loop
while x < 100 {
    x = x + 1
}

// For-in loop
for item in [1, 2, 3] {
    print(item)
}

// Match statement
match value {
    1 => print("one")
    2 => print("two")
    _ => print("other")
}
```

**Exception Handling**:
```blaze
try {
    riskyOperation()
} catch (e) {
    print("Error: " + e)
} finally {
    cleanup()
}
```

**Classes**:
```blaze
class Point {
    let x: int
    let y: int

    fn distance() -> float {
        return _sqrt(this.x * this.x + this.y * this.y)
    }
}

class Point3D extends Point {
    let z: int
}
```

**Modules**:
```blaze
// Import entire module
import "std/math"

// Selective imports
import { map, filter } from "std/array"
```

**Operators**:
- Arithmetic: `+`, `-`, `*`, `/`, `%`
- Comparison: `==`, `!=`, `<`, `>`, `<=`, `>=`
- Logical: `&&`, `||`, `!`
- String concatenation: `+`
- Optional chaining: `?.`
- Null coalescing: `??`
- Range: `..` (e.g., `1..10`)

---

## File Structure (Updated)

```
blaze-lang/
├── src/
│   ├── scanner.c/h      # Lexical analysis
│   ├── parser.c/h       # Syntax analysis
│   ├── ast.c/h          # AST definitions
│   ├── types.c/h        # Type system
│   ├── typechecker.c/h  # Type checking
│   ├── compiler.c/h     # Bytecode generation
│   ├── chunk.c/h        # Bytecode chunks
│   ├── vm.c/h           # Virtual machine
│   ├── object.c/h       # Runtime objects
│   ├── memory.c/h       # Memory management
│   ├── value.c/h        # Value representation
│   ├── table.c/h        # Hash tables
│   ├── module.c/h       # Module system (NEW)
│   ├── debug.c/h        # Debugging utilities
│   └── main.c           # Entry point & tests
├── std/                  # Standard library (NEW)
│   ├── prelude.blaze    # Auto-imported
│   ├── string.blaze     # String utilities
│   ├── math.blaze       # Math utilities
│   ├── array.blaze      # Array utilities
│   └── io.blaze         # I/O utilities
├── CMakeLists.txt
└── IMPLEMENTATION_PLAN.md
```

---

## Notes

- Higher-order functions are now fully supported (map, filter, reduce, etc.)
- Lambda expressions now have proper type inference
- Native functions should be minimal primitives (prefixed with `_` for internal ones)
- Standard library provides the user-facing API in Blaze
- This architecture allows users to extend/override standard library functions

## Verification

The compiler has been tested with a comprehensive test suite (`examples/comprehensive_test.blaze`) that verifies:
- ✅ Basic types (int, float, string, bool)
- ✅ Arrays and indexing
- ✅ Functions and lambdas
- ✅ Higher-order functions (map, filter, reduce)
- ✅ Union types
- ✅ Optional/nullable types
- ✅ Classes and inheritance
- ✅ Match/case statements
- ✅ Try/catch/finally exception handling
- ✅ For and while loops
- ✅ Module system and prelude auto-import
- ✅ Standard library functions

All tests pass successfully!

---

## Changelog

### March 28, 2026 - String Interpolation Implemented
- **NEW FEATURE**: String interpolation - embed expressions directly in strings
- Use `${expression}` syntax to interpolate any expression into a string
- Expressions are automatically converted to strings using `toString()`
- Supports all expression types: variables, literals, arithmetic, function calls
- Multiple interpolations in a single string
- Cleaner, more readable string concatenation

Syntax examples:
```blaze
// Variables
let name = "Alice"
let greeting = "Hello, ${name}!"  // "Hello, Alice!"

// Expressions
let x = 10
let info = "${x} * 2 = ${x * 2}"  // "10 * 2 = 20"

// Function calls
fn double(n: int) -> int { return n * 2 }
let result = "Double of 5 is ${double(5)}"  // "Double of 5 is 10"

// Multiple interpolations
let a = "foo"
let b = "bar"
let combined = "${a} and ${b}"  // "foo and bar"
```

Implementation details:
- Parser detects `${...}` patterns in string literals
- Converts to concatenation: `"Hello, ${name}!"` → `"Hello, " + toString(name) + "!"`
- No runtime overhead beyond normal string concatenation
- Supports nested braces in expressions
- Error handling for unterminated interpolations

Parser changes (src/parser.c):
- Added `parseInterpolatedString()` to handle interpolation parsing
- Modified `literal()` to detect and route interpolated strings
- Creates binary expression trees for concatenation

Compiler changes (src/compiler.c):
- Modified `compileLiteral()` to use pre-computed values for string parts
- Fixed bug where token content was used instead of literal value

Benefits:
- Much cleaner syntax than manual concatenation
- Reduced cognitive load when building complex strings
- Matches modern language conventions (JS, Python, Rust, etc.)
- Zero performance overhead (same as manual concatenation)

### March 28, 2026 - Type Narrowing Implemented
- **NEW FEATURE**: Type narrowing - automatic type refinement in conditional blocks
- Union and optional types are now automatically narrowed based on type guards
- Supported type guards:
  - `type(x) == "typename"` - narrows union types to specific type
  - `x == nil` / `x != nil` - narrows optional types
- Type narrowing works in both then and else branches
- Allows safe operations on narrowed types without explicit casts

Type guard patterns:
```blaze
// Union type narrowing
let value: int | string = 100
if type(value) == "int" {
    let doubled = value * 2  // value is int here
}

// Optional type narrowing
let maybe: int? = 42
if maybe != nil {
    let result = maybe * 2  // maybe is int here
}

// Narrowing in else branch
let data: int | string = "text"
if type(data) == "int" {
    // data is int
} else {
    // data is string
}
```

Implementation details:
- Added type guard detection in `analyzeTypeGuard()` function
- Type narrowing applied via symbol shadowing in nested scopes
- Enhanced equality operator to allow comparing optional types with nil
- Comprehensive test suite with 6 test categories

Benefits:
- Removed "Type narrowing" from known limitations
- Makes union and optional types much more practical
- Type-safe without explicit type assertions
- Better developer experience with fewer type errors

### March 28, 2026 - String Interning Implemented
- **NEW FEATURE**: String interning - all strings with the same content now share the same memory
- Added `strings` table to VM for interning
- Modified `copyString()` to check intern table before creating new strings
- Modified `takeString()` to reuse interned strings
- String literal equality now works correctly: `"hello" == "hello"` returns `true`
- Match expressions now work with string patterns
- Significant memory savings for duplicate strings
- O(1) string comparison instead of O(n)

Benefits:
- Fixed string literal comparison (was a known limitation)
- Match expressions now work perfectly with strings
- Reduced memory usage (duplicate strings share memory)
- Faster string comparisons (pointer equality instead of memcmp)

Examples now working:
```blaze
// String equality works!
print("hello" == "hello")  // true

// Match with strings works!
let result = match "cat" {
    "dog" => "Canine"
    "cat" => "Feline"
    _ => "Unknown"
}
```

### March 28, 2026 - Match Expressions Implemented
- **NEW FEATURE**: Match expressions - match can now be used as an expression that returns a value
- Updated parser to support match in expression context
- Added EXPR_MATCH to AST with ExprCaseClause for expression-based case arms
- Implemented type checking for match expressions (all arms must return compatible types)
- Compiler generates bytecode that leaves result value on stack
- Updated comprehensive test suite with match expression examples
- Fixed segmentation fault bug in newExpressionStmt when expression is NULL

Examples:
```blaze
// Match as expression
let day = match n {
    1 => "Monday"
    2 => "Tuesday"
    _ => "Weekend"
}

// Match in return statement
fn getLabel(x: int) -> string {
    return match x {
        1 => "one"
        2 => "two"
        _ => "other"
    }
}
```

### March 28, 2026 - Implementation Plan Update
- Updated plan to reflect production-ready status
- Added comprehensive documentation sections:
  - Getting Started guide
  - Example code showcase
  - VM Architecture details (64 opcodes documented)
  - Verification section with test results
  - Known limitations section
- Documented match/case statement limitation (statement-only, not expression)
- Added codebase statistics (~10,680 lines C, ~481 lines Blaze stdlib)
- Reorganized sections for better clarity
- Marked all core features as complete (99% overall completion)

---

## Recent Changes (Latest First)

### Color-Coded Terminal Output - COMPLETED
- Created `colors.h` and `colors.c` with ANSI color code support
- Added `colorsEnabled()` function to detect TTY and respect user preferences
- Color codes for:
  - **Errors**: Bright red for "Error", yellow for error location, bold for messages
  - **Code context**: Gray for source code lines, bright red caret
  - **Hints**: Bright cyan for hint emoji and text
  - **Type errors**: Bright red for "Type error" label
  - **REPL**: Cyan/bold for welcome, green prompt for input, blue for continuation
- Automatic fallback to plain text when not in a TTY
- Example colored error:
  ```
  [line 3] Error at '->': Expected ')' after parameters.

      fn test(x: int, y: string -> int {
                                ^

      💡 Hint: Did you forget to close the parameter list with ')'?
  ```
  (With colors: line number bold, "Error" red, location yellow, caret red, hint cyan)

### Better Error Messages - COMPLETED
- Enhanced `errorAt()` function in parser to show code context
- Added line display with caret (^) pointing to exact error location
- Implemented `errorAtWithSuggestion()` for errors with helpful hints
- Added `consumeWithSuggestion()` for contextual error suggestions
- Stored source code in Parser struct for error context retrieval
- Example error output now shows:
  ```
  [line 3] Error at '->': Expected ')' after parameters.

      fn test(x: int, y: string -> int {
                                ^

      💡 Hint: Did you forget to close the parameter list with ')'?
  ```

### Union Types - COMPLETED
- Added `TOKEN_PIPE` to scanner for single `|` operator
- Added `TYPE_UNION` to `TypeKind` enum in types.h
- Implemented `UnionType` struct with automatic flattening and deduplication
- Added `createUnionType()`, `isUnionType()`, and `typeIsInUnion()` helper functions
- Updated `typesEqual()` to handle union type equality (order-independent)
- Updated `typeIsAssignableTo()` with union type rules:
  - `T` can be assigned to `T | U` (any type to union containing it)
  - `T | U` can be assigned to `V` if both `T` and `U` can be assigned to `V`
- Added `TYPE_NODE_UNION` to AST with `newUnionTypeNode()` constructor
- Updated parser's `typeAnnotation()` to parse `T | U | V` syntax
- Updated type checker's `resolveTypeNode()` to convert union type nodes to types
- Tested successfully with variables and function parameters
- Example: `let x: int | string = 10; x = "hello"`

### Finally Blocks
- Added `finally` keyword to scanner
- Updated TryStmt AST to include optional finallyBody
- Updated parser to parse `try { } catch (e) { } finally { }`
- Compiler emits finally code after both normal and catch paths
- Fixed pre-existing bug: string concatenation (`+`) now uses `OP_CONCAT`

### Selective Imports
- Implemented selective import syntax: `import { map, filter } from "std/array"`
- Parser already supported the syntax, added VM logic in `processImports()`
- Tracks existing globals before module execution, removes unwanted symbols after

### Higher-Order Functions Implementation
- Added lambda type checking in `typechecker.c` (`checkLambda()` function)
- Added function type compatibility in `types.c` (`typeIsAssignableTo()`)
- Fixed compiler bug where top-level functions weren't stored correctly
- Added 8 higher-order functions to `std/array.blaze`:
  - `map(arr, fn)` - transform elements
  - `filter(arr, predicate)` - filter by predicate
  - `reduce(arr, initial, accumulator)` - fold to single value
  - `forEach(arr, action)` - execute for each
  - `any(arr, predicate)` - any element matches
  - `all(arr, predicate)` - all elements match
  - `find(arr, predicate)` - first matching element
  - `findIndex(arr, predicate)` - index of first match
