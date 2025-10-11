# Known Issues

Currently there are no known issues! 🎉

---

## Recently Fixed Issues

### Nested If Variable Collision (FIXED ✅)

**Status**: Resolved
**Fixed in**: Current version

Previously, nested if expressions caused variable name collisions because hardcoded `%cond` and `%rest` variables were reused. This has been fixed by:

1. Using `fresh_temp()` to generate unique numbered temporaries
2. Ensuring sequential allocation to avoid LLVM IR numbering gaps
3. Allocating temp variables immediately before use

The fix ensures all temporary variables have unique names across nested contexts.

### Musttail Followed by Branch (FIXED ✅)

**Status**: Resolved
**Fixed in**: Current version

Previously, tail calls in if branches were incorrectly followed by a branch to merge:

```llvm
%7 = musttail call ptr @foo(ptr %6)
br label %merge_0   # WRONG - musttail must be followed by ret
```

This has been fixed. Now `musttail` calls are properly followed by immediate `ret`:

```llvm
%7 = musttail call ptr @foo(ptr %6)
ret ptr %7   # CORRECT
```

The fix detects when branches end with tail calls and emits the appropriate control flow.

### Incorrect Phi Node Predecessors in Nested Ifs (FIXED ✅)

**Status**: Resolved
**Fixed in**: Current version

**The Root Cause**: When nested if expressions were compiled, the outer if's phi node used incorrect predecessor labels. For example:

```llvm
merge_6:
  %14 = phi ptr [ %12, %then_6 ], [ %13, %else_6 ]
  br label %merge_0
merge_0:
  %24 = phi ptr [ %14, %then_0 ], [ %23, %else_0 ]  # WRONG!
```

The phi node claimed `%14` came from `%then_0`, but it actually came from `%merge_6` (the inner if's merge block). This violated LLVM IR's Control Flow Graph (CFG) requirements.

**Why It Failed**:
- Without optimization (`-O0`), LLVM didn't strictly validate phi predecessors
- With optimization (`-O2`), the optimizer relied on correct CFG and crashed (Bus error: 10)
- This was **not** a clang bug - it was invalid IR generation!

**The Fix**: Track the current basic block label during code generation using `current_block` field. When emitting phi nodes, use the actual predecessor block:

```llvm
merge_0:
  %24 = phi ptr [ %14, %merge_6 ], [ %23, %merge_15 ]  # CORRECT!
```

Now nested if expressions compile correctly with full optimization enabled.

**Implementation**:
- Added `current_block: String` field to `CodeGen` struct
- Update `current_block` whenever a new block label is emitted
- Capture predecessor block before branching to merge
- Use actual predecessors in phi node generation

See `src/codegen/mod.rs:46, 445-507` for details.
