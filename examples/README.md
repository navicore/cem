# Cem Examples

This directory contains example Cem programs demonstrating various language features.

## Running Examples

Compile and run any example:

```bash
cem compile examples/hello_io.cem
./hello_io
```

Or with a custom output name:

```bash
cem compile examples/echo.cem -o my_echo
./my_echo
```

## Examples

### Basic I/O

**hello_io.cem** - Classic Hello World
```bash
cem compile examples/hello_io.cem && ./hello_io
```
Prints "Hello, World!" to stdout. The simplest possible Cem program.

**echo.cem** - Echo program
```bash
cem compile examples/echo.cem && ./echo
```
Reads a line from stdin and echoes it back. Demonstrates async I/O with green threads.

**chat.cem** - Simple chat bot
```bash
cem compile examples/chat.cem && ./chat
```
Interactive chat that demonstrates how each I/O operation yields to the scheduler.

### Computation

**multiply.cem** - Arithmetic demo
```bash
cem compile examples/multiply.cem && ./multiply
```
Computes 6 * 7 = 42. Once int-to-string conversion is added to the runtime, this will print the computed result.

## What's Next?

These examples currently demonstrate:
- ✅ Async I/O with green threads (`read_line`, `write_line`)
- ✅ String literals and operations
- ✅ Basic arithmetic (`+`, `-`, `multiply`)
- ✅ Stack manipulation (`dup`, `drop`, `swap`, `over`, `rot`, `nip`, `tuck`)
- ✅ Tail-call optimization

Coming soon:
- Comparison operators (`<`, `>`, `<=`, `>=`, `==`, `!=`)
- Conditional execution (`if`/`then`/`else`)
- Boolean type and logical operators
- Integer to string conversion
- Loops and iteration
- Concurrent multi-strand programs

## Language Features Demonstrated

### Green Thread Concurrency

Every I/O operation (`read_line`, `write_line`) yields to the scheduler, allowing:
- 100,000+ concurrent connections
- Zero-cost context switching
- Deterministic execution (no data races)

### Concatenative Style

Cem is a concatenative (stack-based) language:
- Functions compose naturally
- No variables needed
- Data flows through an implicit stack

### Tail-Call Optimization

The compiler automatically converts tail calls into jumps:
- Recursion as efficient as loops
- No stack overflow risk
- Clean, functional code

## Learn More

- [../README.md](../README.md) - Language overview
- [../docs/language/recursion.md](../docs/language/recursion.md) - Recursion patterns
- [../docs/architecture/IO_ARCHITECTURE.md](../docs/architecture/IO_ARCHITECTURE.md) - Concurrency model
- [../BUILD.md](../BUILD.md) - Building from source
