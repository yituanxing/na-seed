#ifndef NASEED_FS_UNIX_SOCKET_H
#define NASEED_FS_UNIX_SOCKET_H
#include <stdbool.h>
#include <stdint.h>
/* In-kernel AF_UNIX SOCK_STREAM endpoint ownership. The actual byte queues
 * are the same refcounted bidirectional pipes used by socketpair(). */
void nas_unix_socket_init(unsigned slot);
void nas_unix_socket_release(unsigned slot);
int nas_unix_socket_bind(unsigned slot,const char *path,uint32_t mode);
int nas_unix_socket_listen(unsigned slot,unsigned backlog);
int nas_unix_socket_connect(unsigned slot,const char *path);
int nas_unix_socket_accept(unsigned slot,unsigned *peer_slot);
bool nas_unix_socket_listening(unsigned slot);
bool nas_unix_socket_readable(unsigned slot);
const char *nas_unix_socket_name(unsigned slot);
#define NAS_UNIX_MAX_RIGHTS 12U
bool nas_unix_rights_available(unsigned sender,unsigned count);
int nas_unix_rights_send(unsigned sender,const uint8_t *kinds,
                         const uint8_t *slots,unsigned count);
unsigned nas_unix_rights_count(unsigned receiver);
unsigned nas_unix_rights_receive(unsigned receiver,uint8_t *kinds,
                                  uint8_t *slots,unsigned max_count);
#endif
