#include <yuser.h>

int main(void)
{
    int rc;

    TracePrintf(0, "cp3_delay: before Delay(3)\n");
    rc = Delay(3);
    TracePrintf(0, "cp3_delay: after Delay(3), rc=%d\n", rc);

    TracePrintf(0, "cp3_delay: before Delay(0)\n");
    rc = Delay(0);
    TracePrintf(0, "cp3_delay: after Delay(0), rc=%d\n", rc);

    TracePrintf(0, "cp3_delay: before Delay(-1)\n");
    rc = Delay(-1);
    TracePrintf(0, "cp3_delay: after Delay(-1), rc=%d\n", rc);
    TracePrintf(0, "cp3_delay: PASS if rc values are 0, 0, -1 and idle runs during Delay(3)\n");

    while (1) {
        Pause();
    }
}
