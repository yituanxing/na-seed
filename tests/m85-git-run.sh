#!/bin/sh
# Original signed Alpine Git on a real persistent ext4 block device.
# --host-first/--host-replay run the same workload against ordinary Linux
# for a semantic oracle; only --first/--replay are accepted by guest CI.
set -eu

case "${1:-}" in
    --first|--replay)
        [ "$#" -eq 1 ] || exit 2
        git_binary=/usr/bin/git
        root=/mnt
        host_oracle=0 ;;
    --host-first|--host-replay)
        [ "$#" -eq 2 ] || exit 2
        git_binary=$(command -v git)
        root=$2
        host_oracle=1 ;;
    *) echo 'usage: m85-git-run.sh --first|--replay|--host-first ROOT|--host-replay ROOT' >&2; exit 2 ;;
esac
phase=${1#--}
case "$phase" in host-*) phase=${phase#host-} ;; esac
[ -x "$git_binary" ] || { echo '[M85 FAIL] original Git ELF missing' >&2; exit 1; }
case "$("$git_binary" --version)" in 'git version '*) ;; *) exit 1 ;; esac

PATH=/usr/bin:/bin
HOME=/tmp
LC_ALL=C
LANG=C
GIT_CONFIG_NOSYSTEM=1
GIT_TERMINAL_PROMPT=0
GIT_PAGER=cat
GIT_AUTHOR_DATE='2026-01-01T00:00:00 +0000'
GIT_COMMITTER_DATE='2026-01-01T00:00:00 +0000'
export PATH HOME LC_ALL LANG GIT_CONFIG_NOSYSTEM GIT_TERMINAL_PROMPT GIT_PAGER
export GIT_AUTHOR_DATE GIT_COMMITTER_DATE

bb() {
    if [ "$host_oracle" -eq 1 ]; then "$@"; else /bin/busybox "$@"; fi
}
fail() { echo "[M85 GIT FAIL] stage=$stage: $*" >&2; exit 1; }
repo=$root/m85-git
template=$root/m85-empty-template
result=$root/m85-result.txt
main_head=$root/m85-main-head.txt
topic_head=$root/m85-topic-head.txt
pack_name=$root/m85-pack-name.txt
git_repo() { "$git_binary" -C "$repo" "$@"; }
stage=authentic-git
echo M85_ORIGINAL_SIGNED_GIT_PASS

if [ "$phase" = first ]; then
    stage=initialize
    [ ! -e "$repo" ] || fail 'physical ext4 repository unexpectedly pre-exists'
    bb mkdir -p "$repo/src" "$repo/docs" "$template"
    git_repo init -q --template="$template" || fail 'git init'
    git_repo config user.name 'NaSeed Original Git'
    git_repo config user.email 'naseed@example.invalid'
    git_repo config gc.auto 0
    git_repo config core.fsync all
    original_branch=$(git_repo symbolic-ref --short HEAD) || fail 'initial branch'
    printf 'int answer(void) { return 7; }\n' > "$repo/src/main.c"
    printf 'stage one\n' > "$repo/docs/notes.txt"
    git_repo add src/main.c docs/notes.txt
    git_repo commit -qm 'first persistent commit' || fail 'first commit'
    [ "$(git_repo rev-list --count HEAD)" -eq 1 ] || fail 'first history'
    echo M85_EXT4_GIT_OBJECT_INDEX_PASS

    stage=atomic-rename-branch
    git_repo mv docs/notes.txt docs/guide.txt || fail 'git mv on ext4'
    printf 'int answer(void) { return 42; }\n' > "$repo/src/main.c"
    printf 'int helper(void) { return 1; }\n' > "$repo/src/helper.c"
    git_repo add src/main.c src/helper.c
    git_repo commit -qm 'renamed and extended source'
    [ "$(git_repo rev-list --count HEAD)" -eq 2 ] || fail 'second history'
    git_repo switch -q -c m85-topic || fail 'create branch'
    printf 'feature persists\n' > "$repo/topic.txt"
    git_repo add topic.txt
    git_repo commit -qm 'topic branch commit'
    [ "$(git_repo rev-list --count HEAD)" -eq 3 ] || fail 'topic history'
    git_repo rev-parse HEAD > "$topic_head"
    git_repo switch -q "$original_branch" || fail 'restore main branch'
    [ ! -e "$repo/topic.txt" ] || fail 'branch checkout left a topic file'
    git_repo rev-parse HEAD > "$main_head"
    [ -z "$(git_repo status --porcelain)" ] || fail 'dirty main branch'
    echo M85_EXT4_GIT_RENAME_BRANCH_PASS

    stage=pack-fsync
    git_repo gc --prune=now || fail 'original Git gc'
    git_repo repack -ad || fail 'original Git repack'
    git_repo fsck --full --strict || fail 'original Git fsck'
    pack_count=0
    for original_pack in "$repo"/.git/objects/pack/*.pack; do
        [ -s "$original_pack" ] || fail 'no nonempty genuine Git pack'
        pack_count=$((pack_count + 1))
        bb basename "$original_pack" > "$pack_name"
        [ -s "${original_pack%.pack}.idx" ] || fail 'missing pack index'
    done
    [ "$pack_count" -eq 1 ] || fail "expected one packed object store, saw $pack_count"
    printf 'NaSeed-M85-original-Git-persistent-ext4-PASS\n' > "$result"
    echo M85_EXT4_GIT_GC_REPACK_FSCK_PASS
    echo M85_FIRST_BOOT_PASS
else
    stage=replay-head-and-pack
    [ -d "$repo/.git" ] || fail 'Git metadata missing after reboot'
    [ "$(git_repo rev-parse HEAD)" = "$(bb cat "$main_head")" ] || fail 'main HEAD changed across reboot'
    [ "$(git_repo rev-list --count HEAD)" -eq 2 ] || fail 'main history changed'
    [ -f "$repo/docs/guide.txt" ] && [ ! -e "$repo/docs/notes.txt" ] ||
        fail 'atomic rename missing after reboot'
    bb grep -Fxq 'int answer(void) { return 42; }' "$repo/src/main.c" ||
        fail 'committed source corrupted across reboot'
    git_repo fsck --full --strict || fail 'Git object store corrupt after reboot'
    [ -s "$repo/.git/objects/pack/$(bb cat "$pack_name")" ] || fail 'pack disappeared'
    [ -z "$(git_repo status --porcelain)" ] || fail 'dirty replay worktree'
    echo M85_REPLAY_GIT_PERSISTENT_PACK_PASS

    stage=replay-branch
    git_repo switch -q m85-topic || fail 'topic branch lost across reboot'
    [ "$(git_repo rev-parse HEAD)" = "$(bb cat "$topic_head")" ] ||
        fail 'topic SHA changed across reboot'
    bb grep -Fxq 'feature persists' "$repo/topic.txt" || fail 'topic content corrupted'
    [ "$(git_repo rev-list --count HEAD)" -eq 3 ] || fail 'topic history changed'
    git_repo switch -q - || fail 'restore initial branch after replay'
    [ "$(git_repo rev-parse HEAD)" = "$(bb cat "$main_head")" ] ||
        fail 'main HEAD not restored'
    [ ! -e "$repo/topic.txt" ] || fail 'topic leakage on replay checkout'
    [ -z "$(git_repo status --porcelain)" ] || fail 'dirty final worktree'
    bb grep -Fxq 'NaSeed-M85-original-Git-persistent-ext4-PASS' "$result" ||
        fail 'first-boot result marker missing'
    echo M85_REPLAY_GIT_BRANCH_CHECKOUT_PASS
    echo M85_REPLAY_PASS
fi
