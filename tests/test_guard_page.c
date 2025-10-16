#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "../runtime/scheduler.h"

static bool guard_test_ran = false;

// Function that uses a lot of stack to trigger overflow
StackCell* strand_stack_overflow(StackCell* stack) {
    write(2, "[GUARD_TEST] Strand started\n", 28);
    
    // Allocate 5KB on stack (more than 4KB initial size)
    // This should trigger stack overflow into guard page
    char buffer[5 * 1024];
    
    write(2, "[GUARD_TEST] Allocated 5KB buffer\n", 35);
    
    // Use the buffer to prevent compiler optimization
    memset(buffer, 0x42, sizeof(buffer));
    
    write(2, "[GUARD_TEST] Buffer initialized\n", 33);
    
    guard_test_ran = (buffer[0] == 0x42);
    return stack;
}

int main(void) {
    printf("=== Guard Page Overflow Test ===\n");
    printf("Initial stack: 4KB\n");
    printf("Buffer size: 5KB\n");
    printf("Expected: Emergency guard page growth\n\n");
    
    write(2, "[MAIN] Calling scheduler_init()\n", 33);
    scheduler_init();
    
    write(2, "[MAIN] Calling strand_spawn()\n", 31);
    strand_spawn(strand_stack_overflow, NULL);
    
    write(2, "[MAIN] Calling scheduler_run()\n", 32);
    scheduler_run();
    
    write(2, "[MAIN] Calling scheduler_shutdown()\n", 37);
    scheduler_shutdown();
    
    if (guard_test_ran) {
        printf("\n✓ Guard page test completed - emergency growth worked!\n");
        return 0;
    } else {
        printf("\n✗ Guard page test failed\n");
        return 1;
    }
}
