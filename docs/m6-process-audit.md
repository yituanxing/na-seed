# M6 process management — NaOS history, NaSeed ownership audit

## Source-first comparison (2026-10-20)

Repositories inspected:

- `yituanxing/naos`: the userspace/rootfs orchestration repository; **not**
  the implementation of the NaOS task scheduler.
- `yituanxing/na-kernel`: actual process, memory and syscall implementation.

The early **2025-04-26** NaOS task code at
`na-kernel@e50f4a900c9:kernel/src/task/task.h` already grouped PID/PPID,
task state, kernel stack, arch context, cwd and FD table, with a simple
task array. The **2025-04-27** state
`na-kernel@897896ab37e` had fork/exec/exit/wait interfaces;
the actual implementations needed multiple fixes shortly afterward.
The 2025-06-09 commit `9608f0eb` introduced `vfork` as *untested*
and subsequent July commits revised it. By October it needed fixes for
forked VMAs, file sharing and syscall return paths. The current NaOS has
far more machinery (`task/task_struct.h`, `task/task_syscall.c`,
`task/wait.c`, `task/sched.c`, x86-64 `fork.S`/`switch.S`), built
incrementally. Copying that final-scale scheduler into NaSeed now would
obscure the real BusyBox driver and duplicate complexity without evidence.

## Present NaSeed baseline and the concrete issue

M5.4 genuinely runs `execve` inside **one user task**. Until M6,
`root_phys`, ELF mappings, brk/mmap, TLS, signal actions, cwd and fd
descriptors were scattered file-static state in `kernel/m1.c`.
Attempting to switch tasks would silently mix parent/child state.

M6.1 introduces `kernel/task.h`, `kernel/task.c`, a small fixed-size
task registry and a current-task pointer. Process-state reads/writes now
refer to the owner task. The boot task is PID 1, PPID 0, RUNNING; `getpid`,
`gettid`, `getppid`, `getpgrp` and `set_tid_address` use actual
current-task identity, not literal numbers. This is a **structural**
prerequisite; only one slot is currently active. Existing live BusyBox
ash `echo $$` and all prior QEMU tests act as regression witnesses.

## Next verified slice: two real tasks, not a fake fork

The next acceptance driver is the authentic Alpine BusyBox command:

```sh
sh -c '/bin/busybox cat /etc/hostname; echo finished'
```

Expected stdout: `naseed`, then `finished`.

A correct implementation has to provide:

1. A **separate kernel stack and saved user trap frame for each task**.
   Current x86-64 `arch.S` uses one global `kernel_stack_top` and
   `syscall_user_rsp`; moving PID alone does not permit context switching.
2. Task-local CR3, heap/mmap and TLS/FS register state, restore on switch.
3. Linux `fork` or `clone` semantics matching the actual ash syscall
   path; verify the recorded syscall/flags, don't guess `vfork`.
   Initially clone actual pages for `fork` correctness. COW is an
   optimization to evaluate later.
4. Correct duplicated **descriptor table vs shared open-file description**
   semantics. Parent/child should inherit offsets and CLOEXEC flags
   correctly; cwd and signal mask are inherited, and independent state
   requires correct lifetime/refcounts.
5. Parent BLOCKED/wakeup on child exit, child's ZOMBIE state and
   `wait4` status encoding/reaping. Ensure the parent executes its
   second `echo` only once the child has exited.
6. A minimal cooperative runnable selection before deciding whether
   real workloads need timer preemption or additional scheduler policy.

Don't publish success from `fork` returning a PID without running a
child; don't claim that `execve` creates a second process. Do not
introduce process groups, a generic scheduler, COW or `pipe` solely
to imitate later NaOS history. The shell pipeline is a *separate*
userspace driver once fork/wait works.

## Engineering controls

- Preserve all original M0–M5.4 QEMU cases unchanged.
- QEMU must verify external-program output *and* parent continuation
  with separate PID/trap contexts.
- Check negative `fork` errors, `wait4` with no children, and
  address-space/FD lifetime on repeated invocations.
- CI result without the expected actual user stdout is not acceptance.
