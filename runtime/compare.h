/**
 * Cem Runtime - Comparison Operations Header
 */

#ifndef CEM_COMPARE_H
#define CEM_COMPARE_H

#include "stack.h"

/**
 * Integer comparison operations
 * All take two integers from the stack and return a boolean
 *
 * Note: These functions use operator symbols as names to match
 * the Cem syntax directly (e.g., < in Cem calls @< in LLVM IR)
 */

// <  : ( Int Int -- Bool )
StackCell *int_less(StackCell *stack);

// >  : ( Int Int -- Bool )
StackCell *int_greater(StackCell *stack);

// <= : ( Int Int -- Bool )
StackCell *int_less_equal(StackCell *stack);

// >= : ( Int Int -- Bool )
StackCell *int_greater_equal(StackCell *stack);

// =  : ( Int Int -- Bool )
StackCell *int_equal(StackCell *stack);

// != : ( Int Int -- Bool )
StackCell *int_not_equal(StackCell *stack);

#endif // CEM_COMPARE_H
