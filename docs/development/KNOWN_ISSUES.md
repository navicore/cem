# Known Issues

## x86-64 Dynamic Stack Growth (CRITICAL ⚠️)

**Status**: Known limitation
**Affects**: x86-64 Linux, x86-64 macOS (when implemented)
**Impact**: Segmentation faults when stack growth is triggered with active call frames
**Workaround**: Increase initial stack size or avoid deep recursion on x86-64

### The Problem

The x86-64 implementation of dynamic stack growth (`runtime/stack_mgmt.c:426-461`) is **incomplete** and will crash under certain conditions:

**Root Cause**: On x86-64, function return addresses are stored on the stack (unlike ARM64 where they're in the x30/LR register). When `stack_grow()` copies the stack to a new memory location, it doesn't adjust these return addresses. When a function tries to return, it jumps to the old (now invalid) stack address → **SEGFAULT**.

**When It Crashes**:
- When stack growth occurs while functions have active stack frames (return addresses on stack)
- Deep call stacks or recursive functions
- Tests that explicitly trigger stack growth (e.g., `test_stack_growth.c`)

**When It Works**:
- Simple strands with no deep call stacks
- When checkpoint-based growth catches the issue before return addresses are pushed
- Strands that complete before needing to grow

### Why Tests Fail in CI But Not Locally

CI runs the comprehensive `test-stack-growth` test which deliberately triggers stack growth with active call frames, exposing this bug. Local testing with `just ci` may skip this test or have different memory layouts that avoid the crash.

### Technical Details

From `runtime/stack_mgmt.c:426-446`:

```c
#elif defined(CEM_ARCH_X86_64)
  // x86-64 IMPLEMENTATION INCOMPLETE - Return address adjustment not yet implemented
  //
  // CRITICAL LIMITATION: On x86-64, return addresses are stored ON THE STACK
  // (not in registers like ARM64's x30). When we memcpy the stack to a new
  // location, these return addresses become invalid and will crash when
  // functions try to return.
  //
  // REQUIRED FOR FULL x86-64 SUPPORT:
  // 1. Walk the stack frame chain using rbp (frame pointer)
  // 2. For each frame, adjust the return address by (new_stack_top - old_stack_top)
  // 3. Handle cases where rbp chain is broken (optimized code, leaf functions)
```

### Workarounds

**For Testing**:
- Use `just test-runtime-x86-safe` instead of `just test-all-runtime` (skips stack growth tests)
- Run `just ci` which uses safe tests on x86-64

**For Production**:
- Increase `CEM_INITIAL_STACK_SIZE` to prevent growth (temporary solution)
- Avoid deep recursion or large local variables on x86-64
- Use ARM64 platforms where stack growth works correctly

### Proper Fix (TODO)

To fully fix this, we need to implement return address adjustment during stack copying:

1. Walk the stack frame chain using rbp (frame pointer)
2. For each frame:
   - Locate the return address (typically at `rbp + 8`)
   - Adjust it by the stack relocation offset: `new_stack_top - old_stack_top`
3. Handle edge cases:
   - Leaf functions (no frame pointer)
   - Optimized code (`-fomit-frame-pointer`)
   - Signal frames
   - Incomplete frame chains

**Reference Implementation**: Go runtime's `adjustframe()` function

**Estimated Effort**: 1-2 days of development + thorough testing

**Priority**: High - This blocks reliable x86-64 support

### Related Files

- `runtime/stack_mgmt.c:426-461` - Incomplete x86-64 stack growth
- `runtime/context_x86_64.s` - Context switching (works correctly)
- `tests/test_stack_growth.c` - Test that exposes the bug
- See also: GitHub Issue #27

---

## Design Decisions & Trade-offs

### Target Triple Warning Suppression

**Decision**: We suppress clang's `-Woverride-module` warning and omit target triple from generated LLVM IR.

**Rationale**: Clang always emits "overriding the module target triple" warning when compiling LLVM IR, regardless of whether we specify a target triple or not:
- With explicit triple: `clang -dumpmachine` returns `arm64-apple-darwin25.0.0` but clang wants `arm64-apple-macosx26.0.0`
- Without explicit triple: Clang treats it as empty and warns when overriding

**Trade-off**: Suppressing this warning means we won't see legitimate target mismatch warnings if they occur. However, we consider this acceptable because:
1. We're compiling for the host platform (same machine running the compiler)
2. Letting clang use its default target is the safest approach
3. The warning was cosmetic noise in 100% of real compilation scenarios
4. Any actual target incompatibility would fail at link time anyway

**Implementation**: See `src/codegen/linker.rs` for `-Wno-override-module` flag usage.

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
