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
#include "../runtime/scheduler.h"

static int test_count = 0;

// ============================================================================
// Test 1: Minimal strand - does nothing
// ============================================================================

static bool minimal_ran = false;

StackCell* strand_minimal(StackCell* stack) {
    fprintf(stderr, "[TEST1] Minimal strand started\n");
    fflush(stderr);
    minimal_ran = true;
    fprintf(stderr, "[TEST1] Minimal strand returning\n");
    fflush(stderr);
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
    fprintf(stderr, "[TEST2] Strand with 64B locals started\n");
    fflush(stderr);

    char buffer[64];
    buffer[0] = 1;
    buffer[63] = 2;

    fprintf(stderr, "[TEST2] Locals allocated and used\n");
    fflush(stderr);
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
    fprintf(stderr, "[TEST3] Strand with 1KB locals started\n");
    fflush(stderr);

    char buffer[1024];
    buffer[0] = 1;
    buffer[1023] = 2;

    fprintf(stderr, "[TEST3] 1KB locals allocated and used\n");
    fflush(stderr);
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
    fprintf(stderr, "[TEST4] Strand with 2KB locals started\n");
    fflush(stderr);

    char buffer[2048];
    buffer[0] = 1;
    buffer[2047] = 2;

    fprintf(stderr, "[TEST4] 2KB locals allocated and used\n");
    fflush(stderr);
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
// Test 5: Strand with 4KB local variable (exceeds initial stack)
// ============================================================================

static bool xlarge_locals_ran = false;

StackCell* strand_xlarge_locals(StackCell* stack) {
    fprintf(stderr, "[TEST5] Strand with 4KB locals started\n");
    fflush(stderr);

    char buffer[4096];
    buffer[0] = 1;
    buffer[4095] = 2;

    fprintf(stderr, "[TEST5] 4KB locals allocated and used\n");
    fflush(stderr);
    xlarge_locals_ran = true;
    return stack;
}

void test_xlarge_locals(void) {
    printf("\n=== Test 5: Strand with 4KB local buffer (== initial stack size) ===\n");

    xlarge_locals_ran = false;
    scheduler_init();
    strand_spawn(strand_xlarge_locals, NULL);
    scheduler_run();
    scheduler_shutdown();

    if (xlarge_locals_ran) {
        printf("✓ Strand with 4KB locals executed\n");
        test_count++;
    } else {
        printf("✗ FAILED: Strand with 4KB locals did not execute\n");
        exit(1);
    }
}

// ============================================================================
// Test 6: Strand with 6KB local variable (REQUIRES stack growth)
// ============================================================================

static bool huge_locals_ran = false;

StackCell* strand_huge_locals(StackCell* stack) {
    fprintf(stderr, "[TEST6] Strand with 6KB locals started\n");
    fflush(stderr);

    char buffer[6 * 1024];
    buffer[0] = 1;
    buffer[6143] = 2;

    fprintf(stderr, "[TEST6] 6KB locals allocated and used\n");
    fflush(stderr);
    huge_locals_ran = true;
    return stack;
}

void test_huge_locals(void) {
    printf("\n=== Test 6: Strand with 6KB local buffer (REQUIRES stack growth) ===\n");

    huge_locals_ran = false;
    scheduler_init();
    strand_spawn(strand_huge_locals, NULL);
    scheduler_run();
    scheduler_shutdown();

    if (huge_locals_ran) {
        printf("✓ Strand with 6KB locals executed\n");
        test_count++;
    } else {
        printf("✗ FAILED: Strand with 6KB locals did not execute\n");
        exit(1);
    }
}

// ============================================================================
// Main
// ============================================================================

int main(void) {
    printf("=== Basic Strand Execution Tests ===\n");
    printf("Initial stack size: 4KB\n");
    printf("These tests progressively increase local variable size\n");
    printf("to isolate exactly where the crash happens.\n\n");

    test_minimal_strand();
    test_small_locals();
    test_medium_locals();
    test_large_locals();
    test_xlarge_locals();
    test_huge_locals();

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
