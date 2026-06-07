#include <yalnix.h>
#include <yuser.h>

static int BytesEqual(char *buf, char *want, int len)
{
    for (int i = 0; i < len; i++) {
        if (buf[i] != want[i]) {
            return 0;
        }
    }
    return 1;
}

int main(void)
{
    char buf[32];
    int n1;
    int n2;
    int n3;

    TracePrintf(0, "cp5_tty_read: starting\n");

    n1 = TtyRead(0, buf, 4);
    TracePrintf(0, "cp5_tty_read: first read returned %d\n", n1);
    if (n1 != 4 || !BytesEqual(buf, "abcd", 4)) {
        TracePrintf(0, "cp5_tty_read: FAIL first read\n");
        Exit(1);
    }

    n2 = TtyRead(0, buf, sizeof(buf));
    TracePrintf(0, "cp5_tty_read: second read returned %d\n", n2);
    if (n2 != 6 || !BytesEqual(buf, "efghi\n", 6)) {
        TracePrintf(0, "cp5_tty_read: FAIL second read\n");
        Exit(2);
    }

    n3 = TtyRead(0, buf, sizeof(buf));
    TracePrintf(0, "cp5_tty_read: third read returned %d\n", n3);
    if (n3 != 7 || !BytesEqual(buf, "second\n", 7)) {
        TracePrintf(0, "cp5_tty_read: FAIL third read\n");
        Exit(3);
    }

    TracePrintf(0, "cp5_tty_read: PASS\n");
    Exit(0);
}
