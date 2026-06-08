// cp6_cvar_simple.c - basic cvar test (producer/consumer)
#include <stdio.h>
#include <stdlib.h>
#include <yalnix.h>

int main()
{
    int lock_id;
    int cvar_id;
    int child_pid;
    int *status_ptr;
    int shared_data = 0;

    // create lock and cvar
    if (LockInit(&lock_id) == ERROR) {
        printf("FAIL: LockInit\n");
        return EXIT_FAILURE;
    }
    printf("LockInit created lock %d\n", lock_id);

    if (CvarInit(&cvar_id) == ERROR) {
        printf("FAIL: CvarInit\n");
        return EXIT_FAILURE;
    }
    printf("CvarInit created cvar %d\n", cvar_id);

    // fork child
    child_pid = Fork();
    if (child_pid == ERROR) {
        printf("FAIL: Fork\n");
        return EXIT_FAILURE;
    }

    if (child_pid == 0) {
        // consumer process
        printf("Consumer: Trying to acquire lock\n");
        if (Acquire(lock_id) == ERROR) {
            printf("Consumer: FAIL: Acquire\n");
            exit(EXIT_FAILURE);
        }
        printf("Consumer: Got lock, waiting on cvar\n");

        // wait on cvar (will release lock, wait, and re-acquire)
        if (CvarWait(cvar_id, lock_id) == ERROR) {
            printf("Consumer: FAIL: CvarWait\n");
            exit(EXIT_FAILURE);
        }
        printf("Consumer: Woken up, have lock back\n");

        if (Release(lock_id) == ERROR) {
            printf("Consumer: FAIL: Release\n");
            exit(EXIT_FAILURE);
        }
        printf("Consumer: PASS - got signaled correctly\n");
        exit(EXIT_SUCCESS);
    } else {
        // producer process
        Delay(1);  // Let consumer reach wait
        printf("Producer: Acquiring lock\n");
        if (Acquire(lock_id) == ERROR) {
            printf("Producer: FAIL: Acquire\n");
            return EXIT_FAILURE;
        }
        printf("Producer: Got lock, signaling cvar\n");

        if (CvarSignal(cvar_id) == ERROR) {
            printf("Producer: FAIL: CvarSignal\n");
            return EXIT_FAILURE;
        }
        printf("Producer: Signaled cvar\n");

        if (Release(lock_id) == ERROR) {
            printf("Producer: FAIL: Release\n");
            return EXIT_FAILURE;
        }
        printf("Producer: Released lock\n");

        // wait for consumer
        if (Wait(status_ptr) == ERROR) {
            printf("Producer: FAIL: Wait\n");
            return EXIT_FAILURE;
        }
        printf("Producer: Consumer exited\n");
    }

    // reclaim resources
    if (Reclaim(lock_id) == ERROR) {
        printf("FAIL: Reclaim lock\n");
        return EXIT_FAILURE;
    }
    printf("Reclaim: lock destroyed\n");

    if (Reclaim(cvar_id) == ERROR) {
        printf("FAIL: Reclaim cvar\n");
        return EXIT_FAILURE;
    }
    printf("Reclaim: cvar destroyed\n");

    printf("PASS\n");
    return EXIT_SUCCESS;
}
