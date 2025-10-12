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

## Concurrency Model

Cem uses **cooperative green threads (strands)** for massive concurrency:

- ✅ **Hundreds of thousands of concurrent strands** - Lightweight (4KB-1MB stacks)
- ✅ **Single OS thread event loop** - All strands run cooperatively on one thread
- ✅ **Non-blocking I/O** - Async event loop (kqueue/epoll) for true concurrency
- ✅ **Explicit yield points** - Strands yield only at I/O operations, never during computation

**What this means:**
- **Excellent for I/O-bound workloads** - Web servers, file processing, network services
- **Deterministic execution** - No data races, no preemption between I/O calls
- **Erlang-scale concurrency** - Handle 100K+ partially-executed concurrent tasks
- **Not parallel** - CPU-bound work uses one core (multi-threading planned for Phase 4+)

**Perfect for:** Web servers with 100K concurrent connections, async file processing pipelines, actor-based systems where tasks spend most time waiting on I/O.

See [docs/IO_ARCHITECTURE.md](docs/IO_ARCHITECTURE.md) for detailed concurrency semantics.

## Core Values

- **Compile-time safety**: Type system catches errors before runtime
- **Zero-cost abstractions**: LLVM native compilation, no GC
- **Static linking**: Self-contained binaries with no runtime dependencies
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

Cem requires:
- Rust toolchain (stable)
- Clang (for C runtime compilation)
- LLVM tools (`llc`, `lli`)

**Platform Support**: 
- ✅ **ARM64 macOS** - Fully supported (kqueue I/O, custom context switching)
- ✅ **x86-64 Linux** - Fully supported (epoll I/O, custom context switching)
- 🔄 **x86-64 macOS** - Partial support (needs context switching port)
- 🔄 **ARM64 Linux** - Partial support (needs context switching port)
- 🔄 **FreeBSD/OpenBSD/NetBSD** - Partial support (has kqueue, needs context switching per arch)

See `PLATFORM_STATUS.md` for detailed platform support information.

Build the compiler and runtime:
```bash
cargo build
just build-runtime
```

Compile and run examples:
```bash
cargo run --example compile_hello_io
./hello_io
```

## Debugging

Cem binaries work with standard debuggers (LLDB/GDB) out of the box. You can step through runtime code, set breakpoints, and inspect memory. For Cem-specific debugging features (stack visualization, quotation inspection), see [docs/DEBUGGING.md](docs/DEBUGGING.md).

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
