/* Native Linux AF_UNIX name/inode lifecycle oracle. This is intentionally
 * independent of NaSeed: the real NaSeed backend is checked by the paired
 * host regression, while this executable fixes the expected ABI semantics.
 * Do not replace this with a model of how Linux sockets ought to behave. */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

static struct sockaddr_un addr_of(const char *path) {
    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    assert(strlen(path) < sizeof(addr.sun_path));
    strcpy(addr.sun_path, path);
    return addr;
}
static socklen_t addr_len(const struct sockaddr_un *address) {
    return (socklen_t)(offsetof(struct sockaddr_un, sun_path) +
                       strlen(address->sun_path) + 1);
}
static int listen_at(const char *path) {
    int s = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    assert(s >= 0);
    struct sockaddr_un address = addr_of(path);
    if(bind(s, (const struct sockaddr *)&address, addr_len(&address)) != 0) {
        fprintf(stderr, "M107 bind(%s): %s\n", path, strerror(errno));
        abort();
    }
    assert(listen(s, 8) == 0);
    return s;
}
static int connect_at(const char *path) {
    int s = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    assert(s >= 0);
    struct sockaddr_un address = addr_of(path);
    assert(connect(s, (const struct sockaddr *)&address,
                   addr_len(&address)) == 0);
    return s;
}
static void expect_connect_error(const char *path, int expected) {
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    assert(s >= 0);
    struct sockaddr_un address = addr_of(path);
    assert(connect(s, (const struct sockaddr *)&address,
                   addr_len(&address)) == -1);
    assert(errno == expected);
    assert(close(s) == 0);
}
static void ping(int from, int to, char value) {
    char got = 0;
    assert(send(from, &value, 1, 0) == 1);
    assert(recv(to, &got, 1, 0) == 1 && got == value);
}
static void send_right(int channel, int descriptor) {
    char payload = 'r';
    struct iovec vec = {.iov_base = &payload, .iov_len = 1};
    union { struct cmsghdr hdr; unsigned char bytes[CMSG_SPACE(sizeof(int))]; } ancillary = {0};
    struct msghdr msg = {.msg_iov = &vec, .msg_iovlen = 1,
                         .msg_control = ancillary.bytes,
                         .msg_controllen = sizeof(ancillary.bytes)};
    struct cmsghdr *hdr = CMSG_FIRSTHDR(&msg);
    assert(hdr);
    hdr->cmsg_level = SOL_SOCKET;
    hdr->cmsg_type = SCM_RIGHTS;
    hdr->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(hdr), &descriptor, sizeof(descriptor));
    assert(sendmsg(channel, &msg, 0) == 1);
}
static int recv_right(int channel) {
    char payload = 0;
    struct iovec vec = {.iov_base = &payload, .iov_len = 1};
    union { struct cmsghdr hdr; unsigned char bytes[CMSG_SPACE(sizeof(int))]; } ancillary = {0};
    struct msghdr msg = {.msg_iov = &vec, .msg_iovlen = 1,
                         .msg_control = ancillary.bytes,
                         .msg_controllen = sizeof(ancillary.bytes)};
    assert(recvmsg(channel, &msg, MSG_CMSG_CLOEXEC) == 1);
    assert(payload == 'r' && !(msg.msg_flags & MSG_CTRUNC));
    struct cmsghdr *hdr = CMSG_FIRSTHDR(&msg);
    assert(hdr && hdr->cmsg_level == SOL_SOCKET && hdr->cmsg_type == SCM_RIGHTS);
    int result = -1;
    memcpy(&result, CMSG_DATA(hdr), sizeof(result));
    assert(result >= 0 && (fcntl(result, F_GETFD) & FD_CLOEXEC));
    return result;
}
int main(void) {
    char dir[] = "/tmp/naseed-m107-linux-XXXXXX";
    assert(mkdtemp(dir));
    char original[108], moved[108], alias[108];
    assert(snprintf(original, sizeof(original), "%s/old", dir) > 0);
    assert(snprintf(moved, sizeof(moved), "%s/new", dir) > 0);
    assert(snprintf(alias, sizeof(alias), "%s/alias", dir) > 0);
    int old_listener = listen_at(original);
    int old_client = connect_at(original);
    int old_accepted = accept4(old_listener, NULL, NULL, SOCK_CLOEXEC);
    assert(old_accepted >= 0);
    ping(old_client, old_accepted, 'o');

    /* Unlink removes reachability, not existing socket/connection objects.
     * Reusing the name must bind a DIFFERENT inode/listener identity. */
    assert(unlink(original) == 0);
    expect_connect_error(original, ENOENT);
    ping(old_accepted, old_client, 'u');
    int replacement = listen_at(original);
    int current = connect_at(original);
    int fresh = accept(replacement, NULL, NULL);
    assert(fresh >= 0);
    ping(current, fresh, 'n');
    ping(old_client, old_accepted, 'p');

    /* Rename changes pathname resolution but not the bound inode. */
    assert(rename(original, moved) == 0);
    expect_connect_error(original, ENOENT);
    int renamed = connect_at(moved);
    int renamed_peer = accept(replacement, NULL, NULL);
    assert(renamed_peer >= 0);
    ping(renamed, renamed_peer, 'm');
    assert(symlink(moved, alias) == 0);
    int linked = connect_at(alias);
    int linked_peer = accept(replacement, NULL, NULL);
    assert(linked_peer >= 0);
    ping(linked, linked_peer, 's');

    /* Duplication extends listener life; only LAST close removes the active
     * listener. The socket filesystem node survives final close. */
    int duplicate = dup(replacement);
    assert(duplicate >= 0);
    assert(close(replacement) == 0);
    int before_last_close = connect_at(moved);
    int before_last_peer = accept(duplicate, NULL, NULL);
    assert(before_last_peer >= 0);
    ping(before_last_close, before_last_peer, 'd');
    assert(close(duplicate) == 0);
    expect_connect_error(moved, ECONNREFUSED);

    /* SCM_RIGHTS carries an open DESCRIPTION, independently of sender FD.
     * The receiver still holds it after the original descriptor closes. */
    int carrier[2], stream[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, carrier) == 0);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, stream) == 0);
    send_right(carrier[0], stream[0]);
    assert(close(stream[0]) == 0);
    int transferred = recv_right(carrier[1]);
    ping(transferred, stream[1], 'r');

    assert(close(transferred) == 0);
    assert(close(stream[1]) == 0);
    assert(close(carrier[0]) == 0 && close(carrier[1]) == 0);
    assert(close(before_last_close) == 0 && close(before_last_peer) == 0);
    assert(close(linked) == 0 && close(linked_peer) == 0);
    assert(close(renamed) == 0 && close(renamed_peer) == 0);
    assert(close(current) == 0 && close(fresh) == 0);
    assert(close(old_client) == 0 && close(old_accepted) == 0);
    assert(close(old_listener) == 0);
    assert(unlink(alias) == 0);
    assert(unlink(moved) == 0);
    assert(rmdir(dir) == 0);
    puts("[M107 LINUX AF_UNIX ORACLE PASS] unlink/rebind/rename/symlink/dup/SCM_RIGHTS inode lifetime");
    return 0;
}
