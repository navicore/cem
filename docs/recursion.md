# Recursion in Cem

## Philosophy

Cem follows the concatenative language philosophy of favoring pure, composable primitives over imperative convenience features. **Recursion with tail-call optimization is the idiomatic flow control mechanism**, not while loops or other imperative constructs.

This design choice keeps the language minimal and elegant while providing all the power needed for complex control flow.

## Tail-Call Optimization

The Cem compiler automatically detects and optimizes tail calls. When a word call appears as the last expression in a word's body, the compiler generates a `musttail` call instruction in LLVM IR. This tells LLVM to:

1. Reuse the current stack frame instead of creating a new one
2. Convert the call into a jump, avoiding stack growth
3. Enable unbounded recursion without risk of stack overflow

### Example: Tail Position

```cem
# This is a tail call - 'helper' is the last operation
: factorial ( n -- n! )
  1 factorial-helper ;

# This is also a tail call - 'factorial-helper' is last in the branch
: factorial-helper ( n acc -- result )
  over 1 > if
    [ over 1 - swap over * factorial-helper ]  # Tail call!
    [ nip ]
  ;
```

### Example: NOT Tail Position

```cem
# This is NOT a tail call - multiplication happens after 'factorial'
: naive-factorial ( n -- n! )
  dup 1 > if
    [ dup 1 - factorial * ]  # NOT tail position (has * after)
    [ drop 1 ]
  ;
```

## Patterns

### Accumulator Pattern

The most common pattern for tail recursion is to use an accumulator parameter:

```cem
: factorial-helper ( n acc -- result )
  over 1 > if
    [ over 1 - swap over * factorial-helper ]
    [ nip ]  # Return accumulator
  ;

: factorial ( n -- n! )
  1 factorial-helper ;  # Start with accumulator = 1
```

This works because:
- Each recursive call updates both `n` (counting down) and `acc` (accumulating result)
- The recursive call is in tail position
- No work happens after the recursive call returns

### Countdown Pattern

For operations that need to repeat N times:

```cem
: countdown ( n -- 0 )
  dup 0 > if
    [ 1 - countdown ]
    [ ]
  ;
```

### Mutual Recursion

Tail-call optimization also works with mutually recursive functions:

```cem
: even? ( n -- bool )
  dup 0 = if
    [ drop true ]
    [ 1 - odd? ]
  ;

: odd? ( n -- bool )
  dup 0 = if
    [ drop false ]
    [ 1 - even? ]
  ;
```

## Why Not While Loops?

While loops are an imperative construct that doesn't fit the concatenative paradigm:

1. **Not Composable**: Loops are statements, not expressions that compose with other operations
2. **Hidden State**: Loop counters and conditions create implicit state
3. **Not Minimal**: Recursion + tail-call optimization provides the same power without adding new primitives

By using recursion, we get:
- Pure functional composition
- Explicit data flow through the stack
- Zero-cost abstraction via tail-call optimization
- Simpler language semantics

## Implementation Details

### LLVM IR Generation

For a tail call, the compiler generates:

```llvm
%result = musttail call ptr @factorial-helper(ptr %stack)
ret ptr %result
```

The `musttail` attribute guarantees LLVM will optimize this into a jump instruction, making it as efficient as any imperative loop.

### Verification

You can verify tail-call optimization is working by:

1. Looking at the generated LLVM IR for `musttail` annotations
2. Running programs with deep recursion (thousands of calls) without stack overflow
3. Checking the compiled assembly shows jumps instead of calls

## Future Extensions

As Cem develops, additional patterns may emerge:

- **Trampolines**: For recursion schemes that don't fit tail position
- **Lazy Evaluation**: For infinite data structures
- **Continuation Passing**: For complex control flow

But the core principle remains: **favor pure, minimal primitives over imperative convenience**.
