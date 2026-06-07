#include <yuser.h>

int main(int argc, char **argv)
{
    //test fork + wait functionality.
    int parent_pid = GetPid();

    TracePrintf(0, "cp4_wait: Parent starting with PID %d\n", parent_pid);

    int pid = Fork();

    if (pid == 0) {
        //child process.
        int child_pid = GetPid();
        TracePrintf(0, "cp4_wait: CHILD PID %d starting work\n", child_pid);

        //child does some work.
        int brk_result = Brk((void *)0x2000);
        TracePrintf(0, "cp4_wait: CHILD allocated heap, Brk returned %d\n", brk_result);

        TracePrintf(0, "cp4_wait: CHILD exiting with status 42\n");
        Exit(42);
    } else if (pid > 0) {
        //parent process - wait for child.
        TracePrintf(0, "cp4_wait: PARENT created child with PID %d\n", pid);
        TracePrintf(0, "cp4_wait: PARENT calling Wait for child\n");

        int status = 0;
        int child_result = Wait(&status);

        if (child_result == pid && status == 42) {
            TracePrintf(0, "cp4_wait: PASS child PID %d status %d\n",
                         child_result, status);
        } else if (child_result == -1) {
            TracePrintf(0, "cp4_wait: Wait returned ERROR\n");
        } else {
            TracePrintf(0, "cp4_wait: FAIL Wait returned PID %d with status %d\n",
                         child_result, status);
        }

        TracePrintf(0, "cp4_wait: PARENT exiting\n");
    } else {
        //fork failed.
        TracePrintf(0, "cp4_wait: Fork FAILED\n");
    }

    while (1) {
        Pause();
    }

    return 0;
}
