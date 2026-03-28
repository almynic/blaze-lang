pa# Blaze Language Roadmap

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
- ✅ Garbage collection (mark-and-sweep)
- ✅ Lambda expressions with closures and type inference
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

### Optimizations (Partial - 60% Complete)
- ✅ String interning for efficient string comparison and memory usage
- ✅ String interpolation with `${expression}` syntax
- ✅ Constant folding optimization for compile-time evaluation
- ✅ Dead code elimination removes unreachable code after returns/throws
- ⬜ Tail call optimization
- ⬜ NaN boxing for value representation
- ⬜ Loop unrolling

---

## 🔮 Future Enhancements (Optional)

The core compiler is complete and has excellent developer experience. These are optional enhancements that could be added in the future:

### Phase 7: Advanced Type System (Not Started)
**Priority**: Low | **Complexity**: High

#### Generic Types
- Add support for generic types (e.g., `Array<T>`, `Map<K,V>`)
- Would require significant type system extensions
- Template instantiation at compile time
- Type parameter constraints

**Benefits**:
- More type-safe collections
- Better code reuse
- Improved standard library APIs

**Challenges**:
- Complex type inference
- Code generation complexity
- Backward compatibility concerns

**Estimated Effort**: 4-6 weeks

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

### Phase 9: Performance Optimizations (Partial)
**Priority**: Low | **Complexity**: Medium-High

#### Completed Optimizations
- ✅ Constant folding
- ✅ Dead code elimination
- ✅ String interning

#### Remaining Optimizations

**Tail Call Optimization**
- Optimize tail-recursive functions to avoid stack overflow
- Detect tail call patterns in compiler
- Reuse current stack frame instead of creating new one

**NaN Boxing**
- Use IEEE 754 NaN values to pack type tags into double
- Reduce Value struct from 16 bytes to 8 bytes
- Potential 2x memory improvement for value stack

**Loop Unrolling**
- Unroll small fixed-iteration loops
- Reduce loop overhead for hot paths
- Requires loop analysis in compiler

**Estimated Effort**: 2-3 weeks per optimization

---

### Phase 10: Destructuring (Not Started)
**Priority**: Low | **Complexity**: Medium

#### Array Destructuring
```blaze
let [a, b, c] = [1, 2, 3]
let [first, ...rest] = [1, 2, 3, 4, 5]
```

#### Object/Class Destructuring
```blaze
class Point { let x: int, let y: int }
let {x, y} = point
```

**Benefits**:
- More ergonomic data access
- Cleaner code for working with collections
- Matches modern language features

**Challenges**:
- Parser complexity
- Pattern matching integration
- Type inference complexity

**Estimated Effort**: 2-3 weeks

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

- **Finally blocks and early returns**: Finally blocks don't execute on early return/break (would require more complex bytecode)

---

## Contributing

If you're interested in contributing to any of these future enhancements, please:

1. Open an issue to discuss the feature
2. Review the existing codebase architecture
3. Submit a pull request with tests

---

## Version History

- **v0.1.0** (Current): Feature-complete core language with all essential features
- **v0.2.0** (Future): Generic types and advanced type system features
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

**Last Updated**: March 28, 2026
