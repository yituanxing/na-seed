# M6.7 — real BusyBox ash scratch files, bounded volatile /tmp

**Observed gap:** Read-only CPIO supports external commands and pipelines,
but `sh -c 'echo hello > /tmp/message; cat /tmp/message'` cannot create a
scratch file. Rather than import lwext4 or a full VFS without a device/workload,
mount a tiny independent volatile file store under a real CPIO /tmp directory.

This slice supports up to eight persistent regular files, each up to 16 KiB,
with deterministic names and fixed kernel-owned byte storage. `open/openat`
handle `O_CREAT`, `O_TRUNC`, `O_APPEND`, `O_EXCL`, read/write modes,
`O_CLOEXEC` and `O_DIRECTORY`; `read`, `write`, `writev`,
`lseek`, `fstat`, `newfstatat`, `getdents64` and `poll` use
real kernel bytes and Linux-compatible status/error values. File content
lives across `fork` and `execve`, and open offsets are shared by
`dup`/inherited descriptions, not per-descriptor copies.

**Original-program acceptance:** The pinned, unchanged Alpine BusyBox ash
creates, appends, truncates and reads /tmp files, pipes a stored file to wc,
copies to another file through a child process, lists /tmp, checks a
shared-offset descriptor, and attempts forbidden overwrite of CPIO
/etc/hostname, which must remain read-only. Every case requires genuine
stdout, zero final shell exit and QEMU clean shutdown. Full prior M0–M6.6
regressions remain mandatory.

**Boundaries:** no persistence, no writable CPIO, no arbitrary nested
directory creation, removal/rename/hardlinks or general VFS; `/tmp`
begins empty each boot. Capacity limits return `-ENOSPC` rather than
pretending bytes were written. No timer-driven preemption or SMP.
