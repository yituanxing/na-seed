# M49 — 48-task bounded registry and real four-way userspace acceptance

NaSeed preserves one common scheduler/Task allocation/Futex/exit/signal
algorithm across x86-64, RV64 and AArch64. Expand only the explicit
single-CPU NAS_TASK_LIMIT to 48; every task maintains its own 16KiB
kernel entry stack with sentinel. Capacity exhaustion still returns real
Linux -EAGAIN, 64 shared open-file descriptions stay a *separate* limit.
No kernel arch fork code or process-specific syscall shim is added.

Acceptance tests are real, unmodified signed GNU Make (j4), GNU GCC/as/ld,
musl, GNU findutils xargs (P4), all running INSIDE real NaSeed QEMU.
Four Make recipes rendezvous before compiling independent ELF files;
four xargs workers rendezvous and execute those files; a GCC-compiled
POSIX program forks TWENTY-FOUR children, blocks all on the same inherited
pipe, then broadcasts 24 bytes and checks each child waitpid status.
That final workload is necessarily impossible with the historical 16-slot
task table even if parallel Make happens to underutilize slots.

CI must preserve M38–M48 and full ARM64/native BusyBox tests on all ISAs.
The current design remains bounded 48 user tasks, 64 global open file
descriptions, no SMP, no general unbounded kernel task allocator. Resource
and scheduling thresholds are not silently claimed to equal Linux.
