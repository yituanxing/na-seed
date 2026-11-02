#ifndef NASEED_DESKTOP_PROTOCOL_H
#define NASEED_DESKTOP_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>

/* Private userspace desktop protocol. A client advertises an explicit
 * dimensioned surface over an ordinary Linux AF_UNIX stream, with one memfd
 * descriptor transferred by SCM_RIGHTS. Desktop owns policy/placement and
 * bounds; clients independently own their pixels and processes. */
enum { SCREEN_W=800, SCREEN_H=600, TITLE_H=24, DEVICE_LIMIT=16,
       IMAGE_W=300, IMAGE_H=220, TERM_W=330, TERM_H=250,
       BROWSER_W=310, BROWSER_H=190, WINDOW_LIMIT=8,
       MSG_READY=1, MSG_KEY=2, MSG_UPDATED=3, MSG_QUIT=4 };
enum { IMAGE=0, TERMINAL=1, BROWSER=2, WINDOW_COUNT=2 };
struct message { uint32_t type, key, value, reserved; };
_Static_assert(sizeof(struct message)==16,"atomic desktop wire packet");
void nasd_fail(const char *);
void nasd_mark(const char *);
void nasd_require(int,const char *);
void nasd_write_all(int,const void *,size_t);
void nasd_read_all(int,void *,size_t);
void nasd_send_packet(int,uint32_t,uint32_t);
struct message nasd_recv_packet(int);
void nasd_send_surface(int,int,uint32_t,unsigned,unsigned);
int nasd_receive_surface(int,int,unsigned *,unsigned *);
uint8_t *nasd_make_surface(const char *,int,int,int *);
void nasd_fill_surface(uint8_t *,int,int,uint8_t,uint8_t,uint8_t);
#endif
