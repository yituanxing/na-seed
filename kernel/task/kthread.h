#ifndef NASEED_KTHREAD_H
#define NASEED_KTHREAD_H
#include <stdbool.h>
#include <stdint.h>
struct nas_wait_queue;
/* The IRQ-backed ext4 adapter executes upstream filesystem routines and
 * their nested block callbacks on a worker C stack. It must have room for
 * ext4's local bookkeeping plus block-partition temporary sectors. */
#if defined(NAS_ENABLE_M179_FS_IRQ) && NAS_ENABLE_M179_FS_IRQ
#define NAS_KTHREAD_STACK_BYTES 16384U
#else
#define NAS_KTHREAD_STACK_BYTES 8192U
#endif
/* Three persistent lwIP kernel contexts (TCP/IP, NIC receive and Netconn)
 * coexist with filesystem I/O workers. Keep headroom for both concurrent
 * ext4 workers and a spare subsystem context; all slots remain bounded. */
#define NAS_KTHREAD_LIMIT 8U
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
/* Observable deadline state for M97 native timeout correctness; not a
 * necessary condition for entering IRQ-aware scheduler idle. */
bool nas_kthread_deadlines_armed(void);
bool nas_kthread_runnable(void);
/* True only while executing a real scheduler-owned kernel C continuation.
 * Callers may use this to reject parking from early boot or a user syscall
 * (where no kernel worker owns the current C stack). */
bool nas_kthread_in_worker(void);
/* A driver-owned worker may distinguish its own reentrant filesystem call
 * from a different (e.g. lwIP) worker while all run on kernel C stacks. */
bool nas_kthread_is_current(struct nas_kthread_handle handle);
bool nas_kthread_wake(struct nas_kthread_handle handle);
bool nas_kthread_cancel(struct nas_kthread_handle handle);
unsigned nas_kthread_live(void);
void nas_kthread_boot_selftest(void);
#endif
