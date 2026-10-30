#ifndef NASEED_FS_UNIX_SOCKET_H
#define NASEED_FS_UNIX_SOCKET_H
#include <stdbool.h>
#include <stdint.h>
#include "vfs.h"
/* In-kernel AF_UNIX SOCK_STREAM endpoint ownership. The actual byte queues
 * are the same refcounted bidirectional pipes used by socketpair(). */
void nas_unix_socket_init(unsigned slot);
void nas_unix_socket_pair_ready(unsigned first,unsigned second);
void nas_unix_socket_release(unsigned slot);
/* Bind through one VFS-owned parent Path; the socket open description keeps
 * the returned positive inode Path until its FINAL close. Display text is
 * a sockaddr snapshot only, never a lookup key. */
int nas_unix_socket_bind(unsigned slot,const struct nas_vfs_path *parent,
                         const char *leaf,uint32_t mode,
                         const char *display_name);
int nas_unix_socket_listen(unsigned slot,unsigned backlog);
/* target is a BORROWED positive Path held by the syscall namei owner. */
int nas_unix_socket_connect(unsigned slot,const struct nas_vfs_path *target);
int nas_unix_socket_accept(unsigned slot,unsigned *peer_slot);
bool nas_unix_socket_listening(unsigned slot);
bool nas_unix_socket_readable(unsigned slot);
const char *nas_unix_socket_name(unsigned slot);
/* Linux SO_PEERCRED: connection-time credentials, never the caller's. */
int nas_unix_socket_peercred(unsigned slot,uint32_t credentials[3]);
#define NAS_UNIX_MAX_RIGHTS 12U
bool nas_unix_rights_available(unsigned sender,unsigned count);
int nas_unix_rights_send(unsigned sender,const uint8_t *kinds,
                         const uint8_t *slots,unsigned count);
unsigned nas_unix_rights_count(unsigned receiver);
unsigned nas_unix_rights_receive(unsigned receiver,uint8_t *kinds,
                                  uint8_t *slots,unsigned max_count);
#endif
