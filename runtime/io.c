/**
 * Cem Runtime - Async I/O Implementation
 */

#define _XOPEN_SOURCE 700
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include "io.h"
#include "scheduler.h"

// Forward declare I/O functions to avoid unistd.h (which conflicts with stack.h's dup())
extern ssize_t write(int fd, const void *buf, size_t count);
extern ssize_t read(int fd, void *buf, size_t count);

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

// Make sure stdout and stdin are in non-blocking mode
static bool io_initialized = false;

static void ensure_nonblocking_io(void) {
    if (io_initialized) return;

    // Set stdout to non-blocking
    int flags = fcntl(STDOUT_FILENO, F_GETFL, 0);
    if (flags == -1 || fcntl(STDOUT_FILENO, F_SETFL, flags | O_NONBLOCK) == -1) {
        runtime_error("ensure_nonblocking_io: failed to set stdout non-blocking");
    }

    // Set stdin to non-blocking
    flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags == -1 || fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) == -1) {
        runtime_error("ensure_nonblocking_io: failed to set stdin non-blocking");
    }

    io_initialized = true;
}

/**
 * Write a string to stdout with newline
 * Stack effect: ( str -- )
 *
 * Phase 2b: Uses cleanup handlers to ensure buffer is freed even if
 * strand is terminated while blocked.
 */
StackCell* write_line(StackCell* stack) {
    ensure_nonblocking_io();

    // Pop string from stack
    if (!stack || stack->tag != TAG_STRING) {
        runtime_error("write_line: expected string on stack");
    }

    const char* str = stack->value.s;
    StackCell* rest = stack->next;

    // Calculate total length (string + newline)
    size_t str_len = strlen(str);
    size_t total_len = str_len + 1;  // +1 for newline

    // Allocate buffer for string + newline
    char* buffer = malloc(total_len);
    if (!buffer) {
        runtime_error("write_line: out of memory");
    }
    memcpy(buffer, str, str_len);
    buffer[str_len] = '\n';

    // Register cleanup handler to free buffer if strand terminates while blocked
    strand_push_cleanup(free, buffer);

    // Free the string cell (we've copied the data)
    free_cell(stack);

    // Write the buffer, handling EWOULDBLOCK by yielding
    size_t written = 0;
    while (written < total_len) {
        ssize_t n = write(STDOUT_FILENO, buffer + written, total_len - written);

        if (n > 0) {
            written += n;
        } else if (n == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Would block - yield to scheduler and wait for writability
                strand_block_on_write(STDOUT_FILENO);
                // When we resume, try again
            } else {
                // Real error
                strand_pop_cleanup();  // Remove cleanup handler before freeing manually
                free(buffer);
                runtime_error("write_line: write() failed");
            }
        } else {
            // n == 0, shouldn't happen for stdout
            strand_pop_cleanup();  // Remove cleanup handler before freeing manually
            free(buffer);
            runtime_error("write_line: unexpected write() return 0");
        }
    }

    // Success - remove cleanup handler and free buffer manually
    strand_pop_cleanup();
    free(buffer);
    return rest;
}

/**
 * Read a line from stdin
 * Stack effect: ( -- str )
 *
 * Phase 2b: Uses cleanup handlers to ensure buffer is freed even if
 * strand is terminated while blocked.
 */
StackCell* read_line(StackCell* stack) {
    ensure_nonblocking_io();

    // Buffer for reading (we'll grow it if needed)
    size_t capacity = 128;
    size_t length = 0;
    char* buffer = malloc(capacity);
    if (!buffer) {
        runtime_error("read_line: out of memory");
    }

    // Register cleanup handler to free buffer if strand terminates while blocked
    strand_push_cleanup(free, buffer);

    // Read until we get a newline or EOF
    bool done = false;
    while (!done) {
        // Try to read one character at a time
        // (This is inefficient but simple for now; we could optimize later)
        char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);

        if (n > 0) {
            // Got a character
            if (c == '\n') {
                done = true;
            } else {
                // Add to buffer
                if (length >= capacity) {
                    capacity *= 2;
                    char* new_buffer = realloc(buffer, capacity);
                    if (!new_buffer) {
                        strand_pop_cleanup();  // Remove cleanup handler before freeing manually
                        free(buffer);
                        runtime_error("read_line: out of memory");
                    }
                    // Update cleanup handler with new buffer pointer
                    strand_pop_cleanup();
                    buffer = new_buffer;
                    strand_push_cleanup(free, buffer);
                }
                buffer[length++] = c;
            }
        } else if (n == 0) {
            // EOF
            done = true;
        } else {
            // n == -1
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Would block - yield to scheduler and wait for readability
                strand_block_on_read(STDIN_FILENO);
                // When we resume, try again
            } else {
                // Real error
                strand_pop_cleanup();  // Remove cleanup handler before freeing manually
                free(buffer);
                runtime_error("read_line: read() failed");
            }
        }
    }

    // Null-terminate the string
    if (length >= capacity) {
        capacity++;
        char* new_buffer = realloc(buffer, capacity);
        if (!new_buffer) {
            strand_pop_cleanup();  // Remove cleanup handler before freeing manually
            free(buffer);
            runtime_error("read_line: out of memory");
        }
        // Update cleanup handler with new buffer pointer
        strand_pop_cleanup();
        buffer = new_buffer;
        strand_push_cleanup(free, buffer);
    }
    buffer[length] = '\0';

    // Create string cell and transfer ownership of buffer
    StackCell* result = alloc_cell();
    result->tag = TAG_STRING;
    result->value.s = buffer;  // Transfer ownership to string cell
    result->next = stack;

    // Buffer ownership transferred to string cell, so remove cleanup handler
    // (the string cell will be freed by normal stack cleanup)
    strand_pop_cleanup();

    return result;
}
