#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/uio.h>
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

    /* Real GNU as/ld writes output in vectors larger than 1 MiB. Both
     * write() and writev() must accept those spans on every Linux ISA. */
    const size_t nbytes = 2 * 1024 * 1024 + 4096;
    unsigned char *big = malloc(nbytes);
    if (!big) { perror("largefile malloc"); return 10; }
    for (size_t i = 0; i < nbytes; i++) big[i] = (unsigned char)(i * 31U);
    fd = open("/tmp/m57-large-vectored-write", O_CREAT | O_TRUNC | O_RDWR, 0600);
    if (fd < 0) { perror("largefile writev open"); return 11; }
    struct iovec iov = { .iov_base = big, .iov_len = nbytes };
    if (writev(fd, &iov, 1) != (ssize_t)nbytes) {
        perror("largefile writev"); return 12;
    }
    unsigned char sample[128];
    const off_t probes[] = { 0, (off_t)1024 * 1024, (off_t)nbytes - (off_t)sizeof(sample) };
    for (unsigned p = 0; p < sizeof(probes) / sizeof(probes[0]); p++) {
        if (pread(fd, sample, sizeof(sample), probes[p]) != (ssize_t)sizeof(sample) ||
            memcmp(sample, big + probes[p], sizeof(sample)) != 0) {
            fprintf(stderr, "largefile vectored readback mismatch\n"); return 13;
        }
    }
    if (ftruncate(fd, 0) != 0 || lseek(fd, 0, SEEK_SET) != 0 ||
        write(fd, big, nbytes) != (ssize_t)nbytes) {
        perror("largefile write"); return 14;
    }
    if (pread(fd, sample, sizeof(sample), probes[2]) != (ssize_t)sizeof(sample) ||
        memcmp(sample, big + probes[2], sizeof(sample)) != 0) {
        fprintf(stderr, "largefile write readback mismatch\n"); return 15;
    }
    if (close(fd) != 0 || unlink("/tmp/m57-large-vectored-write") != 0) {
        perror("largefile write release"); return 16;
    }
    free(big);
    return 0;
}
