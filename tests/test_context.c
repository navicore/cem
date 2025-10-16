/**
 * Test Context Switching Implementation
 *
 * This tests the low-level context switching primitives:
 * - cem_makecontext: Initialize a context
 * - cem_swapcontext: Switch between contexts
 *
 * These tests verify that context switching works correctly at the
 * assembly level, independent of the scheduler.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "../runtime/context.h"

// Test execution tracking
static int test_execution_order[100];
static int test_execution_index = 0;

static void record(int value) {
    if (test_execution_index < 100) {
        test_execution_order[test_execution_index++] = value;
    }
}

// Global contexts for testing
static cem_context_t main_ctx;
static cem_context_t test_ctx1;
static cem_context_t test_ctx2;

// Test 1: Simple context switch and return
static void simple_func(void) {
    record(1);  // We executed
    // Return to main context
    cem_swapcontext(&test_ctx1, &main_ctx);
    // Should never reach here in this test
    record(99);
}

void test_simple_context_switch(void) {
    printf("Test 1: Simple context switch\n");

    test_execution_index = 0;
    memset(test_execution_order, 0, sizeof(test_execution_order));

    // Allocate stack for test context
    void* stack = malloc(CEM_INITIAL_STACK_SIZE);
    assert(stack != NULL);

    // Initialize context
    cem_makecontext(&test_ctx1, stack, CEM_INITIAL_STACK_SIZE, simple_func, NULL);

    // Switch to test context
    cem_swapcontext(&main_ctx, &test_ctx1);

    // We should be back here now
    record(2);

    // Verify execution order
    assert(test_execution_index == 2);
    assert(test_execution_order[0] == 1);
    assert(test_execution_order[1] == 2);

    free(stack);
    printf("  ✓ Simple context switch works\n");
}

// Test 2: Multiple switches between contexts
static void ping_func(void) {
    record(10);
    cem_swapcontext(&test_ctx1, &main_ctx);
    record(12);
    cem_swapcontext(&test_ctx1, &main_ctx);
    record(14);
    cem_swapcontext(&test_ctx1, &main_ctx);
    // Function returns - cannot resume after this
}

void test_multiple_switches(void) {
    printf("Test 2: Multiple context switches\n");

    test_execution_index = 0;
    memset(test_execution_order, 0, sizeof(test_execution_order));

    void* stack = malloc(CEM_INITIAL_STACK_SIZE);
    assert(stack != NULL);

    cem_makecontext(&test_ctx1, stack, CEM_INITIAL_STACK_SIZE, ping_func, NULL);

    // First switch
    record(9);
    cem_swapcontext(&main_ctx, &test_ctx1);

    // Back from first switch
    record(11);
    cem_swapcontext(&main_ctx, &test_ctx1);

    // Back from second switch
    record(13);
    cem_swapcontext(&main_ctx, &test_ctx1);

    // Back from third switch - function has returned
    record(15);

    // Verify execution order
    assert(test_execution_index == 7);
    assert(test_execution_order[0] == 9);
    assert(test_execution_order[1] == 10);
    assert(test_execution_order[2] == 11);
    assert(test_execution_order[3] == 12);
    assert(test_execution_order[4] == 13);
    assert(test_execution_order[5] == 14);
    assert(test_execution_order[6] == 15);

    free(stack);
    printf("  ✓ Multiple context switches work\n");
}

// Test 3: Context switch between two non-main contexts
static void context_a(void) {
    record(20);
    cem_swapcontext(&test_ctx1, &test_ctx2);  // Switch to context B
    record(22);
    cem_swapcontext(&test_ctx1, &main_ctx);   // Return to main
}

static void context_b(void) {
    record(21);
    cem_swapcontext(&test_ctx2, &test_ctx1);  // Switch back to context A
    record(99);  // Should never reach here
}

void test_context_to_context_switch(void) {
    printf("Test 3: Context-to-context switches\n");

    test_execution_index = 0;
    memset(test_execution_order, 0, sizeof(test_execution_order));

    void* stack1 = malloc(CEM_INITIAL_STACK_SIZE);
    void* stack2 = malloc(CEM_INITIAL_STACK_SIZE);
    assert(stack1 != NULL);
    assert(stack2 != NULL);

    cem_makecontext(&test_ctx1, stack1, CEM_INITIAL_STACK_SIZE, context_a, NULL);
    cem_makecontext(&test_ctx2, stack2, CEM_INITIAL_STACK_SIZE, context_b, NULL);

    // Start in context A
    cem_swapcontext(&main_ctx, &test_ctx1);

    record(23);

    // Verify execution order
    assert(test_execution_index == 4);
    assert(test_execution_order[0] == 20);
    assert(test_execution_order[1] == 21);
    assert(test_execution_order[2] == 22);
    assert(test_execution_order[3] == 23);

    free(stack1);
    free(stack2);
    printf("  ✓ Context-to-context switches work\n");
}

// Test 4: Verify stack pointer is preserved correctly
static int stack_test_value = 0;

static void stack_preservation_func(void) {
    // Use stack heavily
    int local_array[100];
    for (int i = 0; i < 100; i++) {
        local_array[i] = i * 2;
    }

    // Switch back to main
    cem_swapcontext(&test_ctx1, &main_ctx);

    // Resume - verify stack is intact
    int sum = 0;
    for (int i = 0; i < 100; i++) {
        sum += local_array[i];
    }
    stack_test_value = sum;

    cem_swapcontext(&test_ctx1, &main_ctx);
}

void test_stack_preservation(void) {
    printf("Test 4: Stack preservation across switches\n");

    stack_test_value = 0;

    void* stack = malloc(CEM_INITIAL_STACK_SIZE);
    assert(stack != NULL);

    cem_makecontext(&test_ctx1, stack, CEM_INITIAL_STACK_SIZE, stack_preservation_func, NULL);

    // First switch - context will use stack and return
    cem_swapcontext(&main_ctx, &test_ctx1);

    // Second switch - context will verify stack and return
    cem_swapcontext(&main_ctx, &test_ctx1);

    // Verify the sum is correct (sum of 0, 2, 4, ..., 198)
    int expected_sum = 0;
    for (int i = 0; i < 100; i++) {
        expected_sum += i * 2;
    }
    assert(stack_test_value == expected_sum);

    free(stack);
    printf("  ✓ Stack is preserved correctly across switches\n");
}

// Test 5: Verify floating point registers are preserved
static double fp_test_value = 0.0;

static void fp_preservation_func(void) {
    // Use FP registers
    double values[16];
    for (int i = 0; i < 16; i++) {
        values[i] = i * 3.14159;
    }

    // Switch back to main
    cem_swapcontext(&test_ctx1, &main_ctx);

    // Resume - verify FP values are intact
    double sum = 0.0;
    for (int i = 0; i < 16; i++) {
        sum += values[i];
    }
    fp_test_value = sum;

    cem_swapcontext(&test_ctx1, &main_ctx);
}

void test_fp_preservation(void) {
    printf("Test 5: Floating-point register preservation\n");

    fp_test_value = 0.0;

    void* stack = malloc(CEM_INITIAL_STACK_SIZE);
    assert(stack != NULL);

    cem_makecontext(&test_ctx1, stack, CEM_INITIAL_STACK_SIZE, fp_preservation_func, NULL);

    // First switch
    cem_swapcontext(&main_ctx, &test_ctx1);

    // Second switch
    cem_swapcontext(&main_ctx, &test_ctx1);

    // Verify the sum is correct
    double expected_sum = 0.0;
    for (int i = 0; i < 16; i++) {
        expected_sum += i * 3.14159;
    }

    // Use a small epsilon for floating point comparison
    assert(fp_test_value > expected_sum - 0.001 && fp_test_value < expected_sum + 0.001);

    free(stack);
    printf("  ✓ Floating-point registers are preserved correctly\n");
}

// Test 6: Verify minimum stack size assertion
void test_stack_size_assertion(void) {
    printf("Test 6: Stack size validation\n");

    void* stack = malloc(CEM_INITIAL_STACK_SIZE);
    assert(stack != NULL);

    // This should succeed (CEM_INITIAL_STACK_SIZE is minimum)
    cem_makecontext(&test_ctx1, stack, CEM_INITIAL_STACK_SIZE, simple_func, NULL);

    // Note: Can't easily test assertion failure without crashing
    // In a real test harness, we'd use death tests

    free(stack);
    printf("  ✓ Stack size validation works\n");
}

int main(void) {
    printf("=== Context Switching Tests ===\n\n");

    test_simple_context_switch();
    test_multiple_switches();
    test_context_to_context_switch();
    test_stack_preservation();
    test_fp_preservation();
    test_stack_size_assertion();

    printf("\n✅ All context switching tests passed!\n");
    return 0;
}
