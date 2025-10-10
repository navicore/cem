/**
 * Test program for Cem runtime
 */

#include "stack.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

void test_push_and_drop() {
    printf("Testing push and drop...\n");

    StackCell* stack = NULL;
    stack = push_int(stack, 42);
    assert(stack != NULL);
    assert(stack->tag == TAG_INT);
    assert(stack->value.i == 42);

    stack = drop(stack);
    assert(stack == NULL);

    printf("  ✓ push_int and drop work\n");
}

void test_arithmetic() {
    printf("Testing arithmetic...\n");

    // Test: 10 20 + => 30
    StackCell* stack = NULL;
    stack = push_int(stack, 10);
    stack = push_int(stack, 20);
    stack = add(stack);
    assert(stack->value.i == 30);
    free_stack(stack);

    // Test: 10 3 - => 7
    stack = NULL;
    stack = push_int(stack, 10);
    stack = push_int(stack, 3);
    stack = subtract(stack);
    assert(stack->value.i == 7);
    free_stack(stack);

    // Test: 6 7 * => 42
    stack = NULL;
    stack = push_int(stack, 6);
    stack = push_int(stack, 7);
    stack = multiply(stack);
    assert(stack->value.i == 42);
    free_stack(stack);

    // Test: 20 4 / => 5
    stack = NULL;
    stack = push_int(stack, 20);
    stack = push_int(stack, 4);
    stack = divide_op(stack);
    assert(stack->value.i == 5);
    free_stack(stack);

    printf("  ✓ add, subtract, multiply, divide work\n");
}

void test_stack_ops() {
    printf("Testing stack operations...\n");

    // Test dup: 42 dup => 42 42
    StackCell* stack = NULL;
    stack = push_int(stack, 42);
    stack = dup(stack);
    assert(stack->value.i == 42);
    assert(stack->next->value.i == 42);
    free_stack(stack);

    // Test swap: 1 2 swap => 2 1
    stack = NULL;
    stack = push_int(stack, 1);
    stack = push_int(stack, 2);
    stack = swap(stack);
    assert(stack->value.i == 1);
    assert(stack->next->value.i == 2);
    free_stack(stack);

    // Test over: 1 2 over => 1 2 1
    stack = NULL;
    stack = push_int(stack, 1);
    stack = push_int(stack, 2);
    stack = over(stack);
    assert(stack->value.i == 1);
    assert(stack->next->value.i == 2);
    assert(stack->next->next->value.i == 1);
    free_stack(stack);

    printf("  ✓ dup, swap, over work\n");
}

void test_comparisons() {
    printf("Testing comparisons...\n");

    // Test <
    StackCell* stack = NULL;
    stack = push_int(stack, 5);
    stack = push_int(stack, 10);
    stack = less_than(stack);
    assert(stack->tag == TAG_BOOL);
    assert(stack->value.b == true);
    free_stack(stack);

    // Test >
    stack = NULL;
    stack = push_int(stack, 10);
    stack = push_int(stack, 5);
    stack = greater_than(stack);
    assert(stack->value.b == true);
    free_stack(stack);

    // Test =
    stack = NULL;
    stack = push_int(stack, 42);
    stack = push_int(stack, 42);
    stack = equal(stack);
    assert(stack->value.b == true);
    free_stack(stack);

    printf("  ✓ less_than, greater_than, equal work\n");
}

void test_strings() {
    printf("Testing strings...\n");

    StackCell* stack = NULL;
    stack = push_string(stack, "hello");
    assert(stack->tag == TAG_STRING);
    assert(strcmp(stack->value.s, "hello") == 0);

    stack = push_string(stack, "world");
    stack = equal(stack);
    assert(stack->value.b == false);
    free_stack(stack);

    stack = NULL;
    stack = push_string(stack, "test");
    stack = push_string(stack, "test");
    stack = equal(stack);
    assert(stack->value.b == true);
    free_stack(stack);

    printf("  ✓ push_string and string equality work\n");
}

void test_example_program() {
    printf("Testing example: (5 + 3) * 2...\n");

    // Simulates: 5 3 + 2 *
    StackCell* stack = NULL;
    stack = push_int(stack, 5);
    stack = push_int(stack, 3);
    stack = add(stack);     // Stack: 8
    stack = push_int(stack, 2);
    stack = multiply(stack); // Stack: 16

    assert(stack->value.i == 16);
    free_stack(stack);

    printf("  ✓ Example program: result = 16\n");
}

int main() {
    printf("=== Cem Runtime Tests ===\n\n");

    test_push_and_drop();
    test_arithmetic();
    test_stack_ops();
    test_comparisons();
    test_strings();
    test_example_program();

    printf("\n✅ All runtime tests passed!\n");
    return 0;
}
