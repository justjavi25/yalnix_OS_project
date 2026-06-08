// cp6_pipe_simple.c - basic pipe test
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yalnix.h>

int main()
{
    int pipe_id;
    char write_data[100] = "Hello from pipe!";
    char read_data[100];
    int bytes_written;
    int bytes_read;

    // create pipe
    if (PipeInit(&pipe_id) == ERROR) {
        printf("FAIL: PipeInit\n");
        return EXIT_FAILURE;
    }
    printf("PipeInit created pipe %d\n", pipe_id);

    // write to pipe
    bytes_written = PipeWrite(pipe_id, write_data, strlen(write_data));
    if (bytes_written != strlen(write_data)) {
        printf("FAIL: PipeWrite returned %d, expected %ld\n", bytes_written, strlen(write_data));
        return EXIT_FAILURE;
    }
    printf("PipeWrite: wrote %d bytes\n", bytes_written);

    // read from pipe
    bytes_read = PipeRead(pipe_id, read_data, 100);
    if (bytes_read != strlen(write_data)) {
        printf("FAIL: PipeRead returned %d, expected %ld\n", bytes_read, strlen(write_data));
        return EXIT_FAILURE;
    }

    // verify data
    if (memcmp(read_data, write_data, bytes_read) != 0) {
        printf("FAIL: Data mismatch\n");
        return EXIT_FAILURE;
    }
    printf("PipeRead: read %d bytes, data correct\n", bytes_read);

    // reclaim pipe
    if (Reclaim(pipe_id) == ERROR) {
        printf("FAIL: Reclaim\n");
        return EXIT_FAILURE;
    }
    printf("Reclaim: pipe %d destroyed\n", pipe_id);

    printf("PASS\n");
    return EXIT_SUCCESS;
}
