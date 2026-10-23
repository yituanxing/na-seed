# M45 — robust futexes on three native NaSeed Linux64 kernels

The kernel implements the native LP64 Linux `set_robust_list` and
`get_robust_list` TID-scoped ABI, including exactly 24-byte user list
heads, per-thread registration, fork/clone/exec reset and same-thread-group
TID inspection with checked user pointers. On thread death, walk the
untrusted bounded list and `list_op_pending` using the native signed
`futex_offset`, mark a lock owned by the deceased TID
`(old & FUTEX_WAITERS) | FUTEX_OWNER_DIED`, and wake one Linux futex
waiter where WAITERS was set. Clean this up **before** clear_child_tid
for ordinary thread exit and for every peer in exit_group, including
the early-dead group leader from M44. No fabricated success for
PI futex locking.

An upstream LLVM host-compiled freestanding native Linux ELF stresses:
- size=24, wrong size (-EINVAL), invalid output (-EFAULT) and per-TID retrieval
- a worker-held robust futex, another thread genuinely WAITs; exiting owner
  marks OWNER_DIED and wakes it; clear_child_tid observed
- list_op_pending lock not on linked list, plus a two-node linked list
- three native x86-64 / RV64 / ARM64 guest QEMU executions

Then run the unchanged signed original Alpine Zstandard -T2 binary and
two real 12MiB compress/decompress checks. Preserve M38–M44 and
full userspace/ARM64 regressions. Merge only after all exact-head CI green.

Boundaries: single-core, VM-local futex queues; PI futexes, user PI nodes,
cross-process shared futex MM and fully general robust fault recovery
remain explicitly outside scope. Not a claim of complete Linux pthread
or SMP semantics.
