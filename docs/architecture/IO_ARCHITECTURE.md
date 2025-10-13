# I/O Architecture

## Vision

Cem provides **uniform non-blocking I/O** for stdin/stdout, files, and network
through a **green thread scheduler** with **structured concurrency**. All I/O
appears synchronous to the programmer but executes asynchronously under the
hood.

## Core Principles

1. **Stack Isolation** - Each strand has its own stack
2. **Explicit Yield Points** - Strands yield only at I/O operations
3. **Structured Concurrency** - All concurrency is bounded by program structure
4. **No Shared Mutable State** - Isolated stacks prevent data races by default

## Terminology

- **strand** - A unit of concurrent execution (lightweight thread with its own
  stack)
- **go** - Launch a strand to run concurrently
- **wait** - Synchronize on a strand, blocking until it completes

These terms are Cem-specific vocabulary, chosen to avoid assumptions from other
languages.

## Execution Model

### Sequential Within a Strand

Between I/O operations, execution is sequential and deterministic:

```cem 5 10 add    # Always executes completely read_line   # Yields here (I/O
operation) print       # Resumes here when I/O completes ```

### I/O Operations as Yield Points

Strands yield ONLY at explicit I/O operations:

- `read_line` - yields until data available
- `write_line` - yields until write completes
- `read_file` - yields until file data ready
- `write_file` - yields until write completes
- `accept_connection` - yields until client connects
- `connect` - yields until connection established

**Key property**: No preemption between I/O operations. If you're doing
computation, you own the CPU until you perform I/O.

## Execution Order Guarantees

### Happens-Before Rules

1. **Sequential consistency within a strand**: Operations in a single strand
happen in program order
2. **Go ordering**: `go` happens-before any operation in the spawned strand
3. **Wait ordering**: Any operation in a strand happens-before `wait` returns in
the parent
4. **I/O completion**: I/O completion happens-before strand resumption

### What This Prevents

Unlike Java's threading model, Cem avoids:

- **Hidden races**: Stacks are isolated, no shared mutable state
- **Unpredictable interleaving**: You know exactly when yields can happen (at
  I/O calls)
- **Implicit parallelism**: `go`/`wait` make concurrency visible

## User-Facing Model

### Phase 1: Single-Threaded Event Loop (Current Target)

Even without explicit parallelism, I/O can overlap:

```cem # Server that handles multiple clients server_socket "localhost" 8080
bind listen

[ accept_connection handle_client ] forever ```

- Each `accept_connection` yields to scheduler
- While handling a client, can accept next connection
- All appears sequential to the programmer
- I/O happens concurrently under the hood

### Phase 2: Explicit Parallelism (Future)

Launch concurrent operations explicitly:

```cem # Read two files concurrently [ "file1.txt" read_file ] go [ "file2.txt"
read_file ] go

# Both files read in parallel wait swap wait   # Wait for both results concat
# Combine results ```

**Properties**:
- `go` is explicit (visible in code)
- Each strand has its own stack
- Parent can `wait` to synchronize
- No shared mutable state (stacks are isolated)

## Runtime Architecture

### Components

#### 1. Strand Structure

Each strand contains:
- **Stack pointer**: Pointer to isolated stack
- **Stack base**: Base address of stack allocation
- **Stack size**: Size of allocated stack
- **Instruction pointer**: Saved program counter for resumption
- **I/O state**: Pending I/O operation (if blocked)

#### 2. Scheduler

The scheduler manages strand execution:
- **Ready queue**: Strands ready to run (FIFO)
- **Blocked map**: Strands waiting on I/O (keyed by I/O handle)
- **Event loop**: Polls async I/O completion events
- **Current strand**: Currently executing strand

**Scheduling algorithm**:
1. Pop strand from ready queue
2. Execute until I/O operation or completion
3. If I/O, register in blocked map and submit async request
4. Poll event loop for completions
5. Move completed strands to ready queue
6. Repeat

#### 3. I/O Operations

Each I/O operation follows this pattern:

```rust fn read_line() { // 1. Submit async I/O request let handle =
io_uring_submit_read(stdin, buffer);

    // 2. Save current strand state save_strand_state(current_strand);

    // 3. Register as blocked blocked_map.insert(handle, current_strand);

    // 4. Yield to scheduler scheduler_yield();

    // 5. Resume here when I/O completes // Result is already in buffer } ```

### Platform-Specific I/O Backends

- **Linux**: `io_uring` (modern async I/O)
- **macOS/BSD**: `kqueue` (event notification)
- **Windows**: IOCP (I/O Completion Ports)

All backends provide the same abstraction:
- Submit async operation → get handle
- Poll for completions → get (handle, result) pairs

## Memory Model

### Stack Isolation

Each strand has its own stack allocation:

``` Strand 1 Stack: [0x1000 - 0x2000] Strand 2 Stack: [0x3000 - 0x4000] Strand 3
Stack: [0x5000 - 0x6000] ```

**Properties**:
- No shared mutable state by default
- No data races between strands
- Stack overflow protection (guard pages)

### Communication Between Strands

Future features may include:

1. **Channels**: Message passing between strands
2. **Shared immutable data**: Read-only access to shared structures
3. **Atomic operations**: For rare cases requiring coordination

**Important**: All sharing will be explicit and safe by construction.

## Benefits Over Traditional Models

### vs. Blocking I/O (C, Python, Ruby)

- **Concurrency**: Handle thousands of connections without threads
- **Performance**: No context switching overhead
- **Simplicity**: Looks like sequential code

### vs. Async/Await (Rust, JavaScript)

- **No function coloring**: All functions work the same way
- **Simpler mental model**: No distinction between sync/async functions
- **Stack-based**: Natural for concatenative language

### vs. OS Threads (Java, C++)

- **Lightweight**: Thousands of strands vs. hundreds of threads
- **Deterministic**: No preemption between I/O operations
- **No data races**: Stack isolation prevents shared mutable state

### vs. Go's Goroutines

Similar model! Key differences:

- **Explicit I/O yield points**: Cem strands only yield at I/O
- **Stack-based language**: Natural fit for concatenative paradigm
- **Structured concurrency**: All parallelism is bounded
- **Terminology**: We say `go` not `go`, and `wait` not channel operations

## Implementation Roadmap

### Step 1: Core Scheduler (Minimal)

- Single strand support
- Ready queue only (no I/O yet)
- Test with synthetic yields

### Step 2: Basic I/O (stdin/stdout)

- Integrate `io_uring` (Linux) or `kqueue` (macOS)
- Implement `read_line` and `write_line`
- Blocked map and event loop
- Test with echo server

### Step 3: File I/O

- `read_file` / `write_file`
- Directory operations
- Test with file processing

### Step 4: Network I/O

- `bind` / `listen` / `accept_connection`
- `connect` / `send` / `recv`
- Test with HTTP server

### Step 5: Multiple Strands (Future)

- `go` primitive
- `wait` primitive
- Strand handle type
- Test with concurrent file processing

## Open Design Questions

### 1. Strand Stack Size

- Fixed size (e.g., 64KB per strand)?
- Growable stacks (segmented stacks)?
- Trade-off: Memory usage vs. complexity

### 2. Error Handling

How do I/O errors propagate?

- Return error codes on stack?
- Exception-like mechanism?
- Result types?

### 3. Cancellation

How to cancel pending I/O?

- Explicit `cancel` operation?
- Automatic on strand exit?
- Timeout primitives?

### 4. Fairness

Should scheduler be strictly FIFO?

- Round-robin?
- Priority levels?
- Work-stealing (multiple OS threads)?

## Testing Strategy

### Unit Tests

- Strand creation/destruction
- Queue operations
- I/O submission/completion

### Integration Tests

- Echo server (network I/O)
- File copy (file I/O)
- Pipeline (stdin → processing → stdout)

### Stress Tests

- 10,000 concurrent connections
- Large file I/O
- Rapid strand go/wait

### Benchmarks

- Throughput (requests/second)
- Latency (p50, p99, p999)
- Memory usage (per connection)
- Comparison with Go, Rust, Node.js

## References

### Similar Systems

- **Go**: Goroutines with channel-based communication
- **Erlang/Elixir**: Process-based concurrency with message passing
- **Kotlin**: Coroutines with structured concurrency
- **Swift**: Async/await with structured concurrency

### Research Papers

- "Structured Concurrency" - Martin Sústrik
- "Notes on Structured Concurrency" - Nathaniel J. Smith
- "io_uring: A New Async I/O API" - Jens Axboe

### Relevant Technologies

- Linux `io_uring`: https://kernel.dk/io_uring.pdf
- BSD `kqueue`: man page kqueue(2)
- Windows IOCP: Microsoft Docs

---

**Status**: Design complete ✅ **Next**: Implement core scheduler (Step 1)
