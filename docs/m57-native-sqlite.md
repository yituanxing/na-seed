# M57 — official upstream SQLite built and exercised inside native NaSeed QEMU

Status: draft; NOT accepted until all exact-head CI evidence is green.

- Upstream: SQLite 3.54.1 `sqlite-autoconf-3540100.tar.gz` from
  `https://www.sqlite.org/2026/sqlite-autoconf-3540100.tar.gz`.
- Expected **official published SHA3-256**:
  `b6ced585c750916b1052667239a12c25a7ff34e7f06e1e6e01ecb0427657d9c4`.
- Host: install signed **original Alpine 3.23** GCC/GNU Make/binutils/musl,
  verify SQLite SHA3-256 and stage only original configure/amalgamation
  source in read-only initramfs. Never stage a host-built sqlite3.
- Guest: copy source to tmpfs, run original `./configure`, original
  `make -j2 sqlite3`, execute **that very guest-built ELF**, assert
  SQLite 3.54.1 and expression 42.
- Common tmpfs capacity: replace the old fixed 16-index-page (32 MiB/file)
  inode with a demand-paged two-level indirect root (up to 1 GiB/file),
  and compile/run a real 64 MiB sparse pwrite/pread/truncate/reclaim probe
  IN the native guest before SQLite link. This is shared x86/RV64/ARM64 code.
- Diagnostic: emit a generic tmpfs path/offset/free-frame report only when
  file size or inode metadata allocation really fails; M57 gets a 4 GiB
  QEMU RAM budget to distinguish physical memory pressure from file limits.
- Filesystem workload: native sqlite3 creates a real tmpfs DB in normal
  rollback-journal mode, performs explicit BEGIN/COMMIT, then a **new
  process** opens the same DB and checks rows; a second transaction
  updates and rolls back, validating atomicity. Also assert real CREATE
  INDEX and JOIN, PRAGMA integrity_check, and absence of leftover journal.
- Acceptance: same entrypoint, package set, script, input and required
  stage markers on x86-64, RV64, AArch64 native system QEMU. Separate
  `.github/workflows/m57-native-sqlite.yml` while draft.
- Keep prior M38–M56 grouped acceptance and other workflows untouched.
  Promotion/merge requires M57 3/3 + exact-head existing CI green;
  no BusyBox/SQLite source patches, no fabricated SQL or invented syscalls.

## Explicit limitations

The database lives in NaSeed's **volatile tmpfs**. It persists across
closing and reopening within the running kernel, not across guest reboots.
The initial M57 target is rollback journal, **not WAL**: SQLite WAL uses
file-backed MAP_SHARED/-shm, which must remain a separate generic VFS/MM
milestone. Native pthread/concurrent SQLite locking requires separate
real contention evidence, not an assumption from one successful process.

## Capacity investigation (exact-head evidence)

M57 in all three native QEMU guests exceeds original 32 MiB inode ceiling;
64 MiB sparse pwrite/pread/shrink/regrow proves the new two-level index.
SQLite native link still returns ENOSPC even with the larger file limit and
4 GiB guest budget, so generic dispatch and frame-allocation failure
instrumentation is added; do NOT attribute this second issue to the
32 MiB cap without the exact syscall result. This PR remains Draft.

## Real GNU toolchain syscall finding (three-ISA full log)

The source-stage and native configure pass on all three ISAs, but GNU as/ld
currently fails during final output. **Exact syscall tracing on x86-64 and
RV64 found `writev` returning `EFAULT` (14)** for the output FD, despite the
user-facing GNU error saying ENOSPC. The previous common VFS
`nas_fs_fd_write` rejected any transfer larger than 1 MiB as `EFAULT`;
`writev` forwards an iovec through that same function. M57 now accepts
Linux's per-call `MAX_RW_COUNT` instead and verifies both real >2MiB
`writev` and `write` through the native guest. No upstream source patched.
ARM64 also exposes repeated `munmap` ENOMEM in native cc1; this remains a
separately tracked generic VMA concern, not proof of an SQLite defect.
