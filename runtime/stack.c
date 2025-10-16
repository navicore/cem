/**
 * Cem Runtime - Stack Machine Implementation
 *
 * This file implements the runtime stack operations for Cem.
 */

#define _POSIX_C_SOURCE 200809L
#include "stack.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Compile-time assertions to verify ABI assumptions
// The LLVM codegen assumes bool is 1 byte (i8) - verify this at compile time
_Static_assert(sizeof(bool) == 1, "LLVM codegen assumes bool is 1 byte (i8)");
_Static_assert(sizeof(StackCell) == 32,
               "LLVM codegen assumes StackCell is 32 bytes");

// ============================================================================
// Utility Functions
// ============================================================================

StackCell *alloc_cell(void) {
  StackCell *cell = (StackCell *)malloc(sizeof(StackCell));
  if (!cell) {
    runtime_error("Out of memory");
  }
  cell->next = NULL;
  return cell;
}

void free_cell(StackCell *cell) {
  if (!cell)
    return;

  // Free owned resources based on tag
  if (cell->tag == TAG_STRING && cell->value.s) {
    free(cell->value.s);
  }
  // TODO: Free variant data when implemented

  free(cell);
}

void free_stack(StackCell *stack) {
  while (stack) {
    StackCell *next = stack->next;
    free_cell(stack);
    stack = next;
  }
}

// Runtime error handler
// Note: Currently uses exit(1) for simplicity. In a production runtime,
// this could be replaced with setjmp/longjmp or return error codes up the
// call stack. However, for a stack machine runtime, errors are typically
// unrecoverable (stack corruption, type errors, etc.), so exit(1) is
// acceptable for this phase of development.
//
// MEMORY LEAK BEHAVIOR: This function is marked __attribute__((noreturn))
// and immediately exits the process. Any memory allocated before the error
// (including partially constructed StackCells) will leak. This is acceptable
// because:
// 1. The process terminates immediately (OS reclaims all memory)
// 2. Runtime errors indicate unrecoverable conditions (type errors, stack
//    corruption, etc.)
// 3. Attempting cleanup during error handling could cause further corruption
//
// Example: string_concat allocates a temp buffer, then calls push_string.
// If push_string fails with runtime_error(), the temp buffer leaks. This is
// safe because exit(1) terminates the process immediately.
void runtime_error(const char *message) {
  fprintf(stderr, "Runtime error: %s\n", message);
  exit(1);
}

void print_stack(StackCell *stack) {
  printf("Stack (top to bottom): ");
  StackCell *current = stack;
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

StackCell *stack_dup(StackCell *stack) {
  if (!stack) {
    runtime_error("dup: stack underflow");
  }

  StackCell *new_cell = alloc_cell();
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
    if (stack->value.s) {
      new_cell->value.s = strdup(stack->value.s);
      if (!new_cell->value.s) {
        free_cell(new_cell);
        runtime_error("dup: out of memory");
      }
    } else {
      new_cell->value.s = NULL;
    }
    break;
  case TAG_QUOTATION:
    new_cell->value.quotation = stack->value.quotation;
    break;
  case TAG_VARIANT:
    // TODO: Implement variant copying
    free_cell(new_cell);
    runtime_error("dup: variant copying not yet implemented");
    break;
  }

  new_cell->next = stack;
  return new_cell;
}

StackCell *drop(StackCell *stack) {
  if (!stack) {
    runtime_error("drop: stack underflow");
  }

  StackCell *rest = stack->next;
  free_cell(stack);
  return rest;
}

StackCell *swap(StackCell *stack) {
  if (!stack || !stack->next) {
    runtime_error("swap: stack underflow");
  }

  StackCell *first = stack;
  StackCell *second = stack->next;
  StackCell *rest = second->next;

  second->next = first;
  first->next = rest;

  return second;
}

StackCell *over(StackCell *stack) {
  if (!stack || !stack->next) {
    runtime_error("over: stack underflow");
  }

  StackCell *second = stack->next;
  StackCell *new_cell = alloc_cell();
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
    if (second->value.s) {
      new_cell->value.s = strdup(second->value.s);
      if (!new_cell->value.s) {
        free_cell(new_cell);
        runtime_error("over: out of memory");
      }
    } else {
      new_cell->value.s = NULL;
    }
    break;
  case TAG_QUOTATION:
    new_cell->value.quotation = second->value.quotation;
    break;
  case TAG_VARIANT:
    free_cell(new_cell);
    runtime_error("over: variant copying not yet implemented");
    break;
  }

  new_cell->next = stack;
  return new_cell;
}

StackCell *rot(StackCell *stack) {
  if (!stack || !stack->next || !stack->next->next) {
    runtime_error("rot: stack underflow");
  }

  // Standard Forth rot: ( a b c -- b c a )
  // Stack notation: bottom ... top
  // Before: ... A B C  (C on top)
  // After:  ... B C A  (A on top)
  StackCell *first = stack;        // C (top element)
  StackCell *second = stack->next; // B (second element)
  StackCell *third = second->next; // A (third element)
  StackCell *rest = third->next;

  // Relink: A -> B -> C -> rest
  // Result: B C A (A moves to top)
  second->next = first;
  first->next = rest;
  third->next = second;

  return third; // A is now on top
}

StackCell *nip(StackCell *stack) {
  if (!stack || !stack->next) {
    runtime_error("nip: stack underflow");
  }

  // Remove second element: A B -> B
  StackCell *first = stack;
  StackCell *second = stack->next;
  StackCell *rest = second->next;

  first->next = rest;
  free_cell(second);

  return first;
}

StackCell *tuck(StackCell *stack) {
  if (!stack || !stack->next) {
    runtime_error("tuck: stack underflow");
  }

  // Copy top below second: A B -> B A B
  StackCell *first = stack;        // B
  StackCell *second = stack->next; // A
  StackCell *rest = second->next;

  // Create copy of first (B)
  StackCell *copy = alloc_cell();
  copy->tag = first->tag;

  // Deep copy the value
  switch (first->tag) {
  case TAG_INT:
    copy->value.i = first->value.i;
    break;
  case TAG_BOOL:
    copy->value.b = first->value.b;
    break;
  case TAG_STRING:
    if (first->value.s) {
      copy->value.s = strdup(first->value.s);
      if (!copy->value.s) {
        free_cell(copy);
        runtime_error("tuck: out of memory");
      }
    } else {
      copy->value.s = NULL;
    }
    break;
  case TAG_QUOTATION:
    copy->value.quotation = first->value.quotation;
    break;
  case TAG_VARIANT:
    free_cell(copy);
    runtime_error("tuck: variant copying not yet implemented");
    break;
  }

  // Link: B -> A -> B(copy) -> rest
  copy->next = rest;
  second->next = copy;
  first->next = second;

  return first;
}

// ============================================================================
// Arithmetic Operations
// ============================================================================
// Note: All arithmetic operations use wrapping semantics (like Rust's
// wrapping_*). Integer overflow wraps around according to two's complement
// representation. This is standard behavior for stack-based concatenative
// languages.

StackCell *add(StackCell *stack) {
  if (!stack || !stack->next) {
    runtime_error("add: stack underflow");
  }
  if (stack->tag != TAG_INT || stack->next->tag != TAG_INT) {
    runtime_error("add: type error (expected Int Int)");
  }

  int64_t b = stack->value.i;
  int64_t a = stack->next->value.i;

  StackCell *rest = stack->next->next;
  free_cell(stack->next);
  free_cell(stack);

  return push_int(rest, a + b);
}

StackCell *subtract(StackCell *stack) {
  if (!stack || !stack->next) {
    runtime_error("subtract: stack underflow");
  }
  if (stack->tag != TAG_INT || stack->next->tag != TAG_INT) {
    runtime_error("subtract: type error (expected Int Int)");
  }

  int64_t b = stack->value.i;
  int64_t a = stack->next->value.i;

  StackCell *rest = stack->next->next;
  free_cell(stack->next);
  free_cell(stack);

  return push_int(rest, a - b);
}

StackCell *multiply(StackCell *stack) {
  if (!stack || !stack->next) {
    runtime_error("multiply: stack underflow");
  }
  if (stack->tag != TAG_INT || stack->next->tag != TAG_INT) {
    runtime_error("multiply: type error (expected Int Int)");
  }

  int64_t b = stack->value.i;
  int64_t a = stack->next->value.i;

  StackCell *rest = stack->next->next;
  free_cell(stack->next);
  free_cell(stack);

  return push_int(rest, a * b);
}

StackCell *divide_op(StackCell *stack) {
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

  StackCell *rest = stack->next->next;
  free_cell(stack->next);
  free_cell(stack);

  return push_int(rest, a / b);
}

// ============================================================================
// Comparison Operations
// ============================================================================

StackCell *less_than(StackCell *stack) {
  if (!stack || !stack->next) {
    runtime_error("less_than: stack underflow");
  }
  if (stack->tag != TAG_INT || stack->next->tag != TAG_INT) {
    runtime_error("less_than: type error (expected Int Int)");
  }

  int64_t b = stack->value.i;
  int64_t a = stack->next->value.i;

  StackCell *rest = stack->next->next;
  free_cell(stack->next);
  free_cell(stack);

  return push_bool(rest, a < b);
}

StackCell *greater_than(StackCell *stack) {
  if (!stack || !stack->next) {
    runtime_error("greater_than: stack underflow");
  }
  if (stack->tag != TAG_INT || stack->next->tag != TAG_INT) {
    runtime_error("greater_than: type error (expected Int Int)");
  }

  int64_t b = stack->value.i;
  int64_t a = stack->next->value.i;

  StackCell *rest = stack->next->next;
  free_cell(stack->next);
  free_cell(stack);

  return push_bool(rest, a > b);
}

StackCell *equal(StackCell *stack) {
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

  StackCell *rest = stack->next->next;
  free_cell(stack->next);
  free_cell(stack);

  return push_bool(rest, result);
}

// ============================================================================
// Push Operations
// ============================================================================

StackCell *push_int(StackCell *stack, int64_t value) {
  StackCell *cell = alloc_cell();
  cell->tag = TAG_INT;
  cell->value.i = value;
  cell->next = stack;
  return cell;
}

StackCell *push_bool(StackCell *stack, bool value) {
  StackCell *cell = alloc_cell();
  cell->tag = TAG_BOOL;
  cell->value.b = value;
  cell->next = stack;
  return cell;
}

StackCell *push_string(StackCell *stack, const char *value) {
  StackCell *cell = alloc_cell();
  cell->tag = TAG_STRING;
  cell->value.s = strdup(value);
  if (!cell->value.s) {
    runtime_error("push_string: out of memory");
  }
  cell->next = stack;
  return cell;
}

StackCell *push_quotation(StackCell *stack, void *func_ptr) {
  StackCell *cell = alloc_cell();
  cell->tag = TAG_QUOTATION;
  cell->value.quotation = func_ptr;
  cell->next = stack;
  return cell;
}

// ============================================================================
// String Operations
// ============================================================================

StackCell *string_length(StackCell *stack) {
  if (!stack) {
    runtime_error("string_length: stack underflow");
  }
  if (stack->tag != TAG_STRING) {
    runtime_error("string_length: expected string on top of stack");
  }
  if (!stack->value.s) {
    runtime_error("string_length: NULL string pointer");
  }

  // Calculate length (returns byte count, not UTF-8 character count)
  int64_t len = (int64_t)strlen(stack->value.s);

  // Pop string and push length
  StackCell *rest = stack->next;
  free_cell(stack);

  return push_int(rest, len);
}

StackCell *string_concat(StackCell *stack) {
  if (!stack || !stack->next) {
    runtime_error("string_concat: stack underflow");
  }
  if (stack->tag != TAG_STRING || stack->next->tag != TAG_STRING) {
    runtime_error("string_concat: expected two strings on stack");
  }

  // Get both strings (top is second operand, next is first operand)
  char *str2 = stack->value.s;
  char *str1 = stack->next->value.s;

  // NULL pointer checks
  if (!str1 || !str2) {
    runtime_error("string_concat: NULL string pointer");
  }

  // Calculate lengths and check for overflow
  size_t len1 = strlen(str1);
  size_t len2 = strlen(str2);

  // Check for size_t overflow: len1 + len2 + 1
  if (len1 > SIZE_MAX - len2 - 1) {
    runtime_error("string_concat: string too long (overflow)");
  }
  size_t total_len = len1 + len2 + 1;

  // Allocate new string
  char *result = malloc(total_len);
  if (!result) {
    runtime_error("string_concat: out of memory");
  }

  // Concatenate using memcpy (safer than strcpy/strcat)
  memcpy(result, str1, len1);
  memcpy(result + len1, str2, len2);
  result[len1 + len2] = '\0';

  // Pop both strings BEFORE push_string (in case push_string fails)
  StackCell *rest = stack->next->next;
  free_cell(stack->next);
  free_cell(stack);

  // Push result (this makes its own copy)
  StackCell *new_cell = push_string(rest, result);

  // Free our temporary buffer
  free(result);

  return new_cell;
}

StackCell *string_equal(StackCell *stack) {
  if (!stack || !stack->next) {
    runtime_error("string_equal: stack underflow");
  }
  if (stack->tag != TAG_STRING || stack->next->tag != TAG_STRING) {
    runtime_error("string_equal: expected two strings on stack");
  }

  // NULL pointer checks
  if (!stack->value.s || !stack->next->value.s) {
    runtime_error("string_equal: NULL string pointer");
  }

  // Compare strings
  bool result = (strcmp(stack->value.s, stack->next->value.s) == 0);

  // Pop both strings
  StackCell *rest = stack->next->next;
  free_cell(stack->next);
  free_cell(stack);

  return push_bool(rest, result);
}

// ============================================================================
// Control Flow Operations (Placeholders)
// ============================================================================

StackCell *call_quotation(StackCell *stack) {
  if (!stack) {
    runtime_error("call_quotation: stack underflow");
  }
  if (stack->tag != TAG_QUOTATION) {
    runtime_error("call_quotation: expected quotation on top of stack");
  }

  // Pop the quotation
  void *func_ptr = stack->value.quotation;
  StackCell *rest = stack->next;
  free(stack);

  // Call the function pointer with the rest of the stack
  // The function has signature: StackCell* (*)(StackCell*)
  typedef StackCell *(*QuotationFunc)(StackCell *);
  QuotationFunc func = (QuotationFunc)func_ptr;
  return func(rest);
}

StackCell *if_then_else(StackCell *stack) {
  // TODO: Implement when control flow is supported
  runtime_error("if_then_else: not yet implemented");
  return stack;
}
