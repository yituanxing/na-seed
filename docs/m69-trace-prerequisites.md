# M69: strace prerequisites proven by genuine Linux userland

Strace's dependency graph is not a checklist of syscall numbers. The real
program requires tracer/tracee relationships, PTRACE_SEIZE/TRACEME,
PTRACE_SYSCALL enter/exit stops, ptrace-aware wait4 event status,
GETREGSET native register images, and safe remote-memory reads.
NaSeed does **not** implement ptrace yet.

This milestone intentionally precedes ptrace with reusable Linux abilities:
1. /proc/TGID/task/TID namespace and thread-local stat/status/comm,
   backed by the canonical Task registry, not a synthetic thread table.
   The per-open-file description carries both TGID/TID across open/dup/fork.
2. Genuine original procps-ng `ps -eL` enumerates thread identities;
   original psmisc `pstree -p` traverses actual parent/child relationships.
3. Genuine original util-linux `taskset -p` and `prlimit --nofile`
   interrogate existing scheduling affinity/resource limits.
4. All three native QEMU architectures and M38–M68 regressions must pass.

Future *true* ptrace owner shall be `Task` (trace relationship/stopped
state/event delivery), `scheduler` (enter/exit stop without advancing
tracee), `process/wait` (wait4 __WALL / PTRACE_EVENT), `mm/usercopy`
(remote task address space and robust bounds), and each ISA's `arch`
(native regset UAPI). Do not put Linux-specific ptrace numbers in VFS or
copy CPU backends into common Task logic.

Important: `ps -eL` on a one-thread process cannot prove thread lifecycle.
The host Task fixture specifically contains two TIDs and validates both
nested /proc/task entries and per-TID comm/stat; native QEMU also validates
original Linux software against the namespace. A live pthread guest fixture
is a follow-on stronger validation, not silently assumed.

## Native failures caught in first real prebuilt run

The actual three-ISA integration found original util-linux `prlimit -p 1
--nofile` receiving `EPERM` despite running as the same single supported
root user. This is not ptrace authorization: NaSeed's read-only RLIMIT values
are enforced kernel-wide constants (NOFILE=NAS_FD_LIMIT). Permit lookup
of an existing PID when only reading limits; unknown PIDs still return
`ESRCH` and mutable resource limits remain `EOPNOTSUPP`. When a real
credential/access subsystem exists, its centralized authorization will
replace this simplified all-root policy. No fabricated limit modification.
