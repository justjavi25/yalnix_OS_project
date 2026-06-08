// cp6_cvar_simple.c - basic cvar test (producer/consumer)
#include <yuser.h>

int main()
{
    int lock_id;
    int cvar_id;
    int child_pid;
    int status;

    // create lock and cvar
    if (LockInit(&lock_id) == ERROR) {
        TracePrintf(0, "cp6_cvar: FAIL: LockInit\n");
        Exit(1);
    }
    TracePrintf(0, "cp6_cvar: LockInit created lock %d\n", lock_id);

    if (CvarInit(&cvar_id) == ERROR) {
        TracePrintf(0, "cp6_cvar: FAIL: CvarInit\n");
        Exit(1);
    }
    TracePrintf(0, "cp6_cvar: CvarInit created cvar %d\n", cvar_id);

    // fork child
    child_pid = Fork();
    if (child_pid == ERROR) {
        TracePrintf(0, "cp6_cvar: FAIL: Fork\n");
        Exit(1);
    }

    if (child_pid == 0) {
        // consumer process
        TracePrintf(0, "cp6_cvar: Consumer trying to acquire lock\n");
        if (Acquire(lock_id) == ERROR) {
            TracePrintf(0, "cp6_cvar: Consumer FAIL: Acquire\n");
            Exit(1);
        }
        TracePrintf(0, "cp6_cvar: Consumer got lock, waiting on cvar\n");

        // wait on cvar (will release lock, wait, and re-acquire)
        if (CvarWait(cvar_id, lock_id) == ERROR) {
            TracePrintf(0, "cp6_cvar: Consumer FAIL: CvarWait\n");
            Exit(1);
        }
        TracePrintf(0, "cp6_cvar: Consumer woken up, have lock back\n");

        if (Release(lock_id) == ERROR) {
            TracePrintf(0, "cp6_cvar: Consumer FAIL: Release\n");
            Exit(1);
        }
        TracePrintf(0, "cp6_cvar: Consumer PASS - got signaled correctly\n");
        Exit(0);
    } else {
        // producer process
        Delay(1);  // Let consumer reach wait
        TracePrintf(0, "cp6_cvar: Producer acquiring lock\n");
        if (Acquire(lock_id) == ERROR) {
            TracePrintf(0, "cp6_cvar: Producer FAIL: Acquire\n");
            Exit(1);
        }
        TracePrintf(0, "cp6_cvar: Producer got lock, signaling cvar\n");

        if (CvarSignal(cvar_id) == ERROR) {
            TracePrintf(0, "cp6_cvar: Producer FAIL: CvarSignal\n");
            Exit(1);
        }
        TracePrintf(0, "cp6_cvar: Producer signaled cvar\n");

        if (Release(lock_id) == ERROR) {
            TracePrintf(0, "cp6_cvar: Producer FAIL: Release\n");
            Exit(1);
        }
        TracePrintf(0, "cp6_cvar: Producer released lock\n");

        // wait for consumer
        if (Wait(&status) == ERROR) {
            TracePrintf(0, "cp6_cvar: Producer FAIL: Wait\n");
            Exit(1);
        }
        TracePrintf(0, "cp6_cvar: Producer consumer exited\n");
    }

    // reclaim resources
    if (Reclaim(lock_id) == ERROR) {
        TracePrintf(0, "cp6_cvar: FAIL: Reclaim lock\n");
        Exit(1);
    }
    TracePrintf(0, "cp6_cvar: Reclaim lock destroyed\n");

    if (Reclaim(cvar_id) == ERROR) {
        TracePrintf(0, "cp6_cvar: FAIL: Reclaim cvar\n");
        Exit(1);
    }
    TracePrintf(0, "cp6_cvar: Reclaim cvar destroyed\n");

    TracePrintf(0, "cp6_cvar: PASS\n");
    Exit(0);
}
