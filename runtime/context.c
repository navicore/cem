/**
 * Cem Runtime - Context Switching (C implementation)
 *
 * This file contains the C portion of context switching.
 * Assembly implementations are in context_<arch>.s
 */

#include "context.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/**
 * Initialize a context for a new strand
 *
 * This sets up the stack and registers so that when we switch to this
 * context, it will start executing `func`. When `func` returns, control
 * will pass to `return_func`.
 */
void cem_makecontext(cem_context_t *ctx, void *stack_base, size_t stack_size,
                     void (*func)(void), void (*return_func)(void)) {
  // Validate inputs
  assert(ctx != NULL && "context pointer cannot be NULL");
  assert(stack_base != NULL && "stack base pointer cannot be NULL");
  assert(stack_size > 0 &&
         "stack size must be positive (for alignment safety)");
  assert(stack_size >= CEM_MIN_STACK_SIZE &&
         "stack size must be at least CEM_MIN_STACK_SIZE for safe execution");
  assert(func != NULL && "function pointer cannot be NULL");

  // Zero out the context
  memset(ctx, 0, sizeof(cem_context_t));

#ifdef CEM_ARCH_ARM64
  // ARM64: Stack grows downward (from high address to low address)
  // stack_base is the LOW address of the allocated memory
  // stack_top (high address) is where the stack pointer starts
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
  (void)return_func; // Unused - see safety note above

#elif defined(CEM_ARCH_X86_64)
  // x86-64: Stack grows downward (from high address to low address)
  // stack_base is the LOW address of the allocated memory
  // stack_top (high address) is where the stack pointer starts
  uintptr_t stack_top = (uintptr_t)stack_base + stack_size;

  fprintf(stderr, "[MAKECONTEXT] stack_base=%p, stack_size=%zu, stack_top=%p\n",
          stack_base, stack_size, (void *)stack_top);
  fprintf(stderr, "[MAKECONTEXT] func=%p\n", (void *)func);
  fflush(stderr);

  // Align to 16 bytes (required by x86-64 ABI before 'call')
  stack_top &= ~15ULL;

  // After 'ret', rsp will be incremented by 8, so we need rsp to be
  // 16-byte aligned BEFORE 'ret', which means it should be at (16n + 8)
  // But we just aligned to 16, so subtract 8 to get to (16n + 8)
  stack_top -= 8;

  // Reserve space for red zone (128 bytes below rsp)
  // The red zone is a 128-byte area below rsp that functions can use
  // without adjusting rsp. We need to make sure we don't put our
  // return address there.
  stack_top -= 128;

  // Push the function address onto the stack
  // This will be the return address that 'ret' will jump to
  stack_top -= sizeof(void *);
  *(void **)stack_top = (void *)func;

  fprintf(stderr, "[MAKECONTEXT] Pushed func to stack at %p, rsp will be %p\n",
          (void *)stack_top, (void *)stack_top);
  fflush(stderr);

  // Note: Stack is now misaligned by 8 bytes (as expected after 'call')
  // This matches what swapcontext expects

  ctx->rsp = stack_top;

  // Set frame pointer to 0 (no parent frame)
  // Setting it to stack_top would mean the return address is part of the frame,
  // which is wrong and causes stack corruption when the function uses rbp
  ctx->rbp = 0;

  // Initialize MXCSR to default value (0x1F80)
  // This enables all floating point exceptions masked
  ctx->mxcsr = 0x1F80;

  // Zero out other registers
  ctx->rbx = 0;
  ctx->r12 = 0;
  ctx->r13 = 0;
  ctx->r14 = 0;
  ctx->r15 = 0;

  // NOTE: return_func is intentionally unused (same reasoning as ARM64)
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
  // THREAD-SAFETY NOTE for future work-stealing:
  // This initialization is NOT thread-safe by itself, but that's fine because:
  // - cem_makecontext() is only called during strand_spawn()
  // - strand_spawn() must be synchronized by the scheduler
  // - Once initialized, the context can be safely migrated between threads
  // - The context contains no thread-local state (no TLS pointers, etc.)
  (void)return_func; // Unused - see safety note above

#endif
}
