# Generic Types Design for Blaze Language

**Status (March 2026):** Generic **functions** and user-defined **generic classes** with monomorphization are implemented in the compiler and VM; `type Alias = …` aliases exist. A nominal stdlib `Array<T>` type (separate from fixed `[T]` arrays) remains a possible future extension — see [ROADMAP.md](../ROADMAP.md).

## Introduction

This document outlines the design for adding generic types to the Blaze programming language. Generics allow for type-safe, reusable code by parameterizing types with other types.

## Goals

1. Enable generic types (e.g., `Array<T>`, `Map<K,V>`)
2. Enable generic functions (e.g., `fn identity<T>(x: T) -> T { return x }`)
3. Support type parameter constraints (optional, for future extension)
4. Maintain backward compatibility with existing non-generic code
5. Implement via monomorphization (compile-time specialization) for zero runtime overhead

## Syntax

### Generic Types

```blaze
// Declaration
class Array<T> {
    let data: [T]
    fn get(index: int) -> T { ... }
    fn set(index: int, value: T) -> void { ... }
}

// Usage
let intArray: Array<int> = Array<int>()
let stringArray: Array<string> = Array<string>()
```

### Generic Functions

```blaze
// Declaration
fn identity<T>(value: T) -> T {
    return value
}

// Usage
let x: int = identity<int>(42)
let y: string = identity<string>("hello")
// Type inference allows omitting type arguments in many cases
let z = identity(123) // T inferred as int
```

### Generic Type Aliases

```blaze
type IntArray = Array<int>
type StringMap = Map<string, string>
```

## Semantics

### Type Parameter Substitution

When a generic type or function is used with concrete type arguments, the type parameters are substituted with those concrete types throughout the definition.

### Instantiation

Generic types and functions are instantiated at compile time for each unique set of type arguments used in the program. This is known as monomorphization.

### Constraints (Future Extension)

For now, we will not implement constraints. Type parameters can be any type. Future versions may add constraints like:
- `T: Comparable` (requires T to implement a comparison interface)
- `T: Clone` (requires T to be cloneable)

## Data Structure Changes

### TypeKind Extension

Add new TypeKind values:
- `TYPE_TYPE_PARAM` - Represents a type parameter (T, K, V, etc.)
- `TYPE_GENERIC_INST` - Represents an instantiated generic type (e.g., Array<int>)

### Type Struct Modifications

We need to extend the Type struct to handle generic types and type parameters.

```c
// In src/semantics/types.h
typedef enum {
    // ... existing kinds ...
    TYPE_TYPE_PARAM,    // Type parameter (T, K, V)
    TYPE_GENERIC_INST,  // Instantiated generic type
    // ... existing kinds ...
} TypeKind;

// New structs for generic types
struct TypeParam {
    int index;          // Position in type parameter list
    const char* name;   // Name for debugging
    int nameLength;
};

struct GenericInst {
    Type* genericType;  // The generic type definition (e.g., Array<T>)
    Type** typeArgs;    // Concrete type arguments (e.g., [int])
    int typeArgCount;
};

// Update the Type union
struct Type {
    TypeKind kind;
    union {
        // ... existing union members ...
        TypeParam typeParam;
        GenericInst genericInst;
    } as;
};
```

### Generic Type Definition Storage

We need to store the generic type definitions (with type parameters) separately from their instantiations.

Add to Type struct or create a new struct:
```c
struct GenericType {
    const char* name;
    int nameLength;
    Type** typeParams;   // Array of type parameter types
    int typeParamCount;
    // The actual definition (class, function, etc.)
    // For classes: ClassType* classDef;
    // For functions: FunctionType* funcDef;
};
```

## Implementation Plan

### 1. Parser Changes

- Modify parser to recognize angle brackets `< >` for type arguments
- Parse generic type declarations in class/function definitions
- Parse type arguments in type annotations and expressions
- Handle nested generics (e.g., `Array<Array<int>>`)

### 2. Type Checker Changes

- Extend type checking to handle type parameters
- Implement type argument substitution during type checking
- Validate that the correct number of type arguments are provided
- Check that type arguments satisfy any constraints (when implemented)
- Handle type inference for generic functions

### 3. Compiler Changes

- Modify AST nodes to store generic type information
- During code generation, instantiate generic types/functions for each unique type argument set
- Generate specialized code for each instantiation (monomorphization)
- Handle generic types in the VM (they become regular types after instantiation)

### 4. Standard Library Updates

- Redefine `Array` as `Array<T>` in std/array.blaze
- Update all standard library functions to use generics where appropriate
- Update prelude to export generic types and functions

### 5. Testing

- Add tests for generic type declaration and usage
- Add tests for generic functions
- Add tests for type inference with generics
- Add tests for nested generics
- Ensure existing tests still pass

## Impact on Existing Code

All existing non-generic code will continue to work without changes. Generic types are opt-in.

## Example Implementation Outline

### Parser (src/syntax/parser.c)

Add rules for:
- Type parameters in declarations: `class Name<T, U> { ... }`
- Type arguments in usage: `let x: Array<int>;`
- Type arguments in function calls: `fn<T>(args)`

### Type Checker (src/semantics/typechecker.c)

- When encountering a generic type, create a TypeParam for each type parameter
- When encountering a type argument, substitute type parameters with concrete types
- For generic functions, create a temporary type parameter scope

### Compiler (src/codegen/compiler.c)

- When compiling a generic type/class, store the generic definition
- When encountering a usage with type arguments, create an instantiation
- Generate code for each instantiation only once

## Open Questions

1. Should we support variance (covariance/contravariance) for generic types?
2. How should we handle recursive generic types (e.g., `class Node<T> { let next: Node<T> }`)?
3. Should we allow type aliases to be generic? (e.g., `type Pair<T,U> = { first: T, second: U }`)
4. How do we handle generic types in interfaces (when we add them)?

## References

- C# Generics
- Java Generics
- TypeScript Generics
- Rust Generics
- "Software Generics" by Jack W. Davidson
