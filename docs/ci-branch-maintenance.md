# NaSeed CI and Git hygiene — completed 2026-10-22

## Verified baseline
- M10–M14 are merged into five reviewed capability commits on `main`.
- PR #54 split the original single x86 smoke chain into independently
  executed `smoke`, `x86_fs`, and `x86_process` jobs; no original
  test script or real Linux binary oracle was removed.
- PR #54's complete native userspace CI: **SUCCESS**, eight jobs, run
  39701066788. Measured workflow wall time 4m52s vs 13m02s for prior
  main run 39699050816; a 62.7% wall-time reduction on these runs.
- The independent ARM64 full-kernel gate also passed on PR #54.
- The original Debian static BusyBox 17-case and native Alpine/musl
  7-case compatibility runs stay mandatory; controlled-source BusyBox
  remains an additional, not substituting, test route.

## Branch cleanup outcome
The verified one-time GitHub Actions cleanup, run 39701749264, exited
SUCCESS. It deleted **52** audited remote branch heads:
45 already-merged PR heads, five historical heads with byte-identical
trees at their consolidated M10–M14 milestones, and two explicitly
superseded duplicate M13 PR branches. Commit and PR histories remain.

Three remote branches remain deliberately:
- `main` — canonical source.
- `design/arch-contract-v1` (#31) — distinct design document.
- `port/m96-aarch64-el0-svc` (#44) — previous unmerged architecture
  work; review semantic differences before deleting.

The single-use destructive cleanup workflow and obsolete dry-run script
were removed from `main` after successful completion. Their history is
preserved in Git.

## Running CI
The userspace workflow retains all existing native real-kernel checks,
PR-only cancellation of superseded runs, and the current three-way x86
split. Pure docs / completed-cleanup-file main pushes no longer launch
the expensive userspace matrix; PRs still receive full coverage.
Never treat the fast suite as a substitute for the separate ARM64 gate.

On measured PR #54, the longest job is RV64 Debian static BusyBox
17-case parity plus Alpine native parity (about 4.8 minutes), followed
by x86 VFS/archive (about 4.2 minutes). Do not further fragment tests
without a significant net speed or feedback-quality benefit.
