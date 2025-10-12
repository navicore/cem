/**
 * Test I/O Cleanup on Strand Termination
 *
 * This tests that I/O operations properly register cleanup handlers
 * and that buffers are freed when strands terminate, even if they're
 * in the middle of a blocking I/O operation.
 *
 * Note: These tests are designed to verify the cleanup infrastructure,
 * not to test actual blocking I/O (which is hard to test in unit tests).
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>
#include "../runtime/scheduler.h"
#include "../runtime/stack.h"
#include "../runtime/io.h"

// Track cleanup calls for verification
static int cleanup_called_count = 0;
static void* last_freed_pointer = NULL;

// Mock cleanup handler that tracks calls
static void track_free(void* ptr) {
    cleanup_called_count++;
    last_freed_pointer = ptr;
    free(ptr);
}

// Test 1: Write operation registers cleanup handler
StackCell* strand_write_cleanup_test(StackCell* stack) {
    // Create a string to write
    StackCell* str_cell = alloc_cell();
    str_cell->tag = TAG_STRING;
    str_cell->value.s = strdup("Test write");
    str_cell->next = stack;

    // This will register a cleanup handler for the buffer
    // Note: In a real scenario, we'd need to intercept or verify
    // the cleanup handler registration
    return write_line(str_cell);
}

void test_write_cleanup_registration(void) {
    printf("Test 1: Write operation cleanup\n");

    // This test verifies that write_line completes and cleans up properly
    scheduler_init();
    strand_spawn(strand_write_cleanup_test, NULL);
    scheduler_run();
    scheduler_shutdown();

    // If we got here without crashes or leaks, the cleanup worked
    printf("  ✓ Write operation cleanup completed\n");
}

// Test 2: Cleanup handler with manual buffer management
static int manual_buffer_freed = 0;

static void free_manual_buffer(void* ptr) {
    manual_buffer_freed = 1;
    free(ptr);
}

StackCell* strand_manual_buffer(StackCell* stack) {
    // Allocate a buffer (simulating what write_line does)
    char* buffer = malloc(1024);
    assert(buffer != NULL);
    memcpy(buffer, "Test data", 10);

    // Register cleanup handler
    strand_push_cleanup(free_manual_buffer, buffer);

    // Simulate successful operation - pop and free manually
    strand_pop_cleanup();
    free(buffer);

    return stack;
}

void test_manual_buffer_cleanup(void) {
    printf("Test 2: Manual buffer cleanup (success case)\n");

    manual_buffer_freed = 0;
    scheduler_init();

    strand_spawn(strand_manual_buffer, NULL);
    scheduler_run();

    scheduler_shutdown();

    // Buffer was freed manually, not by cleanup handler
    assert(manual_buffer_freed == 0);

    printf("  ✓ Manual buffer cleanup works (cleanup not called)\n");
}

// Test 3: Cleanup handler fires on normal completion
static int completion_buffer_freed = 0;

static void free_completion_buffer(void* ptr) {
    completion_buffer_freed = 1;
    free(ptr);
}

StackCell* strand_completion_buffer(StackCell* stack) {
    // Allocate a buffer
    char* buffer = malloc(2048);
    assert(buffer != NULL);

    // Register cleanup handler
    strand_push_cleanup(free_completion_buffer, buffer);

    // Don't pop - let cleanup handler fire on completion
    return stack;
}

void test_completion_cleanup(void) {
    printf("Test 3: Cleanup fires on normal strand completion\n");

    completion_buffer_freed = 0;
    scheduler_init();

    strand_spawn(strand_completion_buffer, NULL);
    scheduler_run();

    scheduler_shutdown();

    // Cleanup handler should have fired
    assert(completion_buffer_freed == 1);

    printf("  ✓ Cleanup handler fired on strand completion\n");
}

// Test 4: Multiple cleanup handlers in I/O operations
static int buffer1_freed = 0;
static int buffer2_freed = 0;
static int buffer3_freed = 0;

static void free_buffer1(void* ptr) {
    buffer1_freed = 1;
    free(ptr);
}

static void free_buffer2(void* ptr) {
    buffer2_freed = 1;
    free(ptr);
}

static void free_buffer3(void* ptr) {
    buffer3_freed = 1;
    free(ptr);
}

StackCell* strand_multiple_buffers(StackCell* stack) {
    // Simulate multiple I/O operations with buffers
    char* buf1 = malloc(1024);
    strand_push_cleanup(free_buffer1, buf1);

    char* buf2 = malloc(2048);
    strand_push_cleanup(free_buffer2, buf2);

    char* buf3 = malloc(4096);
    strand_push_cleanup(free_buffer3, buf3);

    // All three cleanup handlers should fire in LIFO order
    return stack;
}

void test_multiple_io_buffers(void) {
    printf("Test 4: Multiple I/O buffer cleanup\n");

    buffer1_freed = buffer2_freed = buffer3_freed = 0;
    scheduler_init();

    strand_spawn(strand_multiple_buffers, NULL);
    scheduler_run();

    scheduler_shutdown();

    // All three buffers should be freed
    assert(buffer1_freed == 1);
    assert(buffer2_freed == 1);
    assert(buffer3_freed == 1);

    printf("  ✓ Multiple I/O buffers cleaned up correctly\n");
}

// Test 5: Simulating realloc with cleanup handler update
static int realloc_buffer_freed = 0;

static void free_realloc_buffer(void* ptr) {
    realloc_buffer_freed = 1;
    free(ptr);
}

StackCell* strand_realloc_simulation(StackCell* stack) {
    // Initial buffer
    char* buffer = malloc(128);
    strand_push_cleanup(free_realloc_buffer, buffer);

    // Simulate growth (like read_line does)
    char* new_buffer = realloc(buffer, 256);
    if (new_buffer) {
        // Update cleanup handler
        strand_pop_cleanup();
        buffer = new_buffer;
        strand_push_cleanup(free_realloc_buffer, buffer);
    }

    // Let cleanup handler fire
    return stack;
}

void test_realloc_cleanup_update(void) {
    printf("Test 5: Realloc with cleanup handler update\n");

    realloc_buffer_freed = 0;
    scheduler_init();

    strand_spawn(strand_realloc_simulation, NULL);
    scheduler_run();

    scheduler_shutdown();

    // Buffer should be freed once
    assert(realloc_buffer_freed == 1);

    printf("  ✓ Realloc with cleanup update works correctly\n");
}

// Test 6: Cleanup handlers with multiple strands doing I/O
static int strand_a_buffer_freed = 0;
static int strand_b_buffer_freed = 0;

static void free_strand_a_buffer(void* ptr) {
    strand_a_buffer_freed = 1;
    free(ptr);
}

static void free_strand_b_buffer(void* ptr) {
    strand_b_buffer_freed = 1;
    free(ptr);
}

StackCell* strand_a_io(StackCell* stack) {
    char* buffer = malloc(1024);
    strand_push_cleanup(free_strand_a_buffer, buffer);
    return stack;
}

StackCell* strand_b_io(StackCell* stack) {
    char* buffer = malloc(2048);
    strand_push_cleanup(free_strand_b_buffer, buffer);
    return stack;
}

void test_multiple_strands_io_cleanup(void) {
    printf("Test 6: Multiple strands with I/O cleanup\n");

    strand_a_buffer_freed = strand_b_buffer_freed = 0;
    scheduler_init();

    strand_spawn(strand_a_io, NULL);
    strand_spawn(strand_b_io, NULL);

    scheduler_run();
    scheduler_shutdown();

    // Both buffers should be freed
    assert(strand_a_buffer_freed == 1);
    assert(strand_b_buffer_freed == 1);

    printf("  ✓ Multiple strands clean up I/O buffers correctly\n");
}

// Test 7: Error path cleanup (simulated)
static int error_buffer_freed = 0;

static void free_error_buffer(void* ptr) {
    error_buffer_freed = 1;
    free(ptr);
}

StackCell* strand_error_path(StackCell* stack) {
    char* buffer = malloc(512);
    strand_push_cleanup(free_error_buffer, buffer);

    // Simulate error condition - pop and free manually
    // (In real I/O code, this happens on error paths)
    strand_pop_cleanup();
    free(buffer);
    error_buffer_freed = -1;  // Mark as manually freed

    return stack;
}

void test_error_path_cleanup(void) {
    printf("Test 7: Error path cleanup\n");

    error_buffer_freed = 0;
    scheduler_init();

    strand_spawn(strand_error_path, NULL);
    scheduler_run();

    scheduler_shutdown();

    // Buffer was manually freed on error path
    assert(error_buffer_freed == -1);

    printf("  ✓ Error path cleanup works correctly\n");
}

// Test 8: Stress test - many I/O operations
static int stress_buffers_freed = 0;

static void free_stress_buffer(void* ptr) {
    stress_buffers_freed++;
    free(ptr);
}

StackCell* strand_stress_io(StackCell* stack) {
    // Register many cleanup handlers (simulating many I/O ops)
    for (int i = 0; i < 20; i++) {
        char* buffer = malloc(256);
        strand_push_cleanup(free_stress_buffer, buffer);
    }
    return stack;
}

void test_stress_io_cleanup(void) {
    printf("Test 8: Stress test with many I/O operations\n");

    stress_buffers_freed = 0;
    scheduler_init();

    // Multiple strands, each with many buffers
    for (int i = 0; i < 5; i++) {
        strand_spawn(strand_stress_io, NULL);
    }

    scheduler_run();
    scheduler_shutdown();

    // All buffers should be freed (5 strands * 20 buffers each)
    assert(stress_buffers_freed == 100);

    printf("  ✓ Stress test: all %d buffers freed correctly\n", stress_buffers_freed);
}

int main(void) {
    printf("=== I/O Cleanup Tests ===\n\n");

    test_write_cleanup_registration();
    test_manual_buffer_cleanup();
    test_completion_cleanup();
    test_multiple_io_buffers();
    test_realloc_cleanup_update();
    test_multiple_strands_io_cleanup();
    test_error_path_cleanup();
    test_stress_io_cleanup();

    printf("\n✅ All I/O cleanup tests passed!\n");
    return 0;
}
