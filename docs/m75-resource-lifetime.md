# M7.5 — real BusyBox resource lifecycle acceptance

M7.0–M7.4 made ownership boundaries explicit: `task`, `fd`,
`pipe`, `tmpfs`, `bootfs`, `path` and Limine/CPIO boot handoff.

Those merges passed each finite functional scenario. This milestone
targets the **next observed need**, not another mechanical file split:
many *consecutive* cycles in one boot must recycle PID/task slots,
private fork address spaces, shared open descriptions, pipe endpoints
and tombstoned RAM inodes without accumulating a leak or reviving
closed pipe writers.

The `scripts/probe-ash-resources.sh` QEMU gate uses the original
checksum-pinned Alpine musl BusyBox and genuine `sh` applet.
Five independent fresh boots exercise:

1. 12 three-stage pipelines (`fork/execve/wait4/pipe/close`).
2. 12 scratch-file create + pipe + remove cycles.
3. 12 subshell forks writing tmpfs followed by parent file reads.
4. 10 unlink-while-open cycles with path replacement and final-close
   reuse (original inode must remain accessible through old FD).
5. 8 repeated early-close `yes | head -c 2` SIGPIPE cycles.

Checks require exact stdout counts, the final marker and actual
userspace exit 0. Full M0–M7.4 historical QEMU, Linux ABI and freestanding
compile/link remain required. Never fake success in kernel.

Audit findings to revisit as usage grows: no orphan reparenting,
four-task table limit, eager rather than COW fork, no preemptive timer,
and conditional blocking at syscall boundaries. Do not call this a
general POSIX process implementation. Stage improvements only from
reproduced failures.
