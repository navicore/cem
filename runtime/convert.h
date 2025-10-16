/**
 * Cem Runtime - Type Conversion Operations Header
 */

#ifndef CEM_CONVERT_H
#define CEM_CONVERT_H

#include "stack.h"

/**
 * Type conversion operations
 */

// int-to-string : ( Int -- String )
StackCell *int_to_string(StackCell *stack);

// bool-to-string : ( Bool -- String )
StackCell *bool_to_string(StackCell *stack);

#endif // CEM_CONVERT_H
