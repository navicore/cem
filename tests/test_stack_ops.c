/**
 * Unit tests for stack manipulation operations
 *
 * Tests: dup, drop, swap, over, rot, nip, tuck
 */

#include "../runtime/stack.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// Helper to create a test stack: bottom -> ... -> top
static StackCell* make_stack_3(int a, int b, int c) {
    StackCell* stack = NULL;
    stack = push_int(stack, a);  // bottom
    stack = push_int(stack, b);
    stack = push_int(stack, c);  // top
    return stack;
}

// Helper to check stack has expected int values from top down
static void assert_stack_ints(StackCell* stack, int count, ...) {
    va_list args;
    va_start(args, count);

    StackCell* current = stack;
    for (int i = 0; i < count; i++) {
        assert(current != NULL && "Stack shorter than expected");
        assert(current->tag == TAG_INT && "Expected INT on stack");
        int expected = va_arg(args, int);
        assert(current->value.i == expected && "Wrong value on stack");
        current = current->next;
    }
    assert(current == NULL && "Stack longer than expected");

    va_end(args);
}

void test_dup() {
    printf("Testing dup...\n");

    // Test: 1 2 3 dup -> 1 2 3 3
    StackCell* stack = make_stack_3(1, 2, 3);
    stack = dup(stack);
    assert_stack_ints(stack, 4, 3, 3, 2, 1);
    free_stack(stack);

    printf("  ✓ dup works\n");
}

void test_drop() {
    printf("Testing drop...\n");

    // Test: 1 2 3 drop -> 1 2
    StackCell* stack = make_stack_3(1, 2, 3);
    stack = drop(stack);
    assert_stack_ints(stack, 2, 2, 1);
    free_stack(stack);

    printf("  ✓ drop works\n");
}

void test_swap() {
    printf("Testing swap...\n");

    // Test: 1 2 3 swap -> 1 3 2
    StackCell* stack = make_stack_3(1, 2, 3);
    stack = swap(stack);
    assert_stack_ints(stack, 3, 2, 3, 1);
    free_stack(stack);

    printf("  ✓ swap works\n");
}

void test_over() {
    printf("Testing over...\n");

    // Test: 1 2 3 over -> 1 2 3 2
    StackCell* stack = make_stack_3(1, 2, 3);
    stack = over(stack);
    assert_stack_ints(stack, 4, 2, 3, 2, 1);
    free_stack(stack);

    printf("  ✓ over works\n");
}

void test_rot() {
    printf("Testing rot...\n");

    // Test: 1 2 3 rot -> 2 3 1
    // (A B C -> B C A where A=1, B=2, C=3)
    StackCell* stack = make_stack_3(1, 2, 3);
    stack = rot(stack);
    assert_stack_ints(stack, 3, 1, 3, 2);  // top to bottom: 1 3 2
    free_stack(stack);

    printf("  ✓ rot works\n");
}

void test_nip() {
    printf("Testing nip...\n");

    // Test: 1 2 3 nip -> 1 3
    StackCell* stack = make_stack_3(1, 2, 3);
    stack = nip(stack);
    assert_stack_ints(stack, 2, 3, 1);
    free_stack(stack);

    printf("  ✓ nip works\n");
}

void test_tuck() {
    printf("Testing tuck...\n");

    // Test: 1 2 3 tuck -> 1 3 2 3
    StackCell* stack = make_stack_3(1, 2, 3);
    stack = tuck(stack);
    assert_stack_ints(stack, 4, 3, 2, 3, 1);
    free_stack(stack);

    printf("  ✓ tuck works\n");
}

void test_string_dup() {
    printf("Testing dup with strings...\n");

    StackCell* stack = NULL;
    stack = push_string(stack, "hello");
    stack = dup(stack);

    assert(stack != NULL);
    assert(stack->tag == TAG_STRING);
    assert(strcmp(stack->value.s, "hello") == 0);
    assert(stack->next != NULL);
    assert(stack->next->tag == TAG_STRING);
    assert(strcmp(stack->next->value.s, "hello") == 0);

    // Verify deep copy - strings should be different pointers
    assert(stack->value.s != stack->next->value.s && "dup should deep copy strings");

    free_stack(stack);

    printf("  ✓ dup deep copies strings\n");
}

void test_bool_operations() {
    printf("Testing stack ops with booleans...\n");

    StackCell* stack = NULL;
    stack = push_bool(stack, true);
    stack = push_bool(stack, false);

    // Test swap with bools
    stack = swap(stack);
    assert(stack->value.b == true);
    assert(stack->next->value.b == false);

    // Test dup with bools
    stack = dup(stack);
    assert(stack->value.b == true);
    assert(stack->next->value.b == true);

    free_stack(stack);

    printf("  ✓ stack ops work with booleans\n");
}

int main() {
    printf("Running stack operation tests...\n\n");

    test_dup();
    test_drop();
    test_swap();
    test_over();
    test_rot();
    test_nip();
    test_tuck();
    test_string_dup();
    test_bool_operations();

    printf("\n✅ All stack operation tests passed!\n");
    return 0;
}
