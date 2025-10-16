/**
 * Cem Runtime - Type Conversion Operations
 *
 * Implements conversions between types:
 * - int-to-string: Convert integer to string representation
 * - bool-to-string: Convert boolean to "true" or "false"
 * - string-to-int: Parse string to integer (TODO)
 */

#include "stack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * int-to-string: ( Int -- String )
 * Convert an integer to its string representation
 */
StackCell *int_to_string(StackCell *stack) {
  if (!stack) {
    runtime_error("int_to_string: stack underflow");
  }

  if (stack->tag != TAG_INT) {
    runtime_error("int_to_string: type error (expected Int)");
  }

  int64_t value = stack->value.i;

  // Allocate buffer for string representation
  // Max length for int64_t is 20 digits + sign + null terminator
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%lld", (long long)value);

  // Pop the integer
  StackCell *rest = stack->next;
  free_cell(stack);

  // Push the string
  return push_string(rest, buffer);
}

/**
 * bool-to-string: ( Bool -- String )
 * Convert a boolean to "true" or "false"
 */
StackCell *bool_to_string(StackCell *stack) {
  if (!stack) {
    runtime_error("bool_to_string: stack underflow");
  }

  if (stack->tag != TAG_BOOL) {
    runtime_error("bool_to_string: type error (expected Bool)");
  }

  bool value = stack->value.b;

  // Pop the boolean
  StackCell *rest = stack->next;
  free_cell(stack);

  // Push the string "true" or "false"
  return push_string(rest, value ? "true" : "false");
}
