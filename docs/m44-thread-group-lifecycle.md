# M44 — Linux thread group exit and stable process ownership

The original signed Alpine zstd and musl pthread runtime remain the
primary real-software workload. Add a small host-LLVM-compiled Linux syscall
ABI stress fixture solely to force two otherwise timing-dependent edge cases.

1. Nonleader `exit_group(37)` must terminate every READY/BLOCKED worker,
   clear worker TIDs and futex-wake in the correct VM, close the descriptor
   table exactly once, leave exactly one waitable process-group leader,
   and allow the parent to read wait4 status 37. Repeat THREE times to
   expose task slot exhaustion and accidental root double free.
2. Group leader `exit(23)` must NOT terminate its worker or close shared
   open files, nor let the parent wait4 prematurely reclaim the VM. An
   independent worker performs a real nanosleep, then writes an immutable
   marker to stdout, uses thread-local `exit(0)`, and causes the group's
   original waitable leader to appear with preserved status 23.
3. After both cases, run the *unchanged* signed Alpine original zstd -T2
   two-pass compression and byte comparison on amd64, RV64, and ARM64.

The VM owner (leader) survives while live threads exist. Group teardown
is centralized in kernel/task/sched.c: it clears all thread TIDs in the
shared VM, releases abandoned task slots, and finalizes one leader so
the parent wait4 reclaims a single root. All three archs use the exact
same thread-group code, test fixture and signed package rootfs.

Limitations deliberately NOT claimed: POSIX-full kill/tgkill delivery,
robust-futex owner death, thread-specific alternate signal stacks, SMP,
asynchronous cancellation or all Linux prctl thread-group features.
