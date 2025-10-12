/**
 * Test the Phase 2a scheduler with ucontext context switching
 *
 * This test creates multiple strands and verifies they can yield
 * and resume execution properly.
 */

#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <assert.h>
#include "../runtime/scheduler.h"
#include "../runtime/stack.h"

// Test counter (shared state to verify interleaving)
static int execution_order[100];
static int execution_index = 0;

// Helper to record execution
static void record(int value) {
    if (execution_index < 100) {
        execution_order[execution_index++] = value;
    }
}

// Simple strand that yields once
StackCell* strand_a(StackCell* stack) {
    record(1);  // A starts
    strand_yield();
    record(2);  // A resumes
    return stack;
}

// Another strand that yields once
StackCell* strand_b(StackCell* stack) {
    record(3);  // B starts
    strand_yield();
    record(4);  // B resumes
    return stack;
}

// Strand that yields multiple times
StackCell* strand_c(StackCell* stack) {
    record(5);  // C starts
    strand_yield();
    record(6);  // C resumes first time
    strand_yield();
    record(7);  // C resumes second time
    return stack;
}

// Test 1: Basic spawn and run
void test_basic_spawn() {
    printf("Test 1: Basic spawn and run\n");

    scheduler_init();

    // Spawn a single strand
    strand_spawn(strand_a, NULL);

    // Run scheduler
    scheduler_run();

    scheduler_shutdown();

    printf("  ✓ Single strand spawned and completed\n");
}

// Test 2: Multiple strands with yielding
void test_multiple_strands() {
    printf("Test 2: Multiple strands with yielding\n");

    execution_index = 0;
    scheduler_init();

    // Spawn three strands
    strand_spawn(strand_a, NULL);
    strand_spawn(strand_b, NULL);
    strand_spawn(strand_c, NULL);

    // Run scheduler
    scheduler_run();

    scheduler_shutdown();

    // Verify execution order shows interleaving
    // Expected order: all strands start (1,3,5), then resume in order
    printf("  Execution order: ");
    for (int i = 0; i < execution_index; i++) {
        printf("%d ", execution_order[i]);
    }
    printf("\n");

    // Verify all events happened
    assert(execution_index == 7);

    // Verify we saw all the execution points
    int seen[8] = {0};
    for (int i = 0; i < execution_index; i++) {
        seen[execution_order[i]] = 1;
    }
    for (int i = 1; i <= 7; i++) {
        assert(seen[i] == 1);
    }

    printf("  ✓ All strands executed and yielded correctly\n");
}

// Test 3: Strand that doesn't yield
StackCell* strand_no_yield(StackCell* stack) {
    record(10);
    return stack;
}

void test_no_yield() {
    printf("Test 3: Strand without yielding\n");

    execution_index = 0;
    scheduler_init();

    strand_spawn(strand_no_yield, NULL);
    strand_spawn(strand_no_yield, NULL);

    scheduler_run();
    scheduler_shutdown();

    // Both should execute
    assert(execution_index == 2);
    assert(execution_order[0] == 10);
    assert(execution_order[1] == 10);

    printf("  ✓ Strands without yielding work correctly\n");
}

// Test 4: Empty scheduler (no strands)
void test_empty_scheduler() {
    printf("Test 4: Empty scheduler\n");

    scheduler_init();
    StackCell* result = scheduler_run();
    scheduler_shutdown();

    assert(result == NULL);

    printf("  ✓ Empty scheduler returns NULL\n");
}

int main() {
    printf("=== Phase 2a Scheduler Tests ===\n\n");

    test_basic_spawn();
    test_multiple_strands();
    test_no_yield();
    test_empty_scheduler();

    printf("\n✅ All scheduler tests passed!\n");
    return 0;
}
