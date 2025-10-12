/**
 * Cem Runtime - Context Switching (C implementation)
 *
 * This file contains the C portion of context switching.
 * Assembly implementations are in context_<arch>.s
 */

#include "context.h"
#include <string.h>
#include <stdint.h>
#include <assert.h>

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
    // Validate inputs
    assert(ctx != NULL && "context pointer cannot be NULL");
    assert(stack_base != NULL && "stack base pointer cannot be NULL");
    assert(stack_size >= 4096 && "stack size must be at least 4KB (4096 bytes) for safe execution");
    assert(func != NULL && "function pointer cannot be NULL");

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

    // NOTE: return_func is intentionally unused in the current implementation
    //
    // SAFETY: This is safe because:
    // 1. All strands are created via strand_spawn() in scheduler.c
    // 2. strand_spawn() ALWAYS uses strand_entry_trampoline as the entry point
    // 3. The trampoline calls the actual strand function and handles returns
    // 4. When a strand function returns, the trampoline:
    //    - Sets strand->state = STRAND_COMPLETED
    //    - Swaps back to scheduler_context
    //    - The scheduler cleans up the strand
    //
    // DEPENDENCY: This implementation requires that cem_makecontext is ONLY
    // called from strand_spawn() with strand_entry_trampoline. Direct calls
    // with arbitrary functions would need return_func to be implemented.
    //
    // FUTURE: If we want to support general-purpose context switching outside
    // the scheduler, we would need to implement return_func properly, perhaps
    // by storing it in a callee-saved register (e.g., x19) and having the
    // assembly check and jump to it when func returns.
    (void)return_func;  // Unused - see safety note above

#elif defined(CEM_ARCH_X86_64)
    // x86-64 context switching is not yet implemented
    // TODO: Implement x86-64 assembly (similar to context_arm64.s)
    #error "x86-64 context switching is not yet implemented. Only ARM64 macOS is currently supported."

#endif
}
