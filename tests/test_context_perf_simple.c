#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>
#include "../runtime/context.h"

#define ITERATIONS 10000000  // 10 million

static cem_context_t ctx1, ctx2;
static int iterations_done = 0;

static void bench_func1(void) {
    while (iterations_done < ITERATIONS) {
        iterations_done++;
        cem_swapcontext(&ctx1, &ctx2);
    }
}

static void bench_func2(void) {
    while (iterations_done < ITERATIONS) {
        cem_swapcontext(&ctx2, &ctx1);
    }
}

int main(void) {
    printf("=== Context Switching Performance Test ===\n\n");
    
    void* stack1 = malloc(64 * 1024);
    void* stack2 = malloc(64 * 1024);
    assert(stack1 && stack2);
    
    cem_makecontext(&ctx1, stack1, 64 * 1024, bench_func1, NULL);
    cem_makecontext(&ctx2, stack2, 64 * 1024, bench_func2, NULL);
    
    printf("Performing %d context switches...\n", ITERATIONS * 2);
    
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    cem_swapcontext(&ctx2, &ctx1);
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    long long ns = (end.tv_sec - start.tv_sec) * 1000000000LL +
                   (end.tv_nsec - start.tv_nsec);
    
    long long total_switches = (long long)iterations_done * 2;
    double ns_per_switch = (double)ns / total_switches;
    
    printf("\nResults:\n");
    printf("  Total time: %.2f ms\n", ns / 1000000.0);
    printf("  Total switches: %lld\n", total_switches);
    printf("  Time per switch: %.1f ns\n", ns_per_switch);
    printf("  Switches per second: %.2f million/sec\n", 1000.0 / ns_per_switch);
    
    if (ns_per_switch > 100) {
        printf("\n⚠️  Slower than expected (10-50ns target)\n");
    } else {
        printf("\n✅ Excellent performance!\n");
    }
    
    free(stack1);
    free(stack2);
    return 0;
}
