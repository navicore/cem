# Context Switching: ARM64 vs x86-64 Implementation Comparison

## Overview

This document compares the two context switching implementations in Cem,
highlighting architectural differences and design decisions.

## Implementation Files

| Component | ARM64 macOS | x86-64 Linux |
|-----------|-------------|--------------|
| Assembly | `context_arm64.s` | `context_x86_64.s` |
| Lines of assembly | ~133 (with comments) | ~125 (with comments) |
| Assembly instructions | ~35 | ~15 |
| Binary size | ~140 bytes | ~63 bytes |

## Architecture Comparison

### Registers Saved

**ARM64 (21 values):**
- x19-x28: 10 callee-saved GPRs
- x29: Frame pointer
- x30: Link register (return address)
- sp: Stack pointer
- d8-d15: 8 callee-saved FP registers

**x86-64 (8 values):**
- rbx, rbp, r12-r15: 6 callee-saved GPRs
- rsp: Stack pointer
- mxcsr: FP control/status register (32-bit)

### Key Architectural Differences

| Feature | ARM64 | x86-64 |
|---------|-------|--------|
| **Return address** | In register (x30/LR) | On stack |
| **FP registers** | 8 doubles (64 bytes) | 1 control reg (4 bytes) |
| **Context size** | 168 bytes | 64 bytes |
| **Store instruction** | `stp` (store pair) | `mov` (single) |
| **Load instruction** | `ldp` (load pair) | `mov` (single) |

### Return Address Handling

**ARM64:**
```asm
stp     x29, x30, [x0, #0x50]   # Save FP and LR together
...
ldp     x29, x30, [x1, #0x50]   # Restore FP and LR
ret                              # Jump to address in x30
```

**x86-64:**
```asm
mov     %rsp, 0x30(%rdi)        # Save SP (points to return address)
...
mov     0x30(%rsi), %rsp        # Restore SP
ret                              # Pop return address from stack and jump
```

The x86-64 `ret` instruction automatically pops the return address from the stack,
while ARM64 uses the link register explicitly.

## Calling Convention Differences

### Function Entry

**ARM64 (AAPCS64):**
- Arguments: x0-x7, d0-d7
- Stack must be 16-byte aligned **at function entry**
- Link register (x30) holds return address

**x86-64 (System V AMD64):**
- Arguments: rdi, rsi, rdx, rcx, r8, r9
- Stack must be 16-byte aligned **before `call`**
- Return address pushed onto stack by `call`

### Stack Alignment

Both require 16-byte alignment, but differ in when:

**ARM64:**
```c
// At function entry, SP must be 16-byte aligned
uintptr_t stack_top = (uintptr_t)stack_base + stack_size;
stack_top &= ~15ULL;  // Align to 16 bytes
ctx->sp = stack_top;   // Direct assignment
```

**x86-64:**
```c
// After 'call', SP is misaligned by 8 (pointing to return address)
uintptr_t stack_top = (uintptr_t)stack_base + stack_size;
stack_top &= ~15ULL;   // Align to 16 bytes
stack_top -= 8;        // Push return address (8 bytes)
*(void**)stack_top = (void*)func;
ctx->rsp = stack_top;  // Now misaligned as expected
```

## Performance Characteristics

### Instruction Count

**ARM64:** ~35 instructions
- 4x `stp` (store pair) = 8 registers in 4 instructions
- 4x `ldp` (load pair) = 8 registers in 4 instructions
- Singles for SP and remaining registers
- Efficient pair operations reduce instruction count

**x86-64:** ~15 instructions
- 7x `mov` to save 7 registers
- 7x `mov` to restore 7 registers
- 1x `stmxcsr`, 1x `ldmxcsr` for FP control
- Fewer registers = fewer instructions

### Memory Bandwidth

**ARM64:** 168 bytes per context switch
**x86-64:** 64 bytes per context switch

x86-64 has 2.6× less memory traffic, which can improve cache performance.

### Expected Performance

Both implementations target **10-50ns per context switch**:
- No system calls
- Direct memory operations
- Minimal register file operations
- L1 cache hits for context structure

## Code Quality Comparison

### Assembly Clarity

**ARM64 strengths:**
- Explicit link register makes control flow obvious
- Pair instructions are elegant and efficient
- Larger register file provides more working space

**x86-64 strengths:**
- Fewer registers = simpler implementation
- Stack-based return address is conventional
- Smaller context = better cache utilization

### Portability Within Architecture

**ARM64:**
- Same code works on macOS and Linux (just change function prefix)
- AArch64 ABI is consistent across platforms

**x86-64:**
- System V (Linux/BSD) vs Microsoft (Windows) calling conventions differ
- Our implementation is System V only
- Windows would need different register save set

## Thread Safety Analysis

Both implementations are thread-safe for work-stealing because:

### No Shared Mutable State
- Each context is independent
- No global variables accessed
- No thread-local storage (TLS) dependencies

### Safe for Migration
- Contexts are heap-allocated (not on thread stack)
- Register state is architecture-specific, not thread-specific
- Stack pointers are absolute addresses (work across threads)

### Synchronization Requirements (Future Work)
When implementing work-stealing, the scheduler needs:
- Atomic operations for ready queues
- Memory barriers around context switches
- Proper strand ownership transfer

But the context switching code itself is already thread-safe.

## Lessons Learned from ARM64 Implementation

### What We Copied Directly

1. **Comment style:** Extensive documentation in assembly
2. **Safety notes:** Clear explanation of return_func handling
3. **Stack alignment:** Same approach, different values
4. **Test structure:** Used identical test cases

### What We Adapted

1. **Return address:** Register → stack
2. **FP state:** Full registers → control register only
3. **Instruction choice:** Pairs → singles
4. **Context size:** Larger → smaller

### What We Added

1. **Thread-safety notes:** Explicit work-stealing considerations
2. **ABI comparison:** x86-64 vs ARM64 differences
3. **Platform detection:** Architecture-agnostic header

## Best Practices Established

### 1. Documentation
- Comment every register saved/restored
- Explain calling convention requirements
- Document alignment requirements
- Note thread-safety considerations

### 2. Testing
- Test basic context switch
- Test multiple switches
- Test stack preservation
- Test floating-point state
- Test input validation

### 3. Portability
- Use platform detection macros
- Keep architecture-specific code isolated
- Provide unified C API
- Document porting process

### 4. Future-Proofing
- Design for work-stealing from start
- No global state
- Heap-allocated stacks
- Clear ownership semantics

## Platform Port Difficulty

### Easy Ports (1-2 hours)
- **macOS x86-64:** Change function prefix `cem_swapcontext` → `_cem_swapcontext`
- **Linux ARM64:** Use ARM64 assembly on Linux (no changes needed)

### Medium Ports (1 day)
- **FreeBSD/OpenBSD x86-64:** May need .type directive adjustments
- **Windows x86-64:** Different calling convention (rcx, rdx, r8, r9 for args)

### Hard Ports (1 week)
- **ARM 32-bit:** Different registers, different alignment
- **RISC-V:** Different architecture entirely
- **WebAssembly:** No direct register access

## Recommendations

### For New Platform Ports

1. **Start with the plan document** (like X86_64_LINUX_CONTEXT_PLAN.md)
2. **Study both implementations** - learn from ARM64's comments, x86-64's simplicity
3. **Read the ABI specification** - don't guess at calling conventions
4. **Test thoroughly** - use existing test_context.c
5. **Verify assembly** - use objdump to check generated code
6. **Document differences** - update this comparison document

### For Future Work-Stealing

1. **Keep contexts independent** - no shared state
2. **Use atomic operations** for queues
3. **Consider NUMA** for stack allocation
4. **Add per-thread caches** for scheduling
5. **Measure migration cost** - context switch + cache effects

## Conclusion

The ARM64 and x86-64 implementations demonstrate that:
- Fast context switching is portable across architectures
- Core design (no shared state, heap stacks) enables work-stealing
- Different architectures can share the same high-level structure
- Good documentation makes porting straightforward

The x86-64 implementation benefited enormously from the ARM64 groundwork,
completing in ~2 hours versus the original multi-day ARM64 implementation.

Future platforms can follow this same pattern: study existing implementations,
adapt to architectural differences, and maintain the unified API.
