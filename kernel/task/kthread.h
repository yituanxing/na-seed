#ifndef NASEED_KTHREAD_H
#define NASEED_KTHREAD_H
#include <stdbool.h>
#include <stdint.h>
struct nas_wait_queue;
/* Absolute monotonic deadline; zero means wait indefinitely.
 * All outcomes are explicit; a timed-out or canceled waiter is unlinked
 * before its saved C frame may be resumed/reclaimed. */
enum nas_kwait_result { NAS_KWAIT_INVALID=-1, NAS_KWAIT_READY=0,
                        NAS_KWAIT_TIMED_OUT=1 };
/* Generation-qualified handles prevent stale producers waking a recycled
 * worker slot (e.g. a retired TCP/IP worker replaced by a new one). */
struct nas_kthread_handle {
    unsigned slot;
    uint64_t generation;
};
/* Bounded, single-CPU kernel execution contexts. They have a real suspended
 * C stack, no user MM, FDs or process identity. Workers must use kernel
 * addresses only; their callers preserve the selected userspace MM/TLS/FPU.
 * All APIs are serialized with interrupts disabled. Wake must never swap
 * contexts from an IRQ or device callback. */
void nas_kthread_system_init(void);
bool nas_kthread_spawn(void (*run)(void *),void *argument,
                       struct nas_kthread_handle *handle);
unsigned nas_kthread_run(unsigned budget);
bool nas_kthread_yield(void);
bool nas_kthread_park(struct nas_wait_queue *queue);
enum nas_kwait_result nas_kthread_park_until(struct nas_wait_queue *queue,
                                             uint64_t deadline_ns);
unsigned nas_kthread_timers_tick(uint64_t monotonic_ns);
bool nas_kthread_runnable(void);
bool nas_kthread_wake(struct nas_kthread_handle handle);
bool nas_kthread_cancel(struct nas_kthread_handle handle);
unsigned nas_kthread_live(void);
void nas_kthread_boot_selftest(void);
#endif
