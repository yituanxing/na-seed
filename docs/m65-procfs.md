# M65 — read-only procfs process observability (three native ISAs)

This milestone follows M61–M64. It adds a **real virtual filesystem
provider** backed by the canonical Task registry and frame allocator;
there is no disk copy, app-specific syscall response or second process
table. The procfs backend enters the existing namespace/FD/inode boundary,
not the scheduler.

Paths at first integration:
- `/proc`, `/proc/self`, `/proc/<pid>`
- `/proc/<pid>/{stat,status,comm,cmdline}`
- `/proc/{meminfo,uptime,version,cpuinfo}`

Regular proc files are read-only, use shared `nas_open_file` offsets
(dup/fork obey existing FD ownership), and are enumerated through real
`getdents64` cookies. `stat/fstat/fstatat` use each ISA's existing
Linux struct stat ABI. Existing CPIO and tmpfs namespaces keep their
lifetime and semantics.

**Accurate capabilities and intentional limits:**
- Name, PID, TGID, PPID, process state and thread count are taken directly
  from the existing Task registry; memory totals and free values come
  from the actual frame allocator.
- `cmdline` is empty because NaSeed currently does not retain argv. Do
  not fabricate argv for a monitoring application.
- `stat` untracked CPU/accounting fields are zero and must NOT be interpreted
  as measured CPU usage, fault counts or a complete Linux procfs ABI.
- `/proc/self` is a directory alias, not yet Linux's symlink inode.
  `/proc/<pid>/task`, per-process memory accounting, fd symlinks,
  advanced status fields, complete scheduler statistics and all device
  proc files remain future work.
- No SMP, network, persistent filesystem, ioctl hooks or fabricated
  successful Linux syscalls are introduced.
- Procfs snapshots are regenerated on each read. User space may observe
  process turnover between readdir and open; ENOENT is correct then.

The unit test links the actual `kernel/fs/procfs.c` provider to host
Task/frame/clock fixtures. Native QEMU is still the integration authority.

Original signed Alpine v3.23 procps-ng `ps`, `free` and `uptime`
form an additional native software compatibility gate. They are never
compiled inside NaSeed. It is **not** accepted until original programs
run and exit successfully on amd64/RV64/AArch64. Keep M38–M64 unchanged.
