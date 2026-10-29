#ifndef NASEED_TTY_LDISC_H
#define NASEED_TTY_LDISC_H
#include <stdint.h>
#include <stdbool.h>
#include "tty.h"
#define NAS_LDISC_CAPACITY 256U
/* Pure terminal byte policy: no MMU, syscall, scheduler or UART. */
struct nas_ldisc { uint8_t bytes[NAS_LDISC_CAPACITY]; unsigned used,pos; bool ready,eof; };
enum nas_ldisc_event { NAS_LDISC_OK, NAS_LDISC_SIGNAL_INT, NAS_LDISC_SIGNAL_QUIT, NAS_LDISC_SIGNAL_STOP };
typedef void (*nas_ldisc_echo_fn)(void *context, uint8_t byte);
void nas_ldisc_reset(struct nas_ldisc *ld);
/* Pure ISIG classification BEFORE CR/NL input mappings. Callers may
 * inspect a control byte even when ICANON is off or a line is already ready.
 * Delivery/lifetime belongs to the owning TTY or PTY, never the ldisc. */
enum nas_ldisc_event nas_ldisc_signal_event(const struct nas_termios *t,
                                            uint8_t byte);
enum nas_ldisc_event nas_ldisc_feed(struct nas_ldisc *ld,const struct nas_termios *t,
                                   uint8_t byte,nas_ldisc_echo_fn echo,void *context);
bool nas_ldisc_readable(const struct nas_ldisc *ld,const struct nas_termios *t,
                        unsigned requested);
unsigned nas_ldisc_peek(const struct nas_ldisc *ld,const struct nas_termios *t,
                        uint8_t *buffer,unsigned length);
void nas_ldisc_commit(struct nas_ldisc *ld,unsigned count);
#endif
