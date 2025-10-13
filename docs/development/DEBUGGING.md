# Debugging Cem Programs

## Current State (Phase 1)

### What Works Today

**1. Runtime Debugging with LLDB/GDB**

Standard debuggers work on compiled Cem binaries:

```bash
# Compile with debug symbols
clang -g program.ll runtime/libcem_runtime.a -o program

# Debug with lldb (macOS)
lldb program
(lldb) breakpoint set -n main
(lldb) run
(lldb) step
(lldb) bt  # backtrace

# Or gdb (Linux)
gdb program
(gdb) break main
(gdb) run
(gdb) step
```

**What you can debug**:
- ✅ Step through C runtime code (stack.c, scheduler.c)
- ✅ Inspect C stack frames and variables
- ✅ Set breakpoints in runtime functions (e.g., `add`, `dup`, `string_concat`)
- ✅ View memory, registers, assembly
- ✅ Catch crashes and segfaults

**Limitations**:
- ❌ No source line mapping (shows IR/assembly, not Cem source)
- ❌ Can't easily inspect "the stack" (our StackCell linked list)
- ❌ Variable names are temps like `%0`, `%1`, `%2`
- ❌ No Cem-specific pretty printing

### What We Have: IR Inspection

You can inspect generated LLVM IR to understand compilation:

```bash
# IR is saved as .ll files during compilation
cat program.ll

# Example output:
define ptr @double(ptr %stack) {
entry:
  %0 = call ptr @dup(ptr %stack)
  %1 = call ptr @add(ptr %0)
  ret ptr %1
}
```

**Useful for**:
- Understanding how Cem compiles to LLVM
- Verifying tail call optimization (musttail)
- Debugging codegen issues
- Learning LLVM IR

## Phase 2: LLVM Debug Metadata

**Goal**: Map LLVM IR back to Cem source lines

**Implementation Plan**: See [../archive/DEBUG_METADATA_PLAN.md](../archive/DEBUG_METADATA_PLAN.md) for detailed task breakdown (~3.5 days of work)

### What This Enables

With debug metadata, LLDB/GDB would show:

```
(lldb) step
-> 42: : double ( Int -- Int ) dup + ;
                                 ^^^
(lldb) print stack
(StackCell*) $0 = 0x... { tag=TAG_INT, value.i=10, next=... }
```

### Implementation Plan

**1. Add source locations to AST**:
```rust
// Current
pub enum Expr {
    IntLit(i64),
    // ...
}

// With locations
pub enum Expr {
    IntLit(i64, SourceLoc),  // Line/column info
    // ...
}

pub struct SourceLoc {
    line: usize,
    column: usize,
    file: String,
}
```

**2. Emit LLVM debug metadata**:
```llvm
!0 = !DIFile(filename: "program.cem", directory: "/path/to")
!1 = !DISubprogram(name: "double", file: !0, line: 42, ...)
!2 = !DILocation(line: 42, column: 35, scope: !1)

define ptr @double(ptr %stack) !dbg !1 {
entry:
  %0 = call ptr @dup(ptr %stack), !dbg !2
  %1 = call ptr @add(ptr %0), !dbg !3
  ret ptr %1
}
```

**3. Benefits**:
- ✅ Step through Cem source code line by line
- ✅ Source-level breakpoints: `break program.cem:42`
- ✅ Better error messages showing source location
- ✅ IDE integration (VS Code, vim, emacs)

**Effort**: ~2-3 days of work
**Priority**: Medium (nice to have, not critical for Phase 1)

## Phase 3: Cem-Aware Debugger

**Goal**: Custom debugger that understands Cem's execution model

### Why a Custom Debugger?

Cem has unique execution semantics that LLDB/GDB don't understand:
- **Stack machine**: Data lives on a linked-list stack, not C stack
- **Stack effects**: Functions have type `( A B -- C D )`
- **Quotations**: First-class code blocks
- **Strands**: Green threads with their own stacks

A Cem debugger would visualize these directly.

### Features

**1. Stack Visualization**
```
(cem-db) stack
Stack (top to bottom):
  [0] Int: 10
  [1] String: "hello"
  [2] Bool: true
(cem-db)
```

**2. Stack Effect Tracing**
```
(cem-db) trace
1. push 5           ( -- 5 )
2. push 10          ( 5 -- 5 10 )
3. add              ( 5 10 -- 15 )
4. dup              ( 15 -- 15 15 )
```

**3. Quotation Inspection**
```
(cem-db) print quot_5
Quotation at @quot_5:
  [ 5 10 add ]
  Effect: ( -- Int )
```

**4. Strand Debugging**
```
(cem-db) strands
Strand 1 (RUNNING):  main
Strand 2 (READY):    background_task
Strand 3 (BLOCKED):  waiting_on_io

(cem-db) strand 2
Switched to strand 2
(cem-db) stack
Stack: [Int: 42, String: "data"]
```

**5. Time-Travel Debugging** (future)
- Record stack history
- Step backwards through execution
- Replay specific executions

### Implementation Approaches

**Option 1: LLDB Python Bindings** (Easiest)
```python
# lldb_cem.py
import lldb

def stack_command(debugger, command, result, internal_dict):
    """Print the Cem data stack"""
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    thread = process.GetSelectedThread()

    # Find stack variable
    frame = thread.GetFrameAtIndex(0)
    stack = frame.FindVariable("stack")

    # Walk StackCell linked list
    cell = stack
    index = 0
    while cell:
        tag = cell.GetChildMemberWithName("tag").GetValue()
        if tag == "TAG_INT":
            value = cell.GetChildMemberWithName("value").GetChildMemberWithName("i")
            print(f"[{index}] Int: {value.GetValue()}")
        # ... handle other types
        cell = cell.GetChildMemberWithName("next")
        index += 1

# Register commands
def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand('command script add -f lldb_cem.stack_command stack')
```

**Usage**:
```bash
lldb program
(lldb) command script import lldb_cem.py
(lldb) stack  # Custom command!
```

**Option 2: DAP (Debug Adapter Protocol)** (Best for IDEs)
- Implement DAP server in Rust
- Works with VS Code, vim, emacs, etc.
- Full control over debugging experience
- Can integrate with Cem compiler directly

**Option 3: Standalone Debugger** (Most Control)
- Custom debugger written in Rust
- Uses ptrace (Linux) or mach (macOS) APIs
- Complete control over UX
- Most work, best experience

### Recommended Path

**Phase 2** (Short term - ~1 week):
1. Add source locations to AST
2. Emit LLVM debug metadata
3. Test with LLDB/GDB - get source-level debugging

**Phase 3** (Medium term - ~1 month):
1. LLDB Python pretty-printers for StackCell
2. Custom `stack` command (like Option 1 above)
3. Document debugging workflow

**Phase 4** (Long term - ~3 months):
1. DAP server implementation
2. VS Code extension
3. Full Cem-aware debugging experience

## Debugging Strategies Today

### 1. Print Debugging

Add `print_stack` calls in your Cem code:

```cem
: debug-test ( Int Int -- Int )
  # (implicit call to print_stack for debugging)
  dup   # Stack: a b b
  +     # Stack: a (a+b)
  ;
```

The runtime's `print_stack` function (stack.c:63) already works.

### 2. Inspect Generated IR

Look at the `.ll` file to understand compilation:

```bash
cat program.ll | less
# Search for your word definition
/define ptr @my_word
```

### 3. Step Through Runtime

Set breakpoints in C runtime functions:

```bash
lldb program
(lldb) breakpoint set -n add
(lldb) run
# When it breaks, inspect stack
(lldb) frame variable stack
(lldb) p *stack  # Dereference to see StackCell
```

### 4. Valgrind for Memory Issues

Check for leaks and memory corruption:

```bash
valgrind --leak-check=full ./program
```

## References

- **LLVM Debug Info**: https://llvm.org/docs/SourceLevelDebugging.html
- **DAP Specification**: https://microsoft.github.io/debug-adapter-protocol/
- **LLDB Python Scripting**: https://lldb.llvm.org/use/python-reference.html
- **GDB Pretty Printers**: https://sourceware.org/gdb/current/onlinedocs/gdb/Pretty-Printing.html

## Decision Log

**Current Status**: Phase 1 - Basic LLDB/GDB support
**Next Step**: Add source locations to AST (Phase 2)
**Long-term Goal**: Full DAP server with IDE integration (Phase 4)

---

**Last Updated**: 2025-01-11
**Status**: Design document - implementation pending
