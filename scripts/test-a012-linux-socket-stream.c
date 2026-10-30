/* Linux runtime reference for AF_UNIX SOCK_STREAM's byte-stream writes.
 * The concrete buffer capacity is a socket implementation detail: unlike
 * anonymous pipes, no PIPE_BUF all-or-nothing promise exists. M106 verifies
 * the equivalent partial-admission rule in NaSeed's bounded ring backend.
 */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

int main(void) {
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds)) {
        if (errno == EPERM || errno == EACCES || errno == EAFNOSUPPORT ||
            errno == EPROTONOSUPPORT) {
            /* Development sandboxes can prohibit socket syscalls. A hosted
             * Actions run is an acceptance gate and must never silently
             * downgrade missing Linux oracle coverage to PASS. */
            const char *ci = getenv("GITHUB_ACTIONS");
            if (ci && !strcmp(ci,"true")) {
                perror("[NAS-A012 LINUX SOCKET FAIL] socketpair required in CI");
                return 1;
            }
            puts("[NAS-A012 LINUX SOCKET SKIP] socketpair unavailable in host sandbox");
            return 0;
        }
        perror("socketpair");
        return 1;
    }

    /* A number of CI sandboxes permit socketpair but restrict setsockopt.
     * A large request is enough to exceed ordinary stream send buffers
     * without modifying host socket policy. */
    enum { BLOCK = 4 * 1024 * 1024 };
    int sndbuf = 0;
    socklen_t optlen = sizeof(sndbuf);
    if (!getsockopt(fds[0],SOL_SOCKET,SO_SNDBUF,&sndbuf,&optlen)) {
        /* 8 MiB is deliberately much larger than ordinary AF_UNIX send
         * buffers. If a runner tunes SO_SNDBUF beyond this, fail visibly
         * rather than mistake a full write for a kernel regression. */
        if (sndbuf >= 2 * BLOCK) {
            fprintf(stderr,"[NAS-A012 LINUX SOCKET FAIL] host SO_SNDBUF=%d >= oracle 8MiB; adjust reference request\n",sndbuf);
            return 1;
        }
    }
    uint8_t *source = malloc(2U * BLOCK);
    uint8_t *received = malloc(2U * BLOCK);
    assert(source && received);
    memset(source, 'A', BLOCK);
    memset(source + BLOCK, 'B', BLOCK);
    struct iovec iov[2] = {{source, BLOCK}, {source + BLOCK, BLOCK}};
    ssize_t sent = writev(fds[0], iov, 2);
    /* A stream sender may make progress without reserving its whole write.
     * A pipe would also short-write >PIPE_BUF; this oracle intentionally
     * validates stream partial progress, not a numeric capacity match. */
    assert(sent > 0 && sent < 2 * BLOCK);
    size_t collected = 0;
    while (collected < (size_t)sent) {
        ssize_t n = read(fds[1], received + collected, (size_t)sent - collected);
        assert(n > 0);
        collected += (size_t)n;
    }
    assert(!memcmp(received, source, (size_t)sent));
    puts("[NAS-A012 LINUX SOCKET PASS] AF_UNIX STREAM writev permits short progress without PIPE_BUF admission");
    close(fds[0]);
    close(fds[1]);
    free(received);
    free(source);
    return 0;
}
