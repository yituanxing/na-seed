#!/bin/sh
# Fail closed if ANY copied Linux v6.14 source changed or if extra source is
# added without a reviewed upstream provenance record.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
manifest=scripts/linux-virtgpu-v6.14.blobs
test -s "$manifest"
count=0
tab=$(printf '\t')
while IFS="$tab" read -r expected path; do
    case "$expected" in ''|\#*) continue ;; esac
    case "$path" in third_party/linux-virtgpu/*) ;; *)
        echo "unexpected vendor path in manifest: $path" >&2; exit 1 ;;
    esac
    if ! test -f "$path" || test -L "$path"; then
        echo "missing or symlinked upstream vendor file: $path" >&2
        exit 1
    fi
    actual=$(git hash-object -- "$path")
    if test "$actual" != "$expected"; then
        echo "upstream file differs from Linux v6.14: $path" >&2
        echo "expected $expected, got $actual" >&2
        exit 1
    fi
    count=$((count + 1))
done < "$manifest"
actual_count=$(find third_party/linux-virtgpu -type f | wc -l | tr -d ' ')
symlinks=$(find third_party/linux-virtgpu -type l | wc -l | tr -d ' ')
if test "$count" -ne "$actual_count" || test "$symlinks" -ne 0; then
    echo "vendor tree/manifest mismatch: tracked=$count files=$actual_count links=$symlinks" >&2
    exit 1
fi
echo "PASS: $count Linux v6.14 VirtIO-GPU/DRM upstream Git blobs exactly preserved"
