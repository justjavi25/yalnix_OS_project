#include <yuser.h>

int main(int argc, char **argv)
{
    //test fork followed by exec (the traditional spawn pattern).
    int parent_pid = GetPid();

    TracePrintf(0, "cp4_fork_exec: PARENT PID %d starting\n", parent_pid);

    int pid = Fork();

    if (pid == 0) {
        //child process - replace with different program.
        int child_pid = GetPid();
        TracePrintf(0, "cp4_fork_exec: CHILD PID %d before Exec\n", child_pid);

        //replace this process with cp4_fork.
        char *args[] = { "test/cp4_fork", NULL };
        int status = Exec("test/cp4_fork", args);

        //if we get here, exec failed.
        TracePrintf(0, "cp4_fork_exec: CHILD Exec FAILED\n");
        while (1) {
            Pause();
        }
    } else if (pid > 0) {
        //parent continues.
        int parent_result_pid = GetPid();
        TracePrintf(0, "cp4_fork_exec: PARENT PID %d created child PID %d\n",
                     parent_result_pid, pid);
        TracePrintf(0, "cp4_fork_exec: PARENT continuing while child runs different program\n");
        TracePrintf(0, "cp4_fork_exec: PASS if child process shows cp4_fork output\n");

        while (1) {
            Pause();
        }
    } else {
        //fork failed.
        TracePrintf(0, "cp4_fork_exec: Fork FAILED\n");
    }

    return 0;
}
