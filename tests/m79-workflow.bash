#!/bin/bash
# One genuine multitool local workflow, all applications signed originals.
# Uses native shell pipes/background/wait, actual Vim Ex writes, Git index
# and object commits, Python stdlib JSON/hash/sqlite; zero guest build.
set -euo pipefail
export PATH=/usr/bin:/bin HOME=/tmp LC_ALL=C LANG=C TERM=vt100
export GIT_CONFIG_NOSYSTEM=1 GIT_TERMINAL_PROMPT=0 GIT_PAGER=cat
export PYTHONHOME=/usr PYTHONDONTWRITEBYTECODE=1 PYTHONUNBUFFERED=1
for tool in /bin/bash /usr/bin/vim /usr/bin/git /usr/bin/python3; do
  [[ -x "$tool" ]] || exit 1
done
[[ -n "$BASH_VERSION" ]]
[[ "$(/usr/bin/git --version)" == git\ version\ * ]]
[[ "$(/usr/bin/python3 --version)" == Python\ 3.* ]]
# Consume the complete producer stream: grep -q exits early and causes a real
# SIGPIPE in Vim under pipefail; scheduling makes that probe nondeterministic.
/usr/bin/vim --version | /bin/busybox grep -F 'VIM - Vi IMproved' >/dev/null
echo M79_ORIGINAL_TOOLS_PASS

root=/tmp/m79-project
/bin/busybox mkdir -p "$root" /tmp/m79-empty-git-template
printf 'alpha\nbeta\ngamma\n' > "$root/items.txt"
# Actual background subprocess, wait status and ordinary shell pipeline.
/bin/busybox cat "$root/items.txt" > "$root/source-copy.txt" &
worker=$!
wait "$worker"
/bin/busybox cmp -s "$root/items.txt" "$root/source-copy.txt"
[[ "$(/bin/busybox grep -c '^[a-z]' "$root/source-copy.txt")" == 3 ]]
echo M79_BASH_PIPE_BACKGROUND_PASS

/usr/bin/git -C "$root" init -q --template=/tmp/m79-empty-git-template
/usr/bin/git -C "$root" config user.name "NaSeed Workflow"
/usr/bin/git -C "$root" config user.email "naseed@example.invalid"
/usr/bin/git -C "$root" add items.txt
/usr/bin/git -C "$root" commit -qm "Initial source data"
[[ "$(/usr/bin/git -C "$root" rev-list --count HEAD)" == 1 ]]
/usr/bin/vim -Nu NONE -i NONE -n -N -es "$root/items.txt" \
  -c '%s/beta/BETA/' -c 'wq'
/usr/bin/git -C "$root" diff -- items.txt > "$root/actual.patch"
/bin/busybox grep -Fq '+BETA' "$root/actual.patch"
/bin/busybox grep -Fq -- '-beta' "$root/actual.patch"
echo M79_VIM_GIT_DIFF_PASS

/usr/bin/python3 -B /tests/m79-validate.py "$root"
echo M79_PYTHON_JSON_SQLITE_PASS
/usr/bin/git -C "$root" add items.txt
/usr/bin/git -C "$root" commit -qm "Vim edited shared data"
[[ "$(/usr/bin/git -C "$root" rev-list --count HEAD)" == 2 ]]
[[ "$(/usr/bin/git -C "$root" log -1 --format=%s)" == 'Vim edited shared data' ]]
[[ "$(/usr/bin/git -C "$root" show HEAD:items.txt | /bin/busybox grep -c '^BETA$')" == 1 ]]
/usr/bin/python3 -B /tests/m79-validate.py "$root" verify
[[ -z "$(/usr/bin/git -C "$root" status --porcelain --untracked-files=no)" ]]
echo M79_GIT_COMMIT_RECHECK_PASS
