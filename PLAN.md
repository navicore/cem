# Cem Development Plan

*Pronounced "seam"*

## Project Phases

This document outlines the phased development approach for Cem, with concrete milestones and deliverables.

## Phase 1: Core Type Checker & Effect Inference (Weeks 1-4)

**Goal**: Prove that concatenative + linear types + pattern matching is viable.

### Week 1-2: Type Checker Foundation

**Deliverables**:
- [ ] AST representation for Cem programs
  - Type definitions (sum types/ADTs)
  - Word definitions with effect signatures
  - Quotations
  - Pattern matching expressions
  - Primitives (Int, Bool)
- [ ] Parser (minimal, just enough to test type checker)
  - Can use simple s-expression syntax initially
  - Proper syntax can come later
- [ ] Effect type representation
  - Stack types with row polymorphism
  - Effect signatures `(inputs -- outputs)`
  - Type variables and constraints
- [ ] Basic type checker
  - Bidirectional checking for word bodies
  - Effect composition (concatenation)
  - Stack underflow detection

**Test Cases**:
```cem
: dup ( A -- A A ) dup ;              # Should fail: infinite recursion in type
: square ( Int -- Int ) dup * ;       # Should pass
: bad ( Int -- Bool ) dup + ;         # Should fail: returns Int, not Bool
: underflow ( -- Int ) + ;            # Should fail: stack underflow
```

### Week 3: Pattern Matching & Sum Types

**Deliverables**:
- [ ] Sum type representation in type checker
  - Built-in types: `Option<T>`, `Result<T,E>`, `List<T>`
  - Variant constructors as words
- [ ] Pattern matching type checker
  - Exhaustiveness checking
  - Effect consistency across branches
  - Pattern destructuring onto stack
- [ ] Linear type tracking (basic)
  - Track which types are Copy vs Linear
  - Enforce linearity for `dup`, `drop`

**Test Cases**:
```cem
: unwrap ( Option<Int> -- Int )
  match
    Some => [ ]
    None => [ 0 ]
  end ;                                # Should pass

: incomplete ( Option<Int> -- Int )
  match
    Some => [ ]
  end ;                                # Should fail: non-exhaustive

: inconsistent ( Option<Int> -- )
  match
    Some => [ ]                        # Effect: (Option -- Int)
    None => [ ]                        # Effect: (Option -- )
  end ;                                # Should fail: inconsistent effects
```

### Week 4: Row Polymorphism & Inference Polish

**Deliverables**:
- [ ] Row polymorphism unification algorithm
  - Handle "rest of stack" in effects
  - Unify row variables correctly
- [ ] Better error messages
  - Show expected vs actual stack state
  - Highlight error location in source
- [ ] Standard combinators
  - `swap`, `over`, `rot`, `dip`, etc.
  - Verify polymorphic effects work

**Test Cases**:
```cem
: dip ( rest A [rest -- rest'] -- rest' A )
  swap [ ] swap concat call ;         # Should infer correct polymorphic effect

: over ( A B -- A B A )
  swap dup rot ;                       # Should handle row polymorphism
```

**Phase 1 Success Criteria**:
- Type checker can verify all test cases
- Pattern matching is exhaustiveness-checked
- Linear types prevent `dup` on non-Copy types
- Row polymorphism works for basic combinators
- Error messages are comprehensible

---

## Phase 2: LLVM Backend (Weeks 5-6)

**Goal**: Compile type-checked Seam to native code via LLVM.

### Week 5: Basic Codegen

**Deliverables**:
- [ ] LLVM IR generation for primitives
  - Int, Bool operations
  - Stack operations (push, pop, dup, etc.)
- [ ] Explicit data stack in generated code
  - Stack as memory region (not C stack)
  - Use LLVM allocas for stack slots
  - Optimize small stacks to registers
- [ ] Control flow compilation
  - `if` using quotations
  - `while` loops
- [ ] Word calls
  - Direct calls (inline small words)
  - Tail call optimization

**Test Program**: Factorial
```cem
: factorial ( Int -- Int )
  dup 1 <=
  [ ]
  [ dup 1 - factorial * ]
  if ;
```

### Week 6: Pattern Matching Codegen

**Deliverables**:
- [ ] Sum type representation in LLVM
  - Tagged unions (tag + payload)
  - Efficient memory layout
- [ ] Pattern matching compilation
  - Tag switching (decision tree)
  - Payload extraction
  - Branch compilation
- [ ] Integration & testing
  - End-to-end: parse → type-check → compile → run

**Test Program**: List sum
```cem
type List<T> = Cons(T, List<T>) | Nil

: sum ( List<Int> -- Int )
  match
    Nil => [ 0 ]
    Cons => [ sum + ]
  end ;
```

**Phase 2 Success Criteria**:
- Can compile and run factorial, fibonacci
- Can compile and run list operations (map, filter, sum)
- Pattern matching generates efficient code
- No C stack overflow on deep recursion (tail calls work)

---

## Phase 3: Memory Safety & Linear Types (Weeks 7-8)

**Goal**: Extend type system with full linear type support for heap-allocated types.

### Week 7: String Type & Ownership

**Deliverables**:
- [ ] Add `String` as heap-allocated type
  - Not Copy (must clone explicitly)
  - Runtime representation (pointer + length)
- [ ] Ownership tracking in type checker
  - Linear use analysis
  - Prevent use-after-move
  - Enforce explicit clone
- [ ] Arena allocator
  - Per-thread arena for heap allocations
  - Free arena on thread exit (no GC)

**Test Cases**:
```cem
: bad-dup ( String -- String String )
  dup ;                                # Should fail: String not Copy

: good-clone ( String -- String String )
  clone ;                              # Should pass

: consume-twice ( String -- )
  drop drop ;                          # Should fail: use after move
```

### Week 8: Type-State Pattern

**Deliverables**:
- [ ] User-defined ADTs with linear payloads
  - Example: `File = Open(Handle) | Closed`
- [ ] Linear consumption in pattern matching
  - Verify payload consumed in branches
- [ ] Example: Safe file operations

**Test Program**: Type-state file I/O
```cem
type File = Open(Handle) | Closed

: read-file ( File -- File String )
  match
    Open(h) => [ h read-contents swap Open swap ]
    Closed => [ Closed "ERROR" ]
  end ;

: close-file ( File -- File )
  match
    Open(h) => [ h close-handle Closed ]
    Closed => [ Closed ]
  end ;
```

**Phase 3 Success Criteria**:
- String operations work correctly (allocation, clone, free)
- Type checker prevents use-after-move
- Can't dup non-Copy types
- File example demonstrates type-state safety

---

## Phase 4: CSP Concurrency (Weeks 9-10)

**Goal**: Add safe concurrency with linear channels and green threads.

### Week 9: Green Thread Runtime

**Deliverables**:
- [ ] M:N threading runtime
  - Work-stealing scheduler
  - Task queue per OS thread
  - Green thread spawning
- [ ] `spawn` primitive
  - Takes quotation, runs in new green thread
  - Type: `( [-- ] -- )`
- [ ] Channel type and operations
  - `Chan<T>` as linear type
  - `send: ( Chan<T> T -- Chan<T> )`
  - `recv: ( Chan<T> -- Chan<T> T )`
- [ ] Lock-free channel implementation
  - Bounded or unbounded queues
  - Efficient cross-thread communication

**Test Program**: Simple pipeline
```cem
: generator ( Chan<Int> -- )
  10 0 [
    dup 10 <
    [ dup over send 1 + ]
    [ drop drop ]
    if
  ] ;

: consumer ( Chan<Int> -- )
  [ recv dup 0 > ] [
    print
  ] while drop ;
```

### Week 10: Channel Safety & Integration

**Deliverables**:
- [ ] Linear channel type checking
  - Can't leak channels
  - Can't use after close
  - Must consume or pass channels
- [ ] Select-like primitive (stretch goal)
  - Receive from multiple channels
  - Non-blocking or timeout
- [ ] Example: Concurrent word count

**Test Program**: Parallel map
```cem
: par-map ( List<A> [A -- B] -- List<B> )
  # Spawn workers, distribute work via channels, collect results
  # Details TBD
  ;
```

**Phase 4 Success Criteria**:
- Can spawn thousands of green threads
- Channels safely transfer ownership
- Pipeline example runs without races
- Type checker prevents channel leaks

---

## Phase 5: Ergonomics & Tooling (Weeks 11-12)

**Goal**: Make Seam usable for real programs.

### Deliverables

- [ ] User-defined ADTs
  - Type definitions in source files
  - Generic types
  - Recursive types
- [ ] Module system
  - File-based modules
  - Import/export declarations
  - Namespacing
- [ ] Standard library
  - List, Option, Result operations
  - String operations
  - I/O primitives
  - Common combinators
- [ ] REPL
  - Interactive evaluation
  - Show stack state
  - Effect inference for expressions
- [ ] Better error messages
  - Source locations
  - Suggestions (e.g., "did you mean clone?")
  - Stack trace on panic
- [ ] Proper syntax & parser
  - Finalize concrete syntax
  - Better parser (pest? nom? hand-written?)
  - Syntax highlighting definition

**Phase 5 Success Criteria**:
- Can write multi-module programs
- REPL is usable for development
- Standard library covers common operations
- Documentation and examples exist

---

## Future Phases (Beyond Week 12)

### Potential Features

- **Closures**: Quotations that capture lexical scope
- **Partial application**: Currying for concatenative languages
- **Type classes/traits**: Ad-hoc polymorphism
- **Macros**: Compile-time metaprogramming
- **Async/await**: Syntactic sugar over green threads?
- **Foreign Function Interface (FFI)**: Call C/Rust libraries
- **Package manager**: Dependency management
- **Debugger**: Step through concatenative code
- **Profiler**: Identify performance bottlenecks
- **IDE support**: LSP server for editors

### Research Questions

- **Effect handlers**: Algebraic effects for concatenative?
- **Dependent types**: Can stack effects be more expressive?
- **Formal verification**: Can we prove programs correct?
- **JIT compilation**: Runtime optimization?

---

## Development Approach

### Tools & Technologies

- **Implementation language**: Rust
  - Excellent for compiler development
  - LLVM bindings via Inkwell
  - Existing parser libraries
- **Parser**: TBD (pest, nom, or hand-written)
- **LLVM backend**: Inkwell crate
- **Testing**: Property-based tests with proptest
- **CI/CD**: GitHub Actions
- **Documentation**: mdBook

### Testing Strategy

1. **Unit tests**: Type checker, parser, codegen components
2. **Integration tests**: Full pipeline (source → binary)
3. **Property tests**: Invariants (well-typed programs don't crash)
4. **Example programs**: Fibonacci, tree traversals, pipelines
5. **Benchmarks**: Compare to other languages (Rust, Go, Factor)

### Documentation

- **Language spec**: Formal specification of syntax and semantics
- **Type system**: Formal rules for effect inference
- **Tutorial**: Learn Seam by example
- **Standard library docs**: API reference
- **Implementation notes**: How the compiler works

---

## Success Metrics

### Phase 1-4 (Core Language)
- All test cases pass
- Can write non-trivial programs (100+ lines)
- Performance competitive with other compiled languages
- No memory leaks or data races

### Phase 5 (Usability)
- Positive feedback from early users
- Standard library covers common use cases
- Error messages are helpful
- REPL is productive

### Long-term
- Community adoption
- Real-world projects written in Seam
- Contributions from other developers
- Published research on the type system

---

## Risk Mitigation

### Technical Risks

1. **Effect inference too complex**
   - Mitigation: Start with simple cases, add complexity gradually
   - Fallback: Require more annotations if inference fails

2. **LLVM integration issues**
   - Mitigation: Prototype codegen early (Phase 2)
   - Fallback: Use Cranelift or even interpreter

3. **Linear types too restrictive**
   - Mitigation: Provide escape hatches (unsafe blocks?)
   - Fallback: Loosen requirements based on user feedback

4. **CSP runtime performance**
   - Mitigation: Benchmark against Go early
   - Fallback: Simplify scheduler if needed

### Project Risks

1. **Scope creep**
   - Mitigation: Strict phase gates, don't add features early
   - Focus: Prove core concept first

2. **Losing momentum**
   - Mitigation: Set weekly milestones, maintain momentum
   - Focus: Ship something working each phase

---

## Next Steps

**Immediate**: Start Phase 1, Week 1
1. Design AST representation
2. Write parser (minimal s-expressions)
3. Implement basic type checker
4. Write first test cases

**Ready to begin!**
