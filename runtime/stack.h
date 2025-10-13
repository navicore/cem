/**
 * Cem Runtime - Stack Machine Implementation
 *
 * This header defines the runtime stack representation and operations
 * for the Cem programming language.
 *
 * The stack is implemented as a linked list of heap-allocated cells.
 * Each cell contains a tagged union representing different value types.
 */

#ifndef CEM_RUNTIME_STACK_H
#define CEM_RUNTIME_STACK_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Value type tags
 *
 * Each stack value is tagged with its runtime type to support
 * dynamic dispatch and type checking.
 */
typedef enum {
  TAG_INT,       // 64-bit signed integer
  TAG_BOOL,      // Boolean (true/false)
  TAG_STRING,    // Null-terminated string (heap-allocated)
  TAG_QUOTATION, // Code quotation (function pointer)
  TAG_VARIANT,   // Sum type variant (ADT)
} ValueTag;

/**
 * Stack cell structure
 *
 * Represents a single value on the stack.
 * Forms a linked list via the 'next' pointer.
 *
 * MEMORY LAYOUT (64-bit):
 * - tag: 4 bytes (ValueTag/int32_t) at offset 0
 * - padding: 4 bytes (for union alignment to 8-byte boundary)
 * - value union: 16 bytes at offset 8
 *   - int64_t i: 8 bytes
 *   - bool b: 1 byte (C99 bool from stdbool.h, typically uint8_t)
 *   - char* s: 8 bytes
 *   - void* quotation: 8 bytes
 *   - variant struct: 16 bytes (4-byte tag + 4-byte padding + 8-byte pointer)
 * - next: 8 bytes (pointer) at offset 24
 * TOTAL: 32 bytes
 *
 * IMPORTANT ABI ASSUMPTIONS for LLVM codegen:
 * 1. bool is represented as i8 (uint8_t/unsigned char)
 * 2. bool value is stored at the first byte of the union (offset 8 from struct
 * start)
 * 3. The union is 16 bytes due to the variant struct being the largest member
 *
 * If the C bool type changes (e.g., becomes i32 on some platform), the LLVM
 * code generation in src/codegen/mod.rs must be updated accordingly.
 */
typedef struct StackCell {
  ValueTag tag;

  union {
    int64_t i;       // Integer value
    bool b;          // Boolean value (ABI: typically uint8_t)
    char *s;         // String value (owned)
    void *quotation; // Quotation (function pointer)
    struct {
      uint32_t tag; // Variant tag
      void *data;   // Variant data
    } variant;
  } value;

  struct StackCell *next; // Pointer to rest of stack
} StackCell;

// ============================================================================
// Stack Operations
// ============================================================================

/**
 * dup ( A -- A A )
 * Duplicate the top element of the stack
 */
StackCell *dup(StackCell *stack);

/**
 * drop ( A -- )
 * Remove the top element of the stack
 */
StackCell *drop(StackCell *stack);

/**
 * swap ( A B -- B A )
 * Swap the top two elements
 */
StackCell *swap(StackCell *stack);

/**
 * over ( A B -- A B A )
 * Copy the second element to the top
 */
StackCell *over(StackCell *stack);

/**
 * rot ( A B C -- B C A )
 * Rotate the top three elements
 */
StackCell *rot(StackCell *stack);

// ============================================================================
// Arithmetic Operations
// ============================================================================

/**
 * add ( Int Int -- Int )
 * Add two integers
 */
StackCell *add(StackCell *stack);

/**
 * subtract ( Int Int -- Int )
 * Subtract two integers (second - first)
 */
StackCell *subtract(StackCell *stack);

/**
 * multiply ( Int Int -- Int )
 * Multiply two integers
 */
StackCell *multiply(StackCell *stack);

/**
 * divide ( Int Int -- Int )
 * Divide two integers (second / first)
 */
StackCell *divide_op(StackCell *stack);

// ============================================================================
// Comparison Operations
// ============================================================================

/**
 * less_than ( Int Int -- Bool )
 * Check if second < first
 */
StackCell *less_than(StackCell *stack);

/**
 * greater_than ( Int Int -- Bool )
 * Check if second > first
 */
StackCell *greater_than(StackCell *stack);

/**
 * equal ( Int Int -- Bool )
 * Check if second == first
 */
StackCell *equal(StackCell *stack);

// ============================================================================
// Push Operations
// ============================================================================

/**
 * push_int ( -- Int )
 * Push an integer onto the stack
 */
StackCell *push_int(StackCell *stack, int64_t value);

/**
 * push_bool ( -- Bool )
 * Push a boolean onto the stack
 */
StackCell *push_bool(StackCell *stack, bool value);

/**
 * push_string ( -- String )
 * Push a string onto the stack (makes a copy)
 */
StackCell *push_string(StackCell *stack, const char *value);

/**
 * push_quotation ( -- Quotation )
 * Push a quotation (function pointer) onto the stack
 */
StackCell *push_quotation(StackCell *stack, void *func_ptr);

// ============================================================================
// String Operations
// ============================================================================

/**
 * string_length ( String -- Int )
 * Get the length of a string (number of bytes)
 */
StackCell *string_length(StackCell *stack);

/**
 * string_concat ( String String -- String )
 * Concatenate two strings (second + first)
 */
StackCell *string_concat(StackCell *stack);

/**
 * string_equal ( String String -- Bool )
 * Check if two strings are equal
 */
StackCell *string_equal(StackCell *stack);

// ============================================================================
// Control Flow Operations
// ============================================================================

/**
 * call_quotation ( Quotation -- ... )
 * Call a quotation (function pointer)
 */
StackCell *call_quotation(StackCell *stack);

/**
 * if_then_else ( Bool Quotation Quotation -- ... )
 * Conditional execution: if true call first quotation, else call second
 */
StackCell *if_then_else(StackCell *stack);

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * Allocate a new stack cell
 */
StackCell *alloc_cell(void);

/**
 * Free a stack cell and its contents
 */
void free_cell(StackCell *cell);

/**
 * Free entire stack
 */
void free_stack(StackCell *stack);

/**
 * Print stack contents (for debugging)
 */
void print_stack(StackCell *stack);

/**
 * Runtime error handling
 */
void runtime_error(const char *message) __attribute__((noreturn));

#endif // CEM_RUNTIME_STACK_H
