/**
 * Cem Runtime - Dynamic Stack Management Implementation
 *
 * Implements contiguous stack copying with checkpoint-based growth
 * and emergency guard page overflow detection.
 */

#include "scheduler.h"  // Must be first to get full Scheduler definition
#include "stack_mgmt.h"
#include <sys/mman.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>  // For SIZE_MAX

// Forward declare sysconf to avoid including unistd.h
// (unistd.h conflicts with stack.h's dup() function)
extern long sysconf(int);

// Platform-specific _SC_PAGESIZE values
// These values are OS-specific, not architecture-specific
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    #define _SC_PAGESIZE 29  // BSD-based systems (macOS, FreeBSD, etc.)
#elif defined(__linux__)
    #define _SC_PAGESIZE 30  // Linux
#else
    #error "Unknown platform for _SC_PAGESIZE constant"
#endif

// ============================================================================
// Platform Detection & Configuration
// ============================================================================

// Cached system page size
static size_t g_page_size = 0;

// Global scheduler reference for signal handler (set during init)
// Note: We use the full Scheduler type from scheduler.h here, not the forward declaration
static Scheduler* g_scheduler = NULL;

/**
 * Get system page size (cached)
 */
size_t stack_get_page_size(void) {
    if (g_page_size == 0) {
        g_page_size = (size_t)sysconf(_SC_PAGESIZE);
        if (g_page_size == 0 || g_page_size == (size_t)-1) {
            // Fallback to common page sizes
            #ifdef __APPLE__
                g_page_size = 16384;  // 16KB on Apple Silicon
            #else
                g_page_size = 4096;   // 4KB on most other systems
            #endif
        }
    }
    return g_page_size;
}

// ============================================================================
// Stack Allocation & Deallocation
// ============================================================================

/**
 * Allocate a new dynamic stack with guard page
 */
StackMetadata* stack_alloc(size_t initial_size) {
    size_t page_size = stack_get_page_size();

    // Validate initial size
    if (initial_size < CEM_INITIAL_STACK_SIZE) {
        initial_size = CEM_INITIAL_STACK_SIZE;
    }
    if (initial_size > CEM_MAX_STACK_SIZE) {
        fprintf(stderr, "stack_alloc: requested size %zu exceeds maximum %d\n",
                initial_size, CEM_MAX_STACK_SIZE);
        return NULL;
    }

    // Round up to page boundary with overflow checking
    // Check: initial_size + page_size - 1 might overflow
    if (initial_size > SIZE_MAX - page_size) {
        fprintf(stderr, "stack_alloc: size %zu too large (overflow risk)\n", initial_size);
        return NULL;
    }

    size_t usable_size = ((initial_size + page_size - 1) / page_size) * page_size;

    // Check: usable_size + page_size might overflow
    if (usable_size > SIZE_MAX - page_size) {
        fprintf(stderr, "stack_alloc: usable size %zu too large (overflow risk)\n", usable_size);
        return NULL;
    }

    size_t total_size = usable_size + page_size;  // +1 page for guard

    // Allocate metadata
    StackMetadata* meta = (StackMetadata*)malloc(sizeof(StackMetadata));
    if (!meta) {
        fprintf(stderr, "stack_alloc: failed to allocate metadata\n");
        return NULL;
    }

    // Allocate stack region with mmap (allows guard page protection)
    void* base = mmap(NULL, total_size,
                      PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS,
                      -1, 0);

    if (base == MAP_FAILED) {
        fprintf(stderr, "stack_alloc: mmap failed for %zu bytes\n", total_size);
        free(meta);
        return NULL;
    }

    // Set guard page at bottom (low address) to PROT_NONE
    if (mprotect(base, page_size, PROT_NONE) != 0) {
        fprintf(stderr, "stack_alloc: mprotect failed to set guard page\n");
        munmap(base, total_size);
        free(meta);
        return NULL;
    }

    // Initialize metadata
    meta->base = base;
    meta->usable_base = (void*)((uintptr_t)base + page_size);
    meta->total_size = total_size;
    meta->usable_size = usable_size;
    meta->guard_page_size = page_size;
    meta->growth_count = 0;
    meta->guard_hit = false;

    return meta;
}

/**
 * Free a dynamic stack
 */
void stack_free(StackMetadata* meta) {
    if (!meta) {
        return;
    }

    if (meta->base) {
        munmap(meta->base, meta->total_size);
    }

    free(meta);
}

// ============================================================================
// Stack Usage Statistics
// ============================================================================

/**
 * Calculate current stack usage
 *
 * Stack grows downward: usage = stack_top - current_sp
 */
size_t stack_get_used(const StackMetadata* meta, uintptr_t current_sp) {
    uintptr_t stack_top = (uintptr_t)meta->usable_base + meta->usable_size;

    // Sanity check: SP should be within stack
    if (current_sp > stack_top || current_sp < (uintptr_t)meta->usable_base) {
        // SP is outside valid range - likely corrupted
        return meta->usable_size;  // Return "full" to trigger growth/error
    }

    return (size_t)(stack_top - current_sp);
}

/**
 * Calculate current free stack space
 */
size_t stack_get_free(const StackMetadata* meta, uintptr_t current_sp) {
    size_t used = stack_get_used(meta, current_sp);
    if (used >= meta->usable_size) {
        return 0;
    }
    return meta->usable_size - used;
}

// ============================================================================
// Stack Growth
// ============================================================================

/**
 * Grow a stack to a new size
 *
 * This is the core growth implementation used by both:
 * 1. Checkpoint-based proactive growth
 * 2. Emergency signal handler growth
 */
bool stack_grow(struct Strand* strand, size_t new_usable_size) {
    assert(strand != NULL);
    assert(strand->stack_meta != NULL);

    StackMetadata* old_meta = strand->stack_meta;
    size_t page_size = stack_get_page_size();

    // Validate new size
    if (new_usable_size <= old_meta->usable_size) {
        fprintf(stderr, "stack_grow: new size %zu must be > current size %zu\n",
                new_usable_size, old_meta->usable_size);
        return false;
    }

    if (new_usable_size > CEM_MAX_STACK_SIZE) {
        fprintf(stderr, "stack_grow: strand %llu hit maximum stack size (%d bytes)\n",
                (unsigned long long)strand->id, CEM_MAX_STACK_SIZE);
        fprintf(stderr, "  This usually indicates infinite recursion or excessive local variables.\n");
        return false;
    }

    // Round up to page boundary
    new_usable_size = ((new_usable_size + page_size - 1) / page_size) * page_size;

    // Allocate new stack
    StackMetadata* new_meta = stack_alloc(new_usable_size);
    if (!new_meta) {
        fprintf(stderr, "stack_grow: failed to allocate new stack of size %zu\n",
                new_usable_size);
        return false;
    }

    // Calculate current stack usage from SP
    uintptr_t old_sp = strand->context.sp;
    uintptr_t old_stack_top = (uintptr_t)old_meta->usable_base + old_meta->usable_size;
    size_t used_bytes = (size_t)(old_stack_top - old_sp);

    // Sanity check
    if (used_bytes > old_meta->usable_size) {
        fprintf(stderr, "stack_grow: SP corruption detected (used %zu > size %zu)\n",
                used_bytes, old_meta->usable_size);
        stack_free(new_meta);
        return false;
    }

    // Copy stack contents from old to new
    // Old stack: [usable_base ... SP ... stack_top]
    // New stack: [usable_base ... ??? ... stack_top]
    //
    // We copy from old_sp to old_stack_top
    // And place it at the equivalent position in new stack

    uintptr_t new_stack_top = (uintptr_t)new_meta->usable_base + new_meta->usable_size;
    uintptr_t new_sp = new_stack_top - used_bytes;

    memcpy((void*)new_sp, (void*)old_sp, used_bytes);

    // Update context pointers
    // SP: already calculated above
    // FP (x29 on ARM64, rbp on x86-64): adjust based on offset from stack TOP
    //
    // IMPORTANT: Frame pointers are relative to the stack top, not the base.
    // Since stacks grow downward, we need to maintain the same offset from the top.
    //
    // Calculation:
    // - old_offset_from_top = old_stack_top - old_fp
    // - new_fp = new_stack_top - old_offset_from_top

    #ifdef CEM_ARCH_ARM64
        // Update stack pointer
        strand->context.sp = new_sp;

        // Update frame pointer (x29) - but only if it points into the old stack
        if (strand->context.x29 >= (uintptr_t)old_meta->usable_base &&
            strand->context.x29 <= old_stack_top) {
            uintptr_t offset_from_top = old_stack_top - strand->context.x29;
            strand->context.x29 = new_stack_top - offset_from_top;
        }

        // Note: x30 (LR) contains return addresses, not stack pointers
        // No adjustment needed for x30

    #elif defined(CEM_ARCH_X86_64)
        // Update stack pointer
        strand->context.rsp = new_sp;

        // Update frame pointer (rbp)
        if (strand->context.rbp >= (uintptr_t)old_meta->usable_base &&
            strand->context.rbp <= old_stack_top) {
            uintptr_t offset_from_top = old_stack_top - strand->context.rbp;
            strand->context.rbp = new_stack_top - offset_from_top;
        }

        // RUNTIME WARNING for x86-64 limitation
        static bool x86_64_warning_shown = false;
        if (!x86_64_warning_shown) {
            fprintf(stderr, "\n");
            fprintf(stderr, "WARNING: x86-64 stack growth detected!\n");
            fprintf(stderr, "  x86-64 return address adjustment is NOT implemented.\n");
            fprintf(stderr, "  If this strand has return addresses on its stack,\n");
            fprintf(stderr, "  it will likely crash after growth.\n");
            fprintf(stderr, "  This warning will only be shown once.\n");
            fprintf(stderr, "\n");
            x86_64_warning_shown = true;
        }

        // LIMITATION: x86-64 return addresses need adjustment
        //
        // Unlike ARM64 (which stores return addresses in LR register), x86-64 stores
        // return addresses directly on the stack. When we copy the stack to a new
        // location, these return addresses become invalid pointers into the old stack.
        //
        // To fix this properly, we would need to:
        // 1. Walk the stack frames (following rbp chain)
        // 2. For each frame, find the return address
        // 3. Adjust it by the stack relocation offset
        //
        // This is complex and error-prone. For now, we rely on the checkpoint-based
        // growth strategy catching stack growth BEFORE any function calls push return
        // addresses onto the stack. This works because:
        // - Checkpoints run at every context switch (before strand executes)
        // - Growth happens proactively (8KB free space minimum)
        // - Emergency guard page is a safety net (should never be hit)
        //
        // FUTURE: If x86-64 support becomes critical and guard pages are being hit,
        // implement proper return address adjustment via stack frame walking.

    #endif

    // Save values we need before freeing old_meta
    uint32_t old_growth_count = old_meta->growth_count;
    size_t old_usable_size = old_meta->usable_size;

    // Update strand metadata
    stack_free(old_meta);
    strand->stack_meta = new_meta;
    new_meta->growth_count = old_growth_count + 1;

    // Log growth (useful for debugging/tuning)
    if (new_meta->growth_count <= 3 || (new_meta->growth_count % 10) == 0) {
        fprintf(stderr, "INFO: Strand %llu stack grew %zu -> %zu bytes (growth #%u)\n",
                (unsigned long long)strand->id, old_usable_size, new_meta->usable_size,
                new_meta->growth_count);
    }

    return true;
}

/**
 * Check if stack needs to grow (checkpoint-based)
 */
bool stack_check_and_grow(struct Strand* strand, uintptr_t current_sp) {
    assert(strand != NULL);
    assert(strand->stack_meta != NULL);

    StackMetadata* meta = strand->stack_meta;

    // Calculate usage
    size_t used = stack_get_used(meta, current_sp);
    size_t free = stack_get_free(meta, current_sp);

    // Hybrid growth strategy:
    // 1. Grow if free space < MIN_FREE_STACK (8KB), OR
    // 2. Grow if used > CEM_STACK_GROWTH_THRESHOLD_PERCENT of total

    bool need_growth = false;
    const char* reason = NULL;

    if (free < CEM_MIN_FREE_STACK) {
        need_growth = true;
        reason = "free space below minimum";
    } else if (used > (meta->usable_size * CEM_STACK_GROWTH_THRESHOLD_PERCENT / 100)) {
        need_growth = true;
        reason = "usage above threshold";
    }

    if (!need_growth) {
        return false;
    }

    // Grow by doubling (2x) with overflow check
    if (meta->usable_size > SIZE_MAX / 2) {
        fprintf(stderr, "ERROR: Strand %llu stack size %zu cannot be doubled (overflow)\n",
                (unsigned long long)strand->id, meta->usable_size);
        return false;
    }
    size_t new_size = meta->usable_size * 2;

    // Log reason
    fprintf(stderr, "INFO: Strand %llu growing stack (%s): %zu/%zu bytes used, %zu free\n",
            (unsigned long long)strand->id, reason, used, meta->usable_size, free);

    return stack_grow(strand, new_size);
}

// ============================================================================
// Guard Page Signal Handler
// ============================================================================

/**
 * Async-signal-safe string write helper
 *
 * write() is async-signal-safe, fprintf() is not.
 * Use this in signal handlers to avoid deadlocks.
 */
static void signal_safe_write(const char* str) {
    // Forward declare write() to avoid including unistd.h
    extern ssize_t write(int, const void*, size_t);

    size_t len = 0;
    while (str[len]) len++;
    write(2, str, len);  // 2 = STDERR_FILENO
}

/**
 * Check if an address is within a guard page
 */
bool stack_is_guard_page_fault(uintptr_t addr, const StackMetadata* meta) {
    if (!meta || !meta->base) {
        return false;
    }

    uintptr_t guard_start = (uintptr_t)meta->base;
    uintptr_t guard_end = guard_start + meta->guard_page_size;

    return (addr >= guard_start && addr < guard_end);
}

/**
 * SIGSEGV signal handler for emergency stack overflow
 *
 * THREAD SAFETY: This handler accesses g_scheduler and current_strand without locks.
 * This is SAFE because:
 * 1. Cem uses a single-threaded cooperative scheduler (no preemption)
 * 2. Signals can only arrive while a strand is executing (not during scheduler code)
 * 3. When a strand is running, current_strand is guaranteed to be valid and stable
 * 4. The SIGSEGV is synchronous - it's triggered by the currently executing strand
 * 5. We never modify g_scheduler or current_strand from signal context
 *
 * If Cem ever becomes multi-threaded or preemptive, this code will need
 * atomic operations or locks.
 */
static void stack_sigsegv_handler(int sig, siginfo_t *si, void *unused) {
    (void)sig;
    (void)unused;

    uintptr_t fault_addr = (uintptr_t)si->si_addr;

    // Check if fault is in current strand's guard page
    // This read is safe - see thread safety note above
    if (g_scheduler && g_scheduler->current_strand) {
        Strand* strand = g_scheduler->current_strand;

        if (stack_is_guard_page_fault(fault_addr, strand->stack_meta)) {
            // EMERGENCY GROWTH - guard page was hit!
            // Use async-signal-safe write() instead of fprintf()
            signal_safe_write("\n");
            signal_safe_write("========================================\n");
            signal_safe_write("WARNING: Guard page hit!\n");
            signal_safe_write("========================================\n");
            signal_safe_write("This indicates the checkpoint heuristic failed to predict stack growth.\n");
            signal_safe_write("The stack will be grown now, but this is a FALLBACK mechanism.\n");
            signal_safe_write("Consider tuning CEM_MIN_FREE_STACK if this happens frequently.\n");
            signal_safe_write("\n");

            strand->stack_meta->guard_hit = true;

            // Attempt emergency growth
            size_t new_size = strand->stack_meta->usable_size * 2;
            if (stack_grow(strand, new_size)) {
                signal_safe_write("INFO: Emergency growth succeeded\n");
                // Return and retry the faulting instruction
                return;
            } else {
                signal_safe_write("FATAL: Emergency growth failed - strand will crash\n");
                // Fall through to default handler (crash)
            }
        }
    }

    // Not our stack overflow - re-raise signal for normal crash handling
    signal_safe_write("SIGSEGV: not a guard page fault\n");
    signal(SIGSEGV, SIG_DFL);
    raise(SIGSEGV);
}

/**
 * Initialize SIGSEGV signal handler
 */
void stack_guard_init_signal_handler(void) {
    struct sigaction sa;

    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = stack_sigsegv_handler;

    if (sigaction(SIGSEGV, &sa, NULL) == -1) {
        fprintf(stderr, "WARNING: Failed to install SIGSEGV handler for guard pages\n");
        fprintf(stderr, "  Stack overflow detection will be limited to checkpoints only.\n");
    }
}

/**
 * Set global scheduler reference for signal handler
 *
 * This is called by scheduler_init() to give the signal handler
 * access to the scheduler state.
 */
void stack_guard_set_scheduler(struct Scheduler* scheduler) {
    // Cast from struct Scheduler* to Scheduler* (they're the same type)
    g_scheduler = (Scheduler*)scheduler;
}
