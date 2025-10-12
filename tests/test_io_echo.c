/**
 * Test: Simple echo program using read_line and write_line
 *
 * This test creates a single strand that:
 * 1. Writes a prompt
 * 2. Reads a line from stdin
 * 3. Echoes it back to stdout
 * 4. Repeats 3 times
 */

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

// Echo strand: read line and echo it back
StackCell* echo_strand(StackCell* stack) {
    fprintf(stderr, "[echo] Starting echo loop\n");

    for (int i = 1; i <= 3; i++) {
        fprintf(stderr, "[echo] Iteration %d\n", i);

        // Write prompt
        fprintf(stderr, "[echo] Writing prompt\n");
        stack = write_line(make_string("Enter text:", stack));

        // Read line
        fprintf(stderr, "[echo] Reading line\n");
        stack = read_line(stack);

        // Check what we got
        if (!stack || stack->tag != TAG_STRING) {
            fprintf(stderr, "[echo] ERROR: expected string from read_line\n");
            return stack;
        }

        fprintf(stderr, "[echo] Read: '%s'\n", stack->value.s);

        // Echo it back with prefix
        char* echo_msg = malloc(strlen(stack->value.s) + 20);
        sprintf(echo_msg, "You typed: %s", stack->value.s);

        // Pop the read string and push our echo message
        StackCell* read_str = stack;
        stack = stack->next;
        free_cell(read_str);

        fprintf(stderr, "[echo] Writing echo\n");
        stack = write_line(make_string(echo_msg, stack));
        free(echo_msg);
    }

    fprintf(stderr, "[echo] Completing\n");
    return stack;
}

int main() {
    fprintf(stderr, "=== Echo Test ===\n");
    fprintf(stderr, "This test will read 3 lines from stdin and echo them back.\n");
    fprintf(stderr, "Note: In this test, stdin is non-blocking, so it will read from the input provided.\n\n");

    scheduler_init();
    strand_spawn(echo_strand, NULL);
    scheduler_run();
    scheduler_shutdown();

    fprintf(stderr, "\n✅ Test completed!\n");
    return 0;
}
