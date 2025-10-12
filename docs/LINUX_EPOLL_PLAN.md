# Linux epoll Support for Cem Scheduler - Implementation Plan

## Status: TODO (Separate from x86-64 Context Switching)

**Context switching for x86-64 Linux:** ✅ COMPLETE  
**Scheduler I/O multiplexing for Linux:** ❌ TODO (this document)

## Overview

The Cem scheduler currently uses kqueue (macOS/BSD) for async I/O event notification.
Linux requires epoll instead. This document outlines the work needed to add Linux support.

**Note:** This is completely separate from context switching, which is already done.

## Current State

### What Works
- Context switching on x86-64 Linux (✅ done)
- Strand spawning/yielding infrastructure
- Stack management
- Cleanup handlers

### What's Blocked
- Running scheduler with I/O on Linux
- Building `runtime/scheduler.c` on Linux
- Testing I/O-based Cem programs on Linux

### Platform Check Location
`runtime/scheduler.c`, lines 20-23:
```c
// Platform check - kqueue is only available on BSD-based systems
#if !defined(__APPLE__) && !defined(__FreeBSD__) && !defined(__OpenBSD__) && !defined(__NetBSD__)
#error "This scheduler requires kqueue support (macOS, FreeBSD, OpenBSD, or NetBSD). Linux support (epoll) is planned for Phase 2b."
#endif
```

## kqueue vs epoll - API Comparison

### kqueue (macOS/BSD)

**Setup:**
```c
int kq = kqueue();  // Create kqueue instance
```

**Register interest in event:**
```c
struct kevent ev;
EV_SET(&ev, fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, strand);
kevent(kq, &ev, 1, NULL, 0, NULL);  // Register
```

**Wait for events:**
```c
struct kevent events[MAX_EVENTS];
int n = kevent(kq, NULL, 0, events, MAX_EVENTS, &timeout);
for (int i = 0; i < n; i++) {
    Strand* strand = (Strand*)events[i].udata;
    // Handle event
}
```

### epoll (Linux)

**Setup:**
```c
int epfd = epoll_create1(0);  // Create epoll instance
```

**Register interest in event:**
```c
struct epoll_event ev;
ev.events = EPOLLIN;  // Read events
ev.data.ptr = strand;
epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);  // Register
```

**Wait for events:**
```c
struct epoll_event events[MAX_EVENTS];
int n = epoll_wait(epfd, events, MAX_EVENTS, timeout_ms);
for (int i = 0; i < n; i++) {
    Strand* strand = (Strand*)events[i].data.ptr;
    // Handle event
}
```

### Key Differences

| Feature | kqueue | epoll |
|---------|--------|-------|
| Create | `kqueue()` | `epoll_create1()` |
| Register | `kevent()` with `EV_SET` | `epoll_ctl()` with `EPOLL_CTL_ADD` |
| Wait | `kevent()` with event array | `epoll_wait()` |
| Modify | Same `kevent()` call | `epoll_ctl()` with `EPOLL_CTL_MOD` |
| Delete | `EV_DELETE` flag | `EPOLL_CTL_DEL` |
| Timeout | `struct timespec*` (ns precision) | `int` milliseconds |
| User data | `udata` field in kevent | `data` union in epoll_event |
| Edge vs Level | Both supported | Both supported |
| Read filter | `EVFILT_READ` | `EPOLLIN` |
| Write filter | `EVFILT_WRITE` | `EPOLLOUT` |

## Implementation Strategy

### Option 1: Conditional Compilation (Recommended)

Use `#ifdef` to compile different code paths:

```c
#ifdef __linux__
    // epoll implementation
    int epfd = epoll_create1(0);
#else
    // kqueue implementation
    int kq = kqueue();
#endif
```

**Pros:**
- Single source file
- Clear platform-specific sections
- No runtime overhead

**Cons:**
- More `#ifdef` blocks in code
- Harder to read if not organized well

### Option 2: Abstraction Layer

Create `io_events.h` with unified API:

```c
typedef struct IOEventLoop IOEventLoop;

IOEventLoop* io_event_loop_create(void);
void io_event_loop_destroy(IOEventLoop* loop);
int io_event_loop_register_read(IOEventLoop* loop, int fd, void* data);
int io_event_loop_register_write(IOEventLoop* loop, int fd, void* data);
int io_event_loop_wait(IOEventLoop* loop, int timeout_ms, IOEvent* events, int max_events);
```

Implementation in `io_events_kqueue.c` and `io_events_epoll.c`.

**Pros:**
- Clean abstraction
- Platform code isolated
- Easier to test

**Cons:**
- More files to maintain
- Slight indirection overhead
- Overkill for 2 platforms

### Recommendation: Option 1 (Conditional Compilation)

For just kqueue vs epoll, conditional compilation is simpler and sufficient.
If we add more platforms (Windows IOCP, io_uring), consider abstraction.

## Code Locations to Modify

### 1. Platform Detection (lines 20-23)

**Current:**
```c
#if !defined(__APPLE__) && !defined(__FreeBSD__) && !defined(__OpenBSD__) && !defined(__NetBSD__)
#error "This scheduler requires kqueue support..."
#endif
```

**Change to:**
```c
#if defined(__linux__)
    #define USE_EPOLL
    #include <sys/epoll.h>
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    #define USE_KQUEUE
    #include <sys/event.h>
    #include <sys/time.h>
#else
    #error "Unsupported platform. Need kqueue or epoll support."
#endif
```

### 2. Scheduler Structure (Scheduler typedef)

**Current:**
```c
typedef struct {
    // ...
    int kqueue_fd;
    // ...
} Scheduler;
```

**Change to:**
```c
typedef struct {
    // ...
#ifdef USE_KQUEUE
    int kqueue_fd;
#elif defined(USE_EPOLL)
    int epoll_fd;
#endif
    // ...
} Scheduler;
```

### 3. scheduler_init() (line ~193)

**Current:**
```c
global_scheduler.kqueue_fd = kqueue();
if (global_scheduler.kqueue_fd == -1) {
    runtime_error("scheduler_init: kqueue() failed");
}
```

**Change to:**
```c
#ifdef USE_KQUEUE
    global_scheduler.kqueue_fd = kqueue();
    if (global_scheduler.kqueue_fd == -1) {
        runtime_error("scheduler_init: kqueue() failed");
    }
#elif defined(USE_EPOLL)
    global_scheduler.epoll_fd = epoll_create1(0);
    if (global_scheduler.epoll_fd == -1) {
        runtime_error("scheduler_init: epoll_create1() failed");
    }
#endif
```

### 4. scheduler_shutdown() (line ~232)

**Current:**
```c
if (global_scheduler.kqueue_fd != -1) {
    close(global_scheduler.kqueue_fd);
    global_scheduler.kqueue_fd = -1;
}
```

**Change to:**
```c
#ifdef USE_KQUEUE
    if (global_scheduler.kqueue_fd != -1) {
        close(global_scheduler.kqueue_fd);
        global_scheduler.kqueue_fd = -1;
    }
#elif defined(USE_EPOLL)
    if (global_scheduler.epoll_fd != -1) {
        close(global_scheduler.epoll_fd);
        global_scheduler.epoll_fd = -1;
    }
#endif
```

### 5. strand_block_on_read() (line ~500)

**Current:**
```c
struct kevent ev;
EV_SET(&ev, fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, strand);
if (kevent(global_scheduler.kqueue_fd, &ev, 1, NULL, 0, NULL) == -1) {
    runtime_error("strand_block_on_read: kevent registration failed");
}
```

**Change to:**
```c
#ifdef USE_KQUEUE
    struct kevent ev;
    EV_SET(&ev, fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, strand);
    if (kevent(global_scheduler.kqueue_fd, &ev, 1, NULL, 0, NULL) == -1) {
        runtime_error("strand_block_on_read: kevent registration failed");
    }
#elif defined(USE_EPOLL)
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;  // Edge-triggered like kqueue
    ev.data.ptr = strand;
    if (epoll_ctl(global_scheduler.epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        runtime_error("strand_block_on_read: epoll_ctl failed");
    }
#endif
```

### 6. strand_block_on_write() (line ~536)

Similar to `strand_block_on_read()` but with `EVFILT_WRITE` / `EPOLLOUT`.

### 7. scheduler_run() - Event Loop

This is the main event loop. Needs most work.

**Current (kqueue):**
```c
struct kevent events[MAX_EVENTS];
struct timespec timeout = {0, 0};  // Non-blocking
int nevents = kevent(global_scheduler.kqueue_fd, NULL, 0, 
                     events, MAX_EVENTS, &timeout);

for (int i = 0; i < nevents; i++) {
    Strand* strand = (Strand*)events[i].udata;
    // Handle event...
}
```

**Change to:**
```c
#ifdef USE_KQUEUE
    struct kevent events[MAX_EVENTS];
    struct timespec timeout = {0, 0};
    int nevents = kevent(global_scheduler.kqueue_fd, NULL, 0,
                         events, MAX_EVENTS, &timeout);
    
    for (int i = 0; i < nevents; i++) {
        Strand* strand = (Strand*)events[i].udata;
        // Handle event...
    }
#elif defined(USE_EPOLL)
    struct epoll_event events[MAX_EVENTS];
    int nevents = epoll_wait(global_scheduler.epoll_fd, events, MAX_EVENTS, 0);
    
    for (int i = 0; i < nevents; i++) {
        Strand* strand = (Strand*)events[i].data.ptr;
        // Handle event...
    }
#endif
```

## Testing Strategy

### Phase 1: Compile on Linux
1. Make changes above
2. Build: `just build-runtime`
3. Verify no compilation errors

### Phase 2: Basic Scheduler Tests
1. Run: `just test-scheduler`
2. Verify strands can spawn and yield
3. No I/O needed yet

### Phase 3: I/O Tests
1. Run: `just test-io-simple`
2. Test reading from stdin
3. Test writing to stdout

### Phase 4: Integration Tests
1. Run full test suite
2. Echo server example
3. Concurrent I/O test

## Edge Cases to Consider

### 1. One-shot vs Persistent Events

**kqueue:** Events are edge-triggered by default (one-shot)  
**epoll:** Can be edge-triggered (`EPOLLET`) or level-triggered

**Decision:** Use edge-triggered mode (`EPOLLET`) to match kqueue behavior.

### 2. Event Removal

When a strand completes or I/O fd closes, need to remove from event loop:

**kqueue:** Automatically removes on fd close  
**epoll:** Must explicitly call `EPOLL_CTL_DEL`

**Solution:** Add cleanup in strand cleanup handler.

### 3. Timeout Precision

**kqueue:** Nanosecond precision (`struct timespec`)  
**epoll:** Millisecond precision (`int` ms)

**Solution:** Convert timespec to milliseconds for epoll, accept loss of precision.

### 4. Multiple Threads (Future Work-Stealing)

**kqueue:** Thread-safe, multiple threads can wait  
**epoll:** Thread-safe, but typically one thread per epoll fd

**Solution:** For work-stealing, each OS thread can have its own epoll fd.

## Work Estimate

- **Phase 1 (Compilation):** 2-3 hours
  - Add platform detection
  - Update scheduler structure
  - Add conditional blocks

- **Phase 2 (Basic Testing):** 1-2 hours
  - Test scheduler without I/O
  - Fix any bugs

- **Phase 3 (I/O Testing):** 2-3 hours
  - Test read/write blocking
  - Test event handling
  - Debug edge cases

- **Phase 4 (Integration):** 1-2 hours
  - Run full test suite
  - Fix any remaining issues

**Total:** 6-10 hours

## Dependencies

**None** - This work is independent of context switching (already done).

## Risks

**Low Risk:**
- epoll and kqueue are very similar APIs
- Changes are localized to scheduler.c
- Good test coverage exists

**Medium Risk:**
- Edge-triggered behavior may differ subtly
- Timeout handling needs care
- Event cleanup on fd close

**Mitigation:**
- Start with simple tests
- Test edge cases explicitly
- Reference existing epoll implementations (libuv, tokio)

## Conclusion

Adding Linux epoll support is straightforward conditional compilation work.
The APIs are similar enough that the changes are mechanical, not architectural.

Once done, Cem will have full cross-platform support for:
- ✅ ARM64 macOS (context + I/O)
- ✅ x86-64 Linux (context + I/O)
- 🔄 x86-64 macOS (needs context port only)
- 🔄 ARM64 Linux (needs I/O port only)

## References

- [epoll(7) man page](https://man7.org/linux/man-pages/man7/epoll.7.html)
- [kqueue(2) man page](https://www.freebsd.org/cgi/man.cgi?query=kqueue)
- [libuv event loop](https://github.com/libuv/libuv) - Good reference for cross-platform I/O
- [The C10K problem](http://www.kegel.com/c10k.html) - Historical context
