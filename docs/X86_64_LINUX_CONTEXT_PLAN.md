# x86-64 Linux Context Switching Implementation Plan

## Overview

This document outlines the plan to implement context switching for x86-64 Linux, based on the existing ARM64 macOS implementation. The goal is to add platform support without changing the overall architecture.

**Current Status:** ARM64 macOS fully implemented and tested  
**Target:** x86-64 Linux (Intel/AMD processors on Rocky Linux and other distributions)

## Background

The Cem runtime uses custom assembly context switching to achieve high-performance cooperative multitasking. The current implementation (`context_arm64.s`) provides fast context switches (~10-20ns vs ~500ns for ucontext) on ARM64 macOS.

## Architecture Summary

### Key Files to Study

1. **`runtime/context_arm64.s`** - ARM64 assembly implementation
   - Implements `cem_swapcontext()` function
   - Saves/restores callee-saved registers
   - ~133 lines with extensive comments

2. **`runtime/context.h`** - Platform-agnostic header
   - Defines `cem_context_t` structure (architecture-specific layout)
   - Platform detection macros
   - API declarations
   - Contains x86-64 structure definition (ready to use)

3. **`runtime/context.c`** - C helper functions
   - Implements `cem_makecontext()` for initializing new strand contexts
   - Handles stack setup and alignment
   - Has x86-64 section (needs implementation)

4. **`runtime/scheduler.h/c`** - Scheduler that uses context switching
   - Shows how context switching is used in practice
   - Entry points: `strand_spawn()`, `strand_yield()`

### ARM64 Context Layout (for reference)

```c
typedef struct {
    uint64_t x19, x20, x21, x22, x23, x24, x25, x26, x27, x28;  // Callee-saved GPRs (10 regs)
    uint64_t x29;  // Frame pointer
    uint64_t x30;  // Link register (return address)
    uint64_t sp;   // Stack pointer
    double   d8, d9, d10, d11, d12, d13, d14, d15;  // Callee-saved FP registers (8 regs)
} cem_context_t;
```

Total: 168 bytes (13 int regs × 8 bytes + 8 FP regs × 8 bytes)

### x86-64 Context Layout (already defined in context.h)

```c
typedef struct {
    uint64_t rbx;      // Callee-saved GPR
    uint64_t rbp;      // Frame pointer
    uint64_t r12;      // Callee-saved GPR
    uint64_t r13;      // Callee-saved GPR
    uint64_t r14;      // Callee-saved GPR
    uint64_t r15;      // Callee-saved GPR
    uint64_t rsp;      // Stack pointer
    uint32_t mxcsr;    // FP control/status register
    uint32_t _padding;
} cem_context_t;
```

Total: 64 bytes (7 regs × 8 bytes + 4 bytes MXCSR + 4 bytes padding)

## x86-64 Calling Convention (System V AMD64 ABI)

### Register Usage

**Callee-saved (must preserve):**
- `rbx`, `rbp`, `r12`, `r13`, `r14`, `r15` - general purpose
- `rsp` - stack pointer
- MXCSR - floating point control/status (optional but recommended)

**Caller-saved (don't need to preserve):**
- `rax`, `rcx`, `rdx`, `rsi`, `rdi`, `r8`, `r9`, `r10`, `r11`
- XMM registers (XMM0-XMM15) - floating point (not callee-saved in base ABI)

**Special:**
- `rip` - instruction pointer (cannot be directly modified, controlled by `ret`/`call`)
- Return address is stored on the stack (not in a register like ARM64's LR)

### Stack Conventions

- Stack grows downward (high address → low address) - same as ARM64
- Stack must be 16-byte aligned **before** a `call` instruction
- Red zone: 128 bytes below `rsp` can be used without adjustment (leaf functions)
- Return address is pushed on stack by `call`, popped by `ret`

### Key Differences from ARM64

| Feature | ARM64 | x86-64 |
|---------|-------|--------|
| Return address | In register (x30/LR) | On stack |
| Callee-saved registers | 13 + 8 FP = 21 regs | 6 + 1 FP control = 7 "regs" |
| Structure size | 168 bytes | 64 bytes |
| Stack alignment | 16-byte before function entry | 16-byte before `call` |
| FP registers | d8-d15 callee-saved | XMM not callee-saved (but MXCSR is) |

## Implementation Plan

### Step 1: Study ARM64 Implementation (COMPLETE - analysis above)

**Key insights from ARM64:**
- Uses pair load/store (`stp`/`ldp`) for efficiency
- Saves callee-saved registers only (C ABI handles caller-saved)
- Stack pointer saved/restored to switch stacks
- Return address in x30 determines where execution resumes
- Extensive comments explain calling convention and safety

### Step 2: Create context_x86_64.s Assembly File

**File:** `runtime/context_x86_64.s`

**Function to implement:** `cem_swapcontext`

```asm
/**
 * Cem Runtime - x86-64 Context Switching (Linux)
 *
 * This file implements fast context switching for x86-64 (AMD64).
 * It saves and restores callee-saved registers according to the
 * System V AMD64 ABI.
 *
 * Calling convention reference:
 * - https://refspecs.linuxbase.org/elf/x86_64-abi-0.99.pdf
 *
 * Register usage:
 * - rax, rcx, rdx, rsi, rdi, r8-r11: Caller-saved
 * - rbx, rbp, r12-r15: Callee-saved (must be preserved)
 * - rsp: Stack pointer - callee-saved
 * - XMM0-XMM15: FP registers - caller-saved (except MXCSR)
 * - MXCSR: FP control/status - callee-saved (good practice)
 */

.text
.align 16

/**
 * void cem_swapcontext(cem_context_t* save_ctx, const cem_context_t* restore_ctx)
 *
 * Save current context to save_ctx and restore restore_ctx.
 *
 * Arguments (System V AMD64 calling convention):
 *   rdi = save_ctx (pointer to cem_context_t)
 *   rsi = restore_ctx (pointer to cem_context_t)
 *
 * Context layout (matches cem_context_t in context.h):
 *   Offset  Register
 *   0x00    rbx
 *   0x08    rbp
 *   0x10    r12
 *   0x18    r13
 *   0x20    r14
 *   0x28    r15
 *   0x30    rsp
 *   0x38    mxcsr (32-bit)
 */
.globl cem_swapcontext
cem_swapcontext:
    // Save current context to save_ctx (rdi)
    
    // Save callee-saved registers
    mov     %rbx, 0x00(%rdi)
    mov     %rbp, 0x08(%rdi)
    mov     %r12, 0x10(%rdi)
    mov     %r13, 0x18(%rdi)
    mov     %r14, 0x20(%rdi)
    mov     %r15, 0x28(%rdi)
    
    // Save stack pointer
    // Note: We save the SP *after* this function was called
    // (i.e., pointing to the return address on the stack)
    mov     %rsp, 0x30(%rdi)
    
    // Save MXCSR (floating point control/status)
    stmxcsr 0x38(%rdi)
    
    // Restore context from restore_ctx (rsi)
    
    // Restore callee-saved registers
    mov     0x00(%rsi), %rbx
    mov     0x08(%rsi), %rbp
    mov     0x10(%rsi), %r12
    mov     0x18(%rsi), %r13
    mov     0x20(%rsi), %r14
    mov     0x28(%rsi), %r15
    
    // Restore stack pointer
    mov     0x30(%rsi), %rsp
    
    // Restore MXCSR
    ldmxcsr 0x38(%rsi)
    
    // Return to the address on the stack
    // The return address was pushed by the caller when calling us,
    // and we just restored rsp to point to that address
    ret

/**
 * Notes on x86-64 vs ARM64:
 *
 * 1. Return address handling:
 *    - ARM64: Stored in x30 (LR) register
 *    - x86-64: Stored on the stack, popped by 'ret'
 *    - When we save rsp, we're saving the location of the return address
 *
 * 2. Stack alignment:
 *    - Must be 16-byte aligned before 'call'
 *    - After 'call', rsp points to return address (8 bytes)
 *    - So at function entry, rsp is misaligned by 8 (this is expected)
 *    - Our SP save/restore maintains this
 *
 * 3. Red zone:
 *    - 128 bytes below rsp can be used without adjustment
 *    - We don't use it here since we're saving state
 *
 * 4. For makecontext:
 *    - We need to set up the stack with a return address
 *    - The stack should have: [function_ptr] at top
 *    - When swapcontext restores rsp and executes 'ret',
 *      it will pop and jump to function_ptr
 */
```

### Step 3: Update context.c for x86-64

Implement the `cem_makecontext()` function for x86-64 in `context.c`:

```c
#elif defined(CEM_ARCH_X86_64)
    // x86-64: Stack grows downward (from high address to low address)
    // stack_base is the LOW address of the allocated memory
    // stack_top (high address) is where the stack pointer starts
    uintptr_t stack_top = (uintptr_t)stack_base + stack_size;

    // Align to 16 bytes (required by x86-64 ABI)
    stack_top &= ~15ULL;

    // Push the function address onto the stack
    // This will be the return address that 'ret' will jump to
    stack_top -= sizeof(void*);
    *(void**)stack_top = (void*)func;

    // Note: Stack is now misaligned by 8 bytes (as expected after 'call')
    // This matches what swapcontext expects

    ctx->rsp = stack_top;
    
    // Set frame pointer to stack top (no frame yet)
    ctx->rbp = stack_top;
    
    // Initialize MXCSR to default value (0x1F80)
    // This enables all floating point exceptions masked
    ctx->mxcsr = 0x1F80;
    
    // Zero out other registers
    ctx->rbx = 0;
    ctx->r12 = 0;
    ctx->r13 = 0;
    ctx->r14 = 0;
    ctx->r15 = 0;
    
    // NOTE: return_func is unused (same reasoning as ARM64)
    (void)return_func;
#endif
```

### Step 4: Update Platform Detection in context.h

The header already has the right checks, but we need to enable x86-64 Linux:

```c
// Check implementation status
#if defined(CEM_ARCH_ARM64) && defined(CEM_OS_MACOS)
    #define CEM_CONTEXT_IMPLEMENTED
#elif defined(CEM_ARCH_X86_64) && defined(CEM_OS_LINUX)
    #define CEM_CONTEXT_IMPLEMENTED  // <-- ADD THIS
#else
    #error "Context switching not yet implemented for this platform..."
#endif
```

### Step 5: Update Build System

Update `justfile` to build the correct assembly file based on platform:

```just
# Build the C runtime library
build-runtime:
    @echo "Building C runtime library..."
    cd runtime && clang -Wall -Wextra -std=c11 -g -O2 -c stack.c -o stack.o
    cd runtime && clang -Wall -Wextra -std=c11 -g -O2 -c context.c -o context.o
    cd runtime && clang -Wall -Wextra -std=c11 -g -O2 -c scheduler.c -o scheduler.o
    cd runtime && clang -Wall -Wextra -std=c11 -g -O2 -c io.c -o io.o
    cd runtime && clang -Wall -Wextra -std=c11 -g -O2 -c stack_mgmt.c -o stack_mgmt.o
    {{if os() == "macos" { "cd runtime && clang -g -O2 -c context_arm64.s -o context_asm.o" } else { "cd runtime && clang -g -O2 -c context_x86_64.s -o context_asm.o" }}}
    cd runtime && ar rcs libcem_runtime.a stack.o context.o context_asm.o scheduler.o io.o stack_mgmt.o
    @echo "✅ Built runtime/libcem_runtime.a"
```

Or use conditional compilation in a shell script:

```bash
#!/bin/bash
# Build script for runtime

ARCH=$(uname -m)
OS=$(uname -s)

if [ "$ARCH" = "arm64" ] || [ "$ARCH" = "aarch64" ]; then
    CONTEXT_ASM="context_arm64.s"
elif [ "$ARCH" = "x86_64" ]; then
    CONTEXT_ASM="context_x86_64.s"
else
    echo "Unsupported architecture: $ARCH"
    exit 1
fi

cd runtime
clang -Wall -Wextra -std=c11 -g -O2 -c stack.c -o stack.o
clang -Wall -Wextra -std=c11 -g -O2 -c context.c -o context.o
clang -Wall -Wextra -std=c11 -g -O2 -c scheduler.c -o scheduler.o
clang -Wall -Wextra -std=c11 -g -O2 -c io.c -o io.o
clang -Wall -Wextra -std=c11 -g -O2 -c stack_mgmt.c -o stack_mgmt.o
clang -g -O2 -c "$CONTEXT_ASM" -o context_asm.o
ar rcs libcem_runtime.a stack.o context.o context_asm.o scheduler.o io.o stack_mgmt.o
echo "Built libcem_runtime.a with $CONTEXT_ASM"
```

### Step 6: Testing Strategy

1. **Unit test:** Port `tests/test_context.c` to verify basic swapcontext
   - Test save/restore of all callee-saved registers
   - Test stack switching
   - Test return address handling

2. **Scheduler test:** Run existing `tests/test_scheduler.c`
   - Tests strand creation and yielding
   - Tests that strands can switch back and forth

3. **Integration test:** Run full runtime tests
   - `just test-all-runtime` should pass
   - Echo server example should work

4. **Stress test:** Run on Rocky Linux with many strands
   - Verify no memory corruption
   - Check performance (should be similar to ARM64)

### Step 7: Documentation Updates

Update documentation to reflect new platform support:

1. **context.h** - Update "Supported Platforms" comment
2. **README.md** - Add x86-64 Linux to supported platforms
3. **BUILD.md** - Add Linux-specific build instructions if needed

## Testing on Rocky Linux

### Prerequisites

```bash
# Install build tools
sudo dnf install clang llvm gcc make

# Install Rust (for building Cem compiler)
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh

# Install just (task runner)
cargo install just
```

### Build and Test

```bash
cd /home/navicore/git/navicore/cem

# Build runtime library
just build-runtime

# Run context switching tests
just test-context

# Run scheduler tests
just test-scheduler

# Run all runtime tests
just test-all-runtime
```

## Implementation Checklist

- [ ] Create `runtime/context_x86_64.s` with `cem_swapcontext` implementation
- [ ] Implement x86-64 section in `context.c` (`cem_makecontext`)
- [ ] Update platform detection in `context.h` to enable x86-64 Linux
- [ ] Update `justfile` or create build script for platform-specific assembly
- [ ] Test basic context switching (`test_context.c`)
- [ ] Test scheduler integration (`test_scheduler.c`)
- [ ] Run full test suite (`test-all-runtime`)
- [ ] Update documentation
- [ ] Verify on Rocky Linux system

## Risk Assessment

**Low Risk:**
- Assembly is straightforward (fewer registers than ARM64)
- ABI is well-documented and stable
- Similar architecture to ARM64 implementation
- No external dependencies

**Medium Risk:**
- Stack alignment requirements must be correct
- Return address handling is different from ARM64
- MXCSR might not be strictly necessary but good practice

**Mitigation:**
- Extensive comments in assembly
- Start with unit tests before integration
- Reference existing implementations (Boost.Context, libco)
- Test thoroughly on target platform

## Expected Timeline

- **Step 1 (Study):** 1-2 hours ✅
- **Step 2 (Assembly):** 2-3 hours
- **Step 3 (C code):** 1 hour
- **Step 4-5 (Platform detection & build):** 1 hour
- **Step 6 (Testing):** 2-3 hours
- **Step 7 (Documentation):** 30 minutes

**Total:** 8-11 hours for complete implementation and testing

## References

### x86-64 ABI Documentation
- [System V AMD64 ABI](https://refspecs.linuxbase.org/elf/x86_64-abi-0.99.pdf)
- [AMD64 Calling Conventions](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention)
- [Intel 64 and IA-32 Architectures Software Developer Manuals](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)

### Existing Implementations (for reference)
- [Boost.Context x86-64](https://github.com/boostorg/context/blob/develop/src/asm/jump_x86_64_sysv_elf_gas.S)
- [libco x86-64](https://github.com/higan-emu/libco/blob/master/amd64.c)
- [Golang runtime (assembly)](https://github.com/golang/go/blob/master/src/runtime/asm_amd64.s)

### Tools
- `objdump -d` - Disassemble to verify assembly
- `gdb` - Debug context switches step by step
- `valgrind` - Check for memory errors
- `perf` - Measure performance

## Future Work (Out of Scope)

- ARM64 Linux support (similar to ARM64 macOS)
- Windows x86-64 support (different ABI)
- 32-bit x86 support (different registers and calling convention)
- RISC-V support (emerging architecture)
- WebAssembly support (very different model)

## Conclusion

This plan provides a clear path from the existing ARM64 macOS implementation to x86-64 Linux support. The implementation is straightforward because:

1. The architecture is well-understood
2. We have a working reference implementation
3. The structure is already defined
4. The testing infrastructure exists

The main work is translating the ARM64 assembly patterns to x86-64 assembly, accounting for the differences in calling convention (especially return address handling).
