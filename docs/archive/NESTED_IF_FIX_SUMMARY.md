# Nested If Expression Fix - Complete Summary

## Problem

Nested if expressions in the Cem language compiler were failing to compile with clang's `-O2` optimization, causing a **Bus error: 10** crash.

## Root Cause

The issue was **invalid LLVM IR generation**, specifically incorrect phi node predecessor labels. When a nested if expression was compiled:

1. The inner if created its own merge block (e.g., `merge_6`)
2. The inner merge block branched to the outer merge block (e.g., `merge_0`)
3. **The outer phi node incorrectly claimed values came from the branch start label (`then_0`) instead of the actual predecessor (`merge_6`)**

This violated LLVM's Control Flow Graph (CFG) requirements.

### Invalid IR Example

```llvm
then_0:
  ; ... inner if here ...
merge_6:
  %14 = phi ptr [ %12, %then_6 ], [ %13, %else_6 ]
  br label %merge_0        # merge_6 branches to merge_0

merge_0:
  %24 = phi ptr [ %14, %then_0 ], [ %23, %else_0 ]  # WRONG!
  ; Claims %14 comes from then_0, but it comes from merge_6!
```

## Why It Failed Mysteriously

- **With `-O0` (no optimization)**: LLVM's validator was lenient, compiled successfully
- **With `-O2` (optimization)**: The optimizer needs correct CFG, crashed when it hit invalid predecessors
- **Result**: Appeared to be a clang bug, but was actually our IR generation bug

## The Fix

Added tracking of the current basic block label during code generation:

### 1. Added State Tracking
```rust
pub struct CodeGen {
    output: String,
    temp_counter: usize,
    current_block: String,  // Track current basic block
}
```

### 2. Update Current Block on Label Emission
```rust
writeln!(&mut self.output, "{}:", then_label)?;
self.current_block = then_label.clone();
```

### 3. Capture Actual Predecessors
```rust
let (then_stack, then_is_musttail) = self.compile_branch_quotation(then_branch, &rest_var)?;
let then_predecessor = self.current_block.clone();  // Actual block that branches to merge
```

### 4. Use Correct Predecessors in Phi Nodes
```rust
writeln!(&mut self.output, "  %{} = phi ptr [ %{}, %{} ], [ %{}, %{} ]",
    result, then_stack, then_predecessor, else_stack, else_predecessor)?;
```

## Valid IR Result

```llvm
then_0:
  ; ... inner if here ...
merge_6:
  %14 = phi ptr [ %12, %then_6 ], [ %13, %else_6 ]
  br label %merge_0

merge_0:
  %24 = phi ptr [ %14, %merge_6 ], [ %23, %merge_15 ]  # CORRECT!
  ; Now correctly shows values come from merge_6 and merge_15
```

## Additional Fixes

While solving this, we also fixed:

### 1. Variable Name Collision
- **Problem**: Hardcoded `%cond` and `%rest` caused collisions in nested ifs
- **Fix**: Use `fresh_temp()` with sequential allocation

### 2. Musttail Return Requirement
- **Problem**: `musttail call` was followed by `br` instead of `ret`
- **Fix**: Detect tail calls and emit `ret` immediately after `musttail`

## Test Results

**Before**: 8 tests passing, 1 ignored (nested_if)
**After**: **9 tests passing, 0 ignored** ✅

All nested if expressions now compile correctly with full optimization enabled!

## Lessons Learned

1. **Don't assume it's a toolchain bug** - Even famous tools like LLVM/clang can expose bugs in our code
2. **Phi nodes are strict** - Predecessor labels must exactly match the CFG
3. **Optimizers assume valid IR** - Invalid IR may "work" without optimization but crash with it
4. **Track context carefully** - In code generation, knowing "where you are" is as important as "what you're generating"

## Files Modified

- `src/codegen/mod.rs` - Added `current_block` tracking, fixed phi node generation
- `tests/integration_test.rs` - Enabled `test_nested_if_expressions`
- `docs/KNOWN_ISSUES.md` - Documented the fix
- `docs/NESTED_IF_CLANG_CRASH.md` - Retained as investigation history

## Key Takeaway

The "clang optimizer bug" was actually **invalid IR generation on our part**. By properly tracking the current basic block and using correct predecessor labels in phi nodes, nested if expressions now work perfectly with all optimization levels.
