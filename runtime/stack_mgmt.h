/**
 * Cem Runtime - Dynamic Stack Management (Phase 3)
 *
 * This module provides dynamic stack growth for C stacks used by strands.
 * Stacks start small (4KB) and grow automatically via:
 * 1. Checkpoint-based proactive growth (at context switches)
 * 2. Emergency guard page overflow detection (SIGSEGV handler)
 *
 * Design:
 * - Initial allocation: 4KB (CEM_INITIAL_STACK_SIZE)
 * - Growth factor: 2x (double size on each growth)
 * - Maximum size: 1MB (CEM_MAX_STACK_SIZE)
 * - Minimum free space: 8KB (CEM_MIN_FREE_STACK)
 * - Guard page: 1 page at stack bottom (PROT_NONE)
 *
 * Stack Layout (ARM64/x86-64, stacks grow downward):
 *
 *   High Address
 *   +------------------+
 *   |                  |
 *   |   Usable Stack   |  <- SP starts here
 *   |   (grows down)   |
 *   |                  |
 *   +------------------+  <- stack_base (low address)
 *   |   Guard Page     |  <- PROT_NONE (causes SIGSEGV if accessed)
 *   +------------------+
 *   Low Address
 *
 * Growth Strategy:
 * 1. At every context switch checkpoint, check stack usage
 * 2. If (free < MIN_FREE_STACK) or (used > 75% of total), grow proactively
 * 3. Growth: allocate new stack (2x size), memcpy old contents, update pointers
 * 4. If guard page is hit, SIGSEGV handler grows stack as emergency fallback
 * 5. If guard page is hit, log warning (indicates heuristic needs tuning)
 *
 * ============================================================================
 * THREAD SAFETY WARNING - CRITICAL ASSUMPTIONS
 * ============================================================================
 *
 * This implementation makes STRONG assumptions about the threading model:
 *
 * 1. **Single-threaded cooperative scheduler only**
 *    - Cem currently uses ONE OS thread for all strands (see README.md "Concurrency Model")
 *    - Cooperative multitasking with explicit yield points at I/O operations
 *    - No OS-level preemption between strands
 *    - Only ONE strand executes at a time
 *
 * 2. **Signal handler accesses global state WITHOUT locks**
 *    - The SIGSEGV handler reads g_scheduler and current_strand
 *    - This is SAFE ONLY because signals arrive synchronously during strand execution
 *    - The executing strand's metadata is stable (not being modified elsewhere)
 *
 * 3. **If you port Cem to multi-threading or preemptive scheduling:**
 *    - Add proper locking around scheduler state (g_scheduler, current_strand)
 *    - Use atomic operations for stack metadata updates
 *    - Consider per-thread signal handlers or disable signals during critical sections
 *    - Review ALL uses of global state in this module
 *
 * 4. **Stack growth operations are NOT reentrant**
 *    - If a signal interrupts stack_grow(), behavior is undefined
 *    - For production use, consider blocking signals during growth operations
 *
 * VIOLATING THESE ASSUMPTIONS WILL CAUSE DATA RACES AND CRASHES.
 * See stack_sigsegv_handler() in stack_mgmt.c for detailed thread safety notes.
 */

#ifndef CEM_RUNTIME_STACK_MGMT_H
#define CEM_RUNTIME_STACK_MGMT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "context.h"

// Forward declarations
struct Strand;
struct Scheduler;

// ============================================================================
// Stack Metadata
// ============================================================================

/**
 * Dynamic stack metadata
 *
 * Tracks the allocated stack region, guard page, and usage statistics.
 * Stored in Strand structure (Phase 3 replaces c_stack/c_stack_size).
 */
typedef struct {
    void* base;              // Base address (low address, includes guard page)
    void* usable_base;       // Start of usable stack (base + PAGE_SIZE)
    size_t total_size;       // Total allocated size (including guard page)
    size_t usable_size;      // Usable stack size (total_size - PAGE_SIZE)
    size_t guard_page_size;  // Size of guard page (usually 4KB or 16KB)

    // Growth statistics
    uint32_t growth_count;   // Number of times this stack has grown
    bool guard_hit;          // True if guard page was ever hit (warning flag)
} StackMetadata;

// ============================================================================
// Stack Allocation & Deallocation
// ============================================================================

/**
 * Allocate a new dynamic stack with guard page
 *
 * Uses mmap() to allocate a memory region with a guard page at the bottom.
 * The guard page has PROT_NONE protection, causing SIGSEGV if accessed.
 *
 * @param initial_size - Initial usable stack size (typically CEM_INITIAL_STACK_SIZE)
 * @return Stack metadata, or NULL on allocation failure
 */
StackMetadata* stack_alloc(size_t initial_size);

/**
 * Free a dynamic stack
 *
 * Unmaps the entire stack region including guard page.
 *
 * @param meta - Stack metadata to free
 */
void stack_free(StackMetadata* meta);

// ============================================================================
// Stack Growth
// ============================================================================

/**
 * Check if stack needs to grow and grow if necessary (checkpoint-based)
 *
 * Called at context switch checkpoints. Implements the hybrid growth strategy:
 * - Grow if free space < CEM_MIN_FREE_STACK (2KB), OR
 * - Grow if used > 75% of total
 *
 * This is the PRIMARY growth mechanism (not the signal handler).
 *
 * @param strand - Strand to check and potentially grow
 * @param current_sp - Current stack pointer value (from context)
 * @return true if stack was grown, false if no growth needed
 */
bool stack_check_and_grow(struct Strand* strand, uintptr_t current_sp);

/**
 * Grow a stack to a new size (internal)
 *
 * Allocates a new larger stack, copies contents, updates context pointers.
 * This is called by both checkpoint-based growth and emergency signal handler.
 *
 * When in_signal_handler is true, only async-signal-safe functions are used
 * (no fprintf, uses signal_safe_write instead).
 *
 * @param strand - Strand whose stack to grow
 * @param new_usable_size - New usable stack size (must be > current size)
 * @param in_signal_handler - true if called from SIGSEGV handler, false otherwise
 * @return true on success, false on failure
 */
bool stack_grow(struct Strand* strand, size_t new_usable_size, bool in_signal_handler);

// ============================================================================
// Emergency Guard Page Handler
// ============================================================================

/**
 * Initialize the SIGSEGV signal handler for guard page detection
 *
 * Installs a signal handler that catches stack overflows into guard pages
 * and attempts emergency stack growth. Should be called once during
 * scheduler initialization.
 *
 * The signal handler checks if the fault address is within any strand's
 * guard page. If so, it grows that strand's stack and returns.
 * If not, it re-raises the signal for normal crash handling.
 *
 * IMPORTANT - ASYNC-SIGNAL-SAFETY:
 * The signal handler is designed to be async-signal-safe:
 * - Uses write() instead of fprintf() for all output in signal context
 * - stack_grow() detects signal context and uses only signal-safe functions
 * - Calls mmap/munmap/memcpy (all async-signal-safe per POSIX)
 * - Accesses g_scheduler without locks (safe in cooperative single-threaded model)
 *
 * REMAINING LIMITATIONS:
 * - malloc/free in stack_alloc() are NOT async-signal-safe
 *   * Risk: If SIGSEGV arrives during malloc(), calling malloc() in handler may deadlock
 *   * Mitigation: Could pre-allocate emergency stack metadata (future work)
 * - If stack_grow() is interrupted by another signal, behavior is undefined
 *   * Mitigation: Could block signals during growth with sigprocmask() (future work)
 *
 * These limitations are acceptable for the current single-threaded cooperative
 * scheduler, but should be addressed if porting to multi-threaded or real-time use
 */
void stack_guard_init_signal_handler(void);

/**
 * Set global scheduler reference for signal handler (internal)
 *
 * The SIGSEGV handler needs access to the scheduler state to identify
 * which strand's guard page was hit. This function is called by
 * scheduler_init() to provide that access.
 *
 * @param scheduler - Pointer to global scheduler
 */
void stack_guard_set_scheduler(struct Scheduler* scheduler);

/**
 * Check if an address is within a strand's guard page (internal)
 *
 * Used by the SIGSEGV handler to determine if a fault is a stack overflow.
 *
 * @param addr - Fault address from SIGSEGV
 * @param meta - Stack metadata to check
 * @return true if addr is in the guard page
 */
bool stack_is_guard_page_fault(uintptr_t addr, const StackMetadata* meta);

// ============================================================================
// Stack Usage Statistics
// ============================================================================

/**
 * Calculate current stack usage
 *
 * @param meta - Stack metadata
 * @param current_sp - Current stack pointer
 * @return Number of bytes currently used on stack
 */
size_t stack_get_used(const StackMetadata* meta, uintptr_t current_sp);

/**
 * Calculate current free stack space
 *
 * @param meta - Stack metadata
 * @param current_sp - Current stack pointer
 * @return Number of bytes free (between SP and guard page)
 */
size_t stack_get_free(const StackMetadata* meta, uintptr_t current_sp);

/**
 * Get system page size (cached)
 *
 * Returns the system page size (e.g., 4KB on most systems, 16KB on Apple Silicon).
 * Cached after first call for performance.
 *
 * @return Page size in bytes
 */
size_t stack_get_page_size(void);

#endif // CEM_RUNTIME_STACK_MGMT_H
