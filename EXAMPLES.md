# Cem Language Examples

*Pronounced "seam"*

This document contains example Cem programs to illustrate the language design and features.

## Basic Stack Operations

```cem
# Primitive operations
: square ( Int -- Int )
  dup * ;

: pythagorean ( Int Int -- Int )
  square swap square + ;

# Usage: 3 4 pythagorean  => 25
```

## Pattern Matching

### Option Type

```cem
type Option<T> =
  | Some(T)
  | None

: safe-div ( Int Int -- Option<Int> )
  dup 0 =
  [ drop drop None ]
  [ / Some ]
  if ;

: unwrap-or ( Option<Int> Int -- Int )
  swap match
    Some => [ swap drop ]       # Have value, drop default
    None => [ ]                 # No value, use default
  end ;

# Usage:
10 2 safe-div 0 unwrap-or   # Result: 5
10 0 safe-div 0 unwrap-or   # Result: 0
```

### Result Type

```cem
type Result<T, E> =
  | Ok(T)
  | Err(E)

: parse-int ( String -- Result<Int, String> )
  # ... parsing logic ...
  dup is-numeric
  [ parse Ok ]
  [ drop "Not a number" Err ]
  if ;

: handle-result ( Result<Int, String> -- Int )
  match
    Ok => [ ]
    Err(msg) => [
      msg print-error
      0
    ]
  end ;
```

## Recursive Data Types

### Lists

```cem
type List<T> =
  | Cons(T, List<T>)
  | Nil

: sum ( List<Int> -- Int )
  match
    Nil => [ 0 ]
    Cons => [ sum + ]           # Cons pushes: head tail
  end ;

: length ( List<A> -- Int )
  match
    Nil => [ 0 ]
    Cons => [ drop length 1 + ]
  end ;

: map ( List<A> [A -- B] -- List<B> )
  match
    Nil => [ drop Nil ]
    Cons => [                   # Stack: head tail quotation
      [ dip ] dip               # Apply quotation to head
      map                       # Recurse on tail
      Cons                      # Construct result
    ]
  end ;

# Usage:
# [1, 2, 3] [ dup * ] map  => [1, 4, 9]
```

### Binary Trees

```cem
type Tree<T> =
  | Leaf(T)
  | Node(Tree<T>, T, Tree<T>)

: sum-tree ( Tree<Int> -- Int )
  match
    Leaf => [ ]                 # Leaf(x) pushes x
    Node => [                   # Node(l,v,r) pushes l v r
      rot sum-tree              # Sum left subtree
      swap sum-tree             # Sum right subtree
      + +                       # Add all three
    ]
  end ;

: depth ( Tree<A> -- Int )
  match
    Leaf => [ drop 1 ]
    Node => [
      rot depth                 # Depth of left
      swap drop                 # Drop value
      depth                     # Depth of right
      max 1 +                   # Max + 1
    ]
  end ;

: in-order ( Tree<A> [A -- ] -- )
  match
    Leaf => [ swap call ]       # Call quotation on leaf value
    Node => [                   # Stack: l v r quotation
      [ dup ] dip               # Copy quotation for later
      [ dip ] dip               # Process left subtree
      swap over call            # Process value
      in-order                  # Process right subtree
    ]
  end ;
```

## Linear Types & Ownership

### String Operations (Linear)

```cem
: greet ( String -- String )
  "Hello, " swap concat "!" concat ;

: bad-example ( String -- String String )
  dup ;                         # ERROR: String is not Copy

: good-example ( String -- String String )
  clone ;                       # OK: Explicit clone

: consume ( String -- )
  drop ;                        # OK: Consumes ownership

: use-after-move ( String -- )
  drop
  print ;                       # ERROR: String already consumed
```

### Type-State Pattern

```cem
type File =
  | Open(Handle)
  | Closed

: open-file ( String -- File )
  open-handle Open ;

: read-file ( File -- File String )
  match
    Open(h) => [
      h read-contents           # Read from handle
      swap Open swap            # Reconstruct File
    ]
    Closed => [
      Closed "ERROR: File closed"
    ]
  end ;

: write-file ( File String -- File )
  match
    Open(h) => [
      h swap write-contents     # Write to handle
      Open                      # Reconstruct File
    ]
    Closed => [
      drop Closed               # Can't write to closed file
    ]
  end ;

: close-file ( File -- File )
  match
    Open(h) => [
      h close-handle            # Close handle (consumes it)
      Closed
    ]
    Closed => [ Closed ]        # Already closed
  end ;

# Usage: Safe file operations
"test.txt" open-file            # File (Open)
"Hello, world!" write-file      # File String
read-file                       # File String
close-file                      # File (Closed)
drop                            # Clean up
```

## Combinators

### Basic Combinators

```cem
: dip ( rest A [rest -- rest'] -- rest' A )
  # Execute quotation under top element
  swap [ ] swap concat call ;

: keep ( rest A [rest A -- rest' A] -- rest' A )
  # Execute quotation preserving top element
  over [ dip ] dip ;

: bi ( rest A [A -- B] [A -- C] -- rest B C )
  # Apply two quotations to same value
  [ keep ] dip call ;

: bi* ( rest A B [A -- C] [B -- D] -- rest C D )
  # Apply two quotations to two values
  [ dip ] dip call ;

: tri ( rest A [A--B] [A--C] [A--D] -- rest B C D )
  # Apply three quotations to same value
  [ [ keep ] dip keep ] dip call ;
```

### List Combinators

```cem
: filter ( List<A> [A -- Bool] -- List<A> )
  match
    Nil => [ drop Nil ]
    Cons => [                   # Stack: head tail quotation
      [ dup ] dip               # Copy quotation
      [ [ dip ] dip ] dip       # Apply to head
      [
        filter                  # Recurse on tail
        Cons                    # Include head
      ]
      [
        drop filter             # Exclude head
      ]
      if
    ]
  end ;

: fold ( List<A> B [B A -- B] -- B )
  swap match
    Nil => [ drop ]
    Cons => [                   # Stack: head tail acc quotation
      [ swap ] dip              # Stack: head acc tail quotation
      [ [ dip ] dip ] dip       # Apply quotation to acc and head
      fold                      # Recurse on tail
    ]
  end ;

: reverse ( List<A> -- List<A> )
  Nil [ swap Cons ] fold ;
```

## Concurrency (CSP)

### Simple Pipeline

```cem
: generator ( Chan<Int> Int -- )
  # Generate numbers from 0 to n-1
  0 swap [                      # Stack: Chan counter limit
    over over <                 # counter < limit?
    [
      over over send            # Send counter
      1 +                       # Increment
    ]
    [ drop drop drop ]          # Clean up
    if
  ] ;

: doubler ( Chan<Int> Chan<Int> -- )
  # Receive from in, double, send to out
  [
    recv                        # Stack: in out value
    dup 0 >=                    # Check for termination
    [
      2 * send                  # Double and send
      doubler                   # Recurse
    ]
    [ drop drop drop ]          # Clean up
    if
  ] ;

: consumer ( Chan<Int> -- )
  # Receive and print values
  [
    recv                        # Stack: chan value
    dup 0 >=
    [ print consumer ]          # Print and continue
    [ drop drop ]               # Done
    if
  ] ;

# Usage:
# make-channel make-channel    # Create two channels
# [ over 10 generator ] spawn  # Spawn generator
# [ doubler ] spawn            # Spawn doubler
# consumer                     # Run consumer in main thread
```

### Parallel Map

```cem
: worker ( Chan<Option<A>> Chan<B> [A -- B] -- )
  # Receive from input, apply function, send to output
  [
    recv                        # Stack: in out quotation value
    match
      Some => [
        [ [ dup ] dip ] dip     # Copy quotation
        [ dip ] dip             # Apply to value
        send                    # Send result
        worker                  # Recurse
      ]
      None => [
        drop drop drop          # Terminate
      ]
    end
  ] ;

: par-map ( List<A> [A -- B] Int -- List<B> )
  # Map using n workers
  # Create input/output channels
  make-channel make-channel

  # Spawn n workers
  over 0 [
    over over <
    [
      # Stack: list quotation n in out counter
      # ... spawn worker logic ...
      1 +
    ]
    [ drop ]
    if
  ]

  # Send all list elements to input channel
  # Collect results from output channel
  # ... details TBD ...
  ;
```

### Fan-out / Fan-in

```cem
: fan-out ( Chan<A> List<Chan<A>> -- )
  # Read from input, broadcast to all outputs
  [
    over recv                   # Read from input
    # ... send to all channels in list ...
  ] ;

: fan-in ( List<Chan<A>> Chan<A> -- )
  # Read from any input, send to output
  # Requires select primitive
  [
    over select recv            # Receive from any input channel
    over send                   # Send to output
    fan-in                      # Recurse
  ] ;
```

## Practical Examples

### Fibonacci (Recursive)

```cem
: fib ( Int -- Int )
  dup 2 <
  [ ]
  [ dup 1 - fib swap 2 - fib + ]
  if ;
```

### Fibonacci (Iterative)

```cem
: fib-iter ( Int -- Int )
  0 1 rot                       # Stack: a b n
  [ dup 0 > ]
  [
    rot rot                     # Stack: n a b
    over +                      # Stack: n a (a+b)
    rot 1 -                     # Stack: a (a+b) (n-1)
  ]
  while
  drop swap drop ;              # Return b
```

### Quicksort

```cem
: partition ( List<Int> Int -- List<Int> List<Int> )
  # Partition list around pivot
  # Returns: (elements <= pivot) (elements > pivot)
  [ over <= ] filter
  swap [ over > ] filter ;

: quicksort ( List<Int> -- List<Int> )
  match
    Nil => [ Nil ]
    Cons => [                   # Stack: head tail
      over partition            # Partition tail around head
      quicksort                 # Sort lower partition
      swap quicksort            # Sort upper partition
      swap Cons                 # Add pivot between partitions
      concat                    # Concatenate all
    ]
  end ;
```

### Word Count (Concurrent)

```cem
: count-words ( String -- Int )
  split-whitespace length ;

: word-count-worker ( Chan<Option<String>> Chan<Int> -- )
  [
    recv match
      Some => [
        count-words send
        word-count-worker
      ]
      None => [ drop drop ]
    end
  ] ;

: parallel-word-count ( List<String> Int -- Int )
  # Count words in list of strings using n workers
  # Create channels, spawn workers, distribute work
  # ... implementation details ...
  ;
```

## Standard Library Snippets

### Option Utilities

```cem
: is-some ( Option<A> -- Bool )
  match
    Some => [ drop true ]
    None => [ false ]
  end ;

: is-none ( Option<A> -- Bool )
  match
    Some => [ drop false ]
    None => [ true ]
  end ;

: option-map ( Option<A> [A -- B] -- Option<B> )
  swap match
    Some => [ call Some ]
    None => [ drop None ]
  end ;

: option-and-then ( Option<A> [A -- Option<B>] -- Option<B> )
  swap match
    Some => [ call ]
    None => [ drop None ]
  end ;
```

### Result Utilities

```cem
: is-ok ( Result<T,E> -- Bool )
  match
    Ok => [ drop true ]
    Err => [ drop false ]
  end ;

: is-err ( Result<T,E> -- Bool )
  match
    Ok => [ drop false ]
    Err => [ drop true ]
  end ;

: result-map ( Result<T,E> [T -- U] -- Result<U,E> )
  swap match
    Ok => [ call Ok ]
    Err => [ drop Err ]
  end ;

: result-and-then ( Result<T,E> [T -- Result<U,E>] -- Result<U,E> )
  swap match
    Ok => [ call ]
    Err => [ drop Err ]
  end ;
```

These examples demonstrate the key features of Cem and serve as test cases for implementation phases.
