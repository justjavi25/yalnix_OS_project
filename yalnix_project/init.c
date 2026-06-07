#include <yuser.h>

int main(int argc, char **argv)
{
    int i = 0;
    int pid = GetPid();

    TracePrintf(0, "init: started pid=%d argc=%d argv0=%s\n",
                pid, argc, argc > 0 ? argv[0] : "(null)");

    while (1) {
        TracePrintf(0, "init: loop %d before delay\n", i++);
        Delay(2);
        TracePrintf(0, "init: loop woke from delay\n");
    }
}
