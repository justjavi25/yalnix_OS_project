// cp6_pipe_simple.c - basic pipe test
#include <yuser.h>

int main()
{
    int pipe_id;
    char write_data[100] = "Hello from pipe!";
    char read_data[100];
    int bytes_written;
    int bytes_read;
    int i, len;

    // compute strlen without libc
    len = 0;
    while (write_data[len] != '\0') len++;

    // create pipe
    if (PipeInit(&pipe_id) == ERROR) {
        TracePrintf(0, "cp6_pipe: FAIL: PipeInit\n");
        Exit(1);
    }
    TracePrintf(0, "cp6_pipe: PipeInit created pipe %d\n", pipe_id);

    // write to pipe
    bytes_written = PipeWrite(pipe_id, write_data, len);
    if (bytes_written != len) {
        TracePrintf(0, "cp6_pipe: FAIL: PipeWrite returned %d, expected %d\n", bytes_written, len);
        Exit(1);
    }
    TracePrintf(0, "cp6_pipe: PipeWrite wrote %d bytes\n", bytes_written);

    // read from pipe
    bytes_read = PipeRead(pipe_id, read_data, 100);
    if (bytes_read != len) {
        TracePrintf(0, "cp6_pipe: FAIL: PipeRead returned %d, expected %d\n", bytes_read, len);
        Exit(1);
    }

    // verify data
    for (i = 0; i < bytes_read; i++) {
        if (read_data[i] != write_data[i]) {
            TracePrintf(0, "cp6_pipe: FAIL: Data mismatch at %d\n", i);
            Exit(1);
        }
    }
    TracePrintf(0, "cp6_pipe: PipeRead read %d bytes, data correct\n", bytes_read);

    // reclaim pipe
    if (Reclaim(pipe_id) == ERROR) {
        TracePrintf(0, "cp6_pipe: FAIL: Reclaim\n");
        Exit(1);
    }
    TracePrintf(0, "cp6_pipe: Reclaim pipe %d destroyed\n", pipe_id);

    TracePrintf(0, "cp6_pipe: PASS\n");
    Exit(0);
}
