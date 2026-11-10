#!/bin/sh
# H10 extends H8's signature-verified original Alpine NVMe ext4 fixture by
# adding ONLY a shell workload. No host-generated HTTP source is copied into
# the disk, ISO or rescue CPIO; the original signed ELF bytes are unchanged.
set -eu

out=build/h10-qemu-only
parent=build/h8-qemu-only
image="$out/h10-alpine.ext4"
before="$out/h10-media-before.sha256"
rescue="$out/rescue.cpio"
verify="$out/fixture/disk-verify"
for tool in sha256sum mke2fs debugfs cmp cp mkdir; do
    command -v "$tool" >/dev/null || {
        echo "[H10 HOST FAIL] missing $tool" >&2
        exit 1
    }
done
for path in "$image" "$before" "$rescue"; do
    [ ! -L "$path" ] || {
        echo "[H10 HOST FAIL] symlinked output $path" >&2
        exit 1
    }
done

# H8's stage owner verifies official Alpine's original pinned signed APKs,
# its ext4 readback, and a rescue archive with no ELF. Never restage altered
# pre-existing H10 media: only reuse the exact immutable SHA-pinned image.
sh scripts/stage-h8-alpine-nvme.sh
(cd "$parent" && sha256sum -c h8-media-before.sha256)
sha256sum -c "$parent/h8-original-alpine-elfs.sha256"
test -s "$parent/h8-alpine.ext4" && test -s "$parent/rescue.cpio"
mkdir -p "$out" "$verify"
if [ -e "$image" ]; then
    [ -f "$image" ] && [ -s "$before" ] &&
    (cd "$out" && sha256sum -c h10-media-before.sha256 >/dev/null) || {
        echo '[H10 HOST FAIL] existing unknown/mutated H10 image' >&2
        exit 1
    }
else
    cp "$parent/h8-alpine.ext4" "$image"
    debugfs -w -R \
        "write scripts/h10-guest-e1000-http.sh /root/h10-guest-e1000-http.sh" \
        "$image" >/dev/null 2>&1
    debugfs -w -R \
        "write scripts/h10-disk-only.txt /root/h10-disk-only.txt" \
        "$image" >/dev/null 2>&1
    (cd "$out" && sha256sum h10-alpine.ext4 > h10-media-before.sha256)
fi

# Read back each authentic ELF and both H10-only files from ext4 inodes.
# This is a real image verification, not mere host staging directory proof.
root="$parent/fixture/disk-tree"
for path in bin/busybox usr/bin/lua5.4 usr/bin/jq usr/bin/sqlite3 \
            root/h8-workload.sh root/h8-disk-only.txt; do
    filename=$(printf '%s' "$path" | tr / _)
    debugfs -R "dump /$path $verify/$filename" "$image" >/dev/null 2>&1
    cmp "$root/$path" "$verify/$filename" || {
        echo "[H10 HOST FAIL] altered Alpine image ELF/workload: $path" >&2
        exit 1
    }
done
for pair in \
    'root/h10-guest-e1000-http.sh scripts/h10-guest-e1000-http.sh' \
    'root/h10-disk-only.txt scripts/h10-disk-only.txt'; do
    set -- $pair
    path=$1
    original=$2
    filename=$(printf '%s' "$path" | tr / _)
    debugfs -R "dump /$path $verify/$filename" "$image" >/dev/null 2>&1
    cmp "$original" "$verify/$filename" || {
        echo "[H10 HOST FAIL] H10 HTTP workload not on NVMe disk: $path" >&2
        exit 1
    }
done
cp "$parent/rescue.cpio" "$rescue"
cp "$parent/h8-original-alpine-elfs.sha256" "$out/h10-original-alpine-elfs.sha256"
(cd "$out" && sha256sum -c h10-media-before.sha256)
echo '[H10 HOST FIXTURE PASS] signed original Alpine dynamic musl ELF on H8-derived 128MiB NVMe ext4 /; H10 script only on disk; no HTTP source in guest'
