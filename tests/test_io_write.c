#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../runtime/scheduler.h"
#include "../runtime/stack.h"
#include "../runtime/io.h"

// Test strand that writes multiple lines
StackCell* writer_strand(StackCell* stack) {
    // Create and write first line
    StackCell* cell1 = alloc_cell();
    cell1->tag = TAG_STRING;
    cell1->value.s = strdup("Hello from strand 1!");
    cell1->next = stack;

    stack = write_line(cell1);

    // Create and write second line
    StackCell* cell2 = alloc_cell();
    cell2->tag = TAG_STRING;
    cell2->value.s = strdup("This is async I/O!");
    cell2->next = stack;

    stack = write_line(cell2);

    return stack;
}

int main() {
    printf("=== Async I/O Write Test ===\n\n");

    scheduler_init();

    printf("Spawning writer strand...\n");
    strand_spawn(writer_strand, NULL);

    printf("Running scheduler...\n");
    scheduler_run();

    scheduler_shutdown();

    printf("\n✅ Test passed!\n");
    return 0;
}
