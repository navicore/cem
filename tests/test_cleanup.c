/**
 * Test Cleanup Handler Infrastructure
 *
 * This tests the cleanup handler system that ensures resources are freed
 * when strands terminate (either normally or abnormally).
 *
 * Tests include:
 * - LIFO ordering of cleanup handlers
 * - Cleanup on normal strand completion
 * - Push and pop operations
 * - Multiple handlers per strand
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "../runtime/scheduler.h"
#include "../runtime/stack.h"

// Test execution tracking
static int cleanup_execution_order[100];
static int cleanup_execution_index = 0;
static int cleanup_call_count = 0;

static void reset_cleanup_tracking(void) {
    cleanup_execution_index = 0;
    cleanup_call_count = 0;
    memset(cleanup_execution_order, 0, sizeof(cleanup_execution_order));
}

static void record_cleanup(int value) {
    if (cleanup_execution_index < 100) {
        cleanup_execution_order[cleanup_execution_index++] = value;
    }
    cleanup_call_count++;
}

// Cleanup handler functions
static void cleanup_1(void* arg) {
    record_cleanup(1);
}

static void cleanup_2(void* arg) {
    record_cleanup(2);
}

static void cleanup_3(void* arg) {
    record_cleanup(3);
}

static void cleanup_4(void* arg) {
    record_cleanup(4);
}

// Test 1: Basic cleanup handler registration and execution
StackCell* strand_basic_cleanup(StackCell* stack) {
    // Register cleanup handlers
    strand_push_cleanup(cleanup_1, NULL);
    strand_push_cleanup(cleanup_2, NULL);
    strand_push_cleanup(cleanup_3, NULL);

    // Strand completes normally
    return stack;
}

void test_basic_cleanup(void) {
    printf("Test 1: Basic cleanup handler execution\n");

    reset_cleanup_tracking();
    scheduler_init();

    strand_spawn(strand_basic_cleanup, NULL);
    scheduler_run();

    scheduler_shutdown();

    // Verify cleanup handlers were called in LIFO order (3, 2, 1)
    assert(cleanup_call_count == 3);
    assert(cleanup_execution_order[0] == 3);
    assert(cleanup_execution_order[1] == 2);
    assert(cleanup_execution_order[2] == 1);

    printf("  ✓ Cleanup handlers executed in LIFO order\n");
}

// Test 2: Cleanup with pop (successful resource release)
static int resource_freed_count = 0;

static void free_resource(void* arg) {
    resource_freed_count++;
}

StackCell* strand_cleanup_with_pop(StackCell* stack) {
    // Allocate resource and register cleanup
    char* buffer = malloc(1024);
    strand_push_cleanup(free, buffer);

    // Use resource...

    // Successfully release resource, so pop the cleanup handler
    strand_pop_cleanup();
    free(buffer);

    // Register another cleanup that should fire
    strand_push_cleanup(free_resource, NULL);

    return stack;
}

void test_cleanup_with_pop(void) {
    printf("Test 2: Cleanup with pop (successful release)\n");

    resource_freed_count = 0;
    scheduler_init();

    strand_spawn(strand_cleanup_with_pop, NULL);
    scheduler_run();

    scheduler_shutdown();

    // Only the second cleanup should have fired
    assert(resource_freed_count == 1);

    printf("  ✓ Pop removes cleanup handler correctly\n");
}

// Test 3: Multiple strands with independent cleanup handlers
StackCell* strand_a_cleanup(StackCell* stack) {
    strand_push_cleanup(cleanup_1, NULL);
    strand_push_cleanup(cleanup_2, NULL);
    return stack;
}

StackCell* strand_b_cleanup(StackCell* stack) {
    strand_push_cleanup(cleanup_3, NULL);
    strand_push_cleanup(cleanup_4, NULL);
    return stack;
}

void test_multiple_strands_cleanup(void) {
    printf("Test 3: Multiple strands with independent cleanup\n");

    reset_cleanup_tracking();
    scheduler_init();

    strand_spawn(strand_a_cleanup, NULL);
    strand_spawn(strand_b_cleanup, NULL);

    scheduler_run();
    scheduler_shutdown();

    // Both strands should have had their handlers called
    assert(cleanup_call_count == 4);

    // We can't guarantee order between strands, but each strand's handlers
    // should be in LIFO order within that strand
    // Count occurrences
    int count_1 = 0, count_2 = 0, count_3 = 0, count_4 = 0;
    for (int i = 0; i < cleanup_execution_index; i++) {
        if (cleanup_execution_order[i] == 1) count_1++;
        if (cleanup_execution_order[i] == 2) count_2++;
        if (cleanup_execution_order[i] == 3) count_3++;
        if (cleanup_execution_order[i] == 4) count_4++;
    }

    assert(count_1 == 1 && count_2 == 1 && count_3 == 1 && count_4 == 1);

    printf("  ✓ Multiple strands have independent cleanup handlers\n");
}

// Test 4: Cleanup handler receives correct argument
static int cleanup_arg_values[10];
static int cleanup_arg_index = 0;

static void cleanup_with_arg(void* arg) {
    if (cleanup_arg_index < 10) {
        cleanup_arg_values[cleanup_arg_index++] = *(int*)arg;
    }
}

StackCell* strand_cleanup_args(StackCell* stack) {
    static int value1 = 42;
    static int value2 = 99;
    static int value3 = 123;

    strand_push_cleanup(cleanup_with_arg, &value1);
    strand_push_cleanup(cleanup_with_arg, &value2);
    strand_push_cleanup(cleanup_with_arg, &value3);

    return stack;
}

void test_cleanup_args(void) {
    printf("Test 4: Cleanup handlers receive correct arguments\n");

    cleanup_arg_index = 0;
    memset(cleanup_arg_values, 0, sizeof(cleanup_arg_values));

    scheduler_init();
    strand_spawn(strand_cleanup_args, NULL);
    scheduler_run();
    scheduler_shutdown();

    // Verify arguments were passed correctly (in LIFO order)
    assert(cleanup_arg_index == 3);
    assert(cleanup_arg_values[0] == 123);
    assert(cleanup_arg_values[1] == 99);
    assert(cleanup_arg_values[2] == 42);

    printf("  ✓ Cleanup handlers receive correct arguments\n");
}

// Test 5: Many cleanup handlers (stress test)
static int many_cleanup_count = 0;

static void many_cleanup_handler(void* arg) {
    many_cleanup_count++;
}

StackCell* strand_many_cleanups(StackCell* stack) {
    // Register 50 cleanup handlers
    for (int i = 0; i < 50; i++) {
        strand_push_cleanup(many_cleanup_handler, NULL);
    }
    return stack;
}

void test_many_cleanups(void) {
    printf("Test 5: Many cleanup handlers\n");

    many_cleanup_count = 0;
    scheduler_init();

    strand_spawn(strand_many_cleanups, NULL);
    scheduler_run();

    scheduler_shutdown();

    // All 50 should have been called
    assert(many_cleanup_count == 50);

    printf("  ✓ Many cleanup handlers work correctly\n");
}

// Test 6: Cleanup handler that frees actual allocated memory
static bool memory_leaked = true;

static void free_allocated_memory(void* arg) {
    free(arg);
    memory_leaked = false;
}

StackCell* strand_memory_cleanup(StackCell* stack) {
    // Allocate memory
    char* buffer = malloc(4096);
    assert(buffer != NULL);

    // Register cleanup
    strand_push_cleanup(free_allocated_memory, buffer);

    // Use the buffer
    memset(buffer, 0, 4096);

    // Strand completes - cleanup should free the buffer
    return stack;
}

void test_memory_cleanup(void) {
    printf("Test 6: Cleanup properly frees allocated memory\n");

    memory_leaked = true;
    scheduler_init();

    strand_spawn(strand_memory_cleanup, NULL);
    scheduler_run();

    scheduler_shutdown();

    assert(!memory_leaked);

    printf("  ✓ Memory is properly freed by cleanup handler\n");
}

// Test 7: Nested cleanup handlers (simulating nested resource acquisition)
StackCell* strand_nested_cleanup(StackCell* stack) {
    // Outer resource
    strand_push_cleanup(cleanup_1, NULL);

    // Inner resource 1
    strand_push_cleanup(cleanup_2, NULL);

    // Inner resource 2
    strand_push_cleanup(cleanup_3, NULL);

    // Innermost resource
    strand_push_cleanup(cleanup_4, NULL);

    return stack;
}

void test_nested_cleanup(void) {
    printf("Test 7: Nested cleanup handlers\n");

    reset_cleanup_tracking();
    scheduler_init();

    strand_spawn(strand_nested_cleanup, NULL);
    scheduler_run();

    scheduler_shutdown();

    // Should be called in reverse order (4, 3, 2, 1)
    assert(cleanup_call_count == 4);
    assert(cleanup_execution_order[0] == 4);
    assert(cleanup_execution_order[1] == 3);
    assert(cleanup_execution_order[2] == 2);
    assert(cleanup_execution_order[3] == 1);

    printf("  ✓ Nested cleanup handlers execute in correct order\n");
}

// Test 8: Update cleanup handler argument (simulating realloc)
static int* update_test_ptr = NULL;

static void cleanup_free_int(void* arg) {
    update_test_ptr = (int*)arg;
}

StackCell* strand_test_update_cleanup(StackCell* stack) {
    // Allocate initial resource
    int* ptr1 = malloc(sizeof(int));
    *ptr1 = 42;
    strand_push_cleanup(cleanup_free_int, ptr1);

    // Simulate realloc by allocating new memory and updating handler
    int* ptr2 = malloc(sizeof(int));
    *ptr2 = 99;

    // Update the cleanup handler to point to new memory
    strand_update_cleanup_arg(ptr2);

    // Free old pointer manually (as realloc would)
    free(ptr1);

    return stack;
}

void test_update_cleanup_arg(void) {
    printf("Test 8: Update cleanup argument (realloc pattern)\n");

    update_test_ptr = NULL;
    scheduler_init();

    strand_spawn(strand_test_update_cleanup, NULL);
    scheduler_run();

    scheduler_shutdown();

    // Cleanup should have freed the NEW pointer (ptr2), not the old one
    assert(update_test_ptr != NULL);
    assert(*update_test_ptr == 99);
    free(update_test_ptr);

    printf("  ✓ Cleanup argument update works correctly\n");
}

int main(void) {
    printf("=== Cleanup Handler Tests ===\n\n");

    test_basic_cleanup();
    test_cleanup_with_pop();
    test_multiple_strands_cleanup();
    test_cleanup_args();
    test_many_cleanups();
    test_memory_cleanup();
    test_nested_cleanup();
    test_update_cleanup_arg();

    printf("\n✅ All cleanup handler tests passed!\n");
    return 0;
}
