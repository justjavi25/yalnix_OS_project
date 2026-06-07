#include <yuser.h>

int main(int argc, char **argv)
{
    //test multiple fork calls to ensure process independence.
    int my_pid = GetPid();

    TracePrintf(0, "cp4_fork_multi: Starting main process PID %d\n", my_pid);

    //first fork.
    int pid1 = Fork();

    if (pid1 == 0) {
        //child 1.
        int child1_pid = GetPid();
        TracePrintf(0, "cp4_fork_multi: CHILD1 PID %d\n", child1_pid);

        //child 1 grows its heap.
        Brk((void *)0x2000);
        TracePrintf(0, "cp4_fork_multi: CHILD1 allocated heap\n");

        while (1) {
            Pause();
        }
    } else if (pid1 > 0) {
        //parent continues and forks again.
        TracePrintf(0, "cp4_fork_multi: PARENT created child1 PID %d\n", pid1);

        int pid2 = Fork();

        if (pid2 == 0) {
            //child 2.
            int child2_pid = GetPid();
            TracePrintf(0, "cp4_fork_multi: CHILD2 PID %d\n", child2_pid);

            //child 2 grows heap to different size.
            Brk((void *)0x4000);
            TracePrintf(0, "cp4_fork_multi: CHILD2 allocated larger heap\n");

            while (1) {
                Pause();
            }
        } else if (pid2 > 0) {
            //parent has two children now.
            TracePrintf(0, "cp4_fork_multi: PARENT created child2 PID %d\n", pid2);
            TracePrintf(0, "cp4_fork_multi: PARENT now has 2 children\n");
            TracePrintf(0, "cp4_fork_multi: PASS if all 3 processes have different PIDs\n");

            while (1) {
                Pause();
            }
        }
    }

    return 0;
}
