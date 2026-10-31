#ifndef NASEED_POLL_WAIT_POLICY_H
#define NASEED_POLL_WAIT_POLICY_H
#include <stdint.h>
#include <stdbool.h>
#include "ops.h"

/* Linux permits polling zero descriptors forever: it is a real signal
 * wait, not an unsupported I/O provider. musl uses ppoll(0, 0, 0, 0)
 * to implement pause() on asm-generic 64-bit architectures. In all cases
 * the syscall remains parked until an unmasked caught signal arrives;
 * blocked Task readiness is checked without touching its user pointers. */
static inline bool nas_poll_signal_only_infinite(enum nas_syscall_op op,
                                                  const uint64_t *a) {
    if(op==NAS_SC_ppoll)return a[2]==0 && a[3]==0;
    if(op==NAS_SC_poll)return a[2]==0 && (int32_t)a[3]<0;
    if(op==NAS_SC_select || op==NAS_SC_pselect6)
        return a[1]==0 && a[2]==0 && a[3]==0 && a[4]==0 && a[5]==0;
    return false;
}
#endif
