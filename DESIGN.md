# Cem Language Design

*Pronounced "seam"*

## Overview

Cem is a concatenative language with a strong focus on compile-time safety through linear types, effect inference, and exhaustive pattern matching.

## Type System

### Primitive Types

- `Int` - Machine integers (Copy)
- `Bool` - Booleans (Copy)
- `String` - Heap-allocated strings (Linear, requires explicit clone)

### Sum Types (Algebraic Data Types)

```cem
type Option<T> =
  | Some(T)
  | None

type Result<T, E> =
  | Ok(T)
  | Err(E)

type List<T> =
  | Cons(T, List<T>)
  | Nil
```

### Quotation Types (First-class Functions)

```cem
[Int Int -- Int]    # Takes 2 ints, produces 1 int
[A -- B]            # Generic: takes A, produces B
[rest A -- rest B]  # Row polymorphic: preserves rest of stack
```

## Stack Effects

Every word has a stack effect signature describing what it consumes and produces:

```cem
: dup ( A -- A A )          # Duplicates top of stack
: drop ( A -- )             # Removes top of stack
: swap ( A B -- B A )       # Swaps top two elements
: + ( Int Int -- Int )      # Adds two integers
```

### Effect Composition

Concatenation composes effects:

```cem
: square ( Int -- Int )
  dup * ;                   # (Int -- Int Int) then (Int Int -- Int)

: pythagorean ( Int Int -- Int )
  square swap square + ;    # Effects compose left-to-right
```

### Row Polymorphism

The stack effect system uses row polymorphism to handle "rest of stack":

```cem
: dup ( ∀a, rest. rest a -- rest a a )
```

This allows `dup` to work regardless of what's below the top element.

## Linear Types & Ownership

Inspired by Rust, Cem tracks ownership at compile time:

### Rules

1. **Primitives are Copy**: `Int`, `Bool` can be `dup`'d freely
2. **Heap types are Linear**: `String`, ADTs containing heap data must be explicitly cloned
3. **Operations consume values**: `drop` consumes ownership, pattern matching consumes scrutinee
4. **Quotations capture linearly**: Code blocks that capture values must respect ownership

### Examples

```cem
: dup-int ( Int -- Int Int )
  dup ;                     # OK: Int is Copy

: dup-string ( String -- String String )
  dup ;                     # ERROR: String is not Copy

: clone-string ( String -- String String )
  clone ;                   # OK: Explicit clone

: consume ( String -- )
  drop ;                    # OK: Consumes ownership
```

## Pattern Matching

Pattern matching destructures sum types **onto the stack**:

```cem
: unwrap-option ( Option<Int> -- Int )
  match
    Some => [ ]             # Some(x) pops Option, pushes x (Int)
    None => [ 0 ]           # None pops Option, pushes nothing, quotation pushes 0
  end ;
```

### Pattern Branch Effects

Each pattern branch has its own stack effect:

```cem
type Tree<T> =
  | Leaf(T)
  | Node(Tree<T>, T, Tree<T>)

: sum-tree ( Tree<Int> -- Int )
  match
    Leaf => [ ]                         # ( Tree -- Int )
    Node => [ rot sum-tree              # ( Tree -- Tree Int Tree )
              swap sum-tree
              + + ]
  end ;
```

The type checker verifies:
1. **Exhaustiveness**: All variants are covered
2. **Effect consistency**: All branches produce same final stack effect
3. **Linear consumption**: Scrutinee is consumed exactly once

## Memory Model

### No Garbage Collection

Seam uses compile-time memory management:

- **Copy types**: Passed by value (registers/stack)
- **Linear types**: Owned, moved, or explicitly cloned
- **Arena allocation**: Per-thread arenas for heap allocations
- **Compile-time bounds**: Effect inference determines maximum stack depth

### Stack Layout

```
Data Stack:    Explicit memory region (not C stack)
               - Max depth known per word via effect inference
               - Small depths → LLVM uses registers
               - Large depths → arena allocation

Return Stack:  For control flow
               - Can use hardware stack or explicit structure
               - Tail call optimization is trivial (just jump)

Heap:          Arena allocator per thread
               - No GC, ownership tracked by type system
               - Free arena on thread completion
```

## Concurrency - CSP Model

Cem uses Communicating Sequential Processes (CSP) with linear channels:

```cem
type Chan<T> = ...          # Linear type, must be consumed

: spawn ( [-- ] -- )        # Spawn quotation as new process

: send ( Chan<T> T -- Chan<T> )
  # Send value, returns channel (linear!)

: recv ( Chan<T> -- Chan<T> T )
  # Receive value, returns channel and value
```

### Linear Channels Guarantee Safety

```cem
: worker ( Chan<Int> Chan<Int> -- )
  [
    recv                    # Receive input
    dup 0 =
    [ drop drop drop ]      # Must clean up both channels (linear!)
    [ process swap send worker ]
    if
  ] ;
```

The type checker ensures:
- Can't leak channels (must use or pass)
- Can't use channel after close (linearity prevents)
- Can't have data races (ownership transferred through send)

### Implementation

- **M:N green threads**: Work-stealing scheduler (like Go/Tokio)
- **Lock-free channels**: Bounded/unbounded queues
- **No shared memory**: Only message passing
- **Native compilation**: LLVM backend, no runtime overhead

## Syntax

### Type Definitions

```cem
type TypeName<T, U> =
  | VariantOne(T)
  | VariantTwo(U, T)
  | VariantThree
```

### Word Definitions

```cem
: word-name ( input-effect -- output-effect )
  body
  words
  here ;
```

### Quotations

```cem
[ quotation body ]          # Code block, first-class value
```

### Pattern Matching

```cem
match
  Pattern1 => [ quotation1 ]
  Pattern2 => [ quotation2 ]
end
```

### Control Flow

```cem
condition [ then-branch ] [ else-branch ] if

[ body ] [ condition ] while
```

## Effect Inference Algorithm

### Bidirectional Type Checking

1. **Top-level words**: Require explicit effect signatures
2. **Internal compositions**: Effects inferred from concatenation
3. **Quotations**: Effects inferred or checked against expected type
4. **Pattern branches**: Each branch checked, results unified

### Inference Steps

1. Parse word definition with declared effect
2. Type-check body:
   - Start with input stack from effect signature
   - For each word in body:
     - Look up word's effect
     - Apply effect to current stack type
     - Verify stack doesn't underflow
   - Verify final stack matches output effect
3. For quotations:
   - Infer effect from body if no type annotation
   - Check against expected type if used as argument

### Row Polymorphism Unification

Stack types are represented as rows:
```
rest · Int · Bool    (row with Int and Bool on top)
```

Unification handles row variables:
```
rest1 · A  ~  rest2 · Int · Bool
  =>  rest1 = rest2 · Int  AND  A = Bool
```

## Design Decisions & Tradeoffs

### Why Bidirectional?

- **Simpler inference**: Don't need full Hindley-Milner
- **Better errors**: Signatures provide clear boundaries
- **Predictable**: No surprising generalizations
- **Like Rust**: Explicit types on public APIs

### Why Linear Types?

- **No GC needed**: Compile-time memory management
- **Prevents leaks**: Must consume or pass values
- **Safe concurrency**: Ownership transfer prevents races
- **Like Rust**: Ownership without borrow checker

### Why Concatenative?

- **Composition is simple**: Just juxtaposition
- **No variable names**: Point-free style
- **Stack machine**: Natural for compilation
- **Quotations**: Higher-order functions without lambda calculus

### Why CSP?

- **No shared memory**: Eliminates whole class of bugs
- **Natural fit**: Channels are linear types
- **Proven model**: Works in Go, Erlang
- **Composable**: Processes compose like words

## Open Questions

### Phase 1 (Core Implementation)

1. **Quotation representation**:
   - Pure code blocks (function pointers) vs closures (heap allocation)?
   - Start with pure quotations only?

2. **Effect polymorphism**:
   - Full higher-rank polymorphism or prenex polymorphism?
   - How to handle `dip`, `map`, etc.?

3. **Pattern matching exhaustiveness**:
   - Algorithm: decision trees, backtracking, or match matrix?
   - How to report missing cases?

4. **Error messages**:
   - How to pinpoint stack underflow in point-free code?
   - Show expected vs actual stack state?

### Phase 2+ (Future Features)

1. **Closures**: When/how to allow quotations to capture values?
2. **Partial application**: Worth the complexity?
3. **Type classes/traits**: For polymorphic operations?
4. **Module system**: Namespacing, imports, visibility?
5. **Metaprogramming**: Macros, compile-time evaluation?

## References & Inspiration

- **Forth**: Original concatenative language
- **Factor**: Modern concatenative with types
- **Joy**: Pure concatenative, quotation-based
- **Cat**: Concatenative with effect types
- **Kitten**: Concatenative with linear types
- **Rust**: Ownership, linear types, pattern matching
- **Go**: CSP concurrency model
- **Erlang**: Pattern matching, message passing
- **Linear Haskell**: Linear types in functional setting
