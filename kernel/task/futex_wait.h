#ifndef NASEED_FUTEX_WAIT_H
#define NASEED_FUTEX_WAIT_H
#include <stdint.h>
#include <stdbool.h>
struct nas_task;
struct nas_mm;
/* Single-CPU non-PI futex wait ownership. The task retains its MM ref. */
bool nas_futex_wait_arm(struct nas_task *);
void nas_futex_wait_cancel(struct nas_task *);
unsigned nas_futex_wait_wake(struct nas_mm *,uint64_t,unsigned,uint32_t);
unsigned nas_futex_wait_requeue(struct nas_mm *,uint64_t,uint64_t,unsigned);
#endif
