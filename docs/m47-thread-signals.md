# M47 — Linux TID-directed signals with group-fatal semantics

`tkill` (x86 200, asm-generic 130) and `tgkill` (x86 234,
generic 131) resolve exact TIDs rather than process leaders, enforce
a valid, live TID and positive TGID agreement, and return standard Linux
ESRCH/EINVAL errors without fabricated success.

Caught or blocked standard signals are queued on the chosen TID only,
leaving all other thread pending masks unchanged. Default-fatal
SIGKILL and SIGTERM terminate the entire TGID, even when aimed at a
single worker; they reuse M44 group cleanup including Robust Futex
and clear_child_tid against the correct shared page table, and expose
a *single* waitable process zombie with native wait4 WIFSIGNALED.

`kill` now selects process leaders, not TIDs, and remote default
termination uses the shared group exit path (not per-thread exit).

Host-LLVM-compiled freestanding exact Linux ABI program validates
tgkill/tkill sig=0, stale TID, bad TGID, blocked per-worker SIGUSR1
pending (not visible to leader), futex/clear-child-TID wake and
the parent's fatal SIGKILL wait4 status. The same native three-ISA
QEMU run then executes original signed Alpine Zstd with -T2. Keep
M38–M46, ARM64 and native userspace regression gates.

Limitations: only the existing standard signal handler/user-return
support; realtime queued payloads, nested handlers, PI futex,
multicore interprocessor delivery and ptrace are not claimed.
