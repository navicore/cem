/**
 * Cem Runtime - Async I/O Operations
 *
 * This module provides non-blocking I/O operations that cooperate with
 * the scheduler. When I/O would block, these functions yield the strand
 * to the scheduler and resume when I/O is ready.
 *
 * All I/O operations are non-blocking and async:
 * - write_line() - Write a line to stdout (yields on EWOULDBLOCK)
 * - read_line() - Read a line from stdin (yields on EWOULDBLOCK)
 */

#ifndef CEM_RUNTIME_IO_H
#define CEM_RUNTIME_IO_H

#include "stack.h"

/**
 * Write a string to stdout, followed by a newline
 *
 * This function performs non-blocking I/O. If stdout is not ready for writing,
 * the current strand will block until stdout becomes writable, then resume.
 *
 * Stack effect: ( str -- )
 * - Pops a string from the stack
 * - Writes it to stdout with a newline
 * - Yields if write would block, resumes when ready
 *
 * Error handling:
 * - runtime_error if not in a strand context
 * - runtime_error on I/O errors (other than EAGAIN/EWOULDBLOCK)
 */
StackCell *write_line(StackCell *stack);

/**
 * Read a line from stdin
 *
 * This function performs non-blocking I/O. If stdin has no data available,
 * the current strand will block until stdin becomes readable, then resume.
 *
 * Stack effect: ( -- str )
 * - Reads a line from stdin (up to newline or EOF)
 * - Pushes the string onto the stack (without the newline)
 * - Yields if read would block, resumes when ready
 *
 * Error handling:
 * - runtime_error if not in a strand context
 * - runtime_error on I/O errors (other than EAGAIN/EWOULDBLOCK)
 * - Pushes empty string on EOF
 */
StackCell *read_line(StackCell *stack);

#endif // CEM_RUNTIME_IO_H
