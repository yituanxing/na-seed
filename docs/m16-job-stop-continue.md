# M16 — real process stop/continue and Linux wait4 job notifications

## Driven by real POSIX userspace, not one syscall switch

After M14 establishes session/process-group identity and M15 implements
real kill/termination, the next missing shell job-control class is
**job suspension/resumption**. NaOS keeps session/TTY foreground policy
separate from task-level signal and scheduler mechanics; NaSeed follows
this division without copying NaOS's large signal implementation.

- Linux `SIGSTOP` (19) is unignorable and unmaskable; `SIGCONT` (18)
  restores a stopped task's exact kernel-saved resume contract.
- Stopping a READY task preserves the architecture user PC/registers
  already stored by its task. Stopping a BLOCKED task preserves the
  queued `read` or `wait4`, its user pointers, private MMU and
  pending replay. `SIGCONT` returns it to READY or BLOCKED accordingly.
- Self-stop at the syscall boundary captures the live hardware user
  frame and FPU, then yields to the parent without returning to
  userspace. Continuation resumes immediately after the syscall.
- One-shot child notification statuses are Linux `WIFSTOPPED`,
  `WSTOPSIG(SIGSTOP)` and `WIFCONTINUED`, with
  `wait4(WNOHANG|WUNTRACED|WCONTINUED)`. The synchronous and
  blocked-scheduler wake paths call the **same** readiness predicate.
- No spurious child reaping on stop/continue. SIGKILL can subsequently
  terminate resumed or stopped jobs through existing M15 resource
  teardown. Zombie exit remains final and higher priority.
- No fake `SIGINT`, `SIGTSTP`, handler frames, PTY, termios or
  controlling terminal; interactive shell TTY is a separate capability.

## Multi-ISA acceptance
One source `tests/m16-stop.c` builds with unmodified Linux libc for
amd64, riscv64, arm64. Run under native Linux or QEMU-user first, then
under each full NaSeed kernel via the shared canonical BusyBox runner:
1. Two children in one job group, parked on a real pipe.
2. Send SIGSTOP to the group and get distinct one-shot waitpid stop events.
3. Send SIGCONT and get distinct one-shot continued events.
4. SIGKILL the resumed job and verify signaled exit/reaping.
5. One child stops itself and resumes the same user PC after parent's SIGCONT.
6. Retain **all** Debian static BusyBox 17/17 and native Alpine/musl
   7/7 original binaries. Controlled-source workloads grow 11 to 12.

The new M16 suite is not complete until all three real native QEMU
kernels have passed; Linux-host-only compilation is insufficient.
