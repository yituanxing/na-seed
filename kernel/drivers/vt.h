/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NASEED_VT_H
#define NASEED_VT_H
#include <stdbool.h>
#include <stdint.h>

struct nas_task;

/* Linux VT/KD is a distinct logical terminal: a VirtIO keyboard must never
 * inject bytes into the firmware/UART getty's line discipline. */
int nas_vt_chrdev_init(void);
void nas_vt_init(void);
/* Linux session-leader open without O_NOCTTY may claim an unowned VT. */
void nas_vt_open_session(uint64_t flags);
uint64_t nas_vt_ioctl(uint64_t cmd,uint64_t arg);
uint64_t nas_vt_read_plan(uint64_t requested,bool nonblock);
uint64_t nas_vt_read(uint8_t *dst,uint64_t length,bool nonblock);
uint64_t nas_vt_write(const uint8_t *src,uint64_t length);
bool nas_vt_poll_readable(void);
bool nas_vt_read_waitable(void);
bool nas_vt_read_ready(struct nas_task *task,uint64_t length);
void nas_vt_blocking_read_start(struct nas_task *task);
void nas_vt_session_leader_exit(uint64_t sid);
void nas_vt_process_exit(uint64_t pid);
unsigned nas_vt_take_signal(uint64_t *sid,uint64_t *pgid);

/* Input-core handler: Linux EV_KEY codes from a real keyboard-class device.
 * This is called in a serialized kernel context, never directly from IRQ. */
void nas_tty_vt_key_event(uint16_t code,bool pressed);
#endif
