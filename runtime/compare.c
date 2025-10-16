/**
 * Cem Runtime - Comparison Operations
 *
 * Implements comparison operations for integers.
 * Returns boolean values (true/false).
 */

#include "stack.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * Integer less than: ( a b -- bool )
 * Returns true if a < b
 */
StackCell *int_less(StackCell *stack) {
  if (!stack || !stack->next) {
    runtime_error("int_less: stack underflow");
  }

  if (stack->tag != TAG_INT || stack->next->tag != TAG_INT) {
    runtime_error("int_less: type error (expected Int Int)");
  }

  int64_t b = stack->value.i;       // Top of stack
  int64_t a = stack->next->value.i; // Second element

  // Pop both operands
  StackCell *rest = stack->next->next;
  free_cell(stack->next);
  free_cell(stack);

  // Push boolean result
  return push_bool(rest, a < b);
}

/**
 * Integer greater than: ( a b -- bool )
 * Returns true if a > b
 */
StackCell *int_greater(StackCell *stack) {
  if (!stack || !stack->next) {
    runtime_error("int_greater: stack underflow");
  }

  if (stack->tag != TAG_INT || stack->next->tag != TAG_INT) {
    runtime_error("int_greater: type error (expected Int Int)");
  }

  int64_t b = stack->value.i;
  int64_t a = stack->next->value.i;

  StackCell *rest = stack->next->next;
  free_cell(stack->next);
  free_cell(stack);

  return push_bool(rest, a > b);
}

/**
 * Integer less than or equal: ( a b -- bool )
 * Returns true if a <= b
 */
StackCell *int_less_equal(StackCell *stack) {
  if (!stack || !stack->next) {
    runtime_error("int_less_equal: stack underflow");
  }

  if (stack->tag != TAG_INT || stack->next->tag != TAG_INT) {
    runtime_error("int_less_equal: type error (expected Int Int)");
  }

  int64_t b = stack->value.i;
  int64_t a = stack->next->value.i;

  StackCell *rest = stack->next->next;
  free_cell(stack->next);
  free_cell(stack);

  return push_bool(rest, a <= b);
}

/**
 * Integer greater than or equal: ( a b -- bool )
 * Returns true if a >= b
 */
StackCell *int_greater_equal(StackCell *stack) {
  if (!stack || !stack->next) {
    runtime_error("int_greater_equal: stack underflow");
  }

  if (stack->tag != TAG_INT || stack->next->tag != TAG_INT) {
    runtime_error("int_greater_equal: type error (expected Int Int)");
  }

  int64_t b = stack->value.i;
  int64_t a = stack->next->value.i;

  StackCell *rest = stack->next->next;
  free_cell(stack->next);
  free_cell(stack);

  return push_bool(rest, a >= b);
}

/**
 * Integer equality: ( a b -- bool )
 * Returns true if a == b
 */
StackCell *int_equal(StackCell *stack) {
  if (!stack || !stack->next) {
    runtime_error("int_equal: stack underflow");
  }

  if (stack->tag != TAG_INT || stack->next->tag != TAG_INT) {
    runtime_error("int_equal: type error (expected Int Int)");
  }

  int64_t b = stack->value.i;
  int64_t a = stack->next->value.i;

  StackCell *rest = stack->next->next;
  free_cell(stack->next);
  free_cell(stack);

  return push_bool(rest, a == b);
}

/**
 * Integer inequality: ( a b -- bool )
 * Returns true if a != b
 */
StackCell *int_not_equal(StackCell *stack) {
  if (!stack || !stack->next) {
    runtime_error("int_not_equal: stack underflow");
  }

  if (stack->tag != TAG_INT || stack->next->tag != TAG_INT) {
    runtime_error("int_not_equal: type error (expected Int Int)");
  }

  int64_t b = stack->value.i;
  int64_t a = stack->next->value.i;

  StackCell *rest = stack->next->next;
  free_cell(stack->next);
  free_cell(stack);

  return push_bool(rest, a != b);
}
