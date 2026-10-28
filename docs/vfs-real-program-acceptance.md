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
