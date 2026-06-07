#include <yuser.h>

int main(int argc, char **argv)
{
    //test basic fork functionality.
    int pid = Fork();

    TracePrintf(0, "cp4_fork: Fork returned %d\n", pid);

    if (pid == 0) {
        //this is the child process.
        int my_pid = GetPid();
        TracePrintf(0, "cp4_fork: I am CHILD, my PID is %d\n", my_pid);
        TracePrintf(0, "cp4_fork: PASS if child PID is different from parent\n");
    } else if (pid > 0) {
        //this is the parent process.
        int my_pid = GetPid();
        TracePrintf(0, "cp4_fork: I am PARENT, my PID is %d\n", my_pid);
        TracePrintf(0, "cp4_fork: PASS if parent and child PIDs are different\n");
    } else {
        //fork failed.
        TracePrintf(0, "cp4_fork: Fork FAILED with error %d\n", pid);
    }

    //loop forever to prevent exit.
    while (1) {
        Pause();
    }

    return 0;
}
