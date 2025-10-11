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
 * - Context switching via setjmp/longjmp (portable C solution)
 *
 * Phase 1: Core scheduler with synthetic yields (no I/O yet)
 * Phase 2: I/O integration with io_uring/kqueue
 * Phase 3: go/wait primitives for explicit parallelism
 */

#ifndef CEM_RUNTIME_SCHEDULER_H
#define CEM_RUNTIME_SCHEDULER_H

#include "stack.h"
#include <setjmp.h>
#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// Strand State
// ============================================================================

/**
 * Strand execution states
 */
typedef enum {
    STRAND_READY,      // Ready to run (in ready queue)
    STRAND_RUNNING,    // Currently executing
    STRAND_YIELDED,    // Yielded (will be re-queued)
    STRAND_COMPLETED,  // Finished execution
} StrandState;

/**
 * Strand - A lightweight thread of execution
 *
 * Each strand maintains its own:
 * - Execution context (saved via setjmp/longjmp)
 * - Stack state (pointer to StackCell linked list)
 * - Current state (ready/running/yielded/completed)
 *
 * Memory layout considerations:
 * - The jmp_buf is platform-specific but typically 200-400 bytes
 * - Stack pointer is just a pointer to the StackCell list (8 bytes)
 * - Total struct size is small (< 512 bytes typically)
 */
typedef struct Strand {
    uint64_t id;              // Unique strand identifier
    StrandState state;        // Current execution state
    StackCell* stack;         // Strand's isolated stack
    jmp_buf context;          // Saved execution context
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
 * - Current strand: The currently executing strand
 * - Next strand ID: Counter for generating unique IDs
 *
 * Note: This is a single-threaded scheduler. All fields are accessed
 * from a single thread, so no locks are needed.
 */
typedef struct {
    Strand* ready_queue_head;   // Head of ready queue (FIFO)
    Strand* ready_queue_tail;   // Tail of ready queue (FIFO)
    Strand* current_strand;     // Currently executing strand
    uint64_t next_strand_id;    // Counter for strand IDs
    jmp_buf scheduler_context;  // Return point for scheduler loop
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
