#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

/* A strict host watchdog can be longer in software-emulated ISA guests;
 * changing the deadline must never change the readv race assertions. */
#ifndef A014_WATCHDOG_SECONDS
#define A014_WATCHDOG_SECONDS 5
#endif

struct task {
    int fd;
    struct iovec *iov;
    int count;
    atomic_int entered, done;
    ssize_t result;
    int error;
};

static void timeout_exit(int signo) {
    (void)signo;
    static const char message[] = "[A014 FAIL] watchdog: blocked readv did not return\n";
    ssize_t written = write(2, message, sizeof(message) - 1);
    (void)written;
    _exit(124);
}

static void *read_worker(void *arg) {
    struct task *t = arg;
    atomic_store_explicit(&t->entered, 1, memory_order_release);
    t->result = readv(t->fd, t->iov, t->count);
    t->error = errno;
    atomic_store_explicit(&t->done, 1, memory_order_release);
    return NULL;
}

static void short_pause(void) {
    const struct timespec pause = {.tv_sec = 0, .tv_nsec = 10000000L};
    (void)nanosleep(&pause, NULL);
}

/* Handshake immediately before readv; with an empty pipe (no writer yet),
 * 150ms of scheduler time and a still-incomplete worker rule out a finished
 * read and make a late first syscall unlikely. /proc task syscall tracing
 * cannot be relied on in sandboxed CI PID namespaces. */
static void wait_until_kernel_readv(struct task *t) {
    while (!atomic_load_explicit(&t->entered, memory_order_acquire))
        short_pause();
    for (int tries = 0; tries < 15; tries++)
        short_pause();
    assert(!atomic_load_explicit(&t->done, memory_order_acquire));
}

static void test_iov_snapshot(void) {
    int p[2];
    assert(pipe(p) == 0);
    char a = 0, b = 0, c[2] = {0};
    struct iovec vectors[2] = {{.iov_base=&a,.iov_len=1},
                                {.iov_base=&b,.iov_len=1}};
    struct task t = {.fd=p[0],.iov=vectors,.count=2};
    pthread_t worker;
    assert(pthread_create(&worker, NULL, read_worker, &t) == 0);
    wait_until_kernel_readv(&t);

    vectors[0] = (struct iovec){.iov_base=c,.iov_len=2};
    vectors[1] = (struct iovec){.iov_base=NULL,.iov_len=0};
    assert(write(p[1], "XY", 2) == 2);
    assert(pthread_join(worker, NULL) == 0);
    assert(t.result == 2);
    assert(a == 'X' && b == 'Y' && c[0] == 0 && c[1] == 0);
    assert(close(p[0]) == 0 && close(p[1]) == 0);
    puts("A014_LINUX_BLOCKED_READV_IOV_SNAPSHOT_PASS");
}

static void test_open_file_snapshot(void) {
    int original[2], alternate[2];
    assert(pipe(original) == 0);
    assert(pipe(alternate) == 0);
    char output = 0;
    struct iovec vector = {.iov_base=&output,.iov_len=1};
    struct task t = {.fd=original[0],.iov=&vector,.count=1};
    pthread_t worker;
    assert(pthread_create(&worker, NULL, read_worker, &t) == 0);
    wait_until_kernel_readv(&t);

    /* The original FD number now names the alternate pipe, but Linux's
     * already-blocked readv remains attached to its pinned original File. */
    assert(dup2(alternate[0], original[0]) == original[0]);
    assert(write(original[1], "Z", 1) == 1);
    assert(pthread_join(worker, NULL) == 0);
    assert(t.result == 1 && output == 'Z');
    assert(close(original[0]) == 0 && close(original[1]) == 0);
    assert(close(alternate[0]) == 0 && close(alternate[1]) == 0);
    puts("A014_LINUX_BLOCKED_READV_FILE_SNAPSHOT_PASS");
}

int main(void) {
    struct sigaction signal_action = {.sa_handler = timeout_exit};
    assert(sigaction(SIGALRM, &signal_action, NULL) == 0);
    assert(signal(SIGPIPE, SIG_IGN) != SIG_ERR);
    alarm(A014_WATCHDOG_SECONDS);
    test_iov_snapshot();
    test_open_file_snapshot();
    alarm(0);
    puts("A014_LINUX_TWO_BLOCKING_OWNERSHIP_RACES_PASS");
    return 0;
}
