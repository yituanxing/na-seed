#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

int main(void) {
    const char *path = "/tmp/m57-sparse-large-file";
    const off_t at = (off_t)64 * 1024 * 1024 + 123;
    const char payload[] = "NaSeed sparse tmpfs beyond 32 MiB";
    char received[sizeof(payload)] = {0};
    int fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0600);
    if (fd < 0) { perror("largefile open"); return 1; }
    if (pwrite(fd, payload, sizeof(payload), at) != (ssize_t)sizeof(payload)) {
        perror("largefile pwrite"); return 2;
    }
    if (pread(fd, received, sizeof(received), at) != (ssize_t)sizeof(received) ||
        memcmp(received, payload, sizeof(payload)) != 0) {
        fprintf(stderr, "largefile sparse readback mismatch\n"); return 3;
    }
    if (ftruncate(fd, at + 8192) != 0) {
        perror("largefile grow"); return 4;
    }
    char hole[4] = {1, 1, 1, 1};
    if (pread(fd, hole, sizeof(hole), at + 4096) != (ssize_t)sizeof(hole) ||
        hole[0] || hole[1] || hole[2] || hole[3]) {
        fprintf(stderr, "largefile unwritten hole is not zero\n"); return 5;
    }
    if (ftruncate(fd, 7) != 0 || ftruncate(fd, at + 8192) != 0) {
        perror("largefile shrink/regrow"); return 6;
    }
    memset(received, 1, sizeof(received));
    if (pread(fd, received, sizeof(received), at) != (ssize_t)sizeof(received)) {
        perror("largefile regrown readback"); return 7;
    }
    for (unsigned i = 0; i < sizeof(received); i++)
        if (received[i]) {
            fprintf(stderr, "largefile truncated bytes leaked\n"); return 8;
        }
    if (close(fd) != 0 || unlink(path) != 0) {
        perror("largefile release"); return 9;
    }
    return 0;
}
