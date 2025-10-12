#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <assert.h>
#include "../runtime/scheduler.h"
#include "../runtime/stack.h"

static int counter = 0;

// Simple strand that doesn't yield
StackCell* simple_strand(StackCell* stack) {
    printf("Simple strand executing, counter=%d\n", counter);
    counter++;
    return stack;
}

int main() {
    printf("=== Simple Scheduler Test ===\n\n");

    scheduler_init();

    printf("Spawning strand...\n");
    strand_spawn(simple_strand, NULL);

    printf("Running scheduler...\n");
    scheduler_run();

    scheduler_shutdown();

    printf("Counter after execution: %d\n", counter);
    assert(counter == 1);

    printf("\n✅ Test passed!\n");
    return 0;
}
