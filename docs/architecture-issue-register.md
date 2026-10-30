# NaSeed architecture issue register

This is a **source-verified, living list**, not a list of things that must be
rewritten immediately. Findings are evaluated against the actual development
branch, not against older RFCs. A passing workload proves its observed path;
it does not close an ownership or layering issue by itself.

Baseline for this review: `vfs-block-lwext4-foundation@690b234` (M108,
2026-10-29). Keep issue IDs stable as the branch advances. Before changing
an entry to **fixed**, record the exact commit, deleted superseded path,
independent consumer, and test evidence.

| ID | Priority | Status at baseline | Confirmed boundary / evidence | Closure contract |
| --- | --- | --- | --- | --- |
| NAS-A001 | P0 | Confirmed; first cleanup batch | `kernel/task.h` owns `running_bash/busybox/alpine` and stdout matcher; `kernel/fs/syscall.c` observes TTY output; `kernel/init/main.c` judges application success | Test behavior belongs to guest/host oracle; no application identity or stdout PASS sniff in Task, syscall or exec; preserve legacy boot argv and M1–M4 plus M50+ genuine acceptance |
| NAS-A002 | P0 | Confirmed; next ownership batch | `kernel/fs/file_ops.h` passes `(fd,user_addr,count)` to generic read/write; `kernel/task/blocked_completion.c` enumerates FD kinds | Syscall resolves FD/OFD and usercopy; providers use owned OpenFile identity plus kernel buffers; preserve partial I/O, EFAULT, blocking, signals and existing C continuations; remove superseded FD-kind retry path |
| NAS-A003 | P1 | Confirmed | `kernel/fs/vfs.h` only exposes inode `read_at/write_at`; there is no provider-owned per-open handle in common File | One backend open state per shared OFD, initialized before FD publication and released exactly once after final `dup/fork/SCM_RIGHTS` owner; verify failure unwind with independent filesystem |
| NAS-A004 | P1 | Confirmed design limit; needs independent provider | `kernel/fs/vfs.c::dentry_find` hashes and compares exact names; FAT can expose case/8.3 aliases | Provider-specific name identity and coherent cache invalidation, without global case-folding or pretending FAT has Unix unlink/rename semantics |
| NAS-A005 | P1 | Confirmed architectural limit; implementation deferred | Existing private file maps eagerly snapshot data and `MAP_SHARED` lacks an owned file/page-cache contract | File-backed VMAs pin an inode/address-space; tested dirty/writeback, read/write/truncate coherence, COW and sync before claiming shared mapping |
| NAS-A006 | P2 | Known capacity/performance constraint | Bounded task/FD/VMA/heap resources; shared synchronous single-inflight polling VirtIO-Blk | Grow only from measured real-program failures; preserve ownership and compare cold/warm genuine-QEMU performance |
| NAS-A007 | P1 | Process/tooling debt; do not duplicate foundation | `scripts/native-alpine-matrix.tsv` and M50 fast/full, original APK staging, M79 workflow and strict perf provenance already exist | One indexed source/version/ISA/dependency/workload/evidence catalog; reuse present runners and retain all M38–M108 gates, including manual full distinct from automatic fast |

## First cleanup batch (pending exact-head runtime proof)

- **NAS-A001, implementation complete; validation in progress.** Task no longer
  stores program names or stdout-matching state; exec and TTY no longer infer
  test acceptance. PID1 reports its exit status, while host probes verify
  actual user output and emulator exit. Existing no-cmdline Bash/BusyBox argv
  selection remains isolated in boot setup. Only a real Limine ELF module may
  satisfy the historical rootless compatibility endpoint.
- **Do not close NAS-A001** until the new exact-head commit retains M1–M4,
  the explicit-argv cases and the three-ISA grouped runs. Host syntax and
  static boundary checks alone are insufficient evidence.
- **NAS-A002 and later remain open.** In particular, no second OpenFile or
  syscall continuation mechanism is introduced by this batch.

## Already implemented: do not reopen as an unsolved issue

- M103 independent Task MM/Files/FSContext/Sighand, anonymous lazy faults and
  COW, and native C continuation (not old syscall replay).
- Owned VFS Path/Dentry references, mount/inode pinning, and kernel-buffer
  `nas_vfs_read_at` / `nas_vfs_write_at`.
- OFD-level `poll_description(slot)` and the existing safe-degradation flags
  for unlink/rename-while-open. Their presence does **not** mean complete
  Unix semantics or per-open backend state is implemented.

## Required decision record for each architecture batch

1. Give the original unmodified program's observed failure and same-ISA Linux
   baseline. If this is an adapter-specific limitation, state it explicitly.
2. Draw the object path: creator, sole owner, borrowers/consumers, final
   releaser, and all error exits. Record which layer owns the operation.
3. Name the **old implementation to be removed**; globally search for every
   caller after migration. Do not leave a second legacy implementation.
4. Verify a focused failure case and at least two independent consumers,
   then the relevant exact-head three-ISA QEMU gates. Distinguish host tests,
   cross-compilation, automatic fast, and explicit manual full.
5. Report success and unsupported/skipped scenarios separately. An application
   name or test marker must not become a condition in generic kernel code.

Related existing design/evidence: `docs/architecture-foundation-contract.md`,
`docs/ci-proof-coverage-contract.md`, `docs/vfs-open-description.md`,
`docs/architecture-performance-review-2026-10-28.md`.
