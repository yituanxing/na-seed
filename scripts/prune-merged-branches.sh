#!/bin/sh
# Dry-run by default. --apply requires Git push access to delete ONLY branch
# heads of PRs that GitHub already marks as merged (audit: 2026-10-22).
# Closed-but-unmerged or superseded heads M10-M14, M9.6, design V1 are
# deliberately excluded; inspect them before considering deletion.
set -eu
case "${1:-}" in
  ""|--dry-run) apply=0 ;;
  --apply) apply=1 ;;
  *) echo 'Usage: sh scripts/prune-merged-branches.sh [--dry-run|--apply]' >&2; exit 2 ;;
esac
remote="${NASEED_REMOTE:-origin}"
url="$(git remote get-url "$remote")"
case "$url" in
  *github.com*yituanxing/na-seed*) ;;
  *) echo "[REFUSE] unexpected remote $remote: $url" >&2; exit 1 ;;
esac
echo "[BRANCHES] audited 44 already-merged PR heads; remote=$remote; apply=$apply"
while IFS= read -r branch; do
  [ -n "$branch" ] || continue
  if ! git ls-remote --exit-code --heads "$remote" "refs/heads/$branch" >/dev/null 2>&1; then
    echo "[SKIP] already absent: $branch"
    continue
  fi
  if [ "$apply" -eq 1 ]; then
    echo "[DELETE] $branch"
    git push "$remote" --delete "$branch"
  else
    echo "[DRY RUN] would delete $branch"
  fi
done <<'MERGED_BRANCHES'
work/m0-boot-memory-serial
work/m1-userspace-syscall
work/m2-real-bash-probe
work/m3-debian-prebuilt-userspace
work/m4-alpine-dynamic
work/m45-preflight-audit
work/m46-limine-cmdline-user-argv
work/linux-x86-64-syscall-nr
work/linux-x86-64-errno-uapi
feature/m5-cpio-readonly
feature/m51-archive-only-boot
feature/m52-cpio-executable-selection
feature/m53-real-shell-process
feature/m54-ash-execve
feature/m61-task-lifetime
feature/m62-shell-spawn
feature/m63-busybox-pipes
feature/m64-pipe-block-wakeup
feature/m65-pipe-lifecycle
feature/m66-busybox-path-symlinks
feature/m67-busybox-tmp-redirection
feature/m68-busybox-dir-lifecycle
feature/m69-busybox-cp-mv-rename
refactor/m70-tmpfs-backend-boundary
refactor/m71-fd-description-lifetime
refactor/m72-path-walk-boundary
refactor/m73-pipe-backend-boundary
refactor/m74-boot-image-initramfs
feature/m75-resource-reuse-stress
feature/m80-real-shebang-execve
refactor/m8-naos-kernel-foundations
refactor/m8-vm-mmu-boundary
refactor/m82-process-foundations
refactor/m83-elf-user-abi
refactor/m84-fs-syscall-boundary
refactor/m85-finalize-init-exec-dispatch
port/riscv64-m0-first
port/m91-rv64-multimode-page-tables
port/m92-aarch64-limine-m0
port/m93-arm64-user-page-tables
port/m94-rv64-real-user-ecall
port/m95-rv64-real-user-elf
integrate/m10-m12-clean-history
integrate/m13-m14-clean-history
MERGED_BRANCHES
