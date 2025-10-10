/**
 * Cem Runtime - Stack Machine Implementation
 *
 * This file implements the runtime stack operations for Cem.
 */

#include "stack.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// ============================================================================
// Utility Functions
// ============================================================================

StackCell* alloc_cell(void) {
    StackCell* cell = (StackCell*)malloc(sizeof(StackCell));
    if (!cell) {
        runtime_error("Out of memory");
    }
    cell->next = NULL;
    return cell;
}

void free_cell(StackCell* cell) {
    if (!cell) return;

    // Free owned resources based on tag
    if (cell->tag == TAG_STRING && cell->value.s) {
        free(cell->value.s);
    }
    // TODO: Free variant data when implemented

    free(cell);
}

void free_stack(StackCell* stack) {
    while (stack) {
        StackCell* next = stack->next;
        free_cell(stack);
        stack = next;
    }
}

void runtime_error(const char* message) {
    fprintf(stderr, "Runtime error: %s\n", message);
    exit(1);
}

void print_stack(StackCell* stack) {
    printf("Stack (top to bottom): ");
    StackCell* current = stack;
    while (current) {
        switch (current->tag) {
            case TAG_INT:
                printf("%lld ", (long long)current->value.i);
                break;
            case TAG_BOOL:
                printf("%s ", current->value.b ? "true" : "false");
                break;
            case TAG_STRING:
                printf("\"%s\" ", current->value.s);
                break;
            case TAG_QUOTATION:
                printf("<quotation> ");
                break;
            case TAG_VARIANT:
                printf("<variant:%u> ", current->value.variant.tag);
                break;
        }
        current = current->next;
    }
    printf("\n");
}

// ============================================================================
// Stack Operations
// ============================================================================

StackCell* dup(StackCell* stack) {
    if (!stack) {
        runtime_error("dup: stack underflow");
    }

    StackCell* new_cell = alloc_cell();
    new_cell->tag = stack->tag;

    // Deep copy the value
    switch (stack->tag) {
        case TAG_INT:
            new_cell->value.i = stack->value.i;
            break;
        case TAG_BOOL:
            new_cell->value.b = stack->value.b;
            break;
        case TAG_STRING:
            new_cell->value.s = strdup(stack->value.s);
            if (!new_cell->value.s) {
                runtime_error("dup: out of memory");
            }
            break;
        case TAG_QUOTATION:
            new_cell->value.quotation = stack->value.quotation;
            break;
        case TAG_VARIANT:
            // TODO: Implement variant copying
            runtime_error("dup: variant copying not yet implemented");
            break;
    }

    new_cell->next = stack;
    return new_cell;
}

StackCell* drop(StackCell* stack) {
    if (!stack) {
        runtime_error("drop: stack underflow");
    }

    StackCell* rest = stack->next;
    free_cell(stack);
    return rest;
}

StackCell* swap(StackCell* stack) {
    if (!stack || !stack->next) {
        runtime_error("swap: stack underflow");
    }

    StackCell* first = stack;
    StackCell* second = stack->next;
    StackCell* rest = second->next;

    second->next = first;
    first->next = rest;

    return second;
}

StackCell* over(StackCell* stack) {
    if (!stack || !stack->next) {
        runtime_error("over: stack underflow");
    }

    StackCell* second = stack->next;
    StackCell* new_cell = alloc_cell();
    new_cell->tag = second->tag;

    // Deep copy second element
    switch (second->tag) {
        case TAG_INT:
            new_cell->value.i = second->value.i;
            break;
        case TAG_BOOL:
            new_cell->value.b = second->value.b;
            break;
        case TAG_STRING:
            new_cell->value.s = strdup(second->value.s);
            break;
        case TAG_QUOTATION:
            new_cell->value.quotation = second->value.quotation;
            break;
        case TAG_VARIANT:
            runtime_error("over: variant copying not yet implemented");
            break;
    }

    new_cell->next = stack;
    return new_cell;
}

StackCell* rot(StackCell* stack) {
    if (!stack || !stack->next || !stack->next->next) {
        runtime_error("rot: stack underflow");
    }

    StackCell* first = stack;
    StackCell* second = stack->next;
    StackCell* third = second->next;
    StackCell* rest = third->next;

    // Rotate: A B C -> B C A
    second->next = third;
    third->next = first;
    first->next = rest;

    return second;
}

// ============================================================================
// Arithmetic Operations
// ============================================================================

StackCell* add(StackCell* stack) {
    if (!stack || !stack->next) {
        runtime_error("add: stack underflow");
    }
    if (stack->tag != TAG_INT || stack->next->tag != TAG_INT) {
        runtime_error("add: type error (expected Int Int)");
    }

    int64_t b = stack->value.i;
    int64_t a = stack->next->value.i;

    StackCell* rest = stack->next->next;
    free_cell(stack->next);
    free_cell(stack);

    return push_int(rest, a + b);
}

StackCell* subtract(StackCell* stack) {
    if (!stack || !stack->next) {
        runtime_error("subtract: stack underflow");
    }
    if (stack->tag != TAG_INT || stack->next->tag != TAG_INT) {
        runtime_error("subtract: type error (expected Int Int)");
    }

    int64_t b = stack->value.i;
    int64_t a = stack->next->value.i;

    StackCell* rest = stack->next->next;
    free_cell(stack->next);
    free_cell(stack);

    return push_int(rest, a - b);
}

StackCell* multiply(StackCell* stack) {
    if (!stack || !stack->next) {
        runtime_error("multiply: stack underflow");
    }
    if (stack->tag != TAG_INT || stack->next->tag != TAG_INT) {
        runtime_error("multiply: type error (expected Int Int)");
    }

    int64_t b = stack->value.i;
    int64_t a = stack->next->value.i;

    StackCell* rest = stack->next->next;
    free_cell(stack->next);
    free_cell(stack);

    return push_int(rest, a * b);
}

StackCell* divide_op(StackCell* stack) {
    if (!stack || !stack->next) {
        runtime_error("divide: stack underflow");
    }
    if (stack->tag != TAG_INT || stack->next->tag != TAG_INT) {
        runtime_error("divide: type error (expected Int Int)");
    }

    int64_t b = stack->value.i;
    int64_t a = stack->next->value.i;

    if (b == 0) {
        runtime_error("divide: division by zero");
    }

    StackCell* rest = stack->next->next;
    free_cell(stack->next);
    free_cell(stack);

    return push_int(rest, a / b);
}

// ============================================================================
// Comparison Operations
// ============================================================================

StackCell* less_than(StackCell* stack) {
    if (!stack || !stack->next) {
        runtime_error("less_than: stack underflow");
    }
    if (stack->tag != TAG_INT || stack->next->tag != TAG_INT) {
        runtime_error("less_than: type error (expected Int Int)");
    }

    int64_t b = stack->value.i;
    int64_t a = stack->next->value.i;

    StackCell* rest = stack->next->next;
    free_cell(stack->next);
    free_cell(stack);

    return push_bool(rest, a < b);
}

StackCell* greater_than(StackCell* stack) {
    if (!stack || !stack->next) {
        runtime_error("greater_than: stack underflow");
    }
    if (stack->tag != TAG_INT || stack->next->tag != TAG_INT) {
        runtime_error("greater_than: type error (expected Int Int)");
    }

    int64_t b = stack->value.i;
    int64_t a = stack->next->value.i;

    StackCell* rest = stack->next->next;
    free_cell(stack->next);
    free_cell(stack);

    return push_bool(rest, a > b);
}

StackCell* equal(StackCell* stack) {
    if (!stack || !stack->next) {
        runtime_error("equal: stack underflow");
    }

    bool result = false;

    // Check if tags match
    if (stack->tag == stack->next->tag) {
        switch (stack->tag) {
            case TAG_INT:
                result = (stack->value.i == stack->next->value.i);
                break;
            case TAG_BOOL:
                result = (stack->value.b == stack->next->value.b);
                break;
            case TAG_STRING:
                result = (strcmp(stack->value.s, stack->next->value.s) == 0);
                break;
            case TAG_QUOTATION:
                result = (stack->value.quotation == stack->next->value.quotation);
                break;
            case TAG_VARIANT:
                // TODO: Implement variant equality
                runtime_error("equal: variant comparison not yet implemented");
                break;
        }
    }

    StackCell* rest = stack->next->next;
    free_cell(stack->next);
    free_cell(stack);

    return push_bool(rest, result);
}

// ============================================================================
// Push Operations
// ============================================================================

StackCell* push_int(StackCell* stack, int64_t value) {
    StackCell* cell = alloc_cell();
    cell->tag = TAG_INT;
    cell->value.i = value;
    cell->next = stack;
    return cell;
}

StackCell* push_bool(StackCell* stack, bool value) {
    StackCell* cell = alloc_cell();
    cell->tag = TAG_BOOL;
    cell->value.b = value;
    cell->next = stack;
    return cell;
}

StackCell* push_string(StackCell* stack, const char* value) {
    StackCell* cell = alloc_cell();
    cell->tag = TAG_STRING;
    cell->value.s = strdup(value);
    if (!cell->value.s) {
        runtime_error("push_string: out of memory");
    }
    cell->next = stack;
    return cell;
}

// ============================================================================
// Control Flow Operations (Placeholders)
// ============================================================================

StackCell* call_quotation(StackCell* stack) {
    // TODO: Implement when quotations are supported
    runtime_error("call_quotation: not yet implemented");
    return stack;
}

StackCell* if_then_else(StackCell* stack) {
    // TODO: Implement when control flow is supported
    runtime_error("if_then_else: not yet implemented");
    return stack;
}
