# Cem

*Pronounced "seam"*

A minimal, safe, concatenative programming language with linear types, pattern
matching, and CSP-style concurrency.

## Design Philosophy

Cem explores the intersection of four powerful language design ideas:

### Concatenative Programming
Code is composed by juxtaposition (Forth, Factor, Joy). Functions consume and
produce values on a stack, making composition the fundamental operation. `2 3 +`
pushes 2, then 3, then applies addition.

### Linear Type Systems
Values must be used exactly once (Rust ownership, linear logic). This prevents
use-after-free, double-free, and resource leaks at compile time. Channels and
file handles can't be accidentally copied or dropped.

### Pattern Matching
Destructure sum types declaratively (ML, Rust, Erlang). The compiler verifies
all cases are handled. In Cem, pattern matching integrates with the stack, so
matching pops values and pushes results.

### CSP-Style Concurrency
Communicate Sequential Processes (Go channels, Erlang message passing).
Processes communicate through typed channels rather than shared memory. Combined
with linear types, this prevents data races statically.

**The goal**: A production-grade language proving concatenative + linear types
can be both safe and practical.

## Core Values

- **Compile-time safety**: Type system catches errors before runtime
- **Zero-cost abstractions**: LLVM native compilation, no GC
- **No C stack issues**: Explicit data stack with compile-time bounds
- **Safe concurrency**: Linear channels prevent data races
- **Pattern matching**: Destructuring as stack operations
- **Minimal core**: Recursion + tail-call optimization instead of imperative loops

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

## Flow Control: Recursion Over Loops

Cem uses **recursion with tail-call optimization** as its primary flow control
mechanism, not imperative loops. This keeps the language pure and minimal while
providing full expressive power.

```cem
# Tail-recursive factorial with accumulator
: factorial-helper ( n acc -- result )
  over 1 > if
    [ over 1 - swap over * factorial-helper ]  # Tail call - optimized!
    [ nip ]
  ;

: factorial ( n -- n! )
  1 factorial-helper ;
```

The compiler automatically optimizes tail calls into jumps, making recursion as
efficient as any loop. See [docs/recursion.md](docs/recursion.md) for details.

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
