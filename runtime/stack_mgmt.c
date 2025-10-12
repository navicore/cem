/**
 * Cem Runtime - Dynamic Stack Management Implementation
 *
 * Implements contiguous stack copying with checkpoint-based growth
 * and emergency guard page overflow detection.
 */

// Enable MAP_ANONYMOUS and other POSIX extensions on Linux
#if defined(__linux__)
#define _GNU_SOURCE
#endif

#include "scheduler.h"  // Must be first to get full Scheduler definition
#include "stack_mgmt.h"
#include <sys/mman.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>  // For SIZE_MAX
#include <sys/types.h>  // For ssize_t

// Signal handling - we need to avoid including signal.h which pulls in unistd.h
// Forward declare the types and functions we need
typedef struct {
    int si_signo;
    int si_errno;
    int si_code;
    void *si_addr;
    // ... other fields we don't use
} siginfo_t;

struct sigaction {
    void (*sa_sigaction)(int, siginfo_t *, void *);
    unsigned long sa_flags;
    void (*sa_restorer)(void);
    unsigned char sa_mask[128];  // Large enough for sigset_t
};

#define SA_SIGINFO 4
#define SIGSEGV 11
#define SIG_DFL ((void (*)(int))0)

extern int sigaction(int sig, const struct sigaction *act, struct sigaction *oldact);
extern int sigemptyset(void *set);  // Take void* to avoid type mismatch
extern void (*signal(int sig, void (*func)(int)))(int);
extern int raise(int sig);

// Forward declare sysconf to avoid including unistd.h
// (unistd.h conflicts with stack.h's dup() function)
extern long sysconf(int);

// Platform-specific constants
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    // BSD-based systems typically use 29, but we'll try multiple values
    static const int SC_PAGESIZE_CANDIDATES[] = {29, 30, -1};
    #define DEFAULT_PAGE_SIZE 16384  // 16KB on Apple Silicon, 4KB on Intel
    #ifndef MAP_ANONYMOUS
        #define MAP_ANONYMOUS MAP_ANON  // BSD uses MAP_ANON
    #endif
#elif defined(__linux__)
    // Linux typically uses 30, but we'll try multiple values
    static const int SC_PAGESIZE_CANDIDATES[] = {30, 29, -1};
    #define DEFAULT_PAGE_SIZE 4096   // 4KB on most Linux systems
#else
    // Unknown platform - try common values and fall back to 4KB
    static const int SC_PAGESIZE_CANDIDATES[] = {30, 29, -1};
    #define DEFAULT_PAGE_SIZE 4096
    #warning "Unknown platform - page size detection may fail, will fall back to 4KB"
#endif

// ============================================================================
// Platform Detection & Configuration
// ============================================================================

// Cached system page size
static size_t g_page_size = 0;

// Global scheduler reference for signal handler (set during init)
// Note: We use the full Scheduler type from scheduler.h here, not the forward declaration
static Scheduler* g_scheduler = NULL;

// Forward declarations for signal-safe helpers (defined later in this file)
static void signal_safe_write(const char* str);
static void size_to_str(size_t n, char* buf, size_t bufsize);

/**
 * Get system page size (cached)
 *
 * Uses robust detection with multiple fallback strategies:
 * 1. Try multiple _SC_PAGESIZE candidate values via sysconf()
 * 2. If all fail, use platform-specific default
 */
size_t stack_get_page_size(void) {
    if (g_page_size == 0) {
        // Try each candidate _SC_PAGESIZE value until one works
        // This handles variations across OS versions robustly
        for (int i = 0; SC_PAGESIZE_CANDIDATES[i] != -1; i++) {
            long result = sysconf(SC_PAGESIZE_CANDIDATES[i]);
            if (result > 0 && result != -1) {
                g_page_size = (size_t)result;
                break;
            }
        }

        // If all candidates failed, use platform-specific default
        if (g_page_size == 0) {
            g_page_size = DEFAULT_PAGE_SIZE;
            fprintf(stderr, "WARNING: Could not detect page size via sysconf(), using default %zu bytes\n",
                    g_page_size);
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
 *
 * Note on munmap() failure handling:
 * If munmap() fails, we have a memory leak (the mapped region remains).
 * We still free the metadata because:
 * 1. The metadata is small (~80 bytes) compared to the mapped region (KB-MB)
 * 2. Keeping the metadata doesn't help us recover - we can't retry munmap()
 * 3. The strand is terminating anyway, so the leak will be visible in metrics
 * 4. On process exit, the OS will reclaim all mapped memory
 *
 * In practice, munmap() should never fail for our use case (simple anonymous mappings).
 * If it does fail, it indicates a serious kernel issue or corrupted metadata.
 */
void stack_free(StackMetadata* meta) {
    if (!meta) {
        return;
    }

    if (meta->base) {
        if (munmap(meta->base, meta->total_size) != 0) {
            // munmap() failed - this is very rare and indicates either:
            // 1. Corrupted metadata (base/total_size are wrong)
            // 2. Kernel issue (out of memory in kernel page tables)
            // 3. The memory was already unmapped (double-free)
            fprintf(stderr, "ERROR: munmap failed during stack_free\n");
            fprintf(stderr, "  Address: %p\n", meta->base);
            fprintf(stderr, "  Size: %zu bytes\n", meta->total_size);
            fprintf(stderr, "  This will cause a memory leak (%zu bytes)!\n", meta->total_size);
            fprintf(stderr, "  Possible causes:\n");
            fprintf(stderr, "    - Corrupted stack metadata\n");
            fprintf(stderr, "    - Double-free of stack\n");
            fprintf(stderr, "    - Kernel resource exhaustion\n");

            // We still free the metadata to avoid a metadata leak on top of the memory leak.
            // The small metadata leak (80 bytes) is less severe than the large mapped region leak,
            // but we can at least prevent compounding the problem.
        }
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
 * 1. Checkpoint-based proactive growth (in_signal_handler = false)
 * 2. Emergency signal handler growth (in_signal_handler = true)
 *
 * When in_signal_handler is true, this function uses only async-signal-safe
 * operations (no fprintf, uses signal_safe_write instead).
 *
 * @param strand - Strand whose stack to grow
 * @param new_usable_size - New usable stack size (must be > current size)
 * @param in_signal_handler - true if called from SIGSEGV handler, false otherwise
 * @return true on success, false on failure
 */
bool stack_grow(struct Strand* strand, size_t new_usable_size, bool in_signal_handler) {
    assert(strand != NULL);
    assert(strand->stack_meta != NULL);

    StackMetadata* old_meta = strand->stack_meta;
    size_t page_size = stack_get_page_size();

    // Validate new size
    if (new_usable_size <= old_meta->usable_size) {
        if (in_signal_handler) {
            signal_safe_write("ERROR: stack_grow: new size must be > current size\n");
        } else {
            fprintf(stderr, "stack_grow: new size %zu must be > current size %zu\n",
                    new_usable_size, old_meta->usable_size);
        }
        return false;
    }

    if (new_usable_size > CEM_MAX_STACK_SIZE) {
        if (in_signal_handler) {
            signal_safe_write("ERROR: Maximum stack size reached\n");
            signal_safe_write("  This usually indicates infinite recursion\n");
        } else {
            fprintf(stderr, "stack_grow: strand %llu hit maximum stack size (%d bytes)\n",
                    (unsigned long long)strand->id, CEM_MAX_STACK_SIZE);
            fprintf(stderr, "  This usually indicates infinite recursion or excessive local variables.\n");
        }
        return false;
    }

    // Round up to page boundary
    new_usable_size = ((new_usable_size + page_size - 1) / page_size) * page_size;

    // Allocate new stack
    StackMetadata* new_meta = stack_alloc(new_usable_size);
    if (!new_meta) {
        if (in_signal_handler) {
            signal_safe_write("ERROR: Failed to allocate new stack\n");
        } else {
            fprintf(stderr, "stack_grow: failed to allocate new stack of size %zu\n",
                    new_usable_size);
        }
        return false;
    }

    // Calculate current stack usage from SP
    uintptr_t old_sp = CEM_CONTEXT_GET_SP(&strand->context);
    uintptr_t old_stack_top = (uintptr_t)old_meta->usable_base + old_meta->usable_size;
    size_t used_bytes = (size_t)(old_stack_top - old_sp);

    // Sanity check for SP corruption
    // If SP is corrupted, we MUST abort immediately. Continuing execution with
    // a corrupted stack pointer leads to undefined behavior and crashes.
    if (used_bytes > old_meta->usable_size) {
        fprintf(stderr, "\n");
        fprintf(stderr, "========================================\n");
        fprintf(stderr, "FATAL: Stack pointer corruption detected\n");
        fprintf(stderr, "========================================\n");
        fprintf(stderr, "Strand ID: %llu\n", (unsigned long long)strand->id);
        fprintf(stderr, "Stack size: %zu bytes\n", old_meta->usable_size);
        fprintf(stderr, "Calculated usage: %zu bytes (SP is corrupted!)\n", used_bytes);
        fprintf(stderr, "Stack base: %p\n", old_meta->usable_base);
        fprintf(stderr, "Stack top: %p\n", (void*)old_stack_top);
        fprintf(stderr, "Current SP: %p\n", (void*)old_sp);
        fprintf(stderr, "\n");
        fprintf(stderr, "This indicates either:\n");
        fprintf(stderr, "  1. Memory corruption (buffer overflow, use-after-free)\n");
        fprintf(stderr, "  2. Context switching bug\n");
        fprintf(stderr, "  3. Stack metadata corruption\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "Cannot continue safely. Aborting.\n");
        fprintf(stderr, "========================================\n");
        stack_free(new_meta);
        abort();  // Immediate termination - corrupted state is unrecoverable
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
        // x86-64 IMPLEMENTATION INCOMPLETE - Return address adjustment not yet implemented
        //
        // CRITICAL LIMITATION: On x86-64, return addresses are stored ON THE STACK
        // (not in registers like ARM64's x30). When we memcpy the stack to a new
        // location, these return addresses become invalid and will crash when functions
        // try to return.
        //
        // REQUIRED FOR FULL x86-64 SUPPORT:
        // 1. Walk the stack frame chain using rbp (frame pointer)
        // 2. For each frame, adjust the return address by (new_stack_top - old_stack_top)
        // 3. Handle cases where rbp chain is broken (optimized code, leaf functions)
        //
        // CURRENT WORKAROUND: Checkpoint-based growth catches most cases BEFORE
        // return addresses are on the stack. This works for simple strands but
        // will fail for deep call stacks or recursive functions.
        //
        // TODO: Implement full stack frame walking and return address adjustment
        // See Go runtime's adjustframe() for reference implementation.

        // Update stack pointer
        strand->context.rsp = new_sp;

        // Update frame pointer (rbp) - but only if it points into the old stack
        if (strand->context.rbp >= (uintptr_t)old_meta->usable_base &&
            strand->context.rbp <= old_stack_top) {
            uintptr_t offset_from_top = old_stack_top - strand->context.rbp;
            strand->context.rbp = new_stack_top - offset_from_top;
        }

        // WARNING: We are NOT adjusting return addresses on the stack!
        // This will likely cause crashes if the strand has active call frames.
        fprintf(stderr, "WARNING: x86-64 stack growth is INCOMPLETE and may crash\n");
        fprintf(stderr, "  Return addresses on stack are not adjusted!\n");

    #else
        #error "Unsupported architecture for dynamic stack growth"
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
        if (in_signal_handler) {
            // Use signal-safe logging with minimal formatting
            signal_safe_write("INFO: Stack grew to ");
            char buf[32];
            size_to_str(new_meta->usable_size, buf, sizeof(buf));
            signal_safe_write(buf);
            signal_safe_write(" bytes\n");
        } else {
            fprintf(stderr, "INFO: Strand %llu stack grew %zu -> %zu bytes (growth #%u)\n",
                    (unsigned long long)strand->id, old_usable_size, new_meta->usable_size,
                    new_meta->growth_count);
        }
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

    return stack_grow(strand, new_size, false);  // Not in signal handler
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
 * Convert size_t to string (async-signal-safe)
 *
 * Writes decimal representation of n into buffer.
 * Buffer must be at least 21 bytes (max digits in 64-bit number + null).
 */
static void size_to_str(size_t n, char* buf, size_t bufsize) {
    if (bufsize == 0) return;

    // Handle zero specially
    if (n == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }

    // Convert to string (reversed)
    size_t i = 0;
    while (n > 0 && i < bufsize - 1) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    buf[i] = '\0';

    // Reverse the string
    for (size_t j = 0; j < i / 2; j++) {
        char tmp = buf[j];
        buf[j] = buf[i - 1 - j];
        buf[i - 1 - j] = tmp;
    }
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
            if (stack_grow(strand, new_size, true)) {  // IN SIGNAL HANDLER - use safe logging
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
