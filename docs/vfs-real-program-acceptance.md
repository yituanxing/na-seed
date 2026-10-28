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
