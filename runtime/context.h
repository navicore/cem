/**
 * Cem Runtime - Portable Context Switching
 *
 * This module provides fast, portable context switching for the Cem runtime.
 * It replaces the deprecated ucontext API with custom assembly implementations.
 *
 * Supported Platforms:
 * - macOS ARM64 (Apple Silicon) - IMPLEMENTED
 * - macOS x86-64 (Intel)        - TODO
 * - Linux ARM64                 - TODO
 * - Linux x86-64                - TODO
 *
 * Design Philosophy:
 * - Single unified API across all platforms
 * - Platform-specific implementations via conditional compilation
 * - Minimal overhead (~10-20ns context switch vs ~500ns for ucontext)
 * - Callee-saved registers only (caller-saved are preserved by C ABI)
 */

#ifndef CEM_RUNTIME_CONTEXT_H
#define CEM_RUNTIME_CONTEXT_H

#include <stdint.h>
#include <stddef.h>

// ============================================================================
// Platform Detection
// ============================================================================

// Detect architecture
#if defined(__aarch64__) || defined(__arm64__)
    #define CEM_ARCH_ARM64
#elif defined(__x86_64__) || defined(__amd64__)
    #define CEM_ARCH_X86_64
#else
    #error "Unsupported architecture. Supported: ARM64, x86-64"
#endif

// Detect OS
#if defined(__APPLE__) && defined(__MACH__)
    #define CEM_OS_MACOS
#elif defined(__linux__)
    #define CEM_OS_LINUX
#else
    #error "Unsupported OS. Supported: macOS, Linux"
#endif

// Check implementation status
#if defined(CEM_ARCH_ARM64) && defined(CEM_OS_MACOS)
    #define CEM_CONTEXT_IMPLEMENTED
#else
    #error "Context switching not yet implemented for this platform. Currently implemented: ARM64 macOS"
#endif

// ============================================================================
// Context Structure
// ============================================================================

/**
 * Platform-specific CPU context
 *
 * This stores all callee-saved registers needed for context switching.
 * The structure layout is architecture-specific and matches what the
 * assembly code expects.
 */
#ifdef CEM_ARCH_ARM64

/**
 * ARM64 Context Layout (AArch64 calling convention)
 *
 * Callee-saved registers that must be preserved:
 * - x19-x28: General purpose registers (10 registers)
 * - x29: Frame pointer (FP)
 * - x30: Link register (LR)
 * - sp: Stack pointer
 * - d8-d15: Floating point registers (8 registers)
 *
 * Total: 13 integer registers + 8 FP registers = 21 registers
 * Size: 13*8 + 8*8 = 104 + 64 = 168 bytes
 */
typedef struct {
    uint64_t x19, x20, x21, x22, x23, x24, x25, x26, x27, x28;  // Callee-saved GPRs
    uint64_t x29;  // Frame pointer
    uint64_t x30;  // Link register
    uint64_t sp;   // Stack pointer
    double   d8, d9, d10, d11, d12, d13, d14, d15;  // Callee-saved FP registers
} cem_context_t;

#elif defined(CEM_ARCH_X86_64)

/**
 * x86-64 Context Layout (System V AMD64 ABI)
 *
 * Callee-saved registers that must be preserved:
 * - rbx, rbp, r12, r13, r14, r15: General purpose (6 registers)
 * - rsp: Stack pointer
 *
 * Note: x86-64 doesn't require saving XMM registers as callee-saved
 * in the base ABI, but we may need to save MXCSR for FP state.
 *
 * Total: 7 registers
 * Size: 7*8 = 56 bytes
 */
typedef struct {
    uint64_t rbx;
    uint64_t rbp;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rsp;
    uint32_t mxcsr;  // FP control/status register
    uint32_t _padding;
} cem_context_t;

#endif

// ============================================================================
// Configuration Constants
// ============================================================================

/**
 * Minimum stack size for safe execution (Phase 3: Dynamic Growth)
 *
 * 4KB initial allocation per strand provides enough space for most
 * operations while keeping memory overhead low. Stacks grow dynamically
 * by doubling when needed.
 */
#define CEM_INITIAL_STACK_SIZE 4096

/**
 * Minimum free stack space to maintain (Phase 3)
 *
 * If free space falls below this threshold at a context switch checkpoint,
 * the stack will be grown proactively. This prevents sudden allocations
 * (large local arrays, deep recursion) from overflowing.
 *
 * 8KB provides headroom for most function calls with local variables.
 */
#define CEM_MIN_FREE_STACK 8192

/**
 * Stack usage threshold for proactive growth (Phase 3)
 *
 * If stack usage exceeds this percentage of total size, growth is triggered
 * at the next checkpoint even if free space is above CEM_MIN_FREE_STACK.
 *
 * 75% provides a good balance between memory efficiency and preventing overflow.
 */
#define CEM_STACK_GROWTH_THRESHOLD_PERCENT 75

/**
 * Maximum stack size (safety limit)
 *
 * Stacks will not grow beyond this size. If a strand needs more,
 * it will trigger a runtime error. This prevents runaway stack growth
 * from consuming all system memory.
 *
 * 1MB is generous for most strand operations while protecting against
 * pathological cases (infinite recursion, etc.)
 */
#define CEM_MAX_STACK_SIZE (1024 * 1024)

/**
 * Legacy compatibility constant
 *
 * CEM_MIN_STACK_SIZE is now an alias for CEM_INITIAL_STACK_SIZE.
 * Kept for backward compatibility with Phase 2b code.
 */
#define CEM_MIN_STACK_SIZE CEM_INITIAL_STACK_SIZE

// ============================================================================
// Context Switching API
// ============================================================================

/**
 * Save current context and switch to target context
 *
 * This is the core context switching primitive. It:
 * 1. Saves all callee-saved registers to `save_ctx`
 * 2. Restores all callee-saved registers from `restore_ctx`
 * 3. Continues execution from where `restore_ctx` was saved
 *
 * Assembly implementation is in context_<arch>.s
 *
 * @param save_ctx - Where to save current context
 * @param restore_ctx - Context to restore and switch to
 */
void cem_swapcontext(cem_context_t* save_ctx, const cem_context_t* restore_ctx);

/**
 * Initialize a context for a new strand
 *
 * INTERNAL API: This function should ONLY be called from strand_spawn().
 * Direct calls from user code are not supported and may cause undefined behavior.
 *
 * This sets up a context to start executing `func` with the given
 * stack. When `func` returns, control passes to `return_func`.
 *
 * C implementation is in context.c
 *
 * @param ctx - Context to initialize (must be non-NULL)
 * @param stack_base - Starting address of the C stack allocation (low address).
 *                     NOTE: Despite the name "base", this is the LOW address
 *                     of the stack memory. On ARM64/x86-64, stacks grow downward,
 *                     so the stack pointer will be set to (stack_base + stack_size)
 *                     which is the high address. Must be non-NULL.
 * @param stack_size - Size of the C stack in bytes (minimum CEM_MIN_STACK_SIZE).
 *                     Must be positive.
 * @param func - Function to execute (receives no args, returns void). Must be non-NULL.
 * @param return_func - Function to call when func returns. Currently unused because
 *                      strand_spawn() uses strand_entry_trampoline which handles
 *                      cleanup. This parameter exists for potential future use.
 */
void cem_makecontext(cem_context_t* ctx,
                     void* stack_base,
                     size_t stack_size,
                     void (*func)(void),
                     void (*return_func)(void));

// ============================================================================
// Platform-Specific Notes
// ============================================================================

/*
 * ARM64 macOS Notes:
 * - Stack must be 16-byte aligned at function entry
 * - Stack grows downward (high address to low address)
 * - x29 (FP) and x30 (LR) are used for stack frames and return addresses
 * - We store LR so we can return to the correct location on context switch
 *
 * x86-64 Notes (for future implementation):
 * - Stack must be 16-byte aligned before CALL instruction
 * - Red zone: 128 bytes below rsp can be used without adjustment
 * - Return address is on stack (not in register like ARM64)
 */

#endif // CEM_RUNTIME_CONTEXT_H
