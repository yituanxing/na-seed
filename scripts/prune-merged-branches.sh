#!/bin/sh
# Audit and (only with --apply) delete branches whose exact PR heads merged.
# Requires GitHub CLI 'gh' authenticated with repo write permissions.
# Usage: sh scripts/prune-merged-branches.sh [--apply]
set -eu
apply=0
case "${1:-}" in
    "") ;;
    --apply) apply=1 ;;
    *) echo "usage: $0 [--apply]" >&2; exit 2 ;;
esac
command -v gh >/dev/null 2>&1 || {
    echo "GitHub CLI (gh) required" >&2; exit 2;
}
repo=${NASEED_REPO:-$(gh repo view --json nameWithOwner --jq .nameWithOwner)}
[ "${repo#*/}" = "na-seed" ] || {
    echo "refusing to prune non-NaSeed repository: $repo" >&2; exit 2;
}
# Every entry MUST be a specific merged PR and its actual source branch.
# Intentionally exclude unmerged PR #70, design/, work/, staging/ and port/.
while read -r pr branch; do
    [ -n "$pr" ] || continue
    merged=$(gh api "repos/$repo/pulls/$pr" --jq '.merged_at // ""')
    pr_head=$(gh api "repos/$repo/pulls/$pr" --jq '.head.sha')
    pr_branch=$(gh api "repos/$repo/pulls/$pr" --jq '.head.ref')
    if [ -z "$merged" ] || [ "$pr_branch" != "$branch" ]; then
        echo "[SKIP] $branch PR #$pr is not a matching merged PR"
        continue
    fi
    # The current branch must still be the exact reviewed PR tip; refuse
    # branch reuse / post-merge commits even if the old PR was merged.
    current=$(gh api "repos/$repo/git/ref/heads/$branch" --jq '.object.sha' 2>/dev/null) || {
        echo "[GONE] $branch"
        continue
    }
    if [ "$current" != "$pr_head" ]; then
        echo "[SKIP] $branch moved since PR #$pr"
        continue
    fi
    if [ "$apply" -eq 0 ]; then
        echo "[DRY-RUN] PR #$pr merged: safe to delete $branch ($current)"
    else
        gh api -X DELETE "repos/$repo/git/refs/heads/$branch" >/dev/null
        echo "[DELETED] $branch from PR #$pr"
    fi
done <<'MERGED_PR_HEADS'
55 feature/m15-kill-process-groups
56 feature/m16-stop-continue-jobs
57 feature/m17-console-tty-serial-input
58 feature/m18-real-interactive-ash
59 feature/m19-multiarch-interactive-ash
60 feature/m20-tty-line-discipline
61 feature/m21-cooperative-tty-scheduler
62 feature/m22-tty-poll-event-readiness
63 feature/m23-real-foreground-tty-signals
64 feature/m24-standard-signal-pending
65 feature/m25-background-tty-jobs
66 feature/klib-portable-strings
67 feature/task-owned-kernel-stacks
68 feature/m28-x86-timer-preempt
69 feature/m28-rv64-arm64-preempt
71 feature/vfs-typed-open-file-description
72 feature/m29-clock-deadline-three-isa
73 feature/m30-native-user-signal-frames
MERGED_PR_HEADS
