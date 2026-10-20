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

## M6.2 — process creation as demanded by real BusyBox ash

QEMU on the untouched M6.1 kernel confirmed the **actual** source of the
next blocker: unmodified upstream Alpine BusyBox ash executing

```sh
/bin/busybox sh -c '/bin/busybox cat /etc/hostname; echo finished'
```

uses Linux x86-64 **fork syscall 57**, not vfork. The previous kernel
returned `-ENOSYS` and ash exited with status 2. This evidence
determined the implementation: native `fork(57)`, `execve(59)`,
`exit/exit_group` and `wait4(61)`, not speculative clone/vfork.

Kernel architecture introduced at this stage:

- Parent and child have distinct monotonic PID values, private eager-copied
  user page tables and memory, preserved TLS (FS base), FXSAVE/FXRSTOR user
  floating-point state, cwd, signal mask and signal actions.
- Descriptor **tables** are inherited independently, while the open-file
  descriptions (reference counts, offsets and status flags) are genuinely
  shared across fork. Child exit drops descriptor references; wait4 reaps
  the zombie and reclaims its private page hierarchy. A failed fork returns
  a Linux error and frees any partially built private memory.
- System-call entry saves **all 15 user GPRs** (including RBX/RBP/R12–R15)
  as a restorable SYSRET frame. The first scheduler is intentionally
  **child-first cooperative**: the child runs to exit and its saved parent
  resumes at the original fork return. The kernel has one syscall stack
  and never switches in the middle of an in-progress kernel call.
  Two user tasks have independent address spaces and overlapping lifetimes,
  but are **not preemptively time-sliced**.
- `wait4` accepts a specific positive child PID or -1 and basic WNOHANG,
  reaps the exited child with Linux `status = exit_code << 8`, returns
  `-ECHILD` when appropriate, and explicitly rejects unsupported
  process groups/rusage and kernel-blocking waits rather than faking them.

QEMU real-software acceptance runs the original ash with three scenarios:
a child `cat` followed by parent `echo finished`; two consecutive
child `echo` invocations followed by a parent command; and a child
reading a file after the parent changed cwd to `/etc`. Each requires
genuine stdout from **both** processes, observed fork+wait4 boundaries
and successful final process exit. The legacy full suite remains active.

**Not yet supported:** concurrent CPU execution, preemptive scheduling,
interleaved parent/child execution, independent kernel stacks with sleeping
syscalls, `clone` threads, `vfork`, job control, `pipe`, or arbitrary
process-group/TTY semantics. A child blocking indefinitely cannot be
rescued by the parent under this cooperative policy. The next true
software-driven step is a pipeline; it will force runnable scheduling,
pipes and kernel sleep/wake semantics instead of just child-first work.

## M6.3 — real anonymous pipe data and BusyBox shell pipeline

Original upstream Alpine ash first exposed Linux `pipe(22)`. The kernel
now creates two real FD endpoints for each pipe (and `pipe2(293)` with
`O_NONBLOCK`/`O_CLOEXEC`), using a bounded 4096-byte FIFO ring, two
independent shared open-file descriptions and normal dup/fork/close
reference accounting. `fstat` reports `S_IFIFO`, `poll` reports
immediate readiness, `read` returns buffered bytes or genuine EOF
when the last writer closes, and `write` transfers bytes or returns
`-EPIPE` once all readers close. Unsupported blocking reads/writes
on an open empty/full pipe return `-EOPNOTSUPP` instead of claiming
successful waiting.

Black-box QEMU exercises **unmodified BusyBox**, not a custom app:
```sh
sh -c 'echo abc | /bin/busybox wc -c'                           # prints 4
sh -c '/bin/busybox cat /etc/hostname | /bin/busybox wc -c'     # prints 7
sh -c 'echo test | /bin/busybox cat | /bin/busybox wc -c'       # prints 5
```
Each test requires exact stdout, the kernel's observed real pipe
allocation, and zero userspace exit status; all M0–M6.2 QEMU regressions
remain mandatory. The explicit `/bin/busybox` invocation matters:
the current tiny CPIO archive does **not** install the usual
`/bin/wc` and `/bin/cat` BusyBox applet symlinks, so bare `wc`
would fail pathname resolution despite correct pipes.

**Boundary:** these small pipelines can finish using child-first
cooperative scheduling because each producer fits in the pipe ring.
They are not proof of full simultaneous streaming or blocking/wakeup.
A real larger input (e.g. `/bin/busybox cat /bin/busybox | ...`)
will require a scheduler that can yield a blocked producer/consumer,
reschedule another runnable process and deliver the eventual read/write
result, plus proper deadlock detection. Do not introduce a pretend
sleep/wakeup implementation or claim preemptive scheduling.
