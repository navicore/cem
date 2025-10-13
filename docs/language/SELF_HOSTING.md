# Self-Hosting Vision for Cem

*Pronounced "seam"*

## Philosophy

**Self-hosting is our North Star, not our first destination.**

While Rust will remain the production compiler (fast, maintained, excellent tooling), the ability to write the Cem compiler in Cem itself serves as:

1. **Language Completeness Proof** - If Cem can compile itself, it's a complete, practical language
2. **Design Validator** - Compiler implementation stress-tests the language design
3. **Educational Tool** - The Cem-in-Cem compiler teaches advanced language usage
4. **Community Goal** - A rallying point for language maturity

## Why Cem is Well-Suited for Self-Hosting

### 1. Compilers are Pipelines

Concatenative languages excel at data transformation pipelines:

```cem
: compile ( String -- Result(ByteCode, Error) )
  tokenize          # String → Tokens
  parse             # Tokens → AST
  typecheck         # AST → TypedAST
  optimize          # TypedAST → OptimizedAST
  codegen ;         # OptimizedAST → ByteCode
```

The stack-based composition naturally expresses compiler passes.

### 2. Pattern Matching for AST Manipulation

```cem
: optimize-expr ( Expr -- Expr )
  match
    # Constant folding
    BinOp(Add, IntLit(a), IntLit(b)) => [ a b + IntLit ]

    # Identity elimination
    BinOp(Add, e, IntLit(0)) => [ e optimize-expr ]
    BinOp(Mul, e, IntLit(1)) => [ e optimize-expr ]

    # Dead code elimination
    If(BoolLit(true), then_branch, else_branch) => [
      else_branch drop
      then_branch optimize-expr
    ]

    # Recursively optimize children
    other => [ map-children optimize-expr ]
  end ;
```

Sum types and exhaustive pattern matching make AST transformations safe and clear.

### 3. Linear Types for Compiler Safety

```cem
type CompilerState =
  | Parsed(AST)
  | TypeChecked(TypedAST)
  | Optimized(OptimizedAST)
  | Compiled(ByteCode)

: typecheck ( CompilerState(Parsed) -- Result(CompilerState(TypeChecked), Error) )
  # Type system enforces that you can ONLY typecheck parsed programs
  # Original AST is consumed (linear) - no accidental reuse
  ;

: optimize ( CompilerState(TypeChecked) -- CompilerState(Optimized) )
  # Can ONLY optimize type-checked programs
  # Compiler enforces the correct pipeline order!
  ;
```

This is **type-state pattern as a first-class language feature**. The type system prevents:
- Running passes in wrong order
- Using stale intermediate representations
- Forgetting to run required passes

### 4. Effect System Documents Compiler Behavior

```cem
: parse-word-def ( Tokens -- Result(WordDef, ParseError) )
  # Stack effect clearly shows: consumes tokens, produces WordDef or error
  # Self-documenting!
  ;

: infer-effect ( WordDef Env -- Result((Effect, Env), TypeError) )
  # Takes WordDef and environment
  # Returns inferred effect AND updated environment
  # Or type error
  ;
```

Effect signatures serve as inline documentation of what each compiler pass does.

### 5. Quotations for Compiler Passes

```cem
: run-passes ( AST List([AST -- AST]) -- AST )
  # Apply a list of compiler passes
  [ call ] each ;

# Define optimization pipeline
: optimization-pipeline ( -- List([AST -- AST]) )
  [
    [ constant-fold ]
    [ dead-code-eliminate ]
    [ inline-small-functions ]
    [ common-subexpression-elimination ]
  ] ;

# Run it
: optimize ( AST -- AST )
  optimization-pipeline run-passes ;
```

First-class functions make compiler passes composable.

## The Bootstrap Path

### Stage 0: Foundation (Current)

**Rust Compiler (cem-rust)**
- Lexer, Parser, Type Checker
- LLVM code generation
- Full Cem language support
- Production quality, maintained

**Status:** ✅ In progress (Phase 1 complete, Phase 2 starting)

### Stage 1: Essential Infrastructure

**What's Needed:**
- ✅ Pattern matching (have it!)
- ✅ Sum types (have it!)
- ✅ Linear types (have it!)
- ⚠️ Standard library (starting now!)
- ⚠️ File I/O
- ⚠️ String manipulation
- ⚠️ Collections (List, Map, Set)
- ⚠️ FFI to LLVM or ability to emit LLVM IR as text

**Timeline:** Months 3-6 (alongside LLVM backend work)

### Stage 2: Proof of Concept

**Cem-in-Cem Subsystems**

Write individual compiler components in Cem:
```cem
# Example: Write the type checker in Cem
: typecheck-word ( WordDef Env -- Result((TypedWordDef, Env), TypeError) )
  # Full implementation in Cem
  ;
```

Use Rust compiler to compile these, then link with Rust-based driver.

**Hybrid Approach:**
```
Rust:  Lexer, Parser, LLVM interface, Driver
Cem:   Type checker, Optimizer, Transformations
```

**Benefits:**
- Validates language completeness for complex tasks
- Tests performance of Cem-compiled code
- Identifies missing stdlib features
- Community can contribute in Cem (not just Rust)

**Timeline:** Months 6-12

### Stage 3: Full Self-Hosting (Aspirational)

**Complete Cem Compiler in Cem**

```cem
# cem-compiler.cem - The complete Cem compiler

: main ( List(String) -- Int )
  # Parse command-line args
  # Read source file
  # Compile
  # Write output
  # Return exit code
  ;

: compile-file ( String -- Result(ByteCode, CompilerError) )
  read-file
  [ compile ] bind
  [ write-output ] bind ;

: compile ( String -- Result(ByteCode, CompilerError) )
  tokenize
  [ parse ] bind
  [ typecheck ] bind
  [ optimize ] bind
  [ codegen ] bind ;
```

**Bootstrap Process:**
```bash
# Step 1: Use Rust compiler to compile Cem compiler
$ cem-rust compile cem-compiler.cem → cem-v1

# Step 2: Use Cem-v1 to compile itself
$ cem-v1 compile cem-compiler.cem → cem-v2

# Step 3: Verify reproducibility (triple bootstrap)
$ cem-v2 compile cem-compiler.cem → cem-v3
$ diff cem-v2 cem-v3  # Should be byte-identical!

# If identical, cem-v2 is truly self-hosting!
```

**Timeline:** 1-2 years (not a priority, but a milestone)

## Practical Strategy

### Keep Rust as Production Compiler

**Why:**
- Performance: Rust compiler will always be faster
- Tooling: Leverage Rust ecosystem (profiling, debugging, etc.)
- Maintenance: Easier to maintain in mature language
- Trust: Production code should be in well-tested language

### Use Cem for Extensions

**Community contributions in Cem:**
- Linters (written in Cem!)
- Code formatters
- Additional optimizations
- Analysis tools
- IDE plugins

**Example:**
```cem
# cem-lint.cem - Linter written in Cem

: check-unused-words ( Program -- List(Warning) )
  defined-words
  used-words
  set-difference
  [ UnusedWord ] map ;

: check-code ( Program -- List(Warning) )
  [
    check-unused-words
    check-large-stack-effects
    check-missing-docs
  ] cleave
  concat ;
```

### Validate Language Design

**Self-hosting as a design tool:**

When designing new features, ask:
- "Would this make writing a compiler easier?"
- "Can we express compiler algorithms naturally?"
- "Does the type system help or hinder?"

If a feature makes self-hosting harder, reconsider the design.

## What Self-Hosting Teaches Us

### Language Completeness Checklist

A language capable of self-hosting must have:

- ✅ **Data structures:** AST representation, symbol tables, sets, maps
- ✅ **Algorithms:** Graph traversal, unification, type inference
- ✅ **I/O:** File reading, writing, command-line parsing
- ✅ **Error handling:** Recoverable errors with good messages
- ✅ **String processing:** Parsing, formatting, manipulation
- ✅ **Performance:** Fast enough for practical compilation
- ⚠️ **FFI:** Access to LLVM or ability to generate code

### Design Validation

If you can't write a compiler in your language, it's probably missing:
- Good abstractions for complex algorithms
- Efficient data structures
- Practical I/O
- Error handling
- Performance

Self-hosting forces you to eat your own dog food.

## Success Metrics

### Minimum Viable Self-Hosting

**Can compile a subset of itself:**
```cem
# Can compile simple Cem programs
: compile-simple ( SimpleCemProgram -- ByteCode )
  # No pattern matching, no user types
  # Just: words, stack ops, primitives
  ;
```

**Success:** Proves basic completeness

### Partial Self-Hosting

**Can compile most features:**
```cem
# Can compile everything except:
# - Advanced optimizations
# - Full LLVM backend (uses FFI)
```

**Success:** Validates 90% of language design

### Full Self-Hosting

**Can compile entire compiler:**
- Lexer, Parser, Type checker, Optimizer, Codegen
- All in pure Cem (except LLVM FFI)
- Triple bootstrap succeeds

**Success:** Language is truly complete

## Timeline & Priorities

### Near Term (Months 1-6)
- ❌ Don't prioritize self-hosting
- ✅ Focus on: LLVM backend, stdlib, tooling
- ✅ Keep it in mind when designing features

### Medium Term (Months 6-12)
- ✅ Write compiler subsystems in Cem (proof of concept)
- ✅ Identify missing stdlib features
- ✅ Performance testing of Cem-compiled code

### Long Term (1-2 years)
- ⚠️ Full self-hosting (if language proves successful)
- ⚠️ Community-driven (not core team priority)
- ⚠️ Educational resource

## Conclusion

**Self-hosting is a guiding light, not a GPS destination.**

It serves as:
- **Design validator:** Does the language have what it needs?
- **Completeness proof:** Can it handle complex, real programs?
- **Inspiration:** A worthy long-term goal
- **Teaching tool:** The ultimate example program

But it's not:
- ❌ Required for success
- ❌ The fastest compiler
- ❌ A near-term priority
- ❌ More important than stdlib, tooling, docs

**Strategy:**
1. Build production Rust compiler (fast, reliable)
2. Build comprehensive stdlib (enables everything)
3. Write parts of compiler in Cem (validation)
4. Eventually: full self-hosting (if language succeeds)

The journey matters more than the destination. Self-hosting will teach us what Cem needs to be a truly great language.

---

*"A language that can't compile itself is like a cookbook that can't teach you to cook." - But first, you need a kitchen (stdlib)!*
