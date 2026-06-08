// cp6_lock_simple.c - basic lock test
#include <yuser.h>

int main()
{
    int lock_id;
    int child_pid;
    int status;

    // create lock
    if (LockInit(&lock_id) == ERROR) {
        TracePrintf(0, "cp6_lock: FAIL: LockInit\n");
        Exit(1);
    }
    TracePrintf(0, "cp6_lock: LockInit created lock %d\n", lock_id);

    // acquire lock in parent
    if (Acquire(lock_id) == ERROR) {
        TracePrintf(0, "cp6_lock: FAIL: Acquire in parent\n");
        Exit(1);
    }
    TracePrintf(0, "cp6_lock: Parent Acquire succeeded\n");

    // fork child
    child_pid = Fork();
    if (child_pid == ERROR) {
        TracePrintf(0, "cp6_lock: FAIL: Fork\n");
        Exit(1);
    }

    if (child_pid == 0) {
        // child process
        TracePrintf(0, "cp6_lock: Child trying to acquire lock held by parent\n");
        // this should block
        if (Acquire(lock_id) == ERROR) {
            TracePrintf(0, "cp6_lock: Child FAIL: Acquire\n");
            Exit(1);
        }
        TracePrintf(0, "cp6_lock: Child got the lock!\n");

        if (Release(lock_id) == ERROR) {
            TracePrintf(0, "cp6_lock: Child FAIL: Release\n");
            Exit(1);
        }
        TracePrintf(0, "cp6_lock: Child released lock\n");
        Exit(0);
    } else {
        // parent process
        Delay(1);  // Let child try to acquire
        TracePrintf(0, "cp6_lock: Parent releasing lock for child\n");

        if (Release(lock_id) == ERROR) {
            TracePrintf(0, "cp6_lock: FAIL: Release in parent\n");
            Exit(1);
        }
        TracePrintf(0, "cp6_lock: Parent released lock\n");

        // wait for child
        if (Wait(&status) == ERROR) {
            TracePrintf(0, "cp6_lock: FAIL: Wait\n");
            Exit(1);
        }
        TracePrintf(0, "cp6_lock: Parent child exited\n");
    }

    // reclaim lock
    if (Reclaim(lock_id) == ERROR) {
        TracePrintf(0, "cp6_lock: FAIL: Reclaim\n");
        Exit(1);
    }
    TracePrintf(0, "cp6_lock: Reclaim lock %d destroyed\n", lock_id);

    TracePrintf(0, "cp6_lock: PASS\n");
    Exit(0);
}
