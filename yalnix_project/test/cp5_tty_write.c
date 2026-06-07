#include <yuser.h>

int main(int argc, char **argv)
{
    char long_buf[1500];
    int rc;

    rc = TtyWrite(1, "CP5_TTY_WRITE_BEGIN\n", 20);
    TracePrintf(0, "cp5_tty_write: first write returned %d\n", rc);

    for (int i = 0; i < sizeof(long_buf); i++) {
        long_buf[i] = 'A' + (i % 26);
    }

    rc = TtyWrite(1, long_buf, sizeof(long_buf));
    TracePrintf(0, "cp5_tty_write: long write returned %d\n", rc);

    rc = TtyWrite(1, "\nCP5_TTY_WRITE_END\n", 19);
    TracePrintf(0, "cp5_tty_write: final write returned %d\n", rc);

    if (rc == 19) {
        TracePrintf(0, "cp5_tty_write: PASS\n");
    } else {
        TracePrintf(0, "cp5_tty_write: FAIL\n");
    }

    Exit(0);
    return 0;
}
