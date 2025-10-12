/**
 * Cem Runtime - ARM64 Context Switching (macOS)
 *
 * This file implements fast context switching for ARM64 (AArch64).
 * It saves and restores callee-saved registers according to the
 * ARM64 calling convention (AAPCS64).
 *
 * Calling convention reference:
 * - https://developer.arm.com/documentation/ihi0055/latest/
 *
 * Register usage:
 * - x0-x18: Caller-saved (not preserved across function calls)
 * - x19-x28: Callee-saved (must be preserved)
 * - x29: Frame pointer (FP) - callee-saved
 * - x30: Link register (LR) - holds return address
 * - sp: Stack pointer - callee-saved
 * - d0-d7: FP argument/result registers - caller-saved
 * - d8-d15: FP callee-saved registers
 * - d16-d31: FP caller-saved registers
 */

.text
.align 4

/**
 * void cem_swapcontext(cem_context_t* save_ctx, const cem_context_t* restore_ctx)
 *
 * Save current context to save_ctx and restore restore_ctx.
 *
 * Arguments:
 *   x0 = save_ctx (pointer to cem_context_t)
 *   x1 = restore_ctx (pointer to cem_context_t)
 *
 * Context layout (matches cem_context_t in context.h):
 *   Offset  Register
 *   0x00    x19
 *   0x08    x20
 *   0x10    x21
 *   0x18    x22
 *   0x20    x23
 *   0x28    x24
 *   0x30    x25
 *   0x38    x26
 *   0x40    x27
 *   0x48    x28
 *   0x50    x29 (FP)
 *   0x58    x30 (LR)
 *   0x60    sp
 *   0x68    d8
 *   0x70    d9
 *   0x78    d10
 *   0x80    d11
 *   0x88    d12
 *   0x90    d13
 *   0x98    d14
 *   0xA0    d15
 */
.globl _cem_swapcontext
_cem_swapcontext:
    // Save current context to save_ctx (x0)

    // Save general purpose registers x19-x28
    stp     x19, x20, [x0, #0x00]
    stp     x21, x22, [x0, #0x10]
    stp     x23, x24, [x0, #0x20]
    stp     x25, x26, [x0, #0x30]
    stp     x27, x28, [x0, #0x40]

    // Save frame pointer and link register
    stp     x29, x30, [x0, #0x50]

    // Save stack pointer
    // Note: We save the SP *after* this function was called
    // (i.e., the caller's SP)
    mov     x9, sp
    str     x9, [x0, #0x60]

    // Save floating point registers d8-d15
    stp     d8,  d9,  [x0, #0x68]
    stp     d10, d11, [x0, #0x78]
    stp     d12, d13, [x0, #0x88]
    stp     d14, d15, [x0, #0x98]

    // Restore context from restore_ctx (x1)

    // Restore general purpose registers x19-x28
    ldp     x19, x20, [x1, #0x00]
    ldp     x21, x22, [x1, #0x10]
    ldp     x23, x24, [x1, #0x20]
    ldp     x25, x26, [x1, #0x30]
    ldp     x27, x28, [x1, #0x40]

    // Restore frame pointer and link register
    ldp     x29, x30, [x1, #0x50]

    // Restore stack pointer
    ldr     x9, [x1, #0x60]
    mov     sp, x9

    // Restore floating point registers d8-d15
    ldp     d8,  d9,  [x1, #0x68]
    ldp     d10, d11, [x1, #0x78]
    ldp     d12, d13, [x1, #0x88]
    ldp     d14, d15, [x1, #0x98]

    // Return to the address in x30 (LR from restored context)
    ret

/**
 * Notes on stack alignment and calling convention:
 *
 * 1. Stack must be 16-byte aligned at function entry
 *    - The caller (C code) ensures this
 *    - Our SP save/restore maintains this
 *
 * 2. Link register (x30) holds return address
 *    - When we save x30, we save where to return to
 *    - When we restore x30, we restore the return address
 *    - The 'ret' instruction jumps to address in x30
 *
 * 3. For makecontext:
 *    - We set x30 to the function to execute
 *    - When swapcontext restores x30 and executes 'ret',
 *      it will jump to that function
 *    - The function will execute as if it was called normally
 *
 * 4. Callee-saved registers:
 *    - x19-x28: Must be preserved across calls
 *    - x29 (FP): Must be preserved
 *    - d8-d15: Must be preserved
 *    - Everything else is caller-saved (C compiler handles those)
 */
