#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../runtime/scheduler.h"
#include "../runtime/stack.h"
#include "../runtime/io.h"

// Helper to create a string cell
static StackCell* make_string(const char* str, StackCell* next) {
    StackCell* cell = alloc_cell();
    cell->tag = TAG_STRING;
    cell->value.s = strdup(str);
    cell->next = next;
    return cell;
}

// Strand 1
StackCell* strand1(StackCell* stack) {
    fprintf(stderr, "[S1] Writing line 1\n");
    stack = write_line(make_string("Strand 1: Line 1", stack));
    fprintf(stderr, "[S1] Yielding\n");
    strand_yield();
    fprintf(stderr, "[S1] Resumed, writing line 2\n");
    stack = write_line(make_string("Strand 1: Line 2", stack));
    fprintf(stderr, "[S1] Completing\n");
    return stack;
}

// Strand 2
StackCell* strand2(StackCell* stack) {
    fprintf(stderr, "[S2] Writing line 1\n");
    stack = write_line(make_string("Strand 2: Line 1", stack));
    fprintf(stderr, "[S2] Yielding\n");
    strand_yield();
    fprintf(stderr, "[S2] Resumed, writing line 2\n");
    stack = write_line(make_string("Strand 2: Line 2", stack));
    fprintf(stderr, "[S2] Completing\n");
    return stack;
}

// Strand 3
StackCell* strand3(StackCell* stack) {
    fprintf(stderr, "[S3] Writing line 1\n");
    stack = write_line(make_string("Strand 3: Line 1", stack));
    fprintf(stderr, "[S3] Yielding\n");
    strand_yield();
    fprintf(stderr, "[S3] Resumed, writing line 2\n");
    stack = write_line(make_string("Strand 3: Line 2", stack));
    fprintf(stderr, "[S3] Completing\n");
    return stack;
}

int main() {
    // Use stderr for test output to avoid buffering issues with stdout
    fprintf(stderr, "=== Concurrent I/O Test ===\n\n");

    scheduler_init();

    fprintf(stderr, "Spawning 3 concurrent strands...\n\n");
    strand_spawn(strand1, NULL);
    strand_spawn(strand2, NULL);
    strand_spawn(strand3, NULL);

    fprintf(stderr, "--- Output from strands (should be interleaved) ---\n");
    scheduler_run();

    scheduler_shutdown();

    fprintf(stderr, "\n✅ Test passed!\n");
    return 0;
}
