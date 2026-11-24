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
