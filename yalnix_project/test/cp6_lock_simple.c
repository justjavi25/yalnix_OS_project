// cp6_lock_simple.c - basic lock test
#include <stdio.h>
#include <stdlib.h>
#include <yalnix.h>

int main()
{
    int lock_id;
    int child_pid;
    int *status_ptr;

    // create lock
    if (LockInit(&lock_id) == ERROR) {
        printf("FAIL: LockInit\n");
        return EXIT_FAILURE;
    }
    printf("LockInit created lock %d\n", lock_id);

    // acquire lock in parent
    if (Acquire(lock_id) == ERROR) {
        printf("FAIL: Acquire in parent\n");
        return EXIT_FAILURE;
    }
    printf("Parent: Acquire succeeded\n");

    // fork child
    child_pid = Fork();
    if (child_pid == ERROR) {
        printf("FAIL: Fork\n");
        return EXIT_FAILURE;
    }

    if (child_pid == 0) {
        // child process
        printf("Child: Trying to acquire lock held by parent\n");
        // this should block
        if (Acquire(lock_id) == ERROR) {
            printf("Child: FAIL: Acquire\n");
            exit(EXIT_FAILURE);
        }
        printf("Child: Got the lock!\n");

        if (Release(lock_id) == ERROR) {
            printf("Child: FAIL: Release\n");
            exit(EXIT_FAILURE);
        }
        printf("Child: Released lock\n");
        exit(EXIT_SUCCESS);
    } else {
        // parent process
        Delay(1);  // Let child try to acquire
        printf("Parent: Releasing lock for child\n");

        if (Release(lock_id) == ERROR) {
            printf("FAIL: Release in parent\n");
            return EXIT_FAILURE;
        }
        printf("Parent: Released lock\n");

        // wait for child
        if (Wait(status_ptr) == ERROR) {
            printf("FAIL: Wait\n");
            return EXIT_FAILURE;
        }
        printf("Parent: Child exited\n");
    }

    // reclaim lock
    if (Reclaim(lock_id) == ERROR) {
        printf("FAIL: Reclaim\n");
        return EXIT_FAILURE;
    }
    printf("Reclaim: lock %d destroyed\n", lock_id);

    printf("PASS\n");
    return EXIT_SUCCESS;
}
