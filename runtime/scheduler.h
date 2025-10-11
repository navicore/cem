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
 * - Context switching via ucontext (POSIX makecontext/swapcontext)
 *
 * IMPLEMENTATION PHASES:
 * =======================
 * Phase 1: Infrastructure ✅ (Completed)
 * - Scheduler initialization/shutdown
 * - Ready queue (FIFO operations)
 * - Strand state management structures
 *
 * Phase 2a: ucontext Context Switching (CURRENT)
 * - Replace jmp_buf with ucontext_t
 * - Implement strand_spawn() with makecontext()
 * - Implement strand_yield() with swapcontext()
 * - Implement scheduler_run() event loop
 * - 64KB stacks per strand
 * - Target: ~10,000 concurrent strands
 *
 * Phase 2b: Assembly Context Switching (FUTURE)
 * - Custom x86_64 and ARM64 implementations
 * - 8KB stacks per strand
 * - Target: ~100,000 concurrent strands
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
#include <ucontext.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

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
 * - Execution context (ucontext_t for context switching)
 * - C stack (malloced memory for C function calls, 64KB in Phase 2a)
 * - Cem stack (pointer to StackCell linked list)
 * - Current state (ready/running/yielded/completed)
 *
 * Memory layout considerations:
 * Phase 2a:
 * - ucontext_t: ~1KB (platform-specific)
 * - C stack: 64KB (allocated via malloc)
 * - StackCell pointer: 8 bytes
 * - Total per strand: ~65KB
 */
typedef struct Strand {
    uint64_t id;              // Unique strand identifier
    StrandState state;        // Current execution state
    StackCell* stack;         // Strand's isolated Cem stack
    ucontext_t context;       // Execution context for swapcontext
    void* c_stack;            // Allocated C stack (for context switching)
    size_t c_stack_size;      // Size of C stack (64KB in Phase 2a)

    // Entry function (for initial context setup)
    StackCell* (*entry_func)(StackCell*);  // Entry function for this strand

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
    ucontext_t scheduler_context; // Scheduler's main context
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
