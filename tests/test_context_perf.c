/**
 * Context Switching Performance Test
 *
 * This measures the performance of cem_swapcontext to verify we're
 * getting the expected ~10-20ns per context switch.
 */

#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>
#include "../runtime/context.h"

#define ITERATIONS 1000000

static cem_context_t ctx1, ctx2;
static volatile int counter = 0;

static void bench_func1(void) {
    for (int i = 0; i < ITERATIONS; i++) {
        cem_swapcontext(&ctx1, &ctx2);
    }
}

static void bench_func2(void) {
    for (int i = 0; i < ITERATIONS; i++) {
        cem_swapcontext(&ctx2, &ctx1);
    }
}

int main(void) {
    printf("=== Context Switching Performance Test ===\n\n");
    
    // Allocate stacks
    void* stack1 = malloc(64 * 1024);
    void* stack2 = malloc(64 * 1024);
    assert(stack1 != NULL && stack2 != NULL);
    
    // Initialize contexts
    cem_makecontext(&ctx1, stack1, 64 * 1024, bench_func1, NULL);
    cem_makecontext(&ctx2, stack2, 64 * 1024, bench_func2, NULL);
    
    printf("Performing %d context switches...\n", ITERATIONS * 2);
    
    // Benchmark
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    // Start the ping-pong
    cem_swapcontext(&ctx2, &ctx1);  // Start ctx1
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    // Calculate timing
    long long ns = (end.tv_sec - start.tv_sec) * 1000000000LL +
                   (end.tv_nsec - start.tv_nsec);
    
    long long total_switches = ITERATIONS * 2;  // Each iteration is 2 switches (back and forth)
    double ns_per_switch = (double)ns / total_switches;
    
    printf("\nResults:\n");
    printf("  Total time: %lld ns (%.2f ms)\n", ns, ns / 1000000.0);
    printf("  Total switches: %lld\n", total_switches);
    printf("  Time per switch: %.1f ns\n", ns_per_switch);
    printf("  Switches per second: %.0f million\n", 1000.0 / ns_per_switch);
    
    // Verify we're in the expected range (10-50ns per switch)
    if (ns_per_switch > 50) {
        printf("\n⚠️  Warning: Context switches are slower than expected\n");
        printf("   Expected: 10-50ns, Got: %.1fns\n", ns_per_switch);
    } else {
        printf("\n✅ Performance is excellent!\n");
    }
    
    free(stack1);
    free(stack2);
    
    return 0;
}
