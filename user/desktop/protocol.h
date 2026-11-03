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
       MSG_READY=1, MSG_KEY=2, MSG_UPDATED=3, MSG_QUIT=4,
       MSG_CLOSE=5, MSG_POINTER=6 };
enum { IMAGE=0, TERMINAL=1, BROWSER=2, LVGL=3, WINDOW_COUNT=2 };
#define NASD_SOCKET_PATH "/tmp/naseed-desktop.sock"
#define NASD_SOCKET_ENV "NASEED_DESKTOP_SOCKET"
#define NASD_PROTOCOL_ENV "NASEED_DESKTOP_PROTOCOL"
#define NASD_INHERITED_ENV "NASEED_DESKTOP_INHERITED_FD"
enum { NASD_INPUT_KEYS=1U, NASD_INPUT_POINTER=2U };
struct message { uint32_t type, key, value, reserved; };
_Static_assert(sizeof(struct message)==16,"atomic desktop wire packet");
/* The kind in MSG_READY is application metadata, never a window slot.
 * The compositor assigns each accepted connection an independent slot. */
struct nasd_surface_offer {
    struct message packet;
    unsigned received;
    int surface_fd;
};
struct nasd_packet_reader {
    struct message packet;
    unsigned received;
};
void nasd_fail(const char *);
void nasd_mark(const char *);
void nasd_require(int,const char *);
void nasd_write_all(int,const void *,size_t);
void nasd_read_all(int,void *,size_t);
void nasd_send_packet(int,uint32_t,uint32_t);
void nasd_send_pointer(int,unsigned,unsigned,int);
struct message nasd_recv_packet(int);
void nasd_send_surface(int,int,uint32_t,unsigned,unsigned);
void nasd_send_surface_caps(int,int,uint32_t,unsigned,unsigned,uint32_t);
int nasd_receive_surface(int,int,unsigned *,unsigned *);
/* Incremental, nonblocking receive for untrusted external connections:
 * 1=complete validated offer, 0=more bytes needed, -1=bad/disconnected.
 * The caller owns and must close offer.surface_fd after a successful offer. */
void nasd_surface_offer_init(struct nasd_surface_offer *);
void nasd_surface_offer_release(struct nasd_surface_offer *);
int nasd_surface_offer_step(int,struct nasd_surface_offer *,uint32_t *,
                            unsigned *,unsigned *,uint32_t *);
int nasd_packet_step(int,struct nasd_packet_reader *,struct message *);
/* Legacy launched clients inherit connected channel 3; independent clients
 * discover the running compositor through a standard AF_UNIX pathname. */
int nasd_client_channel(void);
uint8_t *nasd_make_surface(const char *,int,int,int *);
void nasd_fill_surface(uint8_t *,int,int,uint8_t,uint8_t,uint8_t);
#endif
