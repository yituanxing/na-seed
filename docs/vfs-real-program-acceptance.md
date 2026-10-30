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

## M82: original GNU Make checks directory dependency semantics

M80's authenticated Alpine v3.23 APK overlay now stages the original
ISA-native **GNU Make 4.4.1** executable alongside the original GNU sed,
gzip and existing 19 functional applications. The same rootfs and same
three-filesystem QEMU boot used by M81 also run `tests/m82-make-run.sh`:
no second installer, application shim or replacement VFS implementation.
The guest asserts an actual `/usr/bin/make` ELF and GNU Make version;
the host additionally records its signed APK version and SHA256.

Nine actual Make dependency-graph oracles run once on **each** of `/tmp`
(tmpfs), `/ramfs` (unchanged upstream RamFS) and `/mnt` (lwext4 on physical
VirtIO-Blk ext4), for **27 cases per ISA**:

1. GNU Make runs sed and gzip recipes and produces the expected bytes.
2. Unchanged inputs cause a true no-op, checked by recipe count.
3. Modified source reruns the dependent recipes and changes output.
4. Creating a file changes its parent directory and rebuilds a
   directory-dependent Make target.
5. Removing a file also rebuilds that target.
6. Renaming a file inside one parent rebuilds it.
7. Creating a subdirectory rebuilds it.
8. Removing a subdirectory rebuilds it.
9. Moving a file between parents updates *both* directories and rebuilds.

Directory baselines use a copied reference timestamp with zero epoch
instead of assuming a valid guest RTC. This is important because NaSeed
falls back to the per-ISA monotonic clock when firmware UTC is unusable.
The actual GNU Make recipe counts, output bytes and no-op checks are the
functional oracle; a successful `stat()` alone does not pass a case.

The existing `scripts/probe-m80-batch.sh` validates **every** guest M82
case marker, then independently reads real ext4 GNU Make output and a
guest-written persistence record with host `debugfs` after clean unmount.
It also performs the existing host `e2fsck` check. This is an authentic
QEMU kernel/filesystem/application acceptance, not just an ELF or syscall
smoke test. The first exact-head M82 run at commit `a33fa1b3` passed
**27/27 on each of AMD64, RV64 and ARM64, 81/81 total**, while retaining
M81's 186/186 original software/provider cases. See the M80/M81/M82
workflow [#41036216761](https://github.com/yituanxing/na-seed/actions/runs/41036216761).

### Ownership and remaining limits

The required repairs were in the existing backends: tmpfs now touches
the actual parent directory inode on successful namespace edits, while
lwext4 updates live parent inode `mtime/ctime`, link/rename `ctime`, and
new symlink timestamps. Synthetic `/tmp` root identity still belongs to
immutable bootfs, so its mutable timestamp is held by the existing
tmpfs namespace owner; the bootstrap does not modify CPIO bytes.

These results **do not** imply RamFS implements hardlink, symlink or
rename-overwrite, nor that `/tmp` root's explicit `utimensat` is fully
POSIX-compatible; those are separate documented constraints. A future
ext4 journal-enabled configuration also needs its own atomic namespace
and timestamp transaction review. No third-party filesystem was patched
to force the GNU Make tests to pass.

## M83: original upstream C project built and replayed on physical ext4

`scripts/probe-m83-source-build.sh amd64|riscv64|arm64` uses the existing
three-ISA QEMU boot runners and the canonical authenticated Alpine APK
overlay. Its package profile reuses `packages-native-busybox.txt` instead
of creating a second GCC/Make installer. The source is the complete,
unmodified upstream zlib 1.3.1 Git tree, pinned by the existing M36
fetcher to `51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf` and exported
with `git archive`. There are no local zlib source or Makefile patches.

In the **first real QEMU boot**, original signed native GCC and GNU Make
run zlib's own `./configure --static`, parallel `make all`, and original
`make teststatic` against writable VirtIO ext4. The actual example ELF
and `libz.a` are checked. `make -q libz.a` must report no work; editing
only `deflate.c` in the writable build tree must report a dirty target,
rebuild `deflate.o` without rebuilding `crc32.o`, and pass the full test
again. The guest separately compiles and runs M54's existing two-file C
project with original GCC/Make on both tmpfs and original RamFS, including
incremental/no-op detection. The ext4-built example must run from all
three providers. The RamFS workload deliberately avoids unsupported
link/symlink/rename-overwrite operations.

The same 32-MiB ext4 image is then checked by host `e2fsck`, and host
`debugfs` extracts the actual guest-produced ELF, static archive, and
persistence record. A **second independent QEMU invocation** uses that
*exact same* disk file without formatting or copying a replacement. It
executes the persisted guest-built ELF, verifies `make -q` is still a
no-op, and repeats `e2fsck`/`debugfs`; the host requires the second
boot's extracted ELF/archive to be byte-identical to the first boot's.

Both phases pass separately on AMD64, RV64, and AArch64 in the dedicated
`.github/workflows/m83-source.yml` pull-request workflow. M54's historical
developer workstation workflow remains manual-only. The final M83 commit
`d60a1770` was verified at exact head with three original native QEMU jobs
[#41043606132](https://github.com/yituanxing/na-seed/actions/runs/41043606132)
(3/3 PASS) alongside the original grouped-native workflow
[#41043605907](https://github.com/yituanxing/na-seed/actions/runs/41043605907)
(9/9 PASS). A script or a local static check alone is not acceptance evidence.

## M84: two original software releases built inside NaSeed, persisted, rebooted

M84 expands the real-software driver from one zlib C project to **official
SQLite 3.54.1 and Lua 5.4.8** on the same 256-MiB external ext4 device.
Neither third-party source nor the existing VFS/driver implementation gets
test-specific modifications. Both releases are separately publisher-hash
verified on the host, then extracted and compiled *inside the guest* by
original signed Alpine GCC and GNU Make. The authenticated APK overlay,
kernel/QEMU orchestration and source-archive downloaders have one owner,
shared with M57/M83/external Lua where applicable.

The SQLite stage reuses `tests/m57-sqlite-run.sh`, including its actual
autoconf `./configure`, GNU Make/GCC native CLI build, `sqlite3` execution,
file-backed `BEGIN IMMEDIATE`/commit/rollback, index, relational join and
`PRAGMA integrity_check` checks. M57's previous tmpfs defaults remain
unchanged; M84 switches its source tree and database to persistent ext4.
The Lua stage uses the unmodified upstream Makefiles to produce both `lua`
and `luac`, executes the existing VM/GC/coroutine and file-IO suites, tests
true Make no-op and selective `lvm.c` rebuild, and executes the *same*
guest-compiled ELF from ext4, tmpfs and original RamFS.

The first QEMU boot builds both applications and writes an ext4 completion
record. The Linux host independently runs `e2fsck` and extracts the
guest-compiled SQLite and Lua ELFs plus real SQLite database using `debugfs`;
ELF architecture and database magic are checked. A separate second QEMU
machine boot on the **same unchanged physical disk** must re-run the
persisted executables, prove the index/database integrity and rollback,
and require GNU Make to report no pending work. The host then requires the
two original executables and database bytes to match across boots and a
second clean `e2fsck`.

Run `sh scripts/probe-m84-source-build.sh amd64|riscv64|arm64` or use the
three-ISA `.github/workflows/m84-source.yml` pull-request workflow. Its
full native QEMU stages can take substantially longer than M83 because
the original SQLite C amalgamation is compiled with software emulation.
**M84 is not considered passed until all three actual exact-head QEMU jobs
finish successfully**, with original serial, physical disk and extracted
ELF/database evidence.

### Real linker exposes sparse writes corrupting disk block zero

The first real M83 three-ISA run at `ba33c539` failed during GNU `ar/ld`,
with corrupted object-file string tables or garbage symbol names. The
actual failed AMD64 and ARM64 QEMU disk artifacts were independently
examined: their ext4 superblock at byte offset 1024 had been replaced
by original zlib object-file symbol strings. In pinned upstream lwext4,
writing a brand-new file first at a non-block-aligned offset past EOF
can resolve its unallocated data block to physical block zero. GNU as/ld
legitimately seek past the ELF header before backfilling it; the defect
belongs to the filesystem adapter, not to the compiler or its input.

The existing lwext4 provider now allocates/zero-fills any gap from EOF
to the requested write position using its *existing* resize path before
calling upstream `ext4_fwrite`; third-party code remains unchanged.
The existing host VFS + real-ext4-image acceptance adds offset-64,
offset-4097 and nonempty-file offset-8193 sparse-write cases, ELF-header
backfill, zero-hole reads, reserved-block byte checks, remount reads
and host `e2fsck`. Any M83 PASS still requires a fresh post-fix QEMU run.
