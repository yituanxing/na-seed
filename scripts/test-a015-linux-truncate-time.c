/* Linux oracle: zero-byte O_TRUNC must refresh timestamps even if size=0.
 * Ninja 1.13.2 uses fopen("wb") with no fwrite to stamp .ninja_lock.
 * No shell touch(1), synthetic timestamp, or patched Ninja involved. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static uint64_t stamp(struct timespec when) {
    return (uint64_t)when.tv_sec * 1000000000ULL +
           (uint64_t)when.tv_nsec;
}
static void set_older(const char *path) {
    struct timespec before[2];
    assert(clock_gettime(CLOCK_REALTIME, &before[0]) == 0);
    before[0].tv_sec -= 5;
    before[1] = before[0];
    assert(utimensat(AT_FDCWD, path, before, 0) == 0);
}
static struct stat stat_path(const char *path) {
    struct stat result;
    assert(stat(path, &result) == 0);
    return result;
}
static void open_trunc_without_write(const char *path) {
    int fd = open(path, O_WRONLY | O_TRUNC);
    assert(fd >= 0);
    assert(close(fd) == 0);
}
static void zero_length_fopen_without_write(const char *path) {
    FILE *f = fopen(path, "wb");
    assert(f);
    assert(fclose(f) == 0);
}
int main(void) {
    char path[] = "/tmp/naseed-a015-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    assert(close(fd) == 0);

    set_older(path);
    struct stat before = stat_path(path);
    open_trunc_without_write(path);
    struct stat after = stat_path(path);
    assert(after.st_size == 0);
    assert(stamp(after.st_mtim) > stamp(before.st_mtim));
    assert(stamp(after.st_ctim) >= stamp(before.st_ctim));
    puts("A015_LINUX_ZERO_LENGTH_OPEN_TRUNC_MTIME_PASS");

    set_older(path);
    before = stat_path(path);
    zero_length_fopen_without_write(path);
    after = stat_path(path);
    assert(after.st_size == 0);
    assert(stamp(after.st_mtim) > stamp(before.st_mtim));
    puts("A015_LINUX_NINJA_EMPTY_FOPEN_WB_MTIME_PASS");

    /* Linux also updates mtime for ftruncate(fd, 0) on a 0-byte inode. */
    set_older(path);
    before = stat_path(path);
    fd = open(path, O_WRONLY);
    assert(fd >= 0);
    assert(ftruncate(fd, 0) == 0);
    assert(close(fd) == 0);
    after = stat_path(path);
    assert(stamp(after.st_mtim) > stamp(before.st_mtim));
    puts("A015_LINUX_FTRUNCATE_SAME_SIZE_MTIME_PASS");

    assert(unlink(path) == 0);
    return 0;
}
