# NaSeed architecture issue register

This is a **source-verified, living list**, not a list of things that must be
rewritten immediately. Findings are evaluated against the actual development
branch, not against older RFCs. A passing workload proves its observed path;
it does not close an ownership or layering issue by itself.

Baseline for this review: `vfs-block-lwext4-foundation@690b234` (M108,
2026-10-29). Keep issue IDs stable as the branch advances. Before changing
an entry to **fixed**, record the exact commit, deleted superseded path,
independent consumer, and test evidence.

Latest exact-head proof (2026-10-30): `771f37b` completed **23 successful
GitHub Actions runs and one intentionally skipped opt-in wrapper**, with zero
failures. The independent full M55 run
[`40999972584`](https://github.com/yituanxing/na-seed/actions/runs/40999972584)
passed on x86-64, AArch64 and RV64 in real QEMU system guests: upstream cJSON
Ninja build, full CTest, strict zero-action no-op, a single-source incremental
rebuild, and a second CTest. This is evidence for the tested paths, not a
blanket claim that every architectural issue is closed.

| ID | Priority | Status at baseline | Confirmed boundary / evidence | Closure contract |
| --- | --- | --- | --- | --- |
| NAS-A001 | P0 | Confirmed; first cleanup batch | `kernel/task.h` owns `running_bash/busybox/alpine` and stdout matcher; `kernel/fs/syscall.c` observes TTY output; `kernel/init/main.c` judges application success | Test behavior belongs to guest/host oracle; no application identity or stdout PASS sniff in Task, syscall or exec; preserve legacy boot argv and M1–M4 plus M50+ genuine acceptance |
| NAS-A002 | P0 | Confirmed; next ownership batch | `kernel/fs/file_ops.h` passes `(fd,user_addr,count)` to generic read/write; `kernel/task/blocked_completion.c` enumerates FD kinds | Syscall resolves FD/OFD and usercopy; providers use owned OpenFile identity plus kernel buffers; preserve partial I/O, EFAULT, blocking, signals and existing C continuations; remove superseded FD-kind retry path |
| NAS-A003 | P1 | Confirmed | `kernel/fs/vfs.h` only exposes inode `read_at/write_at`; there is no provider-owned per-open handle in common File | One backend open state per shared OFD, initialized before FD publication and released exactly once after final `dup/fork/SCM_RIGHTS` owner; verify failure unwind with independent filesystem |
| NAS-A004 | P1 | Confirmed design limit; needs independent provider | `kernel/fs/vfs.c::dentry_find` hashes and compares exact names; FAT can expose case/8.3 aliases | Provider-specific name identity and coherent cache invalidation, without global case-folding or pretending FAT has Unix unlink/rename semantics |
| NAS-A005 | P1 | Confirmed architectural limit; implementation deferred | Existing private file maps eagerly snapshot data and `MAP_SHARED` lacks an owned file/page-cache contract | File-backed VMAs pin an inode/address-space; tested dirty/writeback, read/write/truncate coherence, COW and sync before claiming shared mapping |
| NAS-A006 | P2 | Known capacity/performance constraint | Bounded task/FD/VMA/heap resources; shared synchronous single-inflight polling VirtIO-Blk | Grow only from measured real-program failures; preserve ownership and compare cold/warm genuine-QEMU performance |
| NAS-A007 | P1 | Process/tooling debt; do not duplicate foundation | `scripts/native-alpine-matrix.tsv` and M50 fast/full, original APK staging, M79 workflow and strict perf provenance already exist | One indexed source/version/ISA/dependency/workload/evidence catalog; reuse present runners and retain all M38–M108 gates, including manual full distinct from automatic fast |
| NAS-A008 | P0 | Confirmed vector transaction boundary violation; not fixed by NAS-A002 | `kernel/fs/file_io.c` streams per-iovec user metadata and commits scalar I/O before validating later elements. Linux rejects a malformed later `iov_len=UINT64_MAX` without consuming earlier data; aggregate `PIPE_BUF` must be admitted atomically. Eventfd `readv([4,4])` spans both slices, while `writev([4,4])` must still fail `EINVAL` | First snapshot/validate all vector metadata and FD permissions (including zero-count), then use one provider-capability-aware logical transaction with bounded scatter/gather; preserve Linux eventfd read-iterator vs segmented write distinction. Check iovcnt=0/bad FD, `IOV_MAX=1024`, overflow, pipe low-capacity atomicity, EFAULT and blocking on all ISAs |
| NAS-A009 | P1 | Confirmed Linux-specific positional I/O mismatch; intentionally deferred | `nas_fs_fd_pwrite` writes at its explicit offset even with `O_APPEND`; Linux actually appends while keeping the shared OFD offset unchanged (Linux/POSIX discrepancy verified against a host memfd) | Preserve the provider's positional kernel-buffer operation and OFD offset; select append position in the Linux ABI adapter and verify two independent open descriptions and `pwrite+O_APPEND` against same-ISA Linux |
| NAS-A010 | P1 | Cross-ISA ABI audit finding; requires direct target differential oracle | `kernel/arch/linux_fcntl_abi.h` imports AArch64 open flags into a common internal layout; `kernel/fs/syscall.c` returns `status_flags` from `fcntl(F_GETFL)` without reverse translation | Normalize open flags only at the syscall ABI boundary and export the caller ISA's native layout on `F_GETFL`; keep all providers architecture-neutral; prove each flag across x86-64/RV64/AArch64 |
| NAS-A011 | P1 | Confirmed PTY blocking/continuation gap; deferred to provider-wait migration | `kernel/drivers/pty.c` returns EAGAIN on empty/full queues; `kernel/syscall/dispatch.c` only converts selected Pipe/Socket/Eventfd/TTY kinds into task waits, omitting PTY master/slave | Express retry/wait readiness via OpenFile operations, not another FD-kind list; preserve O_NONBLOCK, VMIN/VTIME, EOF, signal interruption and wakeup ownership through native Bash/PTY regression |
| NAS-A012 | P1 | Fixed in `d7db661`; exact-head CI green in `771f37b` | Previous `kernel/fs/syscall.c::ops_socket` shared `pipe_write_plan` and `NAS_PIPE_CAP` atomic preflight with pipe writes; `kernel/pipe.c::nas_pipe_write_amount` rejected a partial nonblocking stream send below `PIPE_BUF` | Keep the shared ring as a private backend detail, but give AF_UNIX stream its own short-write admission and zero PIPE_BUF guarantee. Linux socketpair and NaSeed M106 vector partial-progress oracles passed; continue covering independent workloads |
| NAS-A013 | P1 | Fixed in `d7db661`; exact-head CI green in `771f37b` | Previous `kernel/fs/file_io.c::io_segmented_read` invoked repeated scalar reads, lowering noncanonical VMIN per slice; `kernel/task/wait.c::task_io_ready` wrongly used `readv` iovcnt as requested TTY bytes | Treat one `readv` as a logical request at the TTY provider, retain correct noncanonical VMIN/VTIME, per-waiter deadlines and EFAULT/partial semantics; readiness must use kernel-owned logical byte length, never inspect another task's user iovecs. Linux and host-production oracles plus CI passed; true blocked-vector mutation remains NAS-A014 |
| NAS-A014 | P1 | Implementation in next batch; exact-head three-ISA guest validation pending | Previous `kernel/syscall/dispatch.c::finish_readv` reimported user iovecs on wake, while `task/wait.c` re-resolved a mutable task FD. A concurrent `dup2` or iovec mutation could redirect a logically existing blocked operation. | The new Task-owned pending I/O request pins the original shared OpenFile and a single imported iovec snapshot. One FileOps slot executor serves scalar/vector first attempts and resume; provider flags own initial/retry/TTY and SIGPIPE behavior. Normal completion, EINTR, exit/group-exit/reap dispose the pin, while fork's by-value reset cannot free the parent's pointer. Two real Linux concurrent mutation/dup2 oracles and host production regressions pass; do not close until authentic NaSeed three-ISA M42 guest executions pass |
| NAS-A015 | P1 | Fixed in `771f37b`; full M55 real guest 3/3 PASS | Original upstream Ninja 1.13.2 `StartEdge` repeatedly opens its empty `.ninja_lock` with `fopen("wb")` and no write to record command-start mtime. `kernel/tmpfs.c::tmp_resize` returned early for unchanged lengths, so `O_TRUNC` left the timestamp frozen. This made M55 on all three ISAs plan 22 redundant no-op rebuild edges | Successful `O_TRUNC` or same-size ftruncate now updates tmpfs `mtime/ctime`; Linux oracle, NaSeed production tmpfs tests (including M108+ASan), and original three-ISA guest Ninja strict no-op, genuine source rebuild and second CTest all passed on exact head. No Ninja patches or bypasses |
| NAS-A016 | P1 | Real three-ISA guest blocker discovered in `75e1ecc` M42; small ABI fix pending CI | The original musl pthread clone uses `0x7d0f00`, while a static GNU libc pthread calls `clone3` (ENOSYS fallback) then `clone(0x3d0f00)`. `nas_linux_decode_call` accepted only musl's exact mask, so all three NaSeed guests rejected glibc's semantically identical flags before A014 could be tested | Linux 6.6 UAPI explicitly marks the differing `CLONE_DETACHED` bit `0x00400000` as unused/ignored. Accept only both pthread variants by masking that bit; retain the required VM/FS/FILES/SIGHAND/THREAD/SYSVSEM/TLS/PARENT_SETTID/CHILD_CLEARTID flags, reject other combinations, and demand original M42 XZ plus glibc A014 guest success on all three ISAs |

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

## NAS-A002 data-plane migration (implementation, exact-head proof pending)

- The common FileOps `read/write` callbacks now accept the shared OpenFile
  slot and **kernel-owned** bytes. The common FD syscall adapters own
  usercopy/COW and bounded transfer chunks. Optional provider plans preserve
  original logical transfer length; Pipe and AF_UNIX sockets make their
  `PIPE_BUF` admission decision before chunks are committed.
- PTY, TTY, eventfd, bootfs and mounted VFS providers migrated in one batch;
  obsolete `(fd,user_addr,count)` provider implementations removed. An
  explicit eventfd copyout-order flag preserves Linux's confirmed
  consume-before-EFAULT semantics. A provider VFS capability, rather than a
  filesystem name string, distinguishes exact size/EOF from dynamic procfs.
- Host evidence: M106, M105 PTY Linux oracle, M102 COW host phase, M89 OFD
  poll, boundary mutation guard and linked three-ISA source manifest. M55
  eventfd differential regression passes on **Linux host**, not yet NaSeed.
  M102 target compile and three-ISA QEMU remain pending, because the local
  environment does not provide Clang/QEMU.
- Later exact-head `24a8f8bd` grouped-native FAST QEMU jobs passed **9/9**
  (three ISAs × foundation/lifecycle/pthread), and M106 passed in Actions.
  The userspace smoke then stopped at M102 target syntax because its new
  `file_io.c` compile lacked the already-present `limine.h` shim include.
  That CI-script error and an independently proven `O_PATH` write access
  bug were fixed in `3db3705d`. The subsequent `74afe88` exact head
  achieved **22/22 automatic workflow success**, including all grouped
  native QEMU jobs and the userspace smoke gate. The independent M55 eventfd
  guest gate is manual-dispatch and is not part of grouped FAST; host oracle
  results do not substitute for it.
- Subsequent Linux-host differential checks found a finer `access_ok` rule:
  an out-of-user-range pointer must fail `EFAULT` even for zero-byte EOF and
  null/zero discard writes, before a backend plan or eventfd counter
  consumption. An unmapped but *in-range* pointer is still accepted by a
  discard sink or a zero-byte EOF without touching its pages. A follow-up
  patch implements this generic range/page-content separation; it requires
  its own exact-head runtime evidence before that behavior can be closed.
- **Do not mark NAS-A002 fully closed yet:** syscall initial blocking and
  native C continuation retry still enumerate FD kinds. Preserve SIGPIPE,
  per-waiter TTY readiness, and exactly-once task wait semantics when
  migrating them to an explicit provider wait capability; see
  `docs/ofd-data-plane-contract.md`. NAS-A008 tracks separate vector-aware
  aggregate-planning correctness.

## NAS-A008 vector transaction migration (local implementation, target proof pending)

- FileOps independently declares aggregate `readv` and `writev` support;
  usercopy remains in one syscall adapter. The importer snapshots at most
  1024 user iovecs in heap-owned kernel storage, checks all descriptors and
  address ranges before any provider I/O, then uses bounded kernel-buffer
  scatter/gather. It removes the old per-iovec partial side effect when a
  later descriptor is structurally invalid.
- Pipe and AF_UNIX writes plan the **whole vector** once and preflight all
  source pages for an admitted `PIPE_BUF` atomic write; eventfd reads as one
  eight-byte value across several iovecs, while its Linux-required segmented
  `writev` fallback is preserved. Other FileOps providers remain segmented
  until independently verified; no new FD-kind switch is added.
- Independent host evidence: `scripts/check-a008-linux-vectors.sh` exercises
  genuine Linux syscalls, and M106 exercises actual NaSeed FileOps, pipe and
  eventfd providers with FD slot and user-MM failure injection. Both pass
  locally. M55's original guest test gains vector/eventfd/pipe acceptance
  cases; the manual QEMU gate and new exact-head automatic CI are still
  required. Do not label NAS-A008 fully closed from host success alone.

- The pushed `dbe0008c` exact head subsequently achieved **22/22 automatic
  workflow success**, including the three-ISA grouped fast native QEMU jobs.
  These fast runs intentionally do **not** execute M55 original CMake/Ninja,
  and cannot be substituted for the separate full native guest gate.
- Follow-up review confirmed NAS-A012 (socket stream is not a pipe) and
  NAS-A013 (TTY's `readv` is not independent scalar reads). Both are tracked
  separately from the original iovec import/ownership defect; neither
  invalidates the evidence that importer state is heap-owned and released.

## Full guest acceptance triggering contract

- M55's three-ISA full native CMake/Ninja workflow stays deliberately opt-in
  to avoid adding 40–55 minute guest builds to every routine change.
- A small PR metadata-triggered wrapper invokes the same M55 workflow. The
  GitHub app can temporarily add `[run-m55-deep]` to the open PR title (or
  apply the `run-m55-deep` label if provisioned), then restore the title;
  this requires neither a new branch nor a website login. A reusable M55
  checkout pins `github.event.pull_request.head.sha`, not the synthetic PR
  merge commit. Artifacts are retained on both PASS and FAIL.
- Mark M55 guest `PASS` only after every matrix ISA reports the complete
  original build, first/second CTest, real source rebuild, and all new
  vector/Eventfd/Pipe stage markers at that exact head.

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
