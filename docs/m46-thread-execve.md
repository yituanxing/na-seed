# M46 — genuine Linux64 thread-group execve on amd64 / RV64 / AArch64

The syscall execve from a process with other CLONE_THREAD peers replaces
the entire address space atomically, **not** return -EOPNOTSUPP. The old
VM and all siblings survive every failed ELF/argv/map stage. A successful
ELF commit first installs the old root, performs robust owner-death and
clear_child_tid cleanup on all other members and releases their task slots.
Only then may it reclaim the old root. The shared file table remains intact
except O_CLOEXEC. No SIGCHLD or zombie from the peer teardown.

Linux de_thread on **nonleader execve** is especially important: the
new image inherits the **original TGID** so the parent waits on its
original process PID. The surviving worker adopts the original leader's
FD table (without incrementing shared file-description refs), VM ownership,
cwd, umask and signal dispositions, then discards the retired leader.
This is done in one common kernel commit path, not three arch paths.

The native host-LLVM freestanding acceptance forks a child, clones a
true worker then execs a separate true ELF twice (leader and nonleader).
The worker scenario first tries a deliberately missing ELF (-ENOENT,
without destroying peer), then replaces the image while its leader sleeps
in a FUTEX_WAIT. The target checks getpid==gettid, reset robust list and
exits successfully. The parent uses real wait4 on the original fork PID.
The guest next executes the unchanged signed Alpine zstd -T2 across all
three architectures. Preserve M38–M45 and full ARM64/userspace CI.

Limits: does not claim SMP, signal-delivery parity across all clone flags,
userspace cancellation, or POSIX-complete ptrace/exec interlocks.
