# Known Issues

## Nested If Expression Variable Collision

**Status**: Open
**Priority**: High
**Affects**: Code generation for nested if expressions

### Description

Currently, nested if expressions will fail to compile due to variable name collisions in the generated LLVM IR. The `compile_expr` function for `Expr::If` uses hardcoded variable names `%cond` and `%rest`, which collide when if expressions are nested.

### Example

```cem
: nested_if ( Bool Bool -- Int )
  if
    [ if [ 1 ] [ 2 ] ]  # Inner if redefines %cond and %rest
    [ if [ 3 ] [ 4 ] ]
  ;
```

### Current Workaround

Avoid nesting if expressions. Use multiple words instead:

```cem
: inner_true ( Bool -- Int )
  if [ 1 ] [ 2 ] ;

: inner_false ( Bool -- Int )
  if [ 3 ] [ 4 ] ;

: nested_if ( Bool Bool -- Int )
  if [ inner_true ] [ inner_false ] ;
```

### Attempted Fix

The obvious fix is to use `fresh_temp()` for these variables:

```rust
let cond_var = self.fresh_temp();
let rest_var = self.fresh_temp();
```

However, this causes LLVM IR numbering errors like:
```
error: instruction expected to be numbered '%5' or greater
```

### Root Cause

The issue appears to be related to how `temp_counter` interacts with:
1. Label generation (which increments `temp_counter`)
2. Temporary variable generation via `fresh_temp()`
3. Named parameters (`ptr %stack`)
4. Nested compilation contexts

When we use `fresh_temp()` for `cond` and `rest`, and then pass `rest_var` to `compile_branch_quotation`, something in the numbering sequence gets disrupted, causing gaps or reuse of temp variable numbers.

### Investigation Needed

1. Trace through the exact temp_counter increments during nested if compilation
2. Understand why hardcoded "cond"/"rest" names work but numbered temps don't
3. Consider whether we need separate counters for labels vs temps
4. Investigate if LLVM requires contiguous numbering (0, 1, 2, ...) or allows gaps
5. Check if named temps (like %cond) vs numbered temps (%5) behave differently

### Test Case

See `test_nested_if_expressions` in `tests/integration_test.rs` (currently marked `#[ignore]`).

### References

- PR #7 review feedback
- `src/codegen/mod.rs:405-438` - If expression codegen
- `src/codegen/mod.rs:58-62` - `fresh_temp()` implementation
- `src/codegen/mod.rs:238` - `temp_counter` reset per word

### Next Steps

1. Create a minimal test case to understand the numbering issue
2. Review LLVM IR specification for temp variable numbering rules
3. Consider whether we need a different approach to variable naming in nested contexts
4. Once fixed, enable `test_nested_if_expressions`
