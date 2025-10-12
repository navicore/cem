#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <assert.h>
#include "../runtime/scheduler.h"
#include "../runtime/stack.h"

static int counter = 0;

// Strand that yields once
StackCell* yielding_strand(StackCell* stack) {
    printf("Strand: before yield, counter=%d\n", counter);
    counter++;

    strand_yield();

    printf("Strand: after yield, counter=%d\n", counter);
    counter++;

    return stack;
}

int main() {
    printf("=== Yielding Scheduler Test ===\n\n");

    scheduler_init();

    printf("Spawning strand...\n");
    strand_spawn(yielding_strand, NULL);

    printf("Running scheduler...\n");
    scheduler_run();

    scheduler_shutdown();

    printf("Counter after execution: %d\n", counter);
    assert(counter == 2);

    printf("\n✅ Test passed!\n");
    return 0;
}
