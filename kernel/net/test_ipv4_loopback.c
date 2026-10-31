/* Host contract test for the real single-core IPv4 loopback backend.
 * Compile with kernel/net/ipv4_loopback.c and the production kernel/pipe.c;
 * only the open-description finalizer and ops lookup are test fixtures. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "ipv4_loopback.h"
#include "../fd.h"
#include "../pipe.h"
#include "../errno.h"
#include "../fs/file_ops.h"

struct nas_open_file nas_open_files[NAS_OPEN_FILE_LIMIT];
static const struct nas_file_ops socket_ops={0};
static unsigned releases;
const struct nas_file_ops *nas_fs_ops_for_kind(uint8_t kind) {
    assert(kind==FD_SOCKET);
    return &socket_ops;
}
void nas_print(const char *s) { (void)s; }
void nas_dec(uint64_t value) { (void)value; }
void nas_fs_drop_kernel_description(unsigned slot) {
    assert(slot<NAS_OPEN_FILE_LIMIT && nas_open_files[slot].refcount==1);
    nas_open_files[slot].refcount=0;
    nas_ipv4_socket_release(slot);
    nas_pipe_release_description(slot);
    releases++;
}
static void open_socket(unsigned slot) {
    assert(!nas_open_files[slot].refcount);
    nas_open_files[slot].refcount=1;
    nas_open_files[slot].ops=&socket_ops;
    nas_ipv4_socket_init(slot);
}
static void send_byte(unsigned from,unsigned to,uint8_t expected) {
    assert(nas_pipe_write_amount(from,1,false)==1);
    nas_pipe_write_byte(from,0,expected);
    nas_pipe_write_commit(from,1);
    assert(nas_pipe_read_amount(to,1,false)==1);
    assert(nas_pipe_read_byte(to,0)==expected);
    nas_pipe_read_commit(to,1);
}
int main(void) {
    const struct nas_ipv4_endpoint service={NAS_IPV4_LOOPBACK,15411};
    open_socket(1); /* listener */
    open_socket(2); /* client */
    assert(nas_ipv4_socket_bind(1,service)==0);
    assert(nas_ipv4_socket_listen(1,2)==0);
    assert(nas_ipv4_socket_listening(1));
    assert(!nas_ipv4_socket_readable(1));
    open_socket(3);
    assert(nas_ipv4_socket_bind(3,(struct nas_ipv4_endpoint){NAS_IPV4_ANY,15411})
           ==-EADDRINUSE);
    assert(nas_ipv4_socket_bind(3,
           (struct nas_ipv4_endpoint){UINT32_C(0x01020304),15411})
           ==-EADDRNOTAVAIL);
    assert(nas_ipv4_socket_connect(3,
           (struct nas_ipv4_endpoint){UINT32_C(0x08080808),15411})
           ==-ENETUNREACH);
    assert(nas_ipv4_socket_connect(3,
           (struct nas_ipv4_endpoint){NAS_IPV4_LOOPBACK,15555})
           ==-ECONNREFUSED);
    assert(nas_ipv4_socket_connect(2,service)==0);
    assert(nas_ipv4_socket_readable(1));
    assert(nas_ipv4_socket_connect(2,service)==-EISCONN);
    unsigned accepted=NAS_OPEN_FILE_LIMIT;
    assert(nas_ipv4_socket_accept(1,&accepted)==0);
    assert(accepted<NAS_OPEN_FILE_LIMIT && accepted!=2);
    assert(!nas_ipv4_socket_readable(1));
    assert(nas_ipv4_socket_accept(1,&accepted)==-EAGAIN);
    struct nas_ipv4_endpoint client_local,remote,accepted_local,accepted_peer;
    assert(nas_ipv4_socket_name(2,false,&client_local)==0);
    assert(client_local.address==NAS_IPV4_LOOPBACK &&
           client_local.port>=32768 && client_local.port<=60999);
    assert(nas_ipv4_socket_name(2,true,&remote)==0);
    assert(remote.address==service.address && remote.port==service.port);
    assert(nas_ipv4_socket_name(accepted,false,&accepted_local)==0);
    assert(accepted_local.port==service.port);
    assert(nas_ipv4_socket_name(accepted,true,&accepted_peer)==0);
    assert(accepted_peer.port==client_local.port);
    send_byte(2,accepted,'Q');
    send_byte(accepted,2,'R');

    /* The listener's final close disposes unaccepted kernel-owned peers,
     * while already-accepted endpoint identities remain live. */
    open_socket(4);
    assert(nas_ipv4_socket_connect(4,service)==0);
    assert(nas_ipv4_socket_readable(1));
    unsigned prior_releases=releases;
    nas_fs_drop_kernel_description(1);
    assert(releases==prior_releases+2); /* listener + pending endpoint */
    assert(!nas_ipv4_socket_active(1));
    assert(nas_ipv4_socket_active(accepted));
    send_byte(2,accepted,'S');
    /* Real socket SHUT_WR is a one-way EOF: opposite direction stays live
     * after the peer has consumed every buffered byte. */
    assert(nas_pipe_socket_shutdown(2,1)==0);
    assert(nas_pipe_socket_write_shutdown(2));
    assert(nas_pipe_read_amount(accepted,1,false)==0);
    send_byte(accepted,2,'T');
    assert(nas_pipe_socket_shutdown(2,0)==0);
    assert(nas_pipe_socket_read_shutdown(2));
    assert(nas_pipe_socket_shutdown(2,2)==0); /* idempotent */
    nas_fs_drop_kernel_description(2);
    nas_fs_drop_kernel_description(accepted);
    nas_fs_drop_kernel_description(3);
    nas_fs_drop_kernel_description(4);
    /* SO_REUSEADDR permits shared pre-listen bind, not two active TCP
     * listeners for one endpoint (that would require SO_REUSEPORT). */
    open_socket(10);
    open_socket(11);
    bool reuse=true;
    assert(nas_ipv4_socket_reuseaddr(10,true,&reuse)==0 && reuse);
    assert(nas_ipv4_socket_reuseaddr(11,true,&reuse)==0 && reuse);
    const struct nas_ipv4_endpoint shared={NAS_IPV4_ANY,15444};
    assert(nas_ipv4_socket_bind(10,shared)==0);
    assert(nas_ipv4_socket_bind(11,shared)==0);
    assert(nas_ipv4_socket_listen(10,2)==0);
    assert(nas_ipv4_socket_listen(11,2)==-EADDRINUSE);
    nas_fs_drop_kernel_description(10);
    nas_fs_drop_kernel_description(11);
    /* Standalone AF_UNIX-style socketpair reuses the same production Pipe:
     * shutdown is per shared open description, not tied to AF_INET state. */
    nas_open_files[40].refcount=1;
    nas_open_files[41].refcount=1;
    assert(nas_pipe_create_socketpair(40,41)==0);
    send_byte(40,41,'U');
    assert(nas_pipe_socket_shutdown(40,1)==0);
    assert(nas_pipe_read_amount(41,1,false)==0); /* EOF */
    send_byte(41,40,'V');
    assert(nas_pipe_socket_shutdown(40,0)==0);
    assert(nas_pipe_write_amount(41,1,false)==-EPIPE);
    nas_pipe_release_description(40);
    nas_pipe_release_description(41);
    nas_open_files[40].refcount=0;
    nas_open_files[41].refcount=0;
    /* Plain unnamed pipes do not inherit socket shutdown flags. */
    nas_open_files[42].refcount=1;
    nas_open_files[43].refcount=1;
    assert(nas_pipe_create(42,43)==0);
    send_byte(43,42,'W');
    nas_pipe_release_description(43);
    nas_open_files[43].refcount=0;
    assert(nas_pipe_read_amount(42,1,false)==0);
    assert(!nas_pipe_socket_read_shutdown(42));
    nas_pipe_release_description(42);
    nas_open_files[42].refcount=0;
    printf("M141_IPV4_LOOPBACK_HOST_PASS\n");
    return 0;
}
