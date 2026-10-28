# NaSeed VFS by unmodified real applications (not synthetic interfaces)

The original Debian **busybox-static 1.37.0** is SHA256 verified from the
existing `scripts/busybox-static-fixtures.txt` on x86-64, RV64, AArch64.
An unchanged guest-side `/bin/busybox` runs
`tests/vfs-busybox-real-workload` via its **actual ash applet**. Child
processes execute real `cp`, `mv`, `tar`, `find`, `grep`, `cat`,
`rm`, `mkdir`, and `rmdir` applets. Their host/guest exit status
and application data are checked; there are no fake ioctl/syscall
handlers and no in-kernel tests substituted for application behavior.

## Controlled filesystem experiment

The **same program** and operations run with `/ramfs` (unmodified
jkent/ramfs with our VFS adapter) and `/tmp` (NaSeed tmpfs). App
workload per FS: nested directories, redirection, copied and renamed
files, recursive copying, traversal and grep, tar archival/extraction,
recursive removal, and final directory deletion. The workload also
copies across the two real mounts and verifies independent content.

The x86-64 CI job first invokes the same script **on the Linux host**,
using the exact same verified Debian busybox-static ELF; only root
paths are overridden, as supported by `NAS_APP_FS_A` and
`NAS_APP_FS_B`. The same original guest script, packaged in CPIO as
`/bin/vfs-busybox-real-workload`, runs inside NaSeed on three native
QEMU targets through the existing Linux-ABI BusyBox matrix harnesses.
The marker is emitted **only after** the real applet/data checks succeed.

The two FS providers remain independent variables, and the project
must respond to observed failures by fixing the **owner**:
generic namespace/dirfd/File/inode/mount semantics in VFS, provider
translation/lifetime in the adapter, or unsupported original vendor
semantics explicitly documented as unsupported. Do not rewrite original
vendor source to make a self-selected test pass. Do not introduce
`if (ramfs)` syscall branches.

Run `sh scripts/probe-vfs-busybox-real.sh amd64|riscv64|arm64`
in an environment equipped with clang/ld.lld, QEMU system emulation,
UEFI firmware, and existing CPIO/ESP tools. The focused original-RamFS
workflow batches all three real runs after proving kernel and
portable Linux ABI probes.

## Deliberate limits

This is NOT yet a comprehensive POSIX conformance suite: original
RamFS's atomic rename-overwrite, hardlinks, symlinks, timestamp
persistence and mmap remain separate capabilities to address based on
actual application requirements. Linux-host acceptance provides
behavioral reference, not proof of identical inode allocation or
security behavior.

### Real BusyBox tar -C catches an LP64 *at syscall ABI defect

The first genuine original Debian BusyBox 1.37.0 archive workload
was compared with Linux-host output, then failed inside NaSeed x86 QEMU
as `tar: src: Bad file descriptor`. Original `recursive_action.c`
calls `lstat("src")` after tar changes cwd via `-C`; the earlier
absolute-path `find/cp/mv` stages passed, isolating the relative
`newfstatat(AT_FDCWD,...)` behavior. NaSeed's common namei input
compared the RAW 64-bit register to a 64-bit `-100`, although Linux
defines this syscall argument as a signed **int**. Zero extension of
`AT_FDCWD` gives `0x00000000ffffff9c` and was incorrectly rejected
as an out-of-range FD. One ABI helper now sign-extends only the low 32
bits, shared across namespace/metadata callers and all three ISA
backends. This also aligns `AT_EMPTY_PATH` with cwd inode ownership.
No VFS inode/dentry, backend or BusyBox source has been special-cased.

The existing portable RamFS ring3 ELF now explicitly tests both
zero- and sign-extended `AT_FDCWD` via the real `newfstatat` syscall,
an owned directory FD, `AT_EMPTY_PATH` on cwd and rejection of `-99`.
The native syscall ABI vector appends this operation without changing
old indices on x86-64/RV64/AArch64. Actual BusyBox tar/extract remains
the end-to-end acceptance. The host original BusyBox oracle PASSED;
the exact-head QEMU test must pass before claiming defect closure.

### Architecture audit: per-inode leases are no longer a global 256 cap

The original backend test intentionally created 320 names, but no more
than one VFS inode was pinned at a time. It did not measure the shared
File/cwd/mount ownership capacity. VFS had one static 256-element array
of size+POSIX-record-lock leases and returned ENFILE for otherwise valid
inodes, even when heap and per-process FD capacity were available.
We replaced this table with **address-stable, reusable 32-entry heap
chunks** in the common VFS. Record-lock pointers are never relocated
when it grows; closing the last owner clears and reuses a slot. Heap
exhaustion returns ENOMEM, and O_CREAT preflights slot capacity before
publishing a filename. The default heap arena remains bounded; this
does NOT claim unlimited memory, nor does it change per-process FD limits.

An actual original-RamFS host integration test now pins 320 independent
real RamFS inodes simultaneously, checks each inode's metadata and
releases every lease. The shared bootfs/tmpfs/procfs mock host VFS test
pins 512 independent inode identities simultaneously, tests O_TRUNC
during pool pressure, releases/reuses chunks, and checks orphan/final
lease lifetime. Host acceptance links the existing common kernel heap
implementation in the three VFS host test paths; no new allocator or
filesystem-specific pointer store was introduced.

Further structural debt not hidden by these changes: mount registry
still has a 16-attachment limit, flock retains a bounded 128-entry
owner table, pathname buffers are fixed at 256, and the root/tmp
bootfs overlay remains a special bootstrapping seam. In particular,
VFS mountpoint selection uses both anchored inode identities and
canonical string snapshots in different paths; a truly dentry-owned
mount tree is not yet finished. These cannot be claimed solved by
increasing inode lease capacity.
