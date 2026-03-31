# Blaze Language Roadmap

> **Current Status**: Experimental | **Completion**: 99%

Blaze is a feature-complete statically-typed programming language. This roadmap outlines the current state and potential future enhancements.

---

## ✅ Completed Features

### Core Language (100% Complete)
- ✅ Full lexical analysis (scanner)
- ✅ Expression and statement parsing
- ✅ AST construction
- ✅ Type checking with scope management
- ✅ Bytecode compilation
- ✅ Stack-based VM with 65 opcodes
- ✅ Classes, closures, and inheritance (including super calls)
- ✅ Garbage collection (mark-and-sweep); compiled script/function objects are rooted on the VM stack before AST teardown so GC cannot reclaim bytecode between compile and run
- ✅ Lambda expressions with closures, type inference, typed parameters, and block bodies (`=> { ... }`)
- ✅ Generic functions with type parameters (`fn identity<T>(x: T) -> T`) and explicit calls (`identity<int>(42)`)
- ✅ Match/case statements and match expressions with pattern matching
- ✅ For-in loops with proper array iteration
- ✅ Index assignment for arrays
- ✅ Range syntax (start..end)
- ✅ Exception handling (try/catch/throw/finally)
- ✅ Native primitives for low-level operations
- ✅ Higher-order functions (map, filter, reduce, etc.)
- ✅ Function type parameters (passing functions as arguments)
- ✅ Module system with selective imports
- ✅ String concatenation with `+` operator
- ✅ Spread operator (`...`) for array expansion and concatenation

### Advanced Type System (100% Complete)
- ✅ Union types (`int | string`)
- ✅ Optional/nullable types (`int?`) with null coalescing (`??`)
- ✅ Type narrowing for union and optional types in conditionals
- ✅ Function types with full compatibility checking
- ✅ Type parameters in function signatures (placeholder `TYPE_TYPE_PARAM` types; generic type usage in annotations resolves conservatively)

### Developer Experience (100% Complete)
- ✅ Interactive REPL with multi-line support and line editing
- ✅ GNU Readline integration with persistent command history
- ✅ Color-coded terminal output for errors, warnings, and REPL
- ✅ Context-aware error messages with code snippets and helpful hints
- ✅ Enhanced error messages with suggestions

### Standard Library (100% Complete)
- ✅ Comprehensive standard library (math, array, string modules)
- ✅ Prelude auto-loading (std/prelude.blaze)
- ✅ Higher-order functions in stdlib
- ✅ Module system with selective imports

### Optimizations (100% Complete)
- ✅ String interning for efficient string comparison and memory usage
- ✅ String interpolation with `${expression}` syntax
- ✅ Constant folding optimization for compile-time evaluation
- ✅ Dead code elimination removes unreachable code after returns/throws
- ✅ Tail call optimization for efficient recursive functions
- ✅ NaN boxing for 50% memory reduction (8-byte values)
- ✅ Loop unrolling for small constant ranges and arrays

---

## 🔮 Future Enhancements (Optional)

The core compiler is complete and has excellent developer experience. These are optional enhancements that could be added in the future:

### Phase 7: Advanced Type System (Core complete)
**Priority**: Medium | **Complexity**: High

#### Interfaces and `implements` (✅ Implemented)
- `interface Drawable { fn area() -> float }` — signatures only; zero runtime footprint.
- `class Circle implements Drawable { ... }` — compile-time conformance (method names and function types must match).
- Variables may use interface types; assignment uses `ClassType.implementedInterfaces`.

#### Generic functions (✅ Implemented)
- `fn name<T, U>(...) -> R` with type parameters in scope for the body
- Explicit call syntax: `name<int>(args)` (type arguments parsed; inference from arguments also supported)
- `std/array.blaze` uses generic-style functions over `[T]` and higher-order helpers

#### Generic classes and type aliases (✅ Implemented)
- User-defined generic classes: `class Box<T> { ... }` with per-instantiation monomorphization (mangled globals such as `Box__int`)
- Type annotations and constructors: `Box<int>`, `Box<int>(...)`, nested `Box<Box<int>>` where supported
- `type Alias = Box<int>` aliases resolve like the underlying type
- **Typed `extends`**: `extends Parent` or `extends Foo<T>` / `extends Foo<int>` with monomorph lowering carrying resolved superclass for `OP_INHERIT`.
- **Type-parameter bounds**: `class C<T: SomeInterface>` with checking at `C<Args>` instantiation (classes must `implements` the bound interface).
- **Variance (✅ Implemented)**: `out` / `in` on class type parameters (e.g. `class Box<out T> {}`, `class Sink<in T> {}`). Subtyping for two `TYPE_GENERIC_INST` sharing a template respects covariance (`out`) and contravariance (`in`); omitted modifiers default to invariant.
- **Recursive nominal guards (✅ Implemented)**: `substituteTypeInType` and `typesEqual` are depth-bounded so deeply nested or pathological generic type trees cannot exhaust the C stack.
- **Generic-only superclass shapes (✅ Implemented)**: For generic classes, `extends Base<T>` is checked so superclass type arguments use only the enclosing class’s type parameters (or concrete types), not unbound or out-of-range type parameters.

#### Enum Types and Pattern Matching (✅ Core implemented)
- Algebraic Data Types (ADTs) with associated data
- `enum Result { Ok(int), Err(string) }`
- **`match` typing for variants (✅ Implemented)**: Patterns such as `Ok(x)` or `Variant(a, b)` bind payload variables to the variant’s field types in the type checker (not `unknown`). Further ADT ergonomics or runtime polish remain optional.

**Benefits**:
- Type-safe collections (Generics)
- Modeling complex data structures (Enums)
- More flexible polymorphism (Interfaces)

**Estimated Effort**: Optional follow-ups (constraints, `Array<T>` nominal, etc.) as separate milestones

---

### Phase 8: Debug Mode (Not Started)
**Priority**: Medium | **Complexity**: Medium

#### Step-Through Execution
- Add breakpoint support in VM
- Step-through execution (step in/over/out)
- Variable inspection at breakpoints
- Call stack visualization

#### Implementation Ideas
- Add debug info to bytecode (line mappings)
- Debugger protocol for IDE integration
- Interactive debug REPL
- Conditional breakpoints

**Benefits**:
- Better debugging experience
- Easier development and troubleshooting
- IDE integration potential

**Estimated Effort**: 3-4 weeks

---

### Phase 9: Performance Optimizations (100% Complete)
**Priority**: Low | **Complexity**: Medium-High

#### Completed Optimizations
- ✅ Constant folding
- ✅ Dead code elimination
- ✅ String interning
- ✅ Tail call optimization
- ✅ NaN boxing
- ✅ Loop unrolling

#### Potential Future Optimizations

**Method Inlining**
- Automatically inline small, frequently called methods/functions
- Removes call overhead and improves pipeline utilization
- Requires call graph analysis and size heuristics

**Foreign Function Interface (FFI)**
- Load and call native C libraries dynamically (`.so`, `.dylib`, `.dll`)
- Enables high-performance native extensions
- Requires dynamic loading (libdl) and argument marshalling

**Estimated Effort**: 2-3 weeks per optimization

---

### Phase 10: Destructuring ✅ (Complete - 100%)
**Priority**: Low | **Complexity**: Medium

#### Array Destructuring ✅ (Complete)
```blaze
let [a, b, c] = [1, 2, 3]  // ✅ Implemented
let [first, ...rest] = [1, 2, 3, 4, 5]  // ✅ Implemented
```

**Features**:
- Basic array destructuring for variable declarations
- Works with both `let` and `const`
- Type checking validates array types
- Support for nested arrays
- Support for expressions as initializers
- Rest parameters (`...rest`) - slices remaining elements into array
- New `OP_ARRAY_SLICE` opcode for efficient array slicing

#### Object/Class Destructuring ✅ (Complete)
```blaze
class Point {
    fn init(x: int, y: int) {
        this.x = x
        this.y = y
    }
}
let p = Point()
p.init(10, 20)
let {x, y} = p  // ✅ Implemented
```

**Features**:
- Object/class property destructuring
- Works with both `let` and `const`
- Type checking validates object types
- Partial destructuring (only extract needed properties)
- Runtime validation of property existence

**Benefits**:
- ✅ More ergonomic data access (arrays and objects)
- ✅ Cleaner code for working with collections
- ✅ Matches modern language features
- ✅ Rest parameters enable flexible patterns
- ✅ Reduces boilerplate code

**Implementation Complete**: All destructuring features implemented!

---

### Phase 11: Low-Level Primitives (Not Started)
**Priority**: Medium | **Complexity**: Low

#### Bitwise Operators
- Implement `&` (AND), `|` (OR), `^` (XOR), `~` (NOT), `<<` (SHL), `>>` (SHR)
- Essential for systems programming and bit-packing
- Low complexity implementation in compiler and VM

#### Pointer Operations
- `addrOf(var)` and `deref(ptr)` (if safe memory access can be guaranteed)
- Or a more abstracted `MemoryBuffer` for raw byte manipulation

---

### Additional Enhancement Ideas

#### Enhanced Standard Library
- File I/O module (read/write files)
- JSON parsing/serialization
- HTTP client library
- Regular expressions
- Date/Time handling

#### Language Server Protocol (LSP)
- IDE integration via LSP
- Autocomplete
- Go to definition
- Find references
- Rename refactoring

#### Package Manager
- Package registry
- Dependency management
- Version resolution
- Lock files

#### Compilation Improvements
- Compile to native code (LLVM backend)
- AOT compilation
- Incremental compilation
- Cross-compilation support

---

## Known Limitations

These are current design choices or limitations:

- **Finally blocks and early exits**: `finally` now runs on early `return`; loop `break` is not a language feature yet
- **Generic limitations**: Interface bounds on generic *class* type parameters (`class C<T: I>`) and variance / recursive nominal guards are implemented (see Phase 7). Optional follow-ups include richer constraint forms (e.g. bounds on generic *function* type parameters) and other refinements listed under Phase 7.

---

## Contributing

If you're interested in contributing to any of these future enhancements, please:

1. Open an issue to discuss the feature
2. Review the existing codebase architecture
3. Submit a pull request with tests

---

## Version History

- **v0.1.0** (Current): Feature-complete core language with all essential features
- **v0.2.0** (In progress): Generic functions, typed/block lambdas, user-defined generic classes with monomorphization, type aliases
- **v0.3.0** (Future): Debug mode and developer tooling
- **v1.0.0** (Future): Performance optimizations and stability improvements

---

## Maintenance

Even though the language is feature-complete, ongoing maintenance includes:

- Bug fixes
- Performance improvements
- Documentation updates
- Standard library enhancements
- Test coverage improvements

---

**Last Updated**: March 30, 2026
