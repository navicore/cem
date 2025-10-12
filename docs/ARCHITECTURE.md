# Cem Architecture

## Overview

Cem uses a **two-layer architecture**:
1. **Rust Compiler** - Parser, typechecker, code generator
2. **C Runtime** - Stack operations, scheduler, I/O primitives

This document explains why this split exists and how the pieces fit together.

## Architecture Diagram

```
┌──────────────────────────────────────────┐
│         Cem Source (.cem files)          │
└──────────────┬───────────────────────────┘
               │
               ▼
┌──────────────────────────────────────────┐
│         Rust Compiler (navcat)           │
│  ┌────────────────────────────────────┐  │
│  │ Parser (Rust)                      │  │
│  │  - Lexer, Parser                   │  │
│  │  - AST construction                │  │
│  └──────────────┬─────────────────────┘  │
│                 ▼                         │
│  ┌────────────────────────────────────┐  │
│  │ Typechecker (Rust)                 │  │
│  │  - Stack effect inference          │  │
│  │  - Type unification                │  │
│  │  - Effect tracking                 │  │
│  └──────────────┬─────────────────────┘  │
│                 ▼                         │
│  ┌────────────────────────────────────┐  │
│  │ Code Generator (Rust)              │  │
│  │  - LLVM IR generation (text)       │  │
│  │  - Runtime function calls          │  │
│  └──────────────┬─────────────────────┘  │
└─────────────────┼────────────────────────┘
                  │
                  ▼
         ┌────────────────┐
         │  LLVM IR (.ll) │
         └────────┬───────┘
                  │
                  ▼
         ┌────────────────────────────┐
         │   clang (LLVM backend)     │
         │   - Optimization           │
         │   - Native code generation │
         └────────┬───────────────────┘
                  │
                  ▼
         ┌────────────────┐
         │  Object (.o)   │
         └────────┬───────┘
                  │
                  ├──────────────────┐
                  ▼                  ▼
         ┌────────────────┐  ┌──────────────────┐
         │ C Runtime (.o) │  │                  │
         │                │  │                  │
         │ stack.o        │  │                  │
         │ scheduler.o    │  │   Linker (ld)    │
         │ io.o           │──►                  │
         │                │  │                  │
         └────────────────┘  └──────┬───────────┘
                                    │
                                    ▼
                           ┌─────────────────┐
                           │  Executable     │
                           └─────────────────┘
```

## Why C for the Runtime?

### 1. LLVM Integration

The Rust compiler generates LLVM IR that calls C functions directly:

```llvm
define ptr @hello(ptr %stack) {
entry:
  %0 = call ptr @push_string(ptr %stack, ptr @str.0)
  %1 = call ptr @write_line(ptr %0)
  ret ptr %1
}

declare ptr @push_string(ptr, ptr)
declare ptr @write_line(ptr)
```

LLVM has first-class support for C calling conventions, making integration seamless.

### 2. System-Level Programming

The runtime needs direct access to:
- **System calls**: `kqueue()`, `kevent()`, `read()`, `write()`
- **Context switching**: `getcontext()`, `swapcontext()`, `makecontext()`
- **Memory management**: `malloc()`, `free()` for stacks
- **File descriptors**: Raw FD manipulation for non-blocking I/O

C provides this without FFI overhead or abstractions.

### 3. Performance Critical Path

Runtime functions are called **constantly**:
- `dup`, `drop`, `swap` - Every stack operation
- `write_line`, `read_line` - Every I/O operation
- Context switching - Every strand yield

C gives:
- Predictable performance (no hidden allocations)
- No runtime overhead (no async executor, no allocator complexity)
- Direct compilation to machine code

### 4. Simple Build Model

The build process is straightforward:
1. Rust compiler generates `.ll` (LLVM IR)
2. C compiler compiles runtime to `.o` (object files)
3. Linker combines them into executable

No complex FFI bindings, no build.rs scripts, no linking tricks.

### 5. Portability

C is the universal systems programming language:
- All platforms have a C compiler
- System libraries expose C APIs
- Easy to port to new architectures

## Why Rust for the Compiler?

### 1. Safety Where It Matters

The compiler deals with:
- Complex AST transformations
- Type inference and unification
- Code generation logic

Rust's ownership system prevents:
- Use-after-free in AST traversal
- Data races in parallel compilation (future)
- Buffer overflows in string handling

### 2. Ergonomics

Rust provides:
- Pattern matching for AST traversal
- Rich type system for representing types/effects
- Error handling with `Result<T, E>`
- Zero-cost abstractions

### 3. Tooling

Rust gives us:
- `cargo` for dependency management
- `rustfmt` for consistent style
- `clippy` for linting
- Great IDE support

## Component Boundaries

### Rust Compiler Responsibilities
- ✅ Parse `.cem` source into AST
- ✅ Type checking and inference
- ✅ Generate LLVM IR text
- ✅ Invoke linker to produce executable

### C Runtime Responsibilities
- ✅ Stack cell allocation/deallocation
- ✅ Stack operations (dup, drop, swap, etc.)
- ✅ Arithmetic and comparison operations
- ✅ String operations
- ✅ Green thread scheduler
- ✅ Async I/O primitives (read_line, write_line)
- ✅ Context switching (ucontext API)

### Clear Interface

The boundary is **function calls**:
- Cem code calls C functions via LLVM IR
- C functions operate on `StackCell*` pointers
- All state is in the C stack cells
- No FFI needed

## Trade-offs

### Current Approach (Rust + C)

**Pros:**
- Simple linking model
- Direct system access
- Predictable performance
- Easy to debug (gdb/lldb work great with C)

**Cons:**
- Two languages to maintain
- C lacks memory safety
- Need to be careful with manual memory management

### Alternative: Pure Rust Runtime

**Pros:**
- Single language
- Memory safety
- Better type system

**Cons:**
- Complex FFI or `extern "C"` everywhere
- Potential allocator conflicts
- Need `#[no_std]` or careful libc usage
- More complex build system
- Rust's async runtime might interfere with custom scheduler

### Alternative: Pure C Everything

**Pros:**
- Single language
- Maximum control

**Cons:**
- No safety in compiler
- Parser/typechecker harder to write correctly
- More potential for bugs in compiler logic

## Decision: Rust + C is the Right Choice

For Cem, the hybrid approach makes sense:
- **Compiler** benefits from Rust's safety and ergonomics
- **Runtime** benefits from C's directness and simplicity
- Clear boundary (function calls via LLVM IR)
- Best of both worlds

## Future Considerations

### Phase 2b: Assembly Context Switching
When we replace `ucontext` with hand-written assembly, we'll have:
- C runtime (stack.c, io.c)
- Assembly (context_switch.S)
- Still straightforward to link

### Phase 3: Multi-core Scheduler
If we add work-stealing and parallel execution:
- C gives us direct access to pthreads
- Or we could use Rust for this part (explicit boundary)
- Decision can be made when we get there

### Self-Hosting
Eventually, Cem might bootstrap itself:
- Cem compiler written in Cem
- But runtime will likely stay C (or assembly)
- System programming requires system-level language

## For New Contributors

**If you're working on the compiler (parser, typechecker, codegen):**
- Write Rust
- Focus on correctness and safety
- Use Rust idioms

**If you're working on the runtime (scheduler, I/O, stack operations):**
- Write C
- Focus on performance and direct system access
- Be careful with memory management
- Document ownership clearly

**Testing:**
- Unit tests for runtime are in `tests/*.c`
- Integration tests for compiler are in `tests/*.rs`
- End-to-end tests compile `.cem` files and run them

## See Also

- [IO_ARCHITECTURE.md](./IO_ARCHITECTURE.md) - Async I/O design
- [SCHEDULER_IMPLEMENTATION.md](./SCHEDULER_IMPLEMENTATION.md) - Green thread scheduler
- [PHASE_2A_RESULTS.md](./PHASE_2A_RESULTS.md) - Phase 2a completion summary
- [LLVM_TEXT_IR.md](./LLVM_TEXT_IR.md) - LLVM IR generation approach
