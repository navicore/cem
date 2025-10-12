# PR #20 Review Updates

## Response to Review Feedback

Thank you for the thorough review! Addressed the "should fix soon" recommendations:

### ✅ Implemented

#### 1. Add errno to Error Messages
**Issue:** Error messages didn't include errno details for debugging

**Fix:** Added `perror()` calls before `runtime_error()` for all I/O operations:
- `scheduler_init`: epoll_create1 failures
- `strand_block_on_read`: epoll_ctl failures  
- `strand_block_on_write`: epoll_ctl failures
- `scheduler_run`: epoll_wait/kevent failures (both platforms)

**Example:**
```c
// Before
if (epoll_ctl(...) == -1) {
    runtime_error("strand_block_on_read: epoll_ctl failed");
}

// After
if (epoll_ctl(...) == -1) {
    perror("strand_block_on_read: epoll_ctl failed");
    runtime_error("strand_block_on_read: Failed to register fd for read events");
}
```

**Benefit:** Developers now see errno values like `EBADF`, `EEXIST`, `ENOMEM` for faster debugging.

#### 2. Named Constants for Magic Numbers
**Issue:** Hard-coded `32` for event batch size lacked documentation

**Fix:** Added `MAX_IO_EVENTS` constant with documentation:
```c
// Maximum number of I/O events to process per event loop iteration
// Larger values process more events per syscall but increase latency
#define MAX_IO_EVENTS 32
```

Used consistently for both kqueue and epoll event arrays.

**Benefit:** Single place to tune batch size, clear performance trade-off documented.

#### 3. Update README.md Platform Support
**Issue:** README claimed "macOS and FreeBSD only", but Linux is now supported

**Fix:** Updated with comprehensive platform matrix:
```markdown
**Platform Support**: 
- ✅ **ARM64 macOS** - Fully supported (kqueue I/O, custom context switching)
- ✅ **x86-64 Linux** - Fully supported (epoll I/O, custom context switching)
- 🔄 **x86-64 macOS** - Partial support (needs context switching port)
- 🔄 **ARM64 Linux** - Partial support (needs context switching port)
- 🔄 **FreeBSD/OpenBSD/NetBSD** - Partial support (has kqueue, needs context switching per arch)

See `PLATFORM_STATUS.md` for detailed platform support information.
```

**Benefit:** Users immediately see Linux is fully supported, with links to details.

### 🤔 Not Implemented (Rationale)

#### Signal Handler Validation During Shutdown
**Suggestion:** Validate `g_scheduler` pointer in SIGSEGV handler

**Decision:** Not implemented

**Rationale:** 
- Handler already has comment explaining it can't safely validate during shutdown
- False positives would be worse than missing edge case
- Signal handlers must be async-signal-safe (very limited operations)
- Current design is intentional: fail-fast on corruption rather than mask issues

**Alternative:** Added more comments explaining the trade-offs.

#### EEXIST Check for Duplicate FD Registration
**Suggestion:** Check errno == EEXIST for duplicate registrations

**Decision:** Not implemented

**Rationale:**
- epoll/kqueue both handle this correctly already (return -1)
- Duplicate registration is programmer error, not runtime condition
- Adding check would mask bugs rather than surface them
- Error message with errno is sufficient for debugging

**Current behavior:** Fails with errno showing the exact issue, which is correct.

## Verification

### Build Status
```bash
$ just build-runtime
✅ Built runtime/libcem_runtime.a for x86_64
```

### Test Results
```bash
$ just test
Unit tests:        46 passed ✅
Integration tests:  2 passed, 10 ignored ✅
Doc tests:          1 passed ✅
Total:            49 passed, 0 failed ✅
```

### No Regressions
- All previous functionality preserved
- Error messages enhanced (not changed)
- Performance characteristics unchanged

## Files Modified

1. `runtime/scheduler.c` - Error messages + MAX_IO_EVENTS constant
2. `README.md` - Platform support matrix updated
3. `PR_REVIEW_UPDATES.md` - This document

## Summary

All "should fix soon" items addressed. Code quality improved with:
- Better debugging (errno in error messages)
- Better maintainability (named constants)
- Better documentation (accurate platform support)

No breaking changes, no performance impact, all tests passing.

---

Ready for re-review and merge! 🚀
