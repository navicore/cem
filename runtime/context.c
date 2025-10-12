/**
 * Cem Runtime - Context Switching (C implementation)
 *
 * This file contains the C portion of context switching.
 * Assembly implementations are in context_<arch>.s
 */

#include "context.h"
#include <string.h>
#include <stdint.h>

/**
 * Initialize a context for a new strand
 *
 * This sets up the stack and registers so that when we switch to this
 * context, it will start executing `func`. When `func` returns, control
 * will pass to `return_func`.
 */
void cem_makecontext(cem_context_t* ctx,
                     void* stack_base,
                     size_t stack_size,
                     void (*func)(void),
                     void (*return_func)(void))
{
    // Zero out the context
    memset(ctx, 0, sizeof(cem_context_t));

#ifdef CEM_ARCH_ARM64
    // ARM64: Stack grows downward, must be 16-byte aligned
    // Set stack pointer to top of stack (high address)
    uintptr_t stack_top = (uintptr_t)stack_base + stack_size;

    // Align to 16 bytes (required by ARM64 ABI)
    stack_top &= ~15ULL;

    ctx->sp = stack_top;

    // Set PC (stored in x30/LR) to the function to execute
    // When we swapcontext, it will jump to this address
    ctx->x30 = (uint64_t)func;

    // Set frame pointer to stack top (no frame yet)
    ctx->x29 = stack_top;

    // Store return function in x19 (callee-saved)
    // This is a convention: when func returns, we can check x19
    // However, we need a better mechanism for this...
    // For now, we'll rely on the strand trampoline to handle returns
    (void)return_func;  // Unused for now

#elif defined(CEM_ARCH_X86_64)
    // x86-64: Stack grows downward, must be 16-byte aligned before CALL
    // Set stack pointer to top of stack (high address)
    uintptr_t stack_top = (uintptr_t)stack_base + stack_size;

    // Align to 16 bytes, then subtract 8 for return address
    stack_top &= ~15ULL;
    stack_top -= 8;

    // Push return address onto stack
    uint64_t* stack_ptr = (uint64_t*)stack_top;
    *stack_ptr = (uint64_t)return_func;

    ctx->rsp = stack_top;

    // Push function address (will be popped by ret instruction)
    stack_top -= 8;
    stack_ptr = (uint64_t*)stack_top;
    *stack_ptr = (uint64_t)func;

    ctx->rsp = stack_top;

    // Initialize MXCSR to default state
    ctx->mxcsr = 0x1F80;  // Default: all exceptions masked

#endif
}
