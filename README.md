# Cem

*Pronounced "seam"*

A minimal, safe, concatenative programming language with linear types, pattern matching, and CSP-style concurrency.

## Design Philosophy

Cem explores the intersection of:
- **Concatenative programming** (Forth, Factor, Joy)
- **Linear type systems** (Rust ownership, linear logic)
- **Pattern matching** (ML, Rust, Erlang)
- **CSP concurrency** (Go, Erlang)

The goal is a production-grade language that proves concatenative + linear types can be both safe and practical.

## Core Values

- **Compile-time safety**: Type system catches errors before runtime
- **Zero-cost abstractions**: LLVM native compilation, no GC
- **No C stack issues**: Explicit data stack with compile-time bounds
- **Safe concurrency**: Linear channels prevent data races
- **Pattern matching**: Destructuring as stack operations

## Quick Example

```cem
type Option<T> =
  | Some(T)
  | None

: safe-div ( Int Int -- Option<Int> )
  dup 0 =
  [ drop drop None ]
  [ / Some ]
  if ;

: unwrap-or ( Option<Int> Int -- Int )
  swap match
    Some => [ swap drop ]
    None => [ ]
  end ;

# Usage
10 2 safe-div 0 unwrap-or  # Result: 5
10 0 safe-div 0 unwrap-or  # Result: 0
```

## Status

**Phase 1 (In Progress)**: Core type checker
- Effect system with stack effect inference
- Pattern matching with exhaustiveness checking
- Linear type tracking
- Sum types (ADTs)

See [DESIGN.md](DESIGN.md) for detailed design decisions.
See [PLAN.md](PLAN.md) for development roadmap.
See [EXAMPLES.md](EXAMPLES.md) for more code examples.

## Building

*(To be added once implementation begins)*

## Project Structure

```
/docs           - Design documents and specifications
/src            - Rust implementation
  /parser       - Lexer and parser
  /typechecker  - Effect inference and type checking
  /codegen      - LLVM backend
  /runtime      - CSP runtime (green threads, channels)
/examples       - Example Seam programs
/tests          - Test suite
```

## Contributing

This is currently an exploratory project. Design feedback welcome via issues.

## License

*(To be determined)*
