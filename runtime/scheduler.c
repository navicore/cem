/**
 * Cem Runtime - Green Thread Scheduler Implementation
 *
 * This file implements the cooperative scheduler for Cem's green threads
 * (strands). The scheduler uses ucontext (makecontext/swapcontext) for
 * context switching and maintains a simple FIFO ready queue.
 *
 * Phase 2a Implementation (current):
 * - Basic strand structure with ucontext_t
 * - FIFO ready queue
 * - Context switching via makecontext/swapcontext
 * - 64KB C stacks per strand
 * - Target: ~10,000 concurrent strands
 *
 * See docs/SCHEDULER_IMPLEMENTATION.md for roadmap.
 */

#define _XOPEN_SOURCE 700
#include "scheduler.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/event.h>  // kqueue, kevent
#include <sys/time.h>   // struct timespec

// Forward declare close() to avoid including unistd.h (which conflicts with stack.h's dup())
extern int close(int);
extern int kqueue(void);

// ============================================================================
// Configuration Constants
// ============================================================================

// Phase 2a: 64KB stacks (generous for initial implementation)
// Phase 2b will reduce this to 8KB with assembly context switching
// Phase 3 will use 2-4KB with dynamic growth
#define STRAND_STACK_SIZE (64 * 1024)

// ============================================================================
// Global Scheduler State
// ============================================================================

static Scheduler global_scheduler;
static bool scheduler_initialized = false;

// ============================================================================
// Strand Management
// ============================================================================

/**
 * Allocate and initialize a new strand (without setting entry function)
 *
 * This allocates the strand structure and its C stack, but does not
 * initialize the context with an entry function. Use strand_init_context()
 * for that.
 */
static Strand* strand_alloc(uint64_t id, StackCell* initial_stack) {
    Strand* strand = (Strand*)malloc(sizeof(Strand));
    if (!strand) {
        runtime_error("strand_alloc: out of memory");
    }

    // Allocate C stack for context switching
    strand->c_stack = malloc(STRAND_STACK_SIZE);
    if (!strand->c_stack) {
        free(strand);
        runtime_error("strand_alloc: out of memory allocating stack");
    }

    strand->id = id;
    strand->state = STRAND_READY;
    strand->stack = initial_stack;
    strand->c_stack_size = STRAND_STACK_SIZE;
    strand->blocked_fd = -1;  // Not blocked on any FD initially
    strand->next = NULL;

    // Initialize context (but don't set entry function yet)
    memset(&strand->context, 0, sizeof(ucontext_t));

    return strand;
}

/**
 * Free a strand and its resources
 */
static void strand_free(Strand* strand) {
    if (!strand) return;

    // Free the Cem stack
    free_stack(strand->stack);

    // Free the C stack
    if (strand->c_stack) {
        free(strand->c_stack);
    }

    free(strand);
}

// ============================================================================
// Ready Queue Operations
// ============================================================================

void ready_queue_push(Strand* strand) {
    if (!strand) return;

    // Note: Don't modify the strand's state here - the caller sets it
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

    // Initialize kqueue for async I/O (macOS/FreeBSD)
    global_scheduler.kqueue_fd = kqueue();
    if (global_scheduler.kqueue_fd == -1) {
        runtime_error("scheduler_init: kqueue() failed");
    }

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

    // Free all strands in blocked list
    while (global_scheduler.blocked_list) {
        Strand* strand = global_scheduler.blocked_list;
        global_scheduler.blocked_list = strand->next;
        strand_free(strand);
    }

    // Free current strand if any
    if (global_scheduler.current_strand) {
        strand_free(global_scheduler.current_strand);
        global_scheduler.current_strand = NULL;
    }

    // Close kqueue
    if (global_scheduler.kqueue_fd != -1) {
        close(global_scheduler.kqueue_fd);
        global_scheduler.kqueue_fd = -1;
    }

    scheduler_initialized = false;
}

// ============================================================================
// Strand Spawning
// ============================================================================

/**
 * Trampoline function for strand entry
 *
 * makecontext() requires a function with int arguments, but we want to pass
 * a function pointer and stack pointer. We read these from the current strand
 * structure instead.
 */
static void strand_entry_trampoline(void) {
    // Get the current strand (set by scheduler before swapping to us)
    Strand* strand = global_scheduler.current_strand;
    if (!strand) {
        runtime_error("strand_entry_trampoline: no current strand");
    }

    // Get the entry function and initial stack from the strand
    StackCell* (*entry_func)(StackCell*) = strand->entry_func;
    StackCell* initial_stack = strand->stack;

    // Run the entry function
    StackCell* final_stack = entry_func(initial_stack);

    // Strand completed - update state
    strand->state = STRAND_COMPLETED;
    strand->stack = final_stack;

    // Return control to scheduler
    // The scheduler will see the COMPLETED state and clean up
    swapcontext(&strand->context, &global_scheduler.scheduler_context);
}

/**
 * Spawn a new strand
 *
 * Creates a new strand that will execute the given entry function with
 * the given initial stack. The strand is added to the ready queue.
 */
uint64_t strand_spawn(StackCell* (*entry_func)(StackCell*), StackCell* initial_stack) {
    if (!scheduler_initialized) {
        runtime_error("strand_spawn: scheduler not initialized");
    }

    if (!entry_func) {
        runtime_error("strand_spawn: entry_func is NULL");
    }

    // Allocate strand
    uint64_t id = global_scheduler.next_strand_id++;
    Strand* strand = strand_alloc(id, initial_stack);

    // Initialize context
    if (getcontext(&strand->context) == -1) {
        strand_free(strand);
        runtime_error("strand_spawn: getcontext failed");
    }

    // Store entry function in the strand for trampoline to use
    strand->entry_func = entry_func;

    // Set up context
    strand->context.uc_stack.ss_sp = strand->c_stack;
    strand->context.uc_stack.ss_size = strand->c_stack_size;
    strand->context.uc_link = NULL;  // Don't auto-return, let trampoline handle it

    // Create context (makecontext only accepts int args, so we use trampoline)
    makecontext(&strand->context, strand_entry_trampoline, 0);

    // Add to ready queue
    ready_queue_push(strand);

    return id;
}

// ============================================================================
// Yielding
// ============================================================================

/**
 * Yield execution from the current strand back to the scheduler
 *
 * This cooperatively yields control, allowing other strands to run.
 * The current strand is re-queued as READY and will be rescheduled later.
 */
void strand_yield(void) {
    if (!scheduler_initialized) {
        runtime_error("strand_yield: scheduler not initialized");
    }

    if (!global_scheduler.current_strand) {
        runtime_error("strand_yield: no current strand (must be called from within a strand)");
    }

    Strand* strand = global_scheduler.current_strand;
    strand->state = STRAND_YIELDED;

    // Re-queue this strand for later execution
    ready_queue_push(strand);

    // Clear current strand (scheduler will pick it up again later)
    global_scheduler.current_strand = NULL;

    // Switch back to scheduler context
    // The scheduler will resume us later when we're popped from the ready queue
    swapcontext(&strand->context, &global_scheduler.scheduler_context);

    // When we resume, execution continues here
    // The strand state will have been set back to RUNNING by the scheduler
}

// ============================================================================
// I/O Blocking Operations
// ============================================================================

/**
 * Helper: Add a strand to the blocked list
 */
static void blocked_list_add(Strand* strand) {
    if (!strand) return;

    strand->next = global_scheduler.blocked_list;
    global_scheduler.blocked_list = strand;
}

/**
 * Helper: Remove a specific strand from the blocked list
 * Returns true if found and removed, false otherwise
 */
static bool blocked_list_remove(Strand* strand) {
    if (!strand || !global_scheduler.blocked_list) return false;

    // Check if it's the head
    if (global_scheduler.blocked_list == strand) {
        global_scheduler.blocked_list = strand->next;
        strand->next = NULL;
        return true;
    }

    // Search the rest of the list
    Strand* prev = global_scheduler.blocked_list;
    Strand* curr = prev->next;

    while (curr) {
        if (curr == strand) {
            prev->next = curr->next;
            curr->next = NULL;
            return true;
        }
        prev = curr;
        curr = curr->next;
    }

    return false;
}

/**
 * Block current strand on read I/O
 */
void strand_block_on_read(int fd) {
    if (!scheduler_initialized) {
        runtime_error("strand_block_on_read: scheduler not initialized");
    }
    if (!global_scheduler.current_strand) {
        runtime_error("strand_block_on_read: no current strand");
    }
    if (fd < 0) {
        runtime_error("strand_block_on_read: invalid file descriptor");
    }

    Strand* strand = global_scheduler.current_strand;
    strand->state = STRAND_BLOCKED_READ;
    strand->blocked_fd = fd;

    // Register for read events with kqueue
    struct kevent ev;
    EV_SET(&ev, fd, EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0, strand);
    if (kevent(global_scheduler.kqueue_fd, &ev, 1, NULL, 0, NULL) == -1) {
        runtime_error("strand_block_on_read: kevent registration failed");
    }

    // Add to blocked list
    blocked_list_add(strand);
    global_scheduler.current_strand = NULL;

    // Switch back to scheduler
    swapcontext(&strand->context, &global_scheduler.scheduler_context);

    // When we resume, clear the blocked_fd
    strand->blocked_fd = -1;
}

/**
 * Block current strand on write I/O
 */
void strand_block_on_write(int fd) {
    if (!scheduler_initialized) {
        runtime_error("strand_block_on_write: scheduler not initialized");
    }
    if (!global_scheduler.current_strand) {
        runtime_error("strand_block_on_write: no current strand");
    }
    if (fd < 0) {
        runtime_error("strand_block_on_write: invalid file descriptor");
    }

    Strand* strand = global_scheduler.current_strand;
    strand->state = STRAND_BLOCKED_WRITE;
    strand->blocked_fd = fd;

    // Register for write events with kqueue
    struct kevent ev;
    EV_SET(&ev, fd, EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0, strand);
    if (kevent(global_scheduler.kqueue_fd, &ev, 1, NULL, 0, NULL) == -1) {
        runtime_error("strand_block_on_write: kevent registration failed");
    }

    // Add to blocked list
    blocked_list_add(strand);
    global_scheduler.current_strand = NULL;

    // Switch back to scheduler
    swapcontext(&strand->context, &global_scheduler.scheduler_context);

    // When we resume, clear the blocked_fd
    strand->blocked_fd = -1;
}

// ============================================================================
// Scheduler Main Loop
// ============================================================================

/**
 * Run the scheduler until all strands complete
 *
 * This is the main scheduler loop that:
 * 1. Saves the scheduler's context (so strands can return here)
 * 2. Picks the next ready strand
 * 3. Switches to that strand
 * 4. When strand yields or completes, loop continues
 * 5. Returns when all strands are complete
 *
 * The scheduler uses cooperative multitasking - strands must explicitly
 * call strand_yield() or complete to give control back.
 */
StackCell* scheduler_run(void) {
    if (!scheduler_initialized) {
        runtime_error("scheduler_run: scheduler not initialized");
    }

    // Initialize scheduler context
    // This is the "home base" that strands return to when they yield
    if (getcontext(&global_scheduler.scheduler_context) == -1) {
        runtime_error("scheduler_run: getcontext failed");
    }

    // Main scheduler loop
    while (true) {
        // If we have ready strands, run them
        if (!ready_queue_is_empty()) {
            // Pop next ready strand
            Strand* strand = ready_queue_pop();
            if (!strand) {
                break;  // No more strands (shouldn't happen if queue wasn't empty)
            }

            // Mark as running
            strand->state = STRAND_RUNNING;
            global_scheduler.current_strand = strand;

            // Switch to strand
            // When strand yields or completes, we'll return here
            swapcontext(&global_scheduler.scheduler_context, &strand->context);

            // We're back from the strand
            // Check its state to see what happened
            if (strand->state == STRAND_COMPLETED) {
                // Strand finished - clean it up
                StackCell* final_stack = strand->stack;
                strand->stack = NULL;  // Don't free the stack, we'll return it

                // If this is the last strand and it's the main one, return its stack
                if (ready_queue_is_empty() && !global_scheduler.blocked_list && strand->id == 1) {
                    StackCell* result = final_stack;
                    strand_free(strand);
                    global_scheduler.current_strand = NULL;
                    return result;
                }

                // Otherwise just free it
                free_stack(final_stack);
                strand_free(strand);
                global_scheduler.current_strand = NULL;
            } else if (strand->state == STRAND_YIELDED) {
                // Strand yielded - it already re-queued itself
                // Nothing to do here, just continue to next iteration
            } else if (strand->state == STRAND_BLOCKED_READ || strand->state == STRAND_BLOCKED_WRITE) {
                // Strand blocked on I/O - it already added itself to blocked list and registered with kqueue
                // Nothing to do here, continue to next iteration
            } else {
                // Unexpected state
                runtime_error("scheduler_run: strand in unexpected state after context switch");
            }
        } else if (global_scheduler.blocked_list) {
            // No ready strands, but we have blocked strands waiting for I/O
            // Wait for I/O events with kqueue
            struct kevent events[32];  // Handle up to 32 events at once
            int nevents = kevent(global_scheduler.kqueue_fd, NULL, 0, events, 32, NULL);

            if (nevents == -1) {
                runtime_error("scheduler_run: kevent wait failed");
            }

            // Process events - move strands from blocked to ready
            for (int i = 0; i < nevents; i++) {
                Strand* strand = (Strand*)events[i].udata;
                if (!strand) continue;

                // Remove from blocked list
                blocked_list_remove(strand);

                // Mark as ready and add to ready queue
                strand->state = STRAND_READY;
                ready_queue_push(strand);
            }
        } else {
            // No ready strands and no blocked strands - we're done
            break;
        }
    }

    // All strands completed
    global_scheduler.current_strand = NULL;
    return NULL;
}

// ============================================================================
// Testing Functions
// ============================================================================

/**
 * test_yield - Synthetic yield for testing
 *
 * This is a runtime function callable from Cem code to test the scheduler.
 * It cooperatively yields execution to other strands.
 *
 * Usage in Cem: `test_yield`
 * Stack effect: ( -- )
 */
StackCell* test_yield(StackCell* stack) {
    // If we're in a strand, yield to the scheduler
    if (global_scheduler.current_strand) {
        strand_yield();
    }
    // Otherwise, we're not in a strand context, so just return
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
