/**
 * Cem Runtime - Green Thread Scheduler
 *
 * This header defines the scheduler for lightweight concurrent execution
 * (strands) in the Cem runtime. The scheduler provides cooperative multitasking
 * with explicit yield points at I/O operations.
 *
 * Architecture:
 * - Each strand has its own isolated stack (StackCell linked list)
 * - Strands yield only at I/O operations (cooperative, not preemptive)
 * - Simple FIFO ready queue for runnable strands
 * - Fast context switching via custom assembly (cem_makecontext/cem_swapcontext)
 *
 * IMPLEMENTATION PHASES:
 * =======================
 * Phase 1: Infrastructure ✅ (Completed)
 * - Scheduler initialization/shutdown
 * - Ready queue (FIFO operations)
 * - Strand state management structures
 *
 * Phase 2a: ucontext Context Switching ✅ (Completed)
 * - Replaced jmp_buf with ucontext_t
 * - Implemented strand_spawn() with makecontext()
 * - Implemented strand_yield() with swapcontext()
 * - Implemented scheduler_run() event loop
 * - 64KB stacks per strand
 * - Achieved: ~10,000 concurrent strands
 *
 * Phase 2b: Custom Assembly Context Switching (CURRENT)
 * - Custom ARM64 and x86-64 implementations
 * - Replace deprecated ucontext with cem_swapcontext/cem_makecontext
 * - 10-20x faster context switches (~10-20ns vs ~500ns)
 * - Add cleanup handler infrastructure to fix memory leaks
 * - 64KB stacks per strand (will reduce to 8KB in Phase 2c)
 * - Target: ~10,000 concurrent strands (same as 2a, but faster and leak-free)
 *
 * Phase 3: Dynamic Stack Growth (FUTURE)
 * - Segmented stacks with growth on overflow
 * - 2-4KB initial stacks
 * - Target: 500,000+ concurrent strands
 *
 * See docs/SCHEDULER_IMPLEMENTATION.md for detailed roadmap
 */

#ifndef CEM_RUNTIME_SCHEDULER_H
#define CEM_RUNTIME_SCHEDULER_H

#include "stack.h"
#include "context.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ============================================================================
// Cleanup Handlers
// ============================================================================

/**
 * Cleanup handler function type
 *
 * Called when a strand terminates (either normally or abnormally).
 * Used to free resources allocated during strand execution.
 *
 * @param arg - Arbitrary data passed to the cleanup function
 */
typedef void (*CleanupFunc)(void* arg);

/**
 * Cleanup handler node
 *
 * Cleanup handlers are stored in a LIFO linked list (stack order).
 * When a strand terminates, handlers are called in reverse order of registration
 * (most recently registered first).
 */
typedef struct CleanupHandler {
    CleanupFunc func;              // Function to call on cleanup
    void* arg;                     // Argument to pass to function
    struct CleanupHandler* next;   // Next handler in list
} CleanupHandler;

// ============================================================================
// Strand State
// ============================================================================

/**
 * Strand execution states
 */
typedef enum {
    STRAND_READY,          // Ready to run (in ready queue)
    STRAND_RUNNING,        // Currently executing
    STRAND_YIELDED,        // Yielded (will be re-queued)
    STRAND_COMPLETED,      // Finished execution
    STRAND_BLOCKED_READ,   // Blocked waiting for readable I/O
    STRAND_BLOCKED_WRITE,  // Blocked waiting for writable I/O
} StrandState;

/**
 * Strand - A lightweight thread of execution
 *
 * Each strand maintains its own:
 * - Execution context (cem_context_t for context switching)
 * - C stack (malloced memory for C function calls, 64KB in Phase 2b)
 * - Cem stack (pointer to StackCell linked list)
 * - Current state (ready/running/yielded/completed)
 *
 * Memory layout considerations:
 * Phase 2b:
 * - cem_context_t: 168 bytes (ARM64) or 64 bytes (x86-64)
 * - C stack: 64KB (allocated via malloc)
 * - StackCell pointer: 8 bytes
 * - Total per strand: ~64.2KB
 */
typedef struct Strand {
    uint64_t id;              // Unique strand identifier
    StrandState state;        // Current execution state
    StackCell* stack;         // Strand's isolated Cem stack
    cem_context_t context;    // Execution context for context switching
    void* c_stack;            // Allocated C stack (for context switching)
    size_t c_stack_size;      // Size of C stack (64KB in Phase 2b)

    // Entry function (for initial context setup)
    StackCell* (*entry_func)(StackCell*);  // Entry function for this strand

    // Cleanup handlers (for resource management)
    CleanupHandler* cleanup_handlers;  // LIFO list of cleanup handlers

    // I/O blocking state
    int blocked_fd;           // File descriptor when blocked on I/O (-1 if not blocked)

    struct Strand* next;      // Next strand in queue (linked list)
} Strand;

// ============================================================================
// Scheduler State
// ============================================================================

/**
 * Global scheduler state
 *
 * The scheduler maintains:
 * - Ready queue: FIFO queue of runnable strands
 * - Blocked list: Strands waiting for I/O events
 * - Current strand: The currently executing strand
 * - Next strand ID: Counter for generating unique IDs
 * - Scheduler context: The main scheduler's execution context
 * - kqueue fd: BSD kqueue for async I/O event notifications (macOS/FreeBSD)
 *
 * Note: This is a single-threaded scheduler. All fields are accessed
 * from a single thread, so no locks are needed.
 */
typedef struct {
    Strand* ready_queue_head;   // Head of ready queue (FIFO)
    Strand* ready_queue_tail;   // Tail of ready queue (FIFO)
    Strand* blocked_list;       // Linked list of strands blocked on I/O
    Strand* current_strand;     // Currently executing strand
    uint64_t next_strand_id;    // Counter for strand IDs
    cem_context_t scheduler_context; // Scheduler's main context
    int kqueue_fd;              // kqueue descriptor for async I/O (-1 if not initialized)
} Scheduler;

// ============================================================================
// Scheduler Operations
// ============================================================================

/**
 * Initialize the global scheduler
 * Must be called before any other scheduler operations
 */
void scheduler_init(void);

/**
 * Shutdown the scheduler and free all resources
 */
void scheduler_shutdown(void);

/**
 * Create a new strand to execute the given function
 *
 * The function receives the strand's initial stack and returns
 * the final stack state. The strand will be added to the ready queue.
 *
 * @param entry_func - Function to execute in the strand
 * @param initial_stack - Initial stack state for the strand
 * @return Strand ID
 */
uint64_t strand_spawn(StackCell* (*entry_func)(StackCell*), StackCell* initial_stack);

/**
 * Register a cleanup handler for the current strand
 *
 * The cleanup handler will be called when the strand terminates (either
 * normally or abnormally). Handlers are called in LIFO order (most recently
 * registered first).
 *
 * This is used to ensure resources are freed even if a strand is terminated
 * while blocked on I/O or otherwise interrupted.
 *
 * IMPORTANT: This must only be called from within a strand.
 *
 * @param func - Cleanup function to call
 * @param arg - Argument to pass to cleanup function
 */
void strand_push_cleanup(CleanupFunc func, void* arg);

/**
 * Remove the most recently registered cleanup handler
 *
 * This is called when the resource has been successfully released and
 * cleanup is no longer needed.
 *
 * IMPORTANT: This must only be called from within a strand.
 */
void strand_pop_cleanup(void);

/**
 * Yield execution from the current strand back to the scheduler
 *
 * This saves the current execution context and transfers control
 * to the scheduler, which will schedule the next ready strand.
 * The current strand will be re-queued as READY.
 *
 * IMPORTANT: This must only be called from within a strand, not from
 * the main scheduler loop.
 */
void strand_yield(void);

/**
 * Block current strand on I/O read operation
 *
 * This saves the current execution context, registers the file descriptor
 * for read events with kqueue, and transfers control back to the scheduler.
 * When the FD becomes readable, the strand will be moved to the ready queue.
 *
 * @param fd - File descriptor to wait for (must be in non-blocking mode)
 */
void strand_block_on_read(int fd);

/**
 * Block current strand on I/O write operation
 *
 * This saves the current execution context, registers the file descriptor
 * for write events with kqueue, and transfers control back to the scheduler.
 * When the FD becomes writable, the strand will be moved to the ready queue.
 *
 * @param fd - File descriptor to wait for (must be in non-blocking mode)
 */
void strand_block_on_write(int fd);

/**
 * Run the scheduler until all strands complete
 *
 * This is the main scheduler loop:
 * 1. Pop a strand from the ready queue
 * 2. Run it until it yields or completes
 * 3. If it yielded, re-queue it
 * 4. Repeat until no strands remain
 *
 * @return Final stack state (from main strand, if any)
 */
StackCell* scheduler_run(void);

// ============================================================================
// Testing & Debug Operations
// ============================================================================

/**
 * Synthetic yield for testing (Phase 1)
 *
 * This is a runtime function callable from Cem code to test
 * the scheduler without implementing full I/O operations.
 *
 * Usage in Cem: `test_yield`
 * Stack effect: ( -- )
 */
StackCell* test_yield(StackCell* stack);

/**
 * Print scheduler state (for debugging)
 */
void scheduler_debug_print(void);

// ============================================================================
// Internal Functions (exposed for testing)
// ============================================================================

/**
 * Enqueue a strand to the ready queue
 */
void ready_queue_push(Strand* strand);

/**
 * Dequeue a strand from the ready queue
 * Returns NULL if queue is empty
 */
Strand* ready_queue_pop(void);

/**
 * Check if ready queue is empty
 */
bool ready_queue_is_empty(void);

#endif // CEM_RUNTIME_SCHEDULER_H
