# Blaze Language Roadmap

> **Current Status**: Experimental (core language complete)  
> **Focus**: Production hardening and ecosystem maturity

Blaze already ships a complete statically typed language core. The roadmap below is organized by delivery priority so contributors can focus on the highest-impact work first.

---

## 1) Snapshot

### Current Strengths
- Type system depth: generics, unions, inference, narrowing
- Broad regression coverage for core language behavior
- Runtime/compiler foundations: GC, string interning, constant folding, TCO
- Good diagnostics and error recovery
- Strong documentation set (`README.md`, `ARCHITECTURE.md`, spec, changelog)

### Current Gaps
- **I/O and paths**: `std/io.blaze` exposes native file and stdin/stdout helpers, but production-grade behavior (typed errors, streaming, resource limits) and broader filesystem APIs are still evolving; `std/path.blaze` is string-only (join/basename/dirname style), not a full FS layer.
- Several runtime failure paths still favor process abort behavior over recoverable runtime errors
- Ecosystem features (LSP, package manager, FFI, richer stdlib modules such as JSON/networking) are not yet in place

---

## 2) Production Readiness Priorities

This is the primary execution plan for moving from experimental to production-ready.

### Critical (Blockers)
1. **Production-grade file I/O and path/FS coverage**
   - Harden and extend native-backed I/O (clear error surfaces, streaming or chunked reads where needed, consistent limits).
   - Expand path and filesystem helpers beyond string joining (`std/path.blaze`) where production use requires it.
2. **Graceful OOM handling**
   - Replace hard exits on allocation failure with runtime exceptions where feasible.
   - Keep diagnostics clear and actionable.
3. **Stack overflow protection**
   - Ensure `FRAMES_MAX` and `STACK_MAX` boundaries produce catchable runtime errors.
   - Avoid crash-style failure behavior under deep recursion/large stacks.

### Important (Production Quality)
4. **Structured error types**
   - Move from string-only exceptions to typed runtime error values.
5. **Array/string bounds safety**
   - Guarantee out-of-bounds access raises runtime exceptions, never undefined behavior.
6. **Complete `super` type checking**
   - Finish the remaining type checker TODO coverage for superclass typing (`src/semantics/typechecker.c` and `typechecker_*.inc` fragments).
7. **Expand practical stdlib**
   - Prioritize collections, JSON parsing, and network I/O.
8. **Stress, fuzz, and benchmark coverage**
   - Add memory pressure, deep recursion, large-data, and baseline performance suites.

### Nice-to-Have
9. **Configurable resource limits** (`FRAMES_MAX`, `STACK_MAX`, etc.)
10. **LSP/IDE integration**
11. **FFI support**
12. **Package manager**
13. **Cross-platform CI/testing** (Linux, macOS, Windows)
14. **Concurrency model** (threads and/or async)

---

## 3) Capability Status

### Core Language (Implemented)
- Scanner, parser, AST, type checker, bytecode compiler, stack VM
- Classes/closures/inheritance (`super` calls supported)
- Exceptions (`try/catch/throw/finally`)
- Lambdas, higher-order functions, range syntax, for-in loops, spread operator
- Module system with selective imports

### Type System (Implemented with follow-up room)
- Union/optional types and narrowing
- Function type compatibility
- Generic functions with inference and explicit type arguments
- Generic classes with monomorphization, typed `extends`, bounds, and variance
- Enum payload typing in `match` patterns

### Runtime and Performance (Implemented)
- Mark-and-sweep GC (with compile/run object rooting)
- String interning and interpolation
- Constant folding and dead code elimination
- Tail call optimization
- NaN-boxed value representation
- Loop unrolling for small constant ranges/arrays

### Developer Experience (Mostly implemented)
- Interactive REPL with Readline history/editing
- Colorized output and improved diagnostics
- Debugger CLI: breakpoints, step/next/out/continue, locals, stack traces
- Conditional breakpoints (`hit`, `line`, `depth`, `local[N]`, `&&`)
- Remaining debugger follow-up: IDE-facing protocol polish and UX docs

### Standard Library (Partially complete for production needs)
- Present and usable: prelude, math/array/string, collections-style helpers, I/O wrappers, time/date, and other `std/` modules
- Missing for production readiness: hardened I/O semantics, JSON/networking baselines, and broader ecosystem-facing modules (see priorities above)

### Notable Completed Language Milestones
- Destructuring (array and object/class forms, including rest slice behavior)
- Bitwise operators (`&`, `|`, `^`, `~`, `<<`, `>>`) with integer-only typing and regression tests

---

## 4) Future Enhancement Tracks (Optional)

These are valuable, but intentionally secondary to production-readiness blockers.

### Tooling and Ecosystem
- Full LSP support (autocomplete, go-to-definition, find references, rename)
- Package registry and dependency management workflow
- Cross-platform release validation and CI hardening

### Runtime/Interop
- FFI for native library calls (`.so`, `.dylib`, `.dll`)
- Configurable runtime resource limits
- Concurrency model design (threaded and/or async approach)

### Compilation Pipeline
- Optional native backend exploration (LLVM/AOT)
- Incremental compilation and broader cross-compilation support
- Method inlining and additional call-graph driven optimizations

---

## 5) Known Limitations

- **Finally and early exits**: `finally` runs on early `return`; loop `break` is not currently a language feature.
- **Generic follow-ups**: core class bounds/variance/recursive nominal guards are implemented; additional constraint forms (for example on generic function parameters) remain open.

---

## 6) Contribution Guide

If you want to help with roadmap items:
1. Open an issue with scope and expected behavior.
2. Align the design with current architecture and language semantics.
3. Submit a PR with focused tests (plus regressions for bug fixes).

---

## 7) Version Direction

- **v0.1.0** (current): feature-complete core language
- **v0.2.0** (in progress): generics/debugger foundations and stabilization
- **v0.3.0** (future): tooling protocol polish and ecosystem growth
- **v0.4.0** (future): production blockers pass 1 (file I/O + runtime hardening)
- **v0.5.0** (future): structured errors and bounds-safety guarantees
- **v0.6.0** (future): stdlib expansion (collections, JSON, networking baseline)
- **v0.7.0** (future): stress/fuzz/performance suites and CI hardening
- **v0.8.0** (future): IDE/LSP integration milestone
- **v0.9.0** (future): ecosystem polish (package workflow + cross-platform validation)
- **v0.10.0** (future): release-candidate quality gate before 1.0
- **v1.0.0** (future): production-readiness goals completed and stabilized

---

## 8) Ongoing Maintenance

Continuous work regardless of milestone:
- Bug fixes and regression hardening
- Performance profiling and targeted improvements
- Documentation quality and examples
- Standard library expansion
- Test coverage expansion

---

**Last Updated**: April 1, 2026
