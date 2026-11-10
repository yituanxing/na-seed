#!/bin/sh
# Host oracle for the M189 guest checker and QMP integration.
# This does not claim an original Nano ran without the native QEMU stage.
set -eu
cd "$(dirname "$0")/.."
python3 -m py_compile scripts/m189-qmp-sequence.py scripts/m187-qmp-verify.py
sh -n scripts/probe-m187-vt100.sh tests/m189-editor-check.sh
python3 scripts/m187-qmp-verify.py --help | grep -Fq -- '--m189'
grep -Fq 'scripts/m189-qmp-sequence.py' scripts/m187-qmp-verify.py
grep -Fq 'M189_NANO_SAVED_FUNCTION_PASS' user/desktop/naseed-terminal.c
grep -Fq 'M189_PTY_SHELL_RESUMED_AFTER_EDITORS' user/desktop/naseed-terminal.c
grep -Fq 'tests/m189-editor-check.sh' scripts/m174-software-rootfs-copies.tsv
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
printf 'm188vimrealinput\nm189nanoroundtrip\n' > "$work/edited"
M189_TEST_TARGET="$work/edited" sh tests/m189-editor-check.sh > "$work/positive"
grep -Fxq M189_NANO_EDITED_CONTENT_VERIFIED "$work/positive"
# Fail closed if data is truncated, renamed, or subtly modified.
printf 'm188vimrealinput\nm189nanoroundtrip' > "$work/truncated"
if M189_TEST_TARGET="$work/truncated" sh tests/m189-editor-check.sh    > "$work/negative" 2>&1; then
    echo '[M189 HOST FAIL] truncated file passed byte oracle' >&2;exit 1
fi
printf 'm188vimrealinput\nm189nanoroundtrips\n' > "$work/altered"
if M189_TEST_TARGET="$work/altered" sh tests/m189-editor-check.sh    > "$work/negative" 2>&1; then
    echo '[M189 HOST FAIL] altered file passed byte oracle' >&2;exit 1
fi
ln -s "$work/edited" "$work/link"
if M189_TEST_TARGET="$work/link" sh tests/m189-editor-check.sh    > "$work/negative" 2>&1; then
    echo '[M189 HOST FAIL] symlink passed regular-file oracle' >&2;exit 1
fi
echo '[M189 HOST SESSION ORACLE PASS] exact content, truncation, mutation, symlink and native QMP wiring'
