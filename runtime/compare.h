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
StackCell *LT(StackCell *stack); // Less than

// >  : ( Int Int -- Bool )
StackCell *GT(StackCell *stack); // Greater than

// <= : ( Int Int -- Bool )
StackCell *LE(StackCell *stack); // Less or equal

// >= : ( Int Int -- Bool )
StackCell *GE(StackCell *stack); // Greater or equal

// =  : ( Int Int -- Bool )
StackCell *EQ(StackCell *stack); // Equal

// != : ( Int Int -- Bool )
StackCell *NE(StackCell *stack); // Not equal

#endif // CEM_COMPARE_H
