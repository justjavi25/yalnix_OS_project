#include <yuser.h>

int main(int argc, char **argv)
{
    //test basic exec functionality.
    int my_pid = GetPid();

    TracePrintf(0, "cp4_exec: Starting with PID %d\n", my_pid);
    TracePrintf(0, "cp4_exec: About to call Exec to replace with cp4_fork\n");

    //prepare arguments for the new program.
    char *args[] = {"test/cp4_fork", NULL };

    //exec to replace this process with cp4_fork.
    int status = Exec("test/cp4_fork", args);

    //if we get here, exec failed.
    TracePrintf(0, "cp4_exec: exec failed with status %d\n", status);
    TracePrintf(0, "cp4_exec: exec should not return on success\n");

    while (1) {
        Pause();
    }

    return 0;
}
