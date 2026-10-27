# M44 — Linux thread group exit and stable process ownership

The original signed Alpine zstd and musl pthread runtime remain the
primary real-software workload. Add a small host-LLVM-compiled Linux syscall
ABI stress fixture solely to force two otherwise timing-dependent edge cases.

1. Nonleader `exit_group(37)` must terminate every READY/BLOCKED worker,
   clear worker TIDs and futex-wake in the correct VM, close the descriptor
   table exactly once, leave exactly one waitable process-group leader,
   and allow the parent to read wait4 status 37. Repeat three times for
   group-exit semantics, then execute 72 consecutive thread-local exits
   to prove the 48-slot Task table and shared MM/FD owners are reclaimed
   only after switching off each terminating native kernel C stack.
2. Group leader `exit(23)` must NOT terminate its worker or close shared
   open files, nor let the parent wait4 prematurely reclaim the VM. An
   independent worker performs a real nanosleep, then writes an immutable
   marker to stdout, uses thread-local `exit(0)`, and causes the group's
   original waitable leader to appear with preserved status 23.
3. Fork from a nonleader thread and let it exit before the new process:
   the stable group leader must successfully wait4 the process using its
   TGID-based PPID, not the worker's now-retired TID.
4. Boot the **same** freestanding native ELF directly as PID 1, not as a
   shell child: its CLONE_THREAD workers inherit PPID 0. Eight worker-local
   SYS_exit operations must leave the PID 1 leader alive for subsequent
   work. Another worker then issues exit_group(0), which alone shuts down
   the PID 1 process. Both independent native stdout markers are required
   before any harness-reported kernel exit PASS.
5. Boot a second variant of the same ELF directly as PID 1: its leader
   calls SYS_exit(0) while another pthread is sleeping in a REAL
   nanosleep. The kernel must defer process-finalization until the worker
   wakes, prints `M44_PID1_WORKER_AFTER_LEADER_PASS`, then exits itself.
   Only the last worker may drive clean QEMU shutdown (with its leader's
   preserved status); premature host debug-exit is a test failure.
6. After those cases, run the *unchanged* signed Alpine original zstd -T2
   two-pass compression and byte comparison on amd64, RV64, and ARM64.

The VM owner (leader) survives while live threads exist. Group teardown
is centralized in kernel/task/sched.c: it clears all thread TIDs in the
shared VM, releases abandoned task slots, and finalizes one leader so
the parent wait4 reclaims a single root. All three archs use the exact
same thread-group code, test fixture and signed package rootfs.

Limitations deliberately NOT claimed: POSIX-full kill/tgkill delivery,
robust-futex owner death, thread-specific alternate signal stacks, SMP,
asynchronous cancellation or all Linux prctl thread-group features.
