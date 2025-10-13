# Cem Documentation

This directory contains comprehensive documentation for the Cem programming language and its implementation.

## Documentation Structure

### Architecture
Deep dives into the system design and implementation decisions:

- **[ARCHITECTURE.md](architecture/ARCHITECTURE.md)** - High-level system architecture (Rust compiler + C runtime)
- **[IO_ARCHITECTURE.md](architecture/IO_ARCHITECTURE.md)** - Concurrency model: green threads, CSP-style channels, cooperative scheduling
- **[SCHEDULER_IMPLEMENTATION.md](architecture/SCHEDULER_IMPLEMENTATION.md)** - Scheduler implementation phases and design decisions
- **[LLVM_TEXT_IR.md](architecture/LLVM_TEXT_IR.md)** - Decision rationale: Why we generate text IR instead of using FFI

### Language Design
Core language concepts and philosophy:

- **[recursion.md](language/recursion.md)** - Recursion as flow control with tail-call optimization
- **[SELF_HOSTING.md](language/SELF_HOSTING.md)** - Long-term vision for self-hosting the Cem compiler

### Development
Guides for working on Cem:

- **[DEBUGGING.md](development/DEBUGGING.md)** - Debugging Cem programs and the compiler
- **[KNOWN_ISSUES.md](development/KNOWN_ISSUES.md)** - Current issues, design decisions, and trade-offs
- **[PLATFORM_STATUS.md](development/PLATFORM_STATUS.md)** - Platform support matrix and implementation status

### Archive
Historical documents tracking completed work:

- **[CONTEXT_SWITCHING_COMPARISON.md](archive/CONTEXT_SWITCHING_COMPARISON.md)** - ARM64 vs x86-64 context switching analysis
- **[DEBUG_METADATA_PLAN.md](archive/DEBUG_METADATA_PLAN.md)** - Plan for LLVM debug metadata implementation
- **[LINUX_EPOLL_PLAN.md](archive/LINUX_EPOLL_PLAN.md)** - Linux epoll I/O implementation plan
- **[NESTED_IF_CLANG_CRASH.md](archive/NESTED_IF_CLANG_CRASH.md)** - Analysis of nested if expression bug
- **[NESTED_IF_FIX_SUMMARY.md](archive/NESTED_IF_FIX_SUMMARY.md)** - Summary of nested if fix
- **[PHASE_2A_RESULTS.md](archive/PHASE_2A_RESULTS.md)** - Phase 2a completion summary
- **[X86_64_LINUX_CONTEXT_PLAN.md](archive/X86_64_LINUX_CONTEXT_PLAN.md)** - x86-64 Linux context switching plan

## Quick Links

### For New Contributors
1. Start with [../README.md](../README.md) - Project overview
2. Read [../BUILD.md](../BUILD.md) - Build instructions
3. Understand [architecture/ARCHITECTURE.md](architecture/ARCHITECTURE.md) - System design
4. Review [development/KNOWN_ISSUES.md](development/KNOWN_ISSUES.md) - Current state

### For Language Users
1. [../README.md](../README.md) - Language overview and examples
2. [language/recursion.md](language/recursion.md) - Understanding Cem's flow control
3. [development/DEBUGGING.md](development/DEBUGGING.md) - Debugging your programs

### For Researchers
1. [architecture/IO_ARCHITECTURE.md](architecture/IO_ARCHITECTURE.md) - Concurrency model
2. [architecture/LLVM_TEXT_IR.md](architecture/LLVM_TEXT_IR.md) - Compilation approach
3. [language/SELF_HOSTING.md](language/SELF_HOSTING.md) - Language completeness vision

## Documentation Philosophy

This documentation follows these principles:

1. **Preserve Decision Rationale** - We keep records of *why* decisions were made, not just *what* was done
2. **Archive Completed Work** - Completed projects are archived, not deleted, to maintain project history
3. **Living Documents** - Active documents are updated as the project evolves
4. **Clear Organization** - Docs are organized by purpose (architecture, language, development)

## See Also

- [examples/](../examples/) - Example Cem programs
- [src/](../src/) - Compiler source code
- [runtime/](../runtime/) - Runtime C implementation
