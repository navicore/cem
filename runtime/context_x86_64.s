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
 * - rax, rcx, rdx, rsi, rdi, r8-r11: Caller-saved (not preserved)
 * - rbx, rbp, r12-r15: Callee-saved (must be preserved)
 * - rsp: Stack pointer - callee-saved
 * - XMM0-XMM15: FP registers - caller-saved
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
.type cem_swapcontext, @function
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
    // NOTE: Currently disabled - not required for correctness
    // (XMM registers are caller-saved per System V AMD64 ABI)
    // stmxcsr 0x38(%rdi)

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
    // NOTE: Currently disabled - not required for correctness
    // (XMM registers are caller-saved per System V AMD64 ABI)
    // ldmxcsr 0x38(%rsi)

    // Return to the address on the stack
    // The return address was pushed by the caller when calling us,
    // and we just restored rsp to point to that address
    ret

.size cem_swapcontext, .-cem_swapcontext

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
 *
 * 5. Thread-safety considerations for future work-stealing:
 *    - This implementation is thread-safe at the register level
 *    - Each strand has its own context and stack (no shared state here)
 *    - The scheduler will need atomic operations for work-stealing queues
 *    - Stack pointers and contexts are completely independent per-strand
 *    - Migration between OS threads is safe as long as:
 *      a) The strand's context is fully saved before migration
 *      b) The strand's stack memory remains valid (heap-allocated)
 *      c) Scheduler uses proper synchronization for queue operations
 */

// Mark stack as non-executable (required for modern Linux security)
// This prevents "missing .note.GNU-stack section implies executable stack" warnings
.section .note.GNU-stack,"",@progbits
