#!/bin/sh
# Run only original signed Alpine Git in the guest, with writable tmpfs.
set -eu
stage=boot
trap 'rc=$?; if [ "$rc" -ne 0 ]; then echo "[FAIL] M51 genuine Git stage=$stage rc=$rc" >&2; fi' EXIT
PATH=/usr/bin:/bin
HOME=/tmp
LC_ALL=C
GIT_CONFIG_NOSYSTEM=1
GIT_TERMINAL_PROMPT=0
GIT_PAGER=cat
export PATH HOME LC_ALL GIT_CONFIG_NOSYSTEM GIT_TERMINAL_PROMPT GIT_PAGER
git=/usr/bin/git
test -x "$git"
case "$("$git" --version)" in 'git version '*) ;; *) echo '[FAIL] invalid Git banner' >&2; exit 1;; esac
echo M51_ORIGINAL_SIGNED_GIT_PASS

stage=init
repo=/tmp/m51-repository
/bin/busybox mkdir -p "$repo" /tmp/m51-empty-template
"$git" -C "$repo" init -q --template=/tmp/m51-empty-template
"$git" -C "$repo" config user.name "NaSeed Test"
"$git" -C "$repo" config user.email "naseed@example.invalid"
test "$("$git" -C "$repo" rev-parse --is-inside-work-tree)" = true
echo M51_GIT_INIT_PASS

stage=first-commit
printf 'int answer(void) {\n    return 1;\n}\n' > "$repo/main.c"
"$git" -C "$repo" add main.c
"$git" -C "$repo" commit -qm "first commit"
test "$("$git" -C "$repo" rev-list --count HEAD)" = 1
test "$("$git" -C "$repo" log -1 --format=%s)" = "first commit"
test -z "$("$git" -C "$repo" status --porcelain)"
echo M51_GIT_OBJECT_INDEX_COMMIT_PASS

stage=diff-second-commit
printf 'int answer(void) {\n    return 7;\n}\n' > "$repo/main.c"
"$git" -C "$repo" diff -- main.c > /tmp/m51-diff.txt
/bin/busybox grep -Fq '+    return 7;' /tmp/m51-diff.txt
"$git" -C "$repo" add main.c
"$git" -C "$repo" commit -qm "second commit"
test "$("$git" -C "$repo" rev-list --count HEAD)" = 2
test "$("$git" -C "$repo" log -1 --format=%s)" = "second commit"
test -z "$("$git" -C "$repo" status --porcelain)"
echo M51_GIT_DIFF_HISTORY_PASS

stage=branch-checkout
original_branch=$("$git" -C "$repo" symbolic-ref --short HEAD)
"$git" -C "$repo" branch m51-topic
"$git" -C "$repo" switch -q m51-topic
test "$("$git" -C "$repo" branch --show-current)" = m51-topic
printf 'branch only\n' > "$repo/topic.txt"
"$git" -C "$repo" add topic.txt
"$git" -C "$repo" commit -qm "topic commit"
test "$("$git" -C "$repo" rev-list --count HEAD)" = 3
"$git" -C "$repo" switch -q "$original_branch"
test ! -e "$repo/topic.txt"
test "$("$git" -C "$repo" rev-list --count HEAD)" = 2
"$git" -C "$repo" switch -q m51-topic
test -f "$repo/topic.txt"
test "$("$git" -C "$repo" log -1 --format=%s)" = "topic commit"
test -z "$("$git" -C "$repo" status --porcelain)"
echo M51_GIT_REFS_CHECKOUT_PASS
echo M51_GIT_SUITE_PASS
