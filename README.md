# Blaze Programming Language

> A modern, statically-typed programming language with excellent developer experience

[![Status](https://img.shields.io/badge/status-experimental-yellow)]()
[![Completion](https://img.shields.io/badge/completion-99%25%2B-blue)]()
[![Language](https://img.shields.io/badge/language-C23-orange)]()
[![License](https://img.shields.io/badge/license-MIT-blue)]()

---

## Overview

**Blaze** is a feature-complete statically-typed programming language featuring:

- ✅ Modern syntax with type inference
- ✅ Classes, closures, and inheritance
- ✅ Pattern matching and exception handling
- ✅ Module system with standard library
- ✅ Generic functions (`fn f<T>(...)`) and explicit calls (`f<int>(...)`)
- ✅ Generic classes (`class Box<T> { ... }`) with compile-time monomorphization and `type` aliases
- ✅ Lambdas with typed parameters and block bodies (`(x: int) => { ... }`)
- ✅ Array spread operator for concatenation
- ✅ Interactive REPL with readline support
- ✅ Excellent developer experience (colored errors, helpful hints)

**Codebase**: ~15,600+ lines of C | ~1,350+ lines of Blaze stdlib (`std/` modules below)

---

## Quick Start

### Building

```bash
# Using CMake
cmake -B build
cmake --build build

# Run the REPL
./build/blaze

# Execute a file
./build/blaze your_file.blaze

# Debug a file (interactive debugger)
./build/blaze --debug your_file.blaze

# Debug with startup breakpoints
./build/blaze --debug --break 12 --break-if 24:hit>=3 your_file.blaze

# Rich conditional breakpoint examples
./build/blaze --debug --break-if 24:"line>=24 && depth>=1" your_file.blaze
./build/blaze --debug --break-if 36:"hit>=2 && local[0]==42" your_file.blaze

# Debug via JSON-line protocol (tooling integration)
./build/blaze --debug-protocol your_file.blaze
```

### Hello World

```blaze
print("Hello, World!")
```

### More Examples

```blaze
// Variables with type inference
let name = "Alice"
let age = 30
let pi = 3.14159

// Functions
fn greet(name: string) -> string {
    return "Hello, ${name}!"
}

// Arrays with spread operator
let arr1 = [1, 2, 3]
let arr2 = [4, 5, 6]
let combined = [...arr1, ...arr2]  // [1, 2, 3, 4, 5, 6]

// Classes
class Person {
    let name: string
    let age: int

    fn greet() -> string {
        return "I'm ${this.name}, ${this.age} years old"
    }
}

// Pattern matching
let result = match value {
    1 => "one"
    2 => "two"
    _ => "other"
}

// Optional types and null coalescing
let maybe: int? = getValue()
let safe = maybe ?? 0  // Use 0 if nil

// Exception handling
try {
    riskyOperation()
} catch (e) {
    print("Error: ${e}")
} finally {
    cleanup()
}

// Higher-order functions
import { map, filter } from "std/array"

let numbers = [1, 2, 3, 4, 5]
let doubled = map(numbers, (x) => x * 2)
let evens = filter(numbers, (x) => x % 2 == 0)

// Generic function
fn identity<T>(x: T) -> T {
    return x
}
let n = identity<int>(42)
```

---

## Features

### Core Language

- **Static typing** with powerful type inference
- **Union types** (`int | string`) and **optional types** (`int?`)
- **Type narrowing** in conditionals for safe type refinement
- **Classes** with inheritance and super calls; **generic classes** with explicit instantiations (e.g. `Box<int>`)
- **Closures** and **lambda expressions** (typed params, optional return type, expression or block body)
- **Match expressions** for pattern matching
- **Exception handling** (try/catch/finally)
- **Module system** with selective imports

### Syntax Features

- **String interpolation**: `"Hello, ${name}!"`
- **Spread operator**: `[...arr1, ...arr2]`
- **Null coalescing**: `value ?? default`
- **Optional chaining**: `obj?.property`
- **Range syntax**: `1..10`
- **For-in loops**: `for item in array { ... }`
- **Bitwise operators**: `& | ^ ~ << >>` (int operands)

Bitwise precedence (high to low): unary `~`, shifts `<< >>`, bitwise AND `&`, bitwise XOR `^`, bitwise OR `|` (formal grammar: [LANGUAGE_SPECIFICATIONS.md](LANGUAGE_SPECIFICATIONS.md#11-expressions)).

```blaze
print(1 + 2 << 3)  // (1 + 2) << 3  => 24
print(1 | 2 & 3)   // 1 | (2 & 3)   => 3
print(~5)          // bitwise not    => -6
```

### Developer Experience

- **Interactive REPL** with GNU Readline support
- **Color-coded output** for better readability
- **Context-aware error messages** with helpful hints
- **Standard library** in `std/` (math, array, string, collections, I/O, time, and more)

### Performance

- **Optimizing compiler** with constant folding, dead code elimination, and tail call optimization
- **String interning** for fast comparisons
- **Type-specialized bytecode** for integers and floats
- **NaN-boxed values** (8-byte `uint64_t` `Value`; details in [ARCHITECTURE.md](ARCHITECTURE.md#value-representation))
- **Mark-and-sweep garbage collection**

---

## Documentation

- **[ARCHITECTURE.md](ARCHITECTURE.md)** - Deep dive into compiler and VM architecture
- **[LANGUAGE_SPECIFICATIONS.md](LANGUAGE_SPECIFICATIONS.md)** - Formal grammar, precedence, and syntax reference
- **[CHANGELOG.md](CHANGELOG.md)** - Detailed version history
- **[ROADMAP.md](ROADMAP.md)** - Future enhancements and project direction

---

## Language Features

### Type System

```blaze
// Primitive types
let x: int = 42
let y: float = 3.14
let s: string = "hello"
let b: bool = true

// Arrays
let numbers: [int] = [1, 2, 3]
let matrix: [[int]] = [[1, 2], [3, 4]]

// Union types
let value: int | string = 100
value = "hello"  // OK

// Optional types
let maybe: int? = nil  // Same as int | nil

// Function types
let add: (int, int) -> int = (a, b) => a + b
```

### Control Flow

```blaze
// If-else
if condition {
    doSomething()
} else {
    doOther()
}

// While loop
while x < 100 {
    x = x + 1
}

// For-in loop
for item in [1, 2, 3, 4, 5] {
    print(item)
}

// Match statement
match value {
    1 => print("one")
    2 => print("two")
    _ => print("other")
}

// Match expression
let label = match n {
    1 => "Monday"
    2 => "Tuesday"
    _ => "Weekend"
}
```

### Classes and OOP

```blaze
class Animal {
    let name: string

    fn speak() -> string {
        return "${this.name} makes a sound"
    }
}

class Dog extends Animal {
    fn speak() -> string {
        return "${this.name} barks"
    }

    fn callSuper() -> string {
        return super.speak()
    }
}

let dog = Dog()
dog.name = "Buddy"
print(dog.speak())  // "Buddy barks"
```

### Arrays and Collections

```blaze
// Array literals
let numbers = [1, 2, 3, 4, 5]

// Spread operator
let arr1 = [1, 2, 3]
let arr2 = [4, 5, 6]
let combined = [...arr1, ...arr2]  // [1, 2, 3, 4, 5, 6]
let copy = [...arr1]                // [1, 2, 3]

// Array indexing
let first = numbers[0]
numbers[1] = 10

// Higher-order functions
import { map, filter, reduce } from "std/array"

let doubled = map(numbers, (x) => x * 2)
let evens = filter(numbers, (x) => x % 2 == 0)
let sum = reduce(numbers, 0, (acc, x) => acc + x)
```

---

## Standard Library

### Modules

- **std/prelude.blaze** — Auto-loaded common functions
- **std/math.blaze** — Math utilities
- **std/string.blaze** — String helpers
- **std/array.blaze** — Higher-order array functions (`map`, `filter`, `reduce`, …)
- **std/collections.blaze** — Collection-style helpers
- **std/date.blaze** / **std/time.blaze** — Date and time utilities
- **std/io.blaze** / **std/path.blaze** — Basic I/O and path helpers
- **std/random.blaze** — Random number helpers
- **std/ascii.blaze** — ASCII character utilities
- **std/debug.blaze** — Debugging helpers

### Example Usage

```blaze
import { sin, cos, sqrt } from "std/math"
import { map, filter } from "std/array"
import { contains, capitalize } from "std/string"

// Math
let x = sqrt(16.0)      // 4.0
let angle = sin(1.57)   // ~1.0

// Array operations
let nums = [1, 2, 3, 4, 5]
let doubled = map(nums, (x) => x * 2)
let evens = filter(nums, (x) => x % 2 == 0)

// String operations
let text = "hello world"
let hasWorld = contains(text, "world")  // true
let capitalized = capitalize(text)      // "Hello world"
```

---

## REPL

The interactive REPL provides:

- ✅ Multi-line input support
- ✅ GNU Readline integration
- ✅ Persistent command history
- ✅ Tab completion
- ✅ Color-coded output

```bash
$ ./build/blaze
Welcome to Blaze v0.1.0
Type 'exit' or press Ctrl+D to quit.

blaze> let x = 42
blaze> print(x * 2)
84
blaze> fn double(n: int) -> int {
  ...>     return n * 2
  ...> }
blaze> print(double(21))
42
```

---

## Testing

Run the test suite:

```bash
# Built-in regression suite (many phases: arithmetic, loops, classes, generics, REPL notes, …)
./build/blaze --test

# Single-file examples
./build/blaze examples/comprehensive_test.blaze

# Individual feature tests
./build/blaze tests/spread/spread_operator.blaze
./build/blaze tests/optional/optional_types.blaze

# Debugger protocol smoke test
python3 tests/debugger/debug_protocol_smoke.py
```

The `--test` runner repeatedly compiles and executes snippets in a fresh VM; use it after VM or compiler changes to catch integration issues early.

---

## Architecture

### Compilation Pipeline

```
Source Code → Scanner → Parser → AST → Type Checker → Compiler → Bytecode → VM
```

### Virtual Machine

- **Stack-based bytecode interpreter** with 67 opcodes
- **Mark-and-sweep garbage collection** (roots include the value stack, frames, globals, open upvalues, and the active compiler chain during compilation)
- **Type-specialized arithmetic** opcodes (int/float/mixed)
- **String interning** for efficient comparison
- **Optimizing compiler** with constant folding and dead code elimination

For more details, see [ARCHITECTURE.md](ARCHITECTURE.md).

The C implementation splits the VM across several translation units (`vm_core.c`, `vm_debugger.c`, `vm_natives.c`, `vm_call.c`, `vm_execute.c`, plus `vm.c`) and keeps the parser, type checker, and bytecode compiler in `.inc` fragments included from their main `.c` files. Paths and filenames are listed in [ARCHITECTURE.md](ARCHITECTURE.md) and [src/README.md](src/README.md).

---

## Project Status

### ✅ Completed (99%+)

All core language features are implemented and tested:

- Scanner, parser, AST, type checker, compiler, VM
- Classes, inheritance, closures, lambda expressions
- Match expressions, exception handling
- Module system with standard library
- Union types, optional types, type narrowing
- REPL with readline support
- Optimizations: constant folding, dead code elimination, string interning
- Spread operator for arrays

### 🔮 Future Enhancements (Optional)

Optional features that could be added:

- Richer stdlib generics (e.g. nominal `Array<T>`-style APIs where the surface area grows)
- Debugger protocol / IDE integration polish
- Interfaces / protocols, expanded enum ADTs

User-defined generic **classes** (`class Box<T> { ... }`) with monomorphization, **generic functions**, destructuring, tail calls, NaN boxing, and loop unrolling are already implemented. For more detail, see [ROADMAP.md](ROADMAP.md).

---

## Contributing

Contributions are welcome! Please:

1. Fork the repository
2. Create a feature branch
3. Make your changes with tests
4. Submit a pull request

---

## License

MIT License — see [LICENSE.md](LICENSE.md) for details

---

## Acknowledgments

Built with inspiration from:
- Crafting Interpreters by Bob Nystrom
- Modern programming language design

---

**Last Updated**: April 1, 2026
