/**
 * Basic Strand Execution Tests
 *
 * Tests the absolute minimum strand functionality to isolate
 * where the crash is happening. Each test gets progressively
 * more complex.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include "../runtime/scheduler.h"

static int test_count = 0;

// ============================================================================
// Test 1: Minimal strand - does nothing
// ============================================================================

static bool minimal_ran = false;

StackCell* strand_minimal(StackCell* stack) {
    // Use signal-safe write to avoid fprintf buffering issues
    write(2, "[TEST1] Minimal strand started\n", 32);
    minimal_ran = true;
    write(2, "[TEST1] Minimal strand returning\n", 34);
    return stack;
}

void test_minimal_strand(void) {
    printf("\n=== Test 1: Minimal strand (no locals) ===\n");

    minimal_ran = false;
    scheduler_init();

    fprintf(stderr, "[TEST1] Spawning strand\n");
    fflush(stderr);
    strand_spawn(strand_minimal, NULL);

    fprintf(stderr, "[TEST1] Running scheduler\n");
    fflush(stderr);
    scheduler_run();

    fprintf(stderr, "[TEST1] Shutting down scheduler\n");
    fflush(stderr);
    scheduler_shutdown();

    if (minimal_ran) {
        printf("✓ Minimal strand executed\n");
        test_count++;
    } else {
        printf("✗ FAILED: Minimal strand did not execute\n");
        exit(1);
    }
}

// ============================================================================
// Test 2: Strand with small local variable (64 bytes)
// ============================================================================

static bool small_locals_ran = false;

StackCell* strand_small_locals(StackCell* stack) {
    write(2, "[TEST2] Strand with 64B locals started\n", 40);

    char buffer[64];
    buffer[0] = 1;
    buffer[63] = 2;

    write(2, "[TEST2] Locals allocated and used\n", 35);
    small_locals_ran = true;
    return stack;
}

void test_small_locals(void) {
    printf("\n=== Test 2: Strand with 64B local buffer ===\n");

    small_locals_ran = false;
    scheduler_init();
    strand_spawn(strand_small_locals, NULL);
    scheduler_run();
    scheduler_shutdown();

    if (small_locals_ran) {
        printf("✓ Strand with 64B locals executed\n");
        test_count++;
    } else {
        printf("✗ FAILED: Strand with 64B locals did not execute\n");
        exit(1);
    }
}

// ============================================================================
// Test 3: Strand with 1KB local variable
// ============================================================================

static bool medium_locals_ran = false;

StackCell* strand_medium_locals(StackCell* stack) {
    write(2, "[TEST3] Strand with 1KB locals started\n", 40);

    char buffer[1024];
    buffer[0] = 1;
    buffer[1023] = 2;

    write(2, "[TEST3] 1KB locals allocated and used\n", 39);
    medium_locals_ran = true;
    return stack;
}

void test_medium_locals(void) {
    printf("\n=== Test 3: Strand with 1KB local buffer ===\n");

    medium_locals_ran = false;
    scheduler_init();
    strand_spawn(strand_medium_locals, NULL);
    scheduler_run();
    scheduler_shutdown();

    if (medium_locals_ran) {
        printf("✓ Strand with 1KB locals executed\n");
        test_count++;
    } else {
        printf("✗ FAILED: Strand with 1KB locals did not execute\n");
        exit(1);
    }
}

// ============================================================================
// Test 4: Strand with 2KB local variable
// ============================================================================

static bool large_locals_ran = false;

StackCell* strand_large_locals(StackCell* stack) {
    write(2, "[TEST4] Strand with 2KB locals started\n", 40);

    char buffer[2048];
    buffer[0] = 1;
    buffer[2047] = 2;

    write(2, "[TEST4] 2KB locals allocated and used\n", 39);
    large_locals_ran = true;
    return stack;
}

void test_large_locals(void) {
    printf("\n=== Test 4: Strand with 2KB local buffer ===\n");

    large_locals_ran = false;
    scheduler_init();
    strand_spawn(strand_large_locals, NULL);
    scheduler_run();
    scheduler_shutdown();

    if (large_locals_ran) {
        printf("✓ Strand with 2KB locals executed\n");
        test_count++;
    } else {
        printf("✗ FAILED: Strand with 2KB locals did not execute\n");
        exit(1);
    }
}

// ============================================================================
// Test 5: Strand with yielding (cooperative multitasking)
// ============================================================================

static int yield_counter = 0;

StackCell* strand_with_yields(StackCell* stack) {
    write(2, "[TEST5] Strand with yielding started\n", 38);

    for (int i = 0; i < 5; i++) {
        yield_counter++;
        strand_yield();  // Yield back to scheduler
    }

    write(2, "[TEST5] Strand completed after 5 yields\n", 41);
    return stack;
}

void test_yielding(void) {
    printf("\n=== Test 5: Strand with yielding (cooperative multitasking) ===\n");

    yield_counter = 0;
    scheduler_init();
    strand_spawn(strand_with_yields, NULL);
    scheduler_run();
    scheduler_shutdown();

    if (yield_counter == 5) {
        printf("✓ Strand yielded 5 times correctly\n");
        test_count++;
    } else {
        printf("✗ FAILED: Expected 5 yields, got %d\n", yield_counter);
        exit(1);
    }
}

// ============================================================================
// Test 6: Multiple concurrent strands
// ============================================================================

static int multi_strand_counter = 0;

StackCell* counting_strand(StackCell* stack) {
    for (int i = 0; i < 3; i++) {
        multi_strand_counter++;
        strand_yield();
    }
    return stack;
}

void test_multiple_strands(void) {
    printf("\n=== Test 6: Multiple concurrent strands ===\n");

    multi_strand_counter = 0;
    scheduler_init();

    // Spawn 3 strands that each increment counter 3 times
    strand_spawn(counting_strand, NULL);
    strand_spawn(counting_strand, NULL);
    strand_spawn(counting_strand, NULL);

    scheduler_run();
    scheduler_shutdown();

    if (multi_strand_counter == 9) {
        printf("✓ Three strands executed concurrently (9 total increments)\n");
        test_count++;
    } else {
        printf("✗ FAILED: Expected 9 increments, got %d\n", multi_strand_counter);
        exit(1);
    }
}

// ============================================================================
// Main
// ============================================================================

int main(void) {
    printf("=== Basic Strand Execution Tests ===\n");
    printf("Fixed stack size: 1MB per strand\n");
    printf("These tests verify strand execution and cooperative multitasking.\n\n");

    test_minimal_strand();
    test_small_locals();
    test_medium_locals();
    test_large_locals();
    test_yielding();
    test_multiple_strands();

    printf("\n=== Summary ===\n");
    printf("Passed: %d/6 tests\n", test_count);

    if (test_count == 6) {
        printf("✅ All basic strand tests passed!\n");
        return 0;
    } else {
        printf("❌ Some tests failed\n");
        return 1;
    }
}
