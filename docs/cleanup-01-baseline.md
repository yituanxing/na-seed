# Cleanup-01: integration baseline and CI de-duplication

Baseline: ef00448c2ae9b99cf0d7de0f0b33ed5ab8a3fa7e (PR #152), descendants of PRs #148 and #150. The README from PR #151 is integrated with its original commit as a second parent. Main is 597 commits behind the starting baseline. Do not merge or delete historical branches merely on the basis of age.

## Branch inventory at 2026-11-24

22 original refs inspected by Git compare against the baseline: 12 ancestors, 9 divergent refs, and the baseline itself. README is the sole change in the documentation branch.

| Divergent ref | Unique commits | Disposition |
| --- | ---: | --- |
| docs/naseed-verified-baseline-20261123 | 1 | Integrated |
| arch/task-mm-unification | 31 | Review behavior and ownership against current Task/MM; no wholesale merge |
| codex/m192-virtio-flush-diag-20261104 | 1 | Review diagnostic against current U-Boot block port |
| codex/m194-lwext4-sparse-hole-20261104 | 1 | Review sparse-hole fix and regression against current lwext4 |
| fix/init-leader-lifetime | 2 | Review init/task lifecycle and tests |
| fix/process-owner-and-unix-rights | 1 | Review FD/SCM_RIGHTS/wait ownership and tests |
| naseed-m85-runtime | 3 | Review CI/provenance changes and Git guest proof |
| permission-test-20261025 | 1 | Separate permission-test text file; no kernel feature |
| vfs-block-lwext4-foundation | 247 | Compare semantics and patch-equivalent changes, especially VFS/Block/port contracts |

Git ancestry divergence is not proof of missing functionality: squash, rebases and independent fixes may already implement the same behavior.

## CI cleanup

218 workflow files at the starting baseline, not byte-identical as whole files. The first batch removes repeated host setup from M109–M118, M120–M122 and M124–M126, preserving each suite's trigger, three-ISA matrix, exact checkout, test commands, timeouts, and artifact contract. M119 and M123 have additional host dependencies and remain unchanged. No regression or test is deleted. Keep CI organization separate from evidence: exact-head checkout checks do not prove runtime acceptance.

Next batches: classify workflows by Host/architecture, ABI/userland, distro, hardware, network and release/admin; share setup only when equivalent, then consolidate dispatch by test profile with retained provenance. Investigate divergent branch semantics before branch retirement. Audit kernel/ports cross-module ownership and remove redundant paths only with tests.

New runtime results are not established by this document.

## Second CI cleanup batch

M109–M141 now all share the same signed-Alpine native QEMU host dependency setup. M119, M123 and M127 retain their additional host dependencies via explicit command:package arguments (gettext, e2fsprogs and ffmpeg respectively). Comparison normalized shell whitespace and line continuations, checked the common dependency set and compared the remaining shell body before replacing each setup step. Later workflow families with different setup logic were left unchanged. Test triggers, test commands, matrices, evidence and timeout settings remain intact. No new runtime acceptance is implied.

## Manual native workflow consolidation

M72–M76 now use `debugger-native.yml` with the original profile selection,
three native ISAs, ptrace host gates and failure evidence. M109–M128 now use
`alpine-signed-batches.yml` with profile-specific prerequisites, original
timeouts and distinct signed ELF/guest acceptance probes. Historical manual
entry names are retired, not the underlying tests. The normal push-triggered
userspace regression remains unchanged. The M152 ELF inventory has one source
file, with the same guest copy destination and exact admission checks.
No historical native result is promoted to the new head.

## Further consolidation and retirement

M129–M141 join the existing `alpine-signed-batches.yml` dispatch; M159,
M163 and M165 share `alpine-signed-inventory.yml`. Profile-specific host
checks, three-ISA guest probes and signed evidence remain mandatory. The
strace/GDB profiles share byte-identical package and ELF lists through the
existing catalog; M163/M165 share the package list only, with separate ELF
inventories. Historic unique-ELF checks still see the same union of paths.

Retired superseded early pathname, pipe, boot-handoff and resource-lifetime
design notes, and explicitly historical architecture/branch review notes.
Their original contents are available at the immutable pre-cleanup commit
`eaa64c246e3b9b60406409aa40adda24de40be53`; the current architecture and CI evidence contracts remain.

The nine M146/M148/M150/M151/M153–M155/M157/M158 manual native
workflows now use `alpine-signed-extended.yml` with the matching profile.
The original workflows and historical results remain in Git history at
`ed63bd01b5914cb7d0aa81a73425a232c673113e`; three-ISA scope, timeouts, profile-specific prerequisite tools,
manifest counts, uniqueness checks and native guest evidence remain intact.

M166/M167/M168/M170/M171 desktop and input native probes are selected in
`desktop-native.yml`; per-profile syntax/host gates, LVGL revision, compiler
flags, three-ISA actual QEMU runs and signed/display evidence remain.

M172–M180 device and IRQ native acceptance now selects profiles in
`device-irq-native.yml`. Independent host gates still precede three-ISA
native QEMU; M179/M180 host ext4/nano setup and M180 FD/broker binaries
remain. Per-profile timeouts, original probe scripts, exact artifact path
sets and M179/M180 cancellation policy are retained.

M245–M247 x86 and M249–M251 ext4 acceptance use two common native
workflows, selecting the original profile on manual dispatch. Legacy
`na-alpine-direct-use` pushes matching any original path filter run all
profiles in the affected group, so no formerly required gate disappears.
The current cleanup/main push regression is unchanged. M252 is already
shared by M254–M257 and retains its existing reusable workflow interface.
M250 owns the common UART transport for M251/M252; commands, original ELF
exec evidence, interpreter thresholds and clean native exit remain distinct.

## Consolidated signed Alpine entrypoints

The staged APK and original native QEMU contracts are unchanged by the
wrapper retirement. The current commands for consolidated software batches are:

    sh scripts/stage-alpine-signed-batch.sh PORT PROFILE
    sh scripts/probe-alpine-signed-batch.sh PORT PROFILE

PROFILE is the original milestone such as m109 or m165, and PORT is amd64,
riscv64 or arm64. M141 retains its specialized native probe and supplemental
network/LLVM cohorts; all of those call the shared M141 staging profile.
The host fixture dispatcher owns M119–M128, M143/M144/M147/M149/M152,
M163/M165 and M166–M168. Original guest scripts, exact marker counts,
independent ELF manifests and native evidence remain separate. In particular,
M163/M165 share actual PNM data and M166–M168 retain the original distinct
static program lists, machine checks and SHA256 evidence names. The common
probe matches complete kernel exec trace lines, not executable-name prefixes.
Host-only test fixtures are not evidence of native guest acceptance.

## Historical milestone archive

These 74 milestone reports are retired from the active worktree as a single
historical snapshot. They had no direct path or basename references from the
tracked tree in the preceding reference inventory. This does not establish
that their historical design discussion is useless: all original bytes,
evidence links, limitations and dated candidate/pass claims remain readable
at [the immutable pre-archive tree](https://github.com/yituanxing/na-seed/tree/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs).
Current build rules, source, tests, signed manifests and hardware workflows
are unchanged. Retained architecture contracts, provenance, active gate
instructions and newer distribution reports remain in the worktree.

Retrieve any archived report without editing the current branch:

    git show b2f2c994558c7ef7c7de2057734e9d91eab462d1:docs/<filename>.md

Exact report index:

- [alpine-m146-original-media-dev-batch-2026-11-01](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/alpine-m146-original-media-dev-batch-2026-11-01.md)
- [alpine-m150-archive-forensic-batch-2026-11-01](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/alpine-m150-archive-forensic-batch-2026-11-01.md)
- [alpine-m153-development-binary-batch-2026-11-01](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/alpine-m153-development-binary-batch-2026-11-01.md)
- [m11-anon-vm-lifetime](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m11-anon-vm-lifetime.md)
- [m12-process-lifecycle](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m12-process-lifecycle.md)
- [m13-init-supervisor](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m13-init-supervisor.md)
- [m14-process-sessions](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m14-process-sessions.md)
- [m148-original-alpine-codec-roundtrips-2026-11-01](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m148-original-alpine-codec-roundtrips-2026-11-01.md)
- [m149-signed-network-service-portfolio-2026-11-01](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m149-signed-network-service-portfolio-2026-11-01.md)
- [m15-kill-process-groups](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m15-kill-process-groups.md)
- [m151-signed-alpine-multimedia-native-codecs-2026-11-01](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m151-signed-alpine-multimedia-native-codecs-2026-11-01.md)
- [m152-signed-dns-smtp-network-2026-11-01](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m152-signed-dns-smtp-network-2026-11-01.md)
- [m154-signed-alpine-text-graphics-conversion-2026-11-01](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m154-signed-alpine-text-graphics-conversion-2026-11-01.md)
- [m155-original-filesystem-batch](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m155-original-filesystem-batch.md)
- [m156-m157-real-app-acceptance-2026-11-02](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m156-m157-real-app-acceptance-2026-11-02.md)
- [m158-native-image-codecs-three-isa-2026-11-02](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m158-native-image-codecs-three-isa-2026-11-02.md)
- [m159-m161-three-isa-original-software-network-pixels](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m159-m161-three-isa-original-software-network-pixels.md)
- [m16-job-stop-continue](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m16-job-stop-continue.md)
- [m168-client-surfaces-and-real-text-2026-11-02](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m168-client-surfaces-and-real-text-2026-11-02.md)
- [m17-console-tty](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m17-console-tty.md)
- [m170-persistent-pty-and-client-lifecycle-2026-11-03](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m170-persistent-pty-and-client-lifecycle-2026-11-03.md)
- [m18-real-interactive-ash](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m18-real-interactive-ash.md)
- [m189-persistent-desktop-editor-session-2026-11-04](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m189-persistent-desktop-editor-session-2026-11-04.md)
- [m190-durable-desktop-file-reboot-2026-11-04](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m190-durable-desktop-file-reboot-2026-11-04.md)
- [m20-tty-line-discipline](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m20-tty-line-discipline.md)
- [m21-tty-scheduler](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m21-tty-scheduler.md)
- [m22-tty-poll-events](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m22-tty-poll-events.md)
- [m23-terminal-foreground-signals](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m23-terminal-foreground-signals.md)
- [m24-standard-signal-pending](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m24-standard-signal-pending.md)
- [m25-background-tty-job-control](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m25-background-tty-job-control.md)
- [m27-task-stacks](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m27-task-stacks.md)
- [m28-three-isa-timers](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m28-three-isa-timers.md)
- [m28-x86-preemption](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m28-x86-preemption.md)
- [m29-real-time](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m29-real-time.md)
- [m31-timekeeping](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m31-timekeeping.md)
- [m38-native-gcc-vm-review](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m38-native-gcc-vm-review.md)
- [m39-packages](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m39-packages.md)
- [m40-real-archive-tools](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m40-real-archive-tools.md)
- [m41-real-gnu-pipeline](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m41-real-gnu-pipeline.md)
- [m42-pthread](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m42-pthread.md)
- [m43-zstd-shared-fs-signals](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m43-zstd-shared-fs-signals.md)
- [m44-thread-group-lifecycle](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m44-thread-group-lifecycle.md)
- [m45-robust-futex](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m45-robust-futex.md)
- [m46-thread-execve](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m46-thread-execve.md)
- [m47-thread-signals](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m47-thread-signals.md)
- [m48-real-parallel-gnu](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m48-real-parallel-gnu.md)
- [m49-real-concurrency-capacity](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m49-real-concurrency-capacity.md)
- [m51-native-git](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m51-native-git.md)
- [m52-native-python](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m52-native-python.md)
- [m53-original-vim](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m53-original-vim.md)
- [m54-integrated-dev-workflow](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m54-integrated-dev-workflow.md)
- [m58-original-prebuilt-runtime](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m58-original-prebuilt-runtime.md)
- [m59-original-worktree](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m59-original-worktree.md)
- [m65-procfs](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m65-procfs.md)
- [m66-monitor](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m66-monitor.md)
- [m66-symlinks](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m66-symlinks.md)
- [m67-process-control](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m67-process-control.md)
- [m67-tmp](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m67-tmp.md)
- [m68-lifecycle](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m68-lifecycle.md)
- [m69-rename](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m69-rename.md)
- [m69-trace-prerequisites](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m69-trace-prerequisites.md)
- [m71-remote-vm](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m71-remote-vm.md)
- [m80-shebang](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m80-shebang.md)
- [m81-vm-mmu-boundary](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m81-vm-mmu-boundary.md)
- [m82-process](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m82-process.md)
- [m83-elf-user-abi](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m83-elf-user-abi.md)
- [m84-fs-syscall](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m84-fs-syscall.md)
- [m85-final-kernel-split](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m85-final-kernel-split.md)
- [m90-riscv64-m0](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m90-riscv64-m0.md)
- [m91-rv64-page-tables](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m91-rv64-page-tables.md)
- [m92-aarch64-m0](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m92-aarch64-m0.md)
- [m93-aarch64-user-pt](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m93-aarch64-user-pt.md)
- [m94-rv64-real-user-trap](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m94-rv64-real-user-trap.md)
- [m95-rv64-real-elf](https://github.com/yituanxing/na-seed/blob/b2f2c994558c7ef7c7de2057734e9d91eab462d1/docs/m95-rv64-real-elf.md)

M38–M49 and M51–M54 individual manual diagnostics now select their original
profile in `alpine-focused-native.yml`. The original probe paths, three ISAs,
job/step limits, CPIO gates, M42 compiler/serial evidence and failure artifact
paths are preserved; the grouped M50 workload matrix is unchanged.
