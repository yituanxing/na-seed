#ifndef NASEED_TTY_LDISC_H
#define NASEED_TTY_LDISC_H
#include <stdint.h>
#include <stdbool.h>
#include "tty.h"
#define NAS_LDISC_CAPACITY 256U
/* Pure terminal byte policy: no MMU, syscall, scheduler or UART. */
struct nas_ldisc { uint8_t bytes[NAS_LDISC_CAPACITY]; unsigned used,pos; bool ready,eof; };
enum nas_ldisc_event { NAS_LDISC_OK, NAS_LDISC_UNSUPPORTED_SIGNAL };
typedef void (*nas_ldisc_echo_fn)(void *context, uint8_t byte);
void nas_ldisc_reset(struct nas_ldisc *ld);
enum nas_ldisc_event nas_ldisc_feed(struct nas_ldisc *ld,const struct nas_termios *t,
                                   uint8_t byte,nas_ldisc_echo_fn echo,void *context);
bool nas_ldisc_readable(const struct nas_ldisc *ld,const struct nas_termios *t,
                        unsigned requested);
unsigned nas_ldisc_peek(const struct nas_ldisc *ld,const struct nas_termios *t,
                        uint8_t *buffer,unsigned length);
void nas_ldisc_commit(struct nas_ldisc *ld,unsigned count);
#endif
