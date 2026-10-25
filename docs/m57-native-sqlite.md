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
- Capacity investigation used temporary kernel diagnostics and a 4 GiB RAM
  budget; both were withdrawn after the shared writev ABI bug was proven.
  Final CI runs at the prior normal 2 GiB QEMU guest memory budget.
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

## Verified bottleneck and latest acceptance

The first M57 run exposed a 32 MiB tmpfs file ceiling; the common two-level
sparse page index now passes the real 64 MiB pwrite/pread/truncate probe.
A second failure reported by GNU as/ld as ENOSPC was definitively traced
through the common syscall dispatcher: real x86-64 and RV64 GNU `writev`
returned **EFAULT (14)** for a single vector larger than the old private
1 MiB `nas_fs_fd_write` limit. All three ISAs now share Linux's
`MAX_RW_COUNT` handling and a real >2 MiB `write()`/`writev()` round-trip
probe in the native guest. No SQLite, GCC, binutils or Make source patches.

Exact-head `3a8d4713` M57 official SQLite 3.54.1 original configure, native
GCC/Make/link, sqlite3 CLI, file transaction/reopen/rollback/index/JOIN and
integrity acceptance passed x86-64, RV64 and AArch64 (3/3). This subsequent
cleanup commit removes temporary error-tracing wrappers and restores the
normal 2 GiB guest RAM budget; re-run all exact-head CI before merging.

Independently, the ARM64 GNU cc1 trace showed repeated `munmap` returning
ENOMEM from the bounded VMA ledger. That did not block M57 functional
acceptance, but it is a real Linux MM compatibility limitation to investigate
under a separate generic workload-driven change; no success is claimed for
arbitrary VMA fragmentation or SQLite WAL/MAP_SHARED.
