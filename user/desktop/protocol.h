#ifndef NASEED_DESKTOP_PROTOCOL_H
#define NASEED_DESKTOP_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>

/* Private userspace M167 protocol. Transport is an ordinary Linux AF_UNIX
 * stream; one shareable memfd surface is transferred with SCM_RIGHTS at
 * READY, then bounded fixed-size change/input messages travel over IPC.
 * Server and clients are independent native ELF processes. */
enum { SCREEN_W=800, SCREEN_H=600, TITLE_H=24, DEVICE_LIMIT=16,
       IMAGE_W=300, IMAGE_H=220, TERM_W=330, TERM_H=250,
       MSG_READY=1, MSG_KEY=2, MSG_UPDATED=3, MSG_QUIT=4 };
enum { IMAGE=0, TERMINAL=1, WINDOW_COUNT=2 };
struct message { uint32_t type, key, value, reserved; };
_Static_assert(sizeof(struct message)==16,"atomic desktop wire packet");
void nasd_fail(const char *);
void nasd_mark(const char *);
void nasd_require(int,const char *);
void nasd_write_all(int,const void *,size_t);
void nasd_read_all(int,void *,size_t);
void nasd_send_packet(int,uint32_t,uint32_t);
struct message nasd_recv_packet(int);
void nasd_send_surface(int,int,uint32_t);
int nasd_receive_surface(int,int);
uint8_t *nasd_make_surface(const char *,int,int,int *);
void nasd_fill_surface(uint8_t *,int,int,uint8_t,uint8_t,uint8_t);
#endif
