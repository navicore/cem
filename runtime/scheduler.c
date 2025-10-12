/**
 * Cem Runtime - Green Thread Scheduler Implementation
 *
 * This file implements the cooperative scheduler for Cem's green threads
 * (strands). The scheduler uses custom assembly context switching
 * (cem_makecontext/cem_swapcontext) for fast, portable context switches.
 *
 * Phase 3 Implementation (current):
 * - Strand structure with cem_context_t
 * - FIFO ready queue
 * - Fast context switching via custom assembly (ARM64/x86-64)
 * - Cleanup handler infrastructure for resource management
 * - Dynamic C stacks: 4KB initial, grows to 1MB max
 * - Checkpoint-based proactive growth + emergency guard page
 * - Target: 500,000+ concurrent strands (Erlang-scale!)
 *
 * See docs/SCHEDULER_IMPLEMENTATION.md for roadmap.
 */

// Platform detection for I/O multiplexing
#if defined(__linux__)
    #define USE_EPOLL
    #include <sys/epoll.h>
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    #define USE_KQUEUE
    #include <sys/event.h>  // kqueue, kevent
    #include <sys/time.h>   // struct timespec
#else
    #error "Unsupported platform. Requires kqueue (BSD/macOS) or epoll (Linux) support."
#endif

#include "scheduler.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// Forward declare close() to avoid including unistd.h (which conflicts with stack.h's dup())
extern int close(int);

#ifdef USE_KQUEUE
extern int kqueue(void);
#endif

// ============================================================================
// Configuration Constants
// ============================================================================

// Phase 3: Dynamic stacks - configuration moved to context.h
// (CEM_INITIAL_STACK_SIZE, CEM_MIN_FREE_STACK, CEM_MAX_STACK_SIZE)

// Maximum number of I/O events to process per event loop iteration
// Larger values process more events per syscall but increase latency
#define MAX_IO_EVENTS 32

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
 * This allocates the strand structure and its dynamic C stack with guard page.
 * The stack starts at 4KB and grows automatically up to 1MB as needed.
 * Use strand_init_context() to initialize the context with an entry function.
 */
static Strand* strand_alloc(uint64_t id, StackCell* initial_stack) {
    Strand* strand = (Strand*)malloc(sizeof(Strand));
    if (!strand) {
        runtime_error("strand_alloc: out of memory");
    }

    // Allocate dynamic C stack with guard page (Phase 3)
    strand->stack_meta = stack_alloc(CEM_INITIAL_STACK_SIZE);
    if (!strand->stack_meta) {
        free(strand);
        runtime_error("strand_alloc: failed to allocate dynamic stack");
    }

    strand->id = id;
    strand->state = STRAND_READY;
    strand->stack = initial_stack;
    strand->cleanup_handlers = NULL;  // No cleanup handlers initially
    strand->blocked_fd = -1;  // Not blocked on any FD initially
    strand->next = NULL;

    // Initialize context (will be set up later with cem_makecontext)
    memset(&strand->context, 0, sizeof(cem_context_t));

    return strand;
}

/**
 * Run all cleanup handlers for a strand
 *
 * Handlers are called in LIFO order (most recently registered first).
 * This ensures proper cleanup ordering (e.g., inner resources freed before outer).
 */
static void strand_run_cleanup_handlers(Strand* strand) {
    if (!strand) return;

    CleanupHandler* handler = strand->cleanup_handlers;
    while (handler) {
        CleanupHandler* next = handler->next;

        // Call the cleanup function
        // NOTE: handler->func should never be NULL if the API is used correctly,
        // but we check anyway for defensive programming
        if (handler->func) {
            handler->func(handler->arg);
        } else {
            // This should never happen - it indicates a bug in cleanup handler registration
            fprintf(stderr, "WARNING: cleanup handler with NULL function pointer\n");
        }

        // Free the handler node itself
        free(handler);

        handler = next;
    }

    strand->cleanup_handlers = NULL;
}

/**
 * Free a strand and its resources
 */
static void strand_free(Strand* strand) {
    if (!strand) return;

    // Run cleanup handlers first (frees any resources allocated by the strand)
    strand_run_cleanup_handlers(strand);

    // Free the Cem stack
    free_stack(strand->stack);

    // Free the dynamic C stack with guard page (Phase 3)
    if (strand->stack_meta) {
        stack_free(strand->stack_meta);
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

    // Initialize I/O multiplexing
#ifdef USE_KQUEUE
    global_scheduler.kqueue_fd = kqueue();
    if (global_scheduler.kqueue_fd == -1) {
        runtime_error("scheduler_init: kqueue() failed");
    }
#elif defined(USE_EPOLL)
    global_scheduler.epoll_fd = epoll_create1(0);
    if (global_scheduler.epoll_fd == -1) {
        perror("scheduler_init: epoll_create1() failed");
        runtime_error("scheduler_init: Failed to create epoll instance");
    }
#endif

    // Initialize dynamic stack management (Phase 3)
    // Set up SIGSEGV handler for emergency guard page overflow
    stack_guard_init_signal_handler();
    stack_guard_set_scheduler(&global_scheduler);

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

    // Close I/O multiplexing descriptor
#ifdef USE_KQUEUE
    if (global_scheduler.kqueue_fd != -1) {
        close(global_scheduler.kqueue_fd);
        global_scheduler.kqueue_fd = -1;
    }
#elif defined(USE_EPOLL)
    if (global_scheduler.epoll_fd != -1) {
        close(global_scheduler.epoll_fd);
        global_scheduler.epoll_fd = -1;
    }
#endif

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
    cem_swapcontext(&strand->context, &global_scheduler.scheduler_context);
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

    // Store entry function in the strand for trampoline to use
    strand->entry_func = entry_func;

    // Initialize context with dynamic stack (Phase 3)
    // Set up the context to call strand_entry_trampoline when first switched to
    cem_makecontext(&strand->context,
                    strand->stack_meta->usable_base,
                    strand->stack_meta->usable_size,
                    strand_entry_trampoline,
                    NULL);  // No return function needed (trampoline handles completion)

    // Add to ready queue
    ready_queue_push(strand);

    return id;
}

// ============================================================================
// Cleanup Handlers
// ============================================================================

/**
 * Register a cleanup handler for the current strand
 *
 * The handler will be called when the strand terminates.
 * Handlers are stored in LIFO order (stack).
 */
void strand_push_cleanup(CleanupFunc func, void* arg) {
    if (!scheduler_initialized) {
        runtime_error("strand_push_cleanup: scheduler not initialized");
    }

    Strand* strand = global_scheduler.current_strand;
    if (!strand) {
        runtime_error("strand_push_cleanup: no current strand");
    }

    // Validate function pointer - NULL cleanup function makes no sense
    if (!func) {
        runtime_error("strand_push_cleanup: cleanup function cannot be NULL");
    }

    // Allocate cleanup handler node
    CleanupHandler* handler = (CleanupHandler*)malloc(sizeof(CleanupHandler));
    if (!handler) {
        runtime_error("strand_push_cleanup: out of memory");
    }

    handler->func = func;
    handler->arg = arg;
    handler->next = strand->cleanup_handlers;

    // Push onto LIFO list
    strand->cleanup_handlers = handler;
}

/**
 * Remove the most recently registered cleanup handler
 *
 * Called when the resource has been successfully released.
 */
void strand_pop_cleanup(void) {
    if (!scheduler_initialized) {
        runtime_error("strand_pop_cleanup: scheduler not initialized");
    }

    Strand* strand = global_scheduler.current_strand;
    if (!strand) {
        runtime_error("strand_pop_cleanup: no current strand");
    }

    CleanupHandler* handler = strand->cleanup_handlers;
    if (!handler) {
        runtime_error("strand_pop_cleanup: no cleanup handlers to pop");
    }

    // Pop from LIFO list
    strand->cleanup_handlers = handler->next;

    // Free the handler (but don't call the cleanup function)
    free(handler);
}

/**
 * Update the argument of the most recently registered cleanup handler
 *
 * This atomically updates the handler's argument without unregistering it.
 * Useful for realloc operations where the pointer changes but cleanup remains the same.
 */
void strand_update_cleanup_arg(void* new_arg) {
    if (!scheduler_initialized) {
        runtime_error("strand_update_cleanup_arg: scheduler not initialized");
    }

    Strand* strand = global_scheduler.current_strand;
    if (!strand) {
        runtime_error("strand_update_cleanup_arg: no current strand");
    }

    CleanupHandler* handler = strand->cleanup_handlers;
    if (!handler) {
        runtime_error("strand_update_cleanup_arg: no cleanup handlers to update");
    }

    // Atomically update the argument
    handler->arg = new_arg;
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
    cem_swapcontext(&strand->context, &global_scheduler.scheduler_context);

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

    // Register for read events
#ifdef USE_KQUEUE
    struct kevent ev;
    EV_SET(&ev, fd, EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0, strand);
    if (kevent(global_scheduler.kqueue_fd, &ev, 1, NULL, 0, NULL) == -1) {
        runtime_error("strand_block_on_read: kevent registration failed");
    }
#elif defined(USE_EPOLL)
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT;  // Edge-triggered, one-shot like kqueue
    ev.data.ptr = strand;
    if (epoll_ctl(global_scheduler.epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        perror("strand_block_on_read: epoll_ctl failed");
        runtime_error("strand_block_on_read: Failed to register fd for read events");
    }
#endif

    // Add to blocked list
    blocked_list_add(strand);
    global_scheduler.current_strand = NULL;

    // Switch back to scheduler
    cem_swapcontext(&strand->context, &global_scheduler.scheduler_context);

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

    // Register for write events
#ifdef USE_KQUEUE
    struct kevent ev;
    EV_SET(&ev, fd, EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0, strand);
    if (kevent(global_scheduler.kqueue_fd, &ev, 1, NULL, 0, NULL) == -1) {
        runtime_error("strand_block_on_write: kevent registration failed");
    }
#elif defined(USE_EPOLL)
    struct epoll_event ev;
    ev.events = EPOLLOUT | EPOLLET | EPOLLONESHOT;  // Edge-triggered, one-shot like kqueue
    ev.data.ptr = strand;
    if (epoll_ctl(global_scheduler.epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        perror("strand_block_on_write: epoll_ctl failed");
        runtime_error("strand_block_on_write: Failed to register fd for write events");
    }
#endif

    // Add to blocked list
    blocked_list_add(strand);
    global_scheduler.current_strand = NULL;

    // Switch back to scheduler
    cem_swapcontext(&strand->context, &global_scheduler.scheduler_context);

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

    // Scheduler context is initialized implicitly by cem_swapcontext
    // When strands yield, they save their context and restore the scheduler context
    // The scheduler context gets populated on the first swap

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

            // Phase 3: Checkpoint-based stack growth check
            // Before switching to the strand, check if its stack needs to grow
            // This is the primary growth mechanism (emergency guard page is backup)
            stack_check_and_grow(strand, CEM_CONTEXT_GET_SP(&strand->context));

            // Switch to strand
            // When strand yields or completes, we'll return here
            cem_swapcontext(&global_scheduler.scheduler_context, &strand->context);

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
            // Wait for I/O events
#ifdef USE_KQUEUE
            struct kevent events[MAX_IO_EVENTS];
            int nevents = kevent(global_scheduler.kqueue_fd, NULL, 0, events, MAX_IO_EVENTS, NULL);

            if (nevents == -1) {
                perror("scheduler_run: kevent wait failed");
                runtime_error("scheduler_run: I/O event wait failed");
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
#elif defined(USE_EPOLL)
            struct epoll_event events[MAX_IO_EVENTS];
            int nevents = epoll_wait(global_scheduler.epoll_fd, events, MAX_IO_EVENTS, -1);  // -1 = block indefinitely

            if (nevents == -1) {
                perror("scheduler_run: epoll_wait failed");
                runtime_error("scheduler_run: I/O event wait failed");
            }

            // Process events - move strands from blocked to ready
            for (int i = 0; i < nevents; i++) {
                Strand* strand = (Strand*)events[i].data.ptr;
                if (!strand) continue;

                // Remove from blocked list
                blocked_list_remove(strand);

                // Remove from epoll (one-shot behavior - already removed automatically by EPOLLONESHOT)
                // No need to call epoll_ctl with EPOLL_CTL_DEL - EPOLLONESHOT does it for us

                // Mark as ready and add to ready queue
                strand->state = STRAND_READY;
                ready_queue_push(strand);
            }
#endif
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
