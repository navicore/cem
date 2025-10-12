# Scheduler Implementation Plan

## Overview

This document outlines the phased implementation of Cem's green thread scheduler, designed to support Erlang-scale concurrency (hundreds of thousands of strands) while providing a pragmatic path to get there.

**Current Status:** Single-threaded cooperative scheduler (Phases 1-3 complete). All strands run on one OS thread with non-blocking I/O. Multi-threaded work-stealing is planned for Phase 4+.

## Vision

Create a scheduler that supports:
- Hundreds of thousands to millions of concurrent strands (✅ Phase 3)
- Tiny memory footprint per strand (✅ 4KB-1MB dynamic stacks)
- True "actors everywhere" programming model
- Cooperative multitasking with explicit yield points at I/O operations (✅ Phase 2a)
- Single OS thread event loop (✅ Current - Phase 1-3)
- Deterministic ordering through stack concatenation

**Future (Phase 4+):** Multi-threaded work-stealing scheduler for CPU-bound parallelism

## Architecture Phases

### Phase 1: Infrastructure ✅ (Completed)

**Status:** Implemented in `runtime/scheduler.c`

**What Works:**
- Scheduler initialization/shutdown
- Ready queue (FIFO operations)
- Strand state management structures
- Memory allocation for strands

**Limitations:**
- No context switching
- `strand_spawn()` and `strand_yield()` are stubs that error
- Single-strand execution only

### Phase 2a: ucontext-based Context Switching (CURRENT TARGET)

**Goal:** Get I/O working and validate the scheduler model

**Timeline:** Near term (days to weeks)

**Implementation:**
1. Replace `jmp_buf` with `ucontext_t` in `Strand` structure
2. Implement `strand_spawn()` using `makecontext()`
   - Allocate 64KB stack per strand (generous for initial phase)
   - Initialize context with entry function
   - Add to ready queue
3. Implement `strand_yield()` using `swapcontext()`
   - Save current strand's context
   - Move to ready queue (cooperative scheduling)
   - Switch to next ready strand
4. Implement `scheduler_run()` event loop
   - Initialize scheduler context
   - Run ready strands round-robin
   - Integrate with kqueue (macOS) for async I/O
5. Implement basic I/O operations:
   - `read_line()` - read with yielding on block
   - `write_line()` - write with yielding on block

**Memory Profile:**
- 64KB per strand
- Target: ~10,000 concurrent strands (640MB stack memory)
- Good enough for initial stdlib development and validation

**Trade-offs:**
- ✅ Quick to implement
- ✅ Portable across POSIX systems (Linux, macOS, BSD)
- ✅ Well-documented with many examples
- ✅ Proves the scheduler design
- ❌ Limited scalability (~10K strands)
- ❌ Wasteful memory usage (most stack space idle)

**Deliverables:**
- Working I/O with cooperative multitasking
- `print` and `println` words in stdlib
- Simple echo program as integration test
- Robust stdlib built on working scheduler

### Phase 2b: Assembly Context Switching (MEDIUM TERM)

**Goal:** Enable Erlang-scale concurrency with efficient stack usage

**Timeline:** After Phase 2a proves correctness

**Implementation:**
1. Implement custom context switching in assembly
   - x86_64 implementation (Linux, macOS, Windows)
   - ARM64 implementation (macOS M-series, Linux ARM)
   - Keep `ucontext` as fallback for other platforms
2. Reduce stack size to 8KB per strand
   - More realistic initial allocation
   - Still fixed-size at this phase
3. Custom stack allocation
   - Maintain stack pools to reduce allocation overhead
   - Guard pages for overflow detection (optional)

**Context Switch Implementation Strategy:**
```c
// Pseudocode for x86_64 context switch
typedef struct {
    void* rsp;    // Stack pointer
    void* rbp;    // Base pointer
    void* rbx;    // Callee-saved registers
    void* r12;
    void* r13;
    void* r14;
    void* r15;
    void* rip;    // Return address
} context_t;

// Assembly implementation
// switch_context(old_ctx, new_ctx)
//   Save callee-saved registers to old_ctx
//   Load callee-saved registers from new_ctx
//   Switch stack pointer
//   Return (jumps to new_ctx->rip)
```

**Memory Profile:**
- 8KB per strand
- Target: ~100,000 concurrent strands (800MB stack memory)
- 8x improvement over Phase 2a

**Trade-offs:**
- ✅ Much better scalability
- ✅ Still relatively simple (no stack growth yet)
- ✅ Platform-specific optimizations possible
- ❌ Platform-specific code to maintain
- ❌ More complex debugging

**Deliverables:**
- Assembly implementations for x86_64 and ARM64
- Platform detection and fallback logic
- Performance benchmarks vs. Phase 2a
- Tested with 100K+ concurrent strands

### Phase 3: Dynamic Stack Growth (LONG TERM)

**Goal:** True Erlang-scale concurrency with minimal memory waste

**Timeline:** After Phase 2b is stable

**Implementation:**
1. Start with tiny stacks (2-4KB per strand)
2. Implement stack overflow detection
   - Guard pages (OS protection)
   - Or explicit checks in function prologues
3. Choose growth strategy:

**Option A: Segmented Stacks (Erlang's approach)**
```
Strand Stack:
┌──────────┐
│ Segment 3│ ← Current (allocated on overflow)
│   4KB    │
├──────────┤
│ Segment 2│ ← Previous
│   4KB    │
├──────────┤
│ Segment 1│ ← Initial
│   2KB    │
└──────────┘
```
- Pros: No copying, can grow indefinitely
- Cons: More complex pointer chasing, cache unfriendly

**Option B: Copying Stacks (Go's original approach)**
```
Overflow detected:
1. Allocate larger stack (2x current size)
2. Copy all data to new stack
3. Update all pointers on stack
4. Free old stack
```
- Pros: Contiguous memory, cache friendly
- Cons: Copying overhead, pointer tracking complex

**Option C: Hybrid (Modern Go approach)**
- Start with small stacks
- Grow by copying until ~128KB
- Switch to segmented for very large stacks

**Recommendation:** Start with Option A (segmented) for simplicity, as it matches Erlang's proven model and concatenative languages naturally handle stack segments.

**Memory Profile:**
- 2-4KB per strand initially
- Grows only as needed
- Target: **500,000+ concurrent strands** (~1-2GB total)
- Most strands stay small (typical usage: 2-8KB)

**Trade-offs:**
- ✅ Erlang-scale concurrency
- ✅ Minimal memory waste
- ✅ Enables true actors-everywhere programming
- ❌ Complex to implement correctly
- ❌ Difficult debugging (segmentation issues)
- ❌ Performance overhead of overflow checks

**Deliverables:**
- Stack overflow detection
- Segmented stack allocator
- Stack walking for GC (future)
- Benchmarks with 500K+ strands

## Key Design Decisions

### Why Cooperative (Yielding) Instead of Preemptive?

**Cooperative:**
- Explicit yield points (only at I/O operations)
- Deterministic within strands
- Simpler implementation (no signal handlers, timers)
- Matches concatenative semantics (visible side effects)

**Preemptive would require:**
- Timer interrupts or signal handlers
- More complex state saving
- Race conditions to handle
- Not needed for I/O-bound workloads

**Decision:** Stick with cooperative. Cem's I/O architecture document explicitly calls for yields at I/O operations.

### Stack Concatenation

The core insight from `docs/IO_ARCHITECTURE.md`:
- Each strand has its own isolated stack
- I/O operations are the yield points
- Sequential execution within a strand
- The concatenative nature makes stack manipulation explicit

This design works **regardless of stack implementation**:
- Phase 2a: 64KB ucontext stacks
- Phase 2b: 8KB assembly stacks
- Phase 3: 2KB dynamic stacks

The high-level scheduler API remains the same across all phases.

## Implementation Checklist

### Phase 2a (Current Focus)
- [ ] Update `Strand` structure to use `ucontext_t`
- [ ] Implement `strand_spawn()` with `makecontext()`
- [ ] Implement `strand_yield()` with `swapcontext()`
- [ ] Implement `scheduler_run()` event loop
- [ ] Add kqueue backend for macOS async I/O
- [ ] Implement `read_line()` with yielding
- [ ] Implement `write_line()` with yielding
- [ ] Add `print` and `println` to stdlib
- [ ] Write integration test: simple echo program
- [ ] Stress test: 1,000+ concurrent strands

### Phase 2b (After 2a proves correctness)
- [ ] x86_64 assembly context switch implementation
- [ ] ARM64 assembly context switch implementation
- [ ] Platform detection and fallback to ucontext
- [ ] Reduce stack size to 8KB
- [ ] Stack pool allocator
- [ ] Performance benchmarks vs Phase 2a
- [ ] Stress test: 100,000+ concurrent strands

### Phase 3 (After 2b is stable)
- [ ] Implement stack overflow detection
- [ ] Segmented stack allocator
- [ ] Stack growth on overflow
- [ ] Stack shrinking for idle strands (optional optimization)
- [ ] Stress test: 500,000+ concurrent strands
- [ ] Memory profiling under load

## Testing Strategy

### Phase 2a Tests
1. **Basic spawn/yield:** Create strand, yield, verify execution continues
2. **Multiple strands:** Spawn 10 strands, verify round-robin scheduling
3. **I/O operations:** Read and write with blocking/yielding
4. **Echo server:** Simple program that reads input and writes it back
5. **Stress test:** 1,000 strands doing I/O concurrently

### Phase 2b Tests
1. **Platform compatibility:** Verify x86_64 and ARM64 work correctly
2. **Fallback logic:** Test ucontext fallback on unsupported platforms
3. **Performance:** Measure context switch time (should be <100ns)
4. **Scale:** 100,000 concurrent strands
5. **Memory:** Verify 8KB stacks are sufficient for typical workloads

### Phase 3 Tests
1. **Stack growth:** Force stack overflow and verify growth
2. **Deep recursion:** Test strands with deep call stacks
3. **Memory efficiency:** Verify most strands stay at 2-4KB
4. **Extreme scale:** 500,000+ concurrent strands
5. **Stress under load:** High I/O volume with many strands

## References

**Similar Implementations:**
- **Erlang BEAM:** Segmented stacks, process-per-actor, assembly context switches
- **Go runtime:** Copying stacks (old) → hybrid approach (new), assembly context switches
- **Rust async:** Zero-cost futures, state machine transformation (different model but similar goals)
- **Project Loom (Java):** Virtual threads with dynamic stacks

**Resources:**
- `docs/IO_ARCHITECTURE.md` - Cem's I/O design philosophy
- `runtime/scheduler.h` - Current scheduler interface
- `runtime/scheduler.c` - Phase 1 implementation
- Erlang BEAM source: `erts/emulator/beam/erl_process.c`
- Go runtime source: `src/runtime/stack.go`, `src/runtime/asm_amd64.s`

## Success Metrics

### Phase 2a
- ✅ I/O operations work with yielding
- ✅ Simple programs run correctly with multiple strands
- ✅ Can support 1,000+ concurrent strands
- ✅ Stdlib can be built on top

### Phase 2b
- ✅ 10x scalability improvement (100K strands)
- ✅ Context switch overhead <100ns
- ✅ Works on Linux and macOS (both x86_64 and ARM64)

### Phase 3
- ✅ 100x scalability improvement (500K+ strands)
- ✅ Memory usage: <10GB for 500K idle strands
- ✅ True actors-everywhere programming model viable

## Timeline Philosophy

> "I don't want to put off the phase 2b and 3 beyond proving the scheduler correctness." - Project directive

**Approach:**
1. **Phase 2a:** Move quickly to prove the model (weeks, not months)
2. **Validation:** Ensure correctness with comprehensive tests
3. **Phase 2b:** Begin immediately after validation (don't wait for full stdlib)
4. **Phase 3:** Begin as soon as Phase 2b is stable

The goal is to have a **working stdlib on Phase 2a**, then **upgrade the engine underneath** without changing high-level APIs.

## Notes

- The scheduler API (`strand_spawn`, `strand_yield`) remains stable across all phases
- High-level I/O operations (`read_line`, `write_line`) remain stable across all phases
- Only the internal context switching mechanism changes between phases
- This allows for incremental improvement without breaking user code
- The concatenative nature of Cem makes stack management explicit, which simplifies this phased approach

---

*Last updated: 2025-10-11*
*Status: Beginning Phase 2a implementation*
