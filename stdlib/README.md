# Cem Standard Library

*Pronounced "seam"*

The standard library for the Cem programming language.

## Organization

```
stdlib/
  core.cem          # Essential combinators (always available)
  prelude.cem       # Auto-imported basics (Option, Result, List utilities)

  data/
    option.cem      # Option<T> utilities
    result.cem      # Result<T,E> utilities
    list.cem        # List operations (map, filter, fold, etc.)
    map.cem         # HashMap<K,V>
    set.cem         # HashSet<T>

  string/
    string.cem      # String manipulation
    format.cem      # String formatting

  io/
    io.cem          # Basic I/O (print, read)
    file.cem        # File operations

  concurrency/
    channel.cem     # CSP channels
    process.cem     # Process spawning

  math/
    int.cem         # Integer operations

  compiler/
    ast.cem         # AST types (for metaprogramming)
```

## Import System

### Auto-imported (Prelude)

Always available without explicit import:
- Core combinators: `dup`, `drop`, `swap`, `over`, `rot`
- Essential combinators: `dip`, `keep`, `bi`, `tri`
- Option utilities: `Some`, `None`, `unwrap-or`, `map-option`
- Result utilities: `Ok`, `Err`, `is-ok`, `is-err`
- Basic List operations: `map`, `filter`, `fold`

### Explicit Import

```cem
# Import specific module
import io.file

# Now can use:
: read-config ( -- Result(String, IOError) )
  "config.txt" read-file ;
```

### Implementation Status

Legend:
- ✅ Implemented
- 🚧 In progress
- ⚠️ Planned (needs FFI)
- ❌ Future work

Current status: 🚧 Starting implementation

## Design Principles

### 1. Stack-First Design

Every function should feel natural in stack-based composition:

```cem
# Good: Natural composition
: process-user ( User -- Result(ProcessedUser, Error) )
  validate
  [ normalize ] map-ok
  [ enrich ] bind
  [ save ] bind ;

# Avoid: Awkward stack shuffling
```

### 2. Polymorphic Where Possible

Use type variables to maximize reusability:

```cem
: map ( List(A) [A -- B] -- List(B) )  # Works for any A, B
  # Not: map-int, map-string, map-user...
```

### 3. Linear Types for Safety

Use linear types to enforce resource cleanup:

```cem
: with-file ( String [File -- A] -- Result(A, IOError) )
  # File is consumed by quotation
  # Guaranteed to be closed
  ;
```

### 4. Consistent Naming

- Predicates end in `?`: `is-some`, `is-empty`, `is-positive`
- Converters use `to-`: `to-string`, `to-int`, `to-list`
- Destructive operations use `!`: `sort!`, `reverse!` (when we have mutation)
- Maps/filters use full names: `map`, `filter`, not `m`, `f`

### 5. Effect Signatures as Documentation

Stack effects should tell the story:

```cem
: parse-config ( String -- Result(Config, ParseError) )
  # Clear: takes string, returns parsed config or error
```

## Testing

Each stdlib module should have comprehensive tests:

```
stdlib/
  data/
    option.cem
    option_test.cem      # Tests for option.cem
    result.cem
    result_test.cem      # Tests for result.cem
```

## Performance Notes

Some stdlib functions will be:
- **Pure Cem:** Fast, portable, easy to understand
- **FFI to Rust:** Maximum performance for hot paths
- **Compiler intrinsics:** Optimized by compiler

Example:
```cem
# Pure Cem version (easy to understand)
: length ( List(A) -- Int )
  0 swap [ drop 1 + ] fold ;

# Compiler intrinsic (optimized)
: length ( List(A) -- Int )
  __builtin_list_length ;
```

## Contributing

When adding stdlib functions, ensure:

1. ✅ Clear stack effect signature
2. ✅ Polymorphic where sensible
3. ✅ Tests included
4. ✅ Documentation with examples
5. ✅ Consistent naming
6. ✅ Natural composition

## Examples

See `examples/` directory for complete programs using stdlib.
