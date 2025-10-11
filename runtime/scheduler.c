/**
 * Cem Runtime - Green Thread Scheduler Implementation
 *
 * This file implements the cooperative scheduler for Cem's green threads
 * (strands). The scheduler uses setjmp/longjmp for context switching and
 * maintains a simple FIFO ready queue.
 *
 * Phase 1 Implementation (current):
 * - Basic strand structure
 * - FIFO ready queue
 * - Context switching via setjmp/longjmp
 * - Synthetic yields for testing
 *
 * Future phases will add I/O integration and go/wait primitives.
 */

#define _POSIX_C_SOURCE 200809L
#include "scheduler.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// ============================================================================
// Global Scheduler State
// ============================================================================

static Scheduler global_scheduler;
static bool scheduler_initialized = false;

// ============================================================================
// Strand Management
// ============================================================================

/**
 * Allocate and initialize a new strand
 */
static Strand* strand_alloc(uint64_t id, StackCell* initial_stack) {
    Strand* strand = (Strand*)malloc(sizeof(Strand));
    if (!strand) {
        runtime_error("strand_alloc: out of memory");
    }

    strand->id = id;
    strand->state = STRAND_READY;
    strand->stack = initial_stack;
    strand->next = NULL;

    // Note: context will be set via setjmp when strand first runs
    memset(&strand->context, 0, sizeof(jmp_buf));

    return strand;
}

/**
 * Free a strand and its resources
 */
static void strand_free(Strand* strand) {
    if (!strand) return;

    // Free the strand's stack
    free_stack(strand->stack);

    free(strand);
}

// ============================================================================
// Ready Queue Operations
// ============================================================================

void ready_queue_push(Strand* strand) {
    if (!strand) return;

    strand->state = STRAND_READY;
    strand->next = NULL;

    if (global_scheduler.ready_queue_tail) {
        // Queue has elements, append to tail
        global_scheduler.ready_queue_tail->next = strand;
        global_scheduler.ready_queue_tail = strand;
    } else {
        // Queue is empty
        global_scheduler.ready_queue_head = strand;
        global_scheduler.ready_queue_tail = strand;
    }
}

Strand* ready_queue_pop(void) {
    if (!global_scheduler.ready_queue_head) {
        return NULL;  // Queue is empty
    }

    Strand* strand = global_scheduler.ready_queue_head;
    global_scheduler.ready_queue_head = strand->next;

    // If we popped the last element, update tail
    if (!global_scheduler.ready_queue_head) {
        global_scheduler.ready_queue_tail = NULL;
    }

    strand->next = NULL;
    return strand;
}

bool ready_queue_is_empty(void) {
    return global_scheduler.ready_queue_head == NULL;
}

// ============================================================================
// Scheduler Initialization
// ============================================================================

void scheduler_init(void) {
    if (scheduler_initialized) {
        runtime_error("scheduler_init: scheduler already initialized");
    }

    memset(&global_scheduler, 0, sizeof(Scheduler));
    global_scheduler.next_strand_id = 1;  // Start IDs at 1 (0 reserved for main)

    scheduler_initialized = true;
}

void scheduler_shutdown(void) {
    if (!scheduler_initialized) {
        return;  // Already shutdown or never initialized
    }

    // Free all strands in ready queue
    while (!ready_queue_is_empty()) {
        Strand* strand = ready_queue_pop();
        strand_free(strand);
    }

    // Free current strand if any
    if (global_scheduler.current_strand) {
        strand_free(global_scheduler.current_strand);
        global_scheduler.current_strand = NULL;
    }

    scheduler_initialized = false;
}

// ============================================================================
// Strand Spawning
// ============================================================================

// ============================================================================
// Phase 1 Note: Simplified Strand Model
// ============================================================================
//
// For Phase 1, we implement a simplified model:
// - Only the main execution thread (not true strand spawning yet)
// - Focus on testing yield mechanism and scheduler infrastructure
// - Future phases will add proper context initialization via makecontext
//   or platform-specific assembly
//
// This gives us:
// ✓ Scheduler infrastructure (ready queue, state machine)
// ✓ Basic context structures and lifecycle functions
// ✓ Test harness for validation (test_yield linkage)
//
// NOT yet implemented:
// ✗ True concurrent strand spawning
// ✗ Multiple isolated stacks
// ✗ go/wait primitives
// ✗ Functional context switching (setjmp/longjmp state not initialized)
//
// IMPORTANT: strand_spawn() and strand_yield() are NON-FUNCTIONAL stubs.
// They will be properly implemented in Phase 2 when I/O integration
// requires actual concurrency. For now, they exist to:
// - Validate API design
// - Allow test_yield() linkage testing
// - Provide structure for future implementation

#ifdef PHASE_2_CONCURRENCY
// Phase 2: Full implementation (not yet enabled)
uint64_t strand_spawn(StackCell* (*entry_func)(StackCell*), StackCell* initial_stack) {
    if (!scheduler_initialized) {
        runtime_error("strand_spawn: scheduler not initialized");
    }

    uint64_t id = global_scheduler.next_strand_id++;
    Strand* strand = strand_alloc(id, initial_stack);

    ready_queue_push(strand);
    return id;
}
#else
// Phase 1: Stub implementation
uint64_t strand_spawn(StackCell* (*entry_func)(StackCell*), StackCell* initial_stack) {
    (void)entry_func;
    (void)initial_stack;
    runtime_error("strand_spawn: not implemented in Phase 1 (requires PHASE_2_CONCURRENCY)");
    return 0;
}
#endif

// ============================================================================
// Yielding
// ============================================================================

#ifdef PHASE_2_CONCURRENCY
// Phase 2: Full implementation (not yet enabled)
void strand_yield(void) {
    if (!scheduler_initialized) {
        runtime_error("strand_yield: scheduler not initialized");
    }

    if (!global_scheduler.current_strand) {
        runtime_error("strand_yield: no current strand (must be called from within a strand)");
    }

    Strand* strand = global_scheduler.current_strand;
    strand->state = STRAND_YIELDED;

    // Save current context and return to scheduler
    if (setjmp(strand->context) == 0) {
        // First call: returning to scheduler
        longjmp(global_scheduler.scheduler_context, 1);
    }

    // Second call: resumed from scheduler
    // Execution continues here after the strand is rescheduled
}
#else
// Phase 1: Stub implementation (non-functional)
// NOTE: This function is not usable in Phase 1 because:
// - global_scheduler.scheduler_context is never initialized
// - No scheduler loop to return to
// - Calling this would cause undefined behavior (longjmp to uninitialized jmp_buf)
//
// It exists as a stub to validate the API design. Do not call directly.
// Use test_yield() instead, which is a safe no-op for testing.
void strand_yield(void) {
    runtime_error("strand_yield: not functional in Phase 1 (use test_yield for testing)");
}
#endif

// ============================================================================
// Scheduler Main Loop
// ============================================================================

/**
 * Phase 1: Simplified execution model
 *
 * For now, scheduler_run() is a placeholder that will be used in Phase 2
 * when we integrate I/O operations. The main program runs directly without
 * going through the scheduler.
 *
 * The yield mechanism (test_yield) still works for testing purposes, but
 * yields immediately return since there are no other strands to switch to.
 */
StackCell* scheduler_run(void) {
    if (!scheduler_initialized) {
        runtime_error("scheduler_run: scheduler not initialized");
    }

    // Phase 1: No strands to run yet
    // This will be implemented properly in Phase 2 with I/O integration
    return NULL;
}

// ============================================================================
// Testing Functions
// ============================================================================

/**
 * test_yield - Synthetic yield for testing (Phase 1)
 *
 * In Phase 1, this is a no-op since we don't have true multitasking yet.
 * It will become functional in Phase 2 when I/O operations are integrated.
 *
 * For now, it serves as a placeholder to ensure the runtime linkage works
 * and to allow testing programs to be written that will work once the
 * scheduler is fully implemented.
 */
StackCell* test_yield(StackCell* stack) {
    // Phase 1: No-op yield
    // Just return the stack unchanged
    // In Phase 2, this will call strand_yield() when within an I/O operation
    return stack;
}

void scheduler_debug_print(void) {
    printf("Scheduler state:\n");
    printf("  Initialized: %s\n", scheduler_initialized ? "true" : "false");
    printf("  Current strand: %llu\n",
           global_scheduler.current_strand ?
           (unsigned long long)global_scheduler.current_strand->id : 0);
    printf("  Next strand ID: %llu\n", (unsigned long long)global_scheduler.next_strand_id);

    printf("  Ready queue: ");
    if (ready_queue_is_empty()) {
        printf("(empty)\n");
    } else {
        Strand* s = global_scheduler.ready_queue_head;
        while (s) {
            printf("%llu ", (unsigned long long)s->id);
            s = s->next;
        }
        printf("\n");
    }
}
