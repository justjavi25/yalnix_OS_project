#include <yuser.h>

int main(int argc, char **argv)
{
    int pid = GetPid();

    TracePrintf(0, "cp3_getpid: start argc=%d argv0=%s\n",
                argc, argc > 0 ? argv[0] : "(null)");
    TracePrintf(0, "cp3_getpid: GetPid returned %d\n", pid);
    TracePrintf(0, "cp3_getpid: PASS if pid is positive\n");

    while (1) {
        Pause();
    }
}
