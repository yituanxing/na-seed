# M64 — per-thread blocked syscall and wait ownership

This architecture cleanup follows the M61–M63 file-object refactors.

```text
nas_task
 ├── identity/session/thread group
 ├── arch CPU context + private kernel stack
 ├── VM root & ELF/heap state; shared group resources via vm_owner
 ├── FD table (fork copies; CLONE_FILES shares), shared nas_open_file objects
 └── wait (per-thread, never inherited when forking)
       ├── blocked syscall ID + four saved arguments + pending
       ├── monotonic deadline & observed TTY VTIME
       ├── poll interest snapshot on 128 shared open-description slots
       └── parked futex key/mask and wake state

task/sched.c  -> selects runnable or nas_task_wait_ready(task),
                 restores the native page-table/CPU/FPU and replays once
task/wait.c   -> tests kernel-owned pipe, UNIX socket, PTY, TTY, eventfd,
                 epoll/poll, wait4, futex and deadline readiness
```

Device-specific wake readiness is no longer an inline policy block inside the
scheduler; it has exactly one owner shared by both syscall-boundary and timer
preemption paths. All **previous** provider calls, errors and timeout semantics
are preserved, including read-vs-write, descriptor sharing above slot 63,
real child wait selection, EINTR and timed Linux syscall replay.

Fork takes copies of inherited VM/FD/signal/session resources but resets its
entire transient wait state. This includes any stale parent poll masks, active
deadline, tty inter-byte timer, blocked I/O syscall, and futex wait key; no
device event is inherited into a new child. Reaping resets the same object.
The host test links the real `kernel/task/wait.c` reset implementation and
checks per-thread isolation while preserving stable process state.

This is **not** a Linux waitqueue scheduler, new kernel C continuation,
SMP work, new ABI feature, or a claim that historical application-specific
M1-M4 test flags were removed (they remain under their old test contract).
After M64 the next growth work can resume genuine external applications
against a clearer Process/Task and FD/FS/VM boundary.
