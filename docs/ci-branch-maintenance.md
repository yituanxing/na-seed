# CI and branch hygiene (2026-10-22)

## Baseline
M10-M14 were consolidated into five milestones on `main`; the exact main
`ccc916c1` has already passed both full userspace and ARM64 gate push workflows.
At the time of audit there were 54 remote branches, zero open PRs, and 53
closed PRs. Among the remaining heads, 44 correspond to **merged** PRs;
nine heads correspond to **closed but not merged** PRs.

## CI ownership
- `smoke`: fast shared ABI / compile preflight plus authentic x86 Bash and
  canonical Debian static BusyBox parity. Preserves the existing job name.
- `x86_fs`: authentic Alpine filesystem, CPIO, FD, path and shebang workloads.
- `x86_process`: authentic Alpine pipe, process, fork/exec, lifecycle workloads.
  Each job builds its own kernel and stages original Alpine binaries; no job
  secretly relies on another job's working directory.
- `riscv64_m0`, `aarch64_m0` and `source_busybox` remain intact, including
  immutable distro oracles, controlled original BusyBox source and matrices.
- The separate ARM64 full-kernel workflow is still mandatory for its Debian
  17-case and Alpine 7-case compatibility gates. Its push and PR path filters
  now use the identical dependency list.

Only independent x86 suites were parallelized. Existing coverage and exact
runner scripts were not removed. Both workflows retain PR-only cancellation
of superseded runs; main pushes remain uncanceled. Each x86 shard uploads its
own failure serial evidence. Never treat a fast pass as a substitute for the
full QEMU gates.

## Remote branch cleanup
`sh scripts/prune-merged-branches.sh` is **dry-run** and lists just the 44
branches whose PR was actually merged according to GitHub at audit time.
Run `sh scripts/prune-merged-branches.sh --apply` in an authenticated local
clone to delete those audited remote heads. The script checks the origin
repository before acting. Git commit and PR history are retained.

The nine closed-but-unmerged branches are intentionally **not deleted**:
- `design/arch-contract-v1` (#31): unique design document.
- `port/m96-aarch64-el0-svc` (#44): unmerged predecessor; review its
  runtime contribution before removal.
- `refactor/shared-kernel-arch-boundaries` (#45) and feature branches
  M11-M14 (#46-51): superseded by #52 and #53, but their old commit
  ancestry differs from consolidated main history. Verify exact tree /
  semantic equivalence before pruning, never delete based solely on
  ahead/behind or a closed PR state.

Do not enable automatic deletion of superseded unmerged work without
reviewing its contents and the actual current `main` tree.
