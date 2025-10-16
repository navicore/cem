# x86-64 Context Switching Crash - Debug Guide

**Status**: ✅ RESOLVED - Emergency stack growth now working!
**Platform**: x86-64 Linux
**Date Reported**: 2025-10-15
**Date Resolved**: 2025-10-15

## Resolution Summary

**Root Cause**: The SIGSEGV handler was installed correctly, but when stack overflow occurred, the handler tried to run on the SAME corrupt stack, causing it to fail immediately without executing any code.

**Fix**: Implemented alternate signal stack using `sigaltstack()` and `SA_ONSTACK` flag. This gives the signal handler its own dedicated 8KB stack, independent of the thread's main stack.

**Additional Fix**: When emergency growth succeeds, the signal handler now updates the CPU's RSP register (via ucontext) to point to the new stack location before returning.

**Impact**:
- ✅ Context switching works correctly on x86-64 Linux
- ✅ SIGSEGV handler is now called when guard page is hit
- ✅ Emergency stack growth works (tested with 5KB allocation on 4KB stack)
- ✅ CPU registers are updated to point to new stack after growth

**Key Changes**:
1. `runtime/stack_mgmt.c`: Added `sigaltstack()` setup in `stack_guard_init_signal_handler()`
2. `runtime/stack_mgmt.c`: Added CPU register updates (RSP/RBP) in SIGSEGV handler after growth
3. `runtime/stack.h`: Renamed `dup()` to `stack_dup()` to avoid conflict with POSIX `dup()`
4. `runtime/context.h`: Keep initial stack at 4KB (dynamic growth works!)

---

## Problem Summary

The `cem_swapcontext` assembly function crashes when attempting to switch to a newly created strand context on x86-64 Linux. The crash occurs BEFORE the strand's trampoline function executes.

### Evidence
- ✅ Stack allocation works (Test 1 in test_stack_growth passes)
- ✅ Scheduler initialization works
- ✅ makecontext sets up the context structure
- ❌ cem_swapcontext crashes during the switch
- ❌ Never prints "Returned from swapcontext"
- ❌ Never sets debug_step variable in trampoline
- ❌ Segfault (signal 11)

### Current Debug Output
```
[MAKECONTEXT] stack_base=0x7f9521199000, stack_size=4096, stack_top=0x7f952119a000
[MAKECONTEXT] func=0x401e70
[MAKECONTEXT] Pushed func to stack at 0x7f9521199f70, rsp will be 0x7f9521199f70
[SCHED] About to check stack growth
[SCHED] Stack check complete, about to swapcontext
error: Recipe `test-basic-strand` was terminated on line 243 by signal 11
```

## Debugging Steps

Follow these steps IN ORDER. Report results after each step before proceeding.

---

## Step 1: Get Exact Crash Location (CRITICAL - DO THIS FIRST)

Run GDB to see exactly which assembly instruction crashes:

```bash
cd ~/git/navicore/cem
gdb tests/test_basic_strand
```

In GDB:
```gdb
(gdb) run
# Wait for crash
(gdb) backtrace
(gdb) info registers
(gdb) x/20x $rsp
(gdb) disassemble
(gdb) print/x $rip
```

**What to capture:**
- Complete backtrace
- Value of rsp, rip, rbp, rsi, rdi
- What's at address $rsp (the stack contents)
- Which instruction caused the segfault

**Save output to**: `DEBUG_STEP1_GDB_OUTPUT.txt`

---

## Step 2: Simplify Assembly to Isolate Problem

Edit `runtime/context_x86_64.s` and replace the ENTIRE `cem_swapcontext` function with this minimal version:

```asm
.globl cem_swapcontext
.type cem_swapcontext, @function
cem_swapcontext:
    # Minimal test - just return immediately without doing anything
    ret

.size cem_swapcontext, .-cem_swapcontext
```

Run test:
```bash
just test-basic-strand
```

**Expected result**: Should print "Returned from swapcontext" and then fail gracefully (because we didn't actually switch contexts).

If this PASSES (doesn't segfault), the problem is in the register save/restore logic.

**Record result**: PASS or FAIL

---

## Step 3: Test Stack Pointer Restoration

If Step 2 passes, try this version that ONLY touches rsp:

```asm
.globl cem_swapcontext
.type cem_swapcontext, @function
cem_swapcontext:
    # Save current rsp to save_ctx (rdi)
    mov     %rsp, 0x30(%rdi)

    # Restore rsp from restore_ctx (rsi)
    mov     0x30(%rsi), %rsp

    # Return
    ret

.size cem_swapcontext, .-cem_swapcontext
```

Run test:
```bash
just test-basic-strand
```

**Expected**: If this crashes, the problem is with the rsp value or stack setup.

**Record result**: PASS or FAIL

---

## Step 4: Verify Stack Memory is Accessible

Add this to `runtime/context.c` after line 102 (after writing func to stack):

```c
  // === DEBUG: Verify stack memory is accessible ===
  fprintf(stderr, "[MAKECONTEXT] Testing stack memory access...\n");
  volatile char *test_ptr = (volatile char *)stack_base;
  for (size_t i = 0; i < stack_size; i += 4096) {
    test_ptr[i] = 0x42;  // Write test
    if (test_ptr[i] != 0x42) {
      fprintf(stderr, "[MAKECONTEXT] ERROR: Stack at offset %zu not accessible!\n", i);
      abort();
    }
  }
  // Test the exact location where we put the function pointer
  volatile void **func_ptr_test = (volatile void **)stack_top;
  void *read_back = *func_ptr_test;
  fprintf(stderr, "[MAKECONTEXT] Wrote func=%p, read back=%p\n", (void*)func, read_back);
  if (read_back != (void*)func) {
    fprintf(stderr, "[MAKECONTEXT] ERROR: Stack write/read verification FAILED!\n");
    abort();
  }
  fprintf(stderr, "[MAKECONTEXT] Stack memory test PASSED\n");
  fflush(stderr);
  // === END DEBUG ===
```

Run test:
```bash
just test-basic-strand
```

**Expected**: Should print "Stack memory test PASSED"

If it fails, the stack allocation is returning invalid memory.

**Record result**: PASS or FAIL + output

---

## Step 5: Verify RSP is Within Stack Bounds

Add this to `runtime/scheduler.c` BEFORE line 680 (before cem_swapcontext call):

```c
      // === DEBUG: Verify context values ===
      fprintf(stderr, "[SCHED] strand->context.rsp = 0x%lx\n",
              (unsigned long)strand->context.rsp);
      fprintf(stderr, "[SCHED] strand->context.rbp = 0x%lx\n",
              (unsigned long)strand->context.rbp);
      fprintf(stderr, "[SCHED] stack range: 0x%lx to 0x%lx (size=%zu)\n",
              (unsigned long)strand->stack_meta->usable_base,
              (unsigned long)((char*)strand->stack_meta->usable_base +
                              strand->stack_meta->usable_size),
              strand->stack_meta->usable_size);

      // Verify rsp is within stack bounds
      uintptr_t rsp_val = strand->context.rsp;
      uintptr_t stack_start = (uintptr_t)strand->stack_meta->usable_base;
      uintptr_t stack_end = stack_start + strand->stack_meta->usable_size;

      if (rsp_val < stack_start || rsp_val > stack_end) {
        fprintf(stderr, "[SCHED] ERROR: rsp (0x%lx) is OUTSIDE stack bounds!\n",
                (unsigned long)rsp_val);
        fprintf(stderr, "[SCHED]   Stack: 0x%lx - 0x%lx\n",
                (unsigned long)stack_start, (unsigned long)stack_end);
        abort();
      }

      // Check what's at the rsp address
      void **rsp_ptr = (void **)rsp_val;
      fprintf(stderr, "[SCHED] Value at [rsp] = %p (should be func ptr)\n", *rsp_ptr);
      fprintf(stderr, "[SCHED] Expected func ptr = 0x401e70\n");
      fflush(stderr);
      // === END DEBUG ===
```

Run test:
```bash
just test-basic-strand
```

**Expected**:
- rsp should be within stack bounds
- Value at [rsp] should match the function pointer (0x401e70)

**Record result**: PASS or FAIL + all printed values

---

## Step 6: Test with Dummy Function

Create a minimal function that doesn't do anything complex. Add to `runtime/scheduler.c` after line 274:

```c
// === DEBUG: Minimal test function ===
static void __attribute__((noinline)) minimal_test_func(void) {
  debug_step = 100;  // We made it into the function!
  asm volatile("nop");
  // Infinite loop so we never return (avoiding return address issues)
  while(1) {
    asm volatile("pause");
  }
}
// === END DEBUG ===
```

Then in `strand_spawn`, change line 336 to use this instead:

```c
  cem_makecontext(
      &strand->context, strand->stack_meta->usable_base,
      strand->stack_meta->usable_size, minimal_test_func,  // <-- CHANGED
      NULL);
```

Run test (you'll need to Ctrl+C to kill it):
```bash
timeout 2 just test-basic-strand
echo "Exit code: $?"
```

Check if debug_step got set:
- If the test hangs and you have to kill it, check if "debug_step=100" appears
- Exit code 124 = timeout (GOOD - means it entered the function)
- Exit code 139 = segfault (BAD - still crashing)

**Record result**: Timeout (entered function) or Segfault

---

## Step 7: Build with No Optimization

Edit `justfile` line 172 and change `-O2` to `-O0`:

```makefile
cd runtime && clang -Wall -Wextra -std=c11 -g -O0 -fno-omit-frame-pointer -c context.c -o context.o
```

Change line 173 similarly:
```makefile
cd runtime && clang -Wall -Wextra -std=c11 -g -O0 -fno-omit-frame-pointer -c scheduler.c -o scheduler.o
```

Rebuild and test:
```bash
just clean
just test-basic-strand
```

**Expected**: Same crash, but might give better debugging info.

**Record result**: Any difference in behavior?

---

## Step 8: Compare with System getcontext/setcontext

Create a test using libc's context switching to prove the concept works:

```bash
cat > /tmp/test_libc_context.c << 'EOF'
#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>

static volatile int reached_func = 0;

void test_func(void) {
    printf("SUCCESS: Entered test_func via setcontext!\n");
    reached_func = 1;
    exit(0);
}

int main(void) {
    ucontext_t ctx;
    char stack[8192];

    printf("Testing libc getcontext/makecontext/setcontext...\n");

    if (getcontext(&ctx) == -1) {
        perror("getcontext");
        return 1;
    }

    ctx.uc_stack.ss_sp = stack;
    ctx.uc_stack.ss_size = sizeof(stack);
    ctx.uc_link = NULL;

    makecontext(&ctx, test_func, 0);

    printf("About to setcontext...\n");
    setcontext(&ctx);

    printf("ERROR: Should never reach here!\n");
    return 1;
}
EOF

gcc -o /tmp/test_libc_context /tmp/test_libc_context.c
/tmp/test_libc_context
```

**Expected**: Should print "SUCCESS: Entered test_func"

If libc's version works but ours doesn't, compare the implementations.

**Record result**: PASS or FAIL

---

## Reporting Results

After completing each step, report:

1. **Step number**
2. **Result**: PASS / FAIL / HANG / CRASH
3. **Complete output** (especially any error messages)
4. **Unexpected values** (if any)

Once we identify which step fails, we'll know exactly where the bug is and can fix it precisely.

---

## Quick Reference - Files to Edit

- `runtime/context_x86_64.s` - Assembly code (Steps 2, 3)
- `runtime/context.c` - Context setup (Step 4)
- `runtime/scheduler.c` - Scheduler and trampoline (Steps 5, 6)
- `justfile` - Build settings (Step 7)

---

## Common Issues and Solutions

### If Step 1 shows crash in `ldmxcsr`
- MXCSR register handling is broken
- Solution: Remove MXCSR save/restore (already done)

### If Step 3 passes but Step 4 fails
- Stack memory is not accessible
- Solution: Check mmap permissions in stack_alloc

### If Step 5 shows rsp outside bounds
- makecontext math is wrong
- Solution: Fix stack_top calculation

### If Step 6 works (hangs) but real trampoline crashes
- Trampoline function itself has issues
- Solution: Simplify trampoline, remove fprintf calls

### If nothing works
- May need to look at how scheduler_context is initialized
- May need to compare with ARM64 working implementation
