#!/bin/sh
# Host failures-only oracle. Native QEMU and post-guest raw disk remain required.
set -eu
cd "$(dirname "$0")/.."
sh -n scripts/probe-m190-persistent-desktop.sh tests/m190-durable-check.sh
python3 -B -m py_compile scripts/m190-qmp-persistent.py \
    scripts/m187-qmp-verify.py scripts/m190-durable-fixture.py
python3 scripts/m187-qmp-verify.py --help | grep -Fq -- '--m190-write'
python3 scripts/m187-qmp-verify.py --help | grep -Fq -- '--m190-reopen'
grep -Fq 'scripts/m190-qmp-persistent.py' scripts/m187-qmp-verify.py
grep -Fq 'tests/m190-durable-check.sh' scripts/m174-software-rootfs-copies.tsv
grep -Fq 'sh scripts/probe-m190-persistent-desktop.sh' scripts/probe-m180-fs-user.sh
grep -Fq 'M190_DURABLE_SAVED_FUNCTION_PASS' user/desktop/naseed-terminal.c
grep -Fq 'M190_VECTORED_IRQ_FS_FUNCTION_PASS' user/desktop/naseed-terminal.c
grep -Fq 'M190_VECTORED_IRQ_FS_FUNCTION_PASS' scripts/m190-qmp-persistent.py
grep -Fq 'nas_fs_io_vectors_import' kernel/fs/user_broker.c
grep -Fq 'nr==NAS_SC_readv || nr==NAS_SC_writev' kernel/fs/user_broker.c
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
printf 'm190vimoriginal\nm190nanopersist\n' > "$work/okay"
M190_TEST_TARGET="$work/okay" sh tests/m190-durable-check.sh > "$work/positive"
grep -Fxq M190_DURABLE_EDITOR_CONTENT_VERIFIED "$work/positive"
printf 'm190vimoriginal\nm190nanopersist' > "$work/truncated"
printf 'm190vimoriginal\nm190nanopersistX\n' > "$work/altered"
for invalid in truncated altered; do
    if M190_TEST_TARGET="$work/$invalid" sh tests/m190-durable-check.sh \
       >/dev/null 2>&1; then
        echo '[M190 HOST FAIL] incorrect content accepted' >&2; exit 1
    fi
done
ln -s "$work/okay" "$work/symlink"
if M190_TEST_TARGET="$work/symlink" sh tests/m190-durable-check.sh \
   >/dev/null 2>&1; then
    echo '[M190 HOST FAIL] symlink accepted' >&2; exit 1
fi
echo '[M190 HOST DURABLE ORACLE PASS] exact bytes and 3 negative cases; QEMU still required'
