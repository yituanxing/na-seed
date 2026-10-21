# M8.2 Process lifecycle extraction

NaOS-inspired incremental ownership split: fork and wait/reap now live in task/process.c, with the original eager MMU fork clone, child-first scheduling behavior, errno and PID lifecycle intact. An arch-owned helper saves the x86 syscall frame and parent fork return register; task/process.c does not inspect saved 15-register fields. A transitional checked user-copy API is defined in mm/usercopy.h, with the existing implementation still in m1.c. Cooperative scheduler, FD syscall adapters, SIGPIPE exit and ELF remain for the next coherent subsystem extraction. No new ISA/algorithm, syscall behavior or preemption claim; requires full Bash/BusyBox/Alpine QEMU regressions.

## Scheduler ownership

- kernel/task/sched.c now owns cooperative syscall-boundary scheduling, wake/readiness checks, replay of blocked read/write/wait, SIGPIPE default child exit and deadlock diagnostics.
- Exactly the existing ready-first policy and the same single-kernel-stack limitation are preserved.
- The x86 saved register-frame indexing and user stack switching are now concentrated in arch/x86_64/context.c.
- kernel/fs/io.h is the explicit transitional interface for the working FD handlers still in m1.c; it is not a new VFS.
- Linux syscall numbers in sched.c are still x86-64 ABI specific. Per-arch ABI adapters remain future work.

All real BusyBox/Bash/Alpine kernel regressions must pass before merging.
