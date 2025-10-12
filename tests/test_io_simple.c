#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../runtime/scheduler.h"
#include "../runtime/stack.h"
#include "../runtime/io.h"

// Test strand that writes one line and completes
StackCell* simple_writer(StackCell* stack) {
    fprintf(stderr, "[strand] Creating string cell...\n");
    StackCell* cell = alloc_cell();
    cell->tag = TAG_STRING;
    cell->value.s = strdup("Hello, async world!");
    cell->next = stack;

    fprintf(stderr, "[strand] Calling write_line...\n");
    stack = write_line(cell);

    fprintf(stderr, "[strand] write_line returned, strand completing\n");
    return stack;
}

int main() {
    fprintf(stderr, "=== Simple I/O Test ===\n\n");

    scheduler_init();

    fprintf(stderr, "Spawning writer strand...\n");
    strand_spawn(simple_writer, NULL);

    fprintf(stderr, "Running scheduler...\n");
    scheduler_run();

    fprintf(stderr, "Scheduler returned\n");
    scheduler_shutdown();

    fprintf(stderr, "\n✅ Test passed!\n");
    return 0;
}
