#!/bin/sh
# M105: verify an ORIGINAL, signature-checked Alpine ET_DYN BusyBox loaded
# from a genuine VirtIO-backed ext4 inode, and its musl PT_INTERP loaded
# via VFS read_at (either ext4 or tmpfs). CPIO has NO dynamic ELF payload.
# Both kernel and disk I/O are provided by the existing native QEMU harness.
set -eu

if [ "$#" -ne 1 ]; then
    echo 'usage: sh scripts/probe-m105-vfs-dynamic-interp-ext4.sh amd64|riscv64|arm64' >&2
    exit 2
fi

arch=$1
case "$arch" in
    amd64)
        loader=ld-musl-x86_64.so.1
        machine='Advanced Micro Devices X86-64'
        kernel=build/virtio-x86/kernel.elf
        harness=scripts/probe-x86-busybox-parity.sh
        log_dir=build/busybox-source-parity-x86 ;;
    riscv64)
        loader=ld-musl-riscv64.so.1
        machine='RISC-V'
        kernel=build/port-riscv64/full/kernel.elf
        harness=scripts/probe-riscv64-busybox-matrix.sh
        log_dir=build/port-riscv64/full/matrix-source ;;
    arm64)
        loader=ld-musl-aarch64.so.1
        machine=AArch64
        kernel=build/port-aarch64/full/kernel.elf
        harness=scripts/probe-aarch64-busybox-matrix.sh
        log_dir=build/port-aarch64/full/matrix-source ;;
    *) echo "[M105 FAIL] unsupported ISA: $arch" >&2; exit 2 ;;
esac

for command in readelf debugfs cpio cmp grep sha256sum; do
    command -v "$command" >/dev/null 2>&1 || {
        echo "[M105 FAIL] missing host tool: $command" >&2
        exit 1
    }
done
test -s "$kernel" || {
    echo "[M105 FAIL] missing REAL VirtIO+lwext4 kernel: $kernel" >&2
    exit 1
}
base_disk=build/external/lwext4/ext4.img
test -s "$base_disk" || {
    echo "[M105 FAIL] missing real ext4 fixture: $base_disk" >&2
    exit 1
}

work="build/m105-vfs-dynamic-interp/$arch"
mkdir -p "$work"
sh scripts/fetch-busybox-fixture.sh "$arch" "$work/static-busybox"
if [ "$arch" = amd64 ]; then
    sh scripts/fetch-alpine.sh "$work/alpine-source"
    dynamic="$work/alpine-source/alpine/bin/busybox"
    ldso="$work/alpine-source/alpine/lib/$loader"
else
    sh scripts/fetch-alpine-native.sh "$arch" "$work/alpine-source"
    dynamic="$work/alpine-source/bin/busybox"
    ldso="$work/alpine-source/lib/$loader"
fi
test -s "$dynamic" && test -s "$ldso"
readelf -h "$dynamic" | grep -Fq "$machine"
readelf -h "$ldso" | grep -Fq "$machine"
readelf -h "$dynamic" | grep -Eq 'Type:[[:space:]]+DYN'
readelf -l "$dynamic" | grep -Fq "[Requesting program interpreter: /lib/$loader]"

# Embed only ORIGINAL Alpine package bytes in a writable raw ext4 block
# disk. Never offer either dynamic image through Limine/CPIO or BootFS.
disk="$work/virtio-musl-ext4.img"
cp "$base_disk" "$disk"
debugfs -w -R 'mkdir /lib' "$disk" >/dev/null 2>&1
debugfs -w -R 'mkdir /m105' "$disk" >/dev/null 2>&1
debugfs -w -R "write $dynamic /m105/busybox" "$disk" >/dev/null 2>&1
debugfs -w -R "write $ldso /lib/$loader" "$disk" >/dev/null 2>&1
debugfs -w -R 'set_inode_field /m105/busybox mode 0100755' "$disk" >/dev/null 2>&1
debugfs -w -R "set_inode_field /lib/$loader mode 0100755" "$disk" >/dev/null 2>&1
# debugfs can return zero despite some logical errors. Recover and compare
# exact bytes, then verify executable permission, BEFORE booting QEMU.
: > "$work/recovered-busybox"
: > "$work/recovered-ldso"
debugfs -R "dump /m105/busybox $work/recovered-busybox" "$disk" 2>/dev/null || :
debugfs -R "dump /lib/$loader $work/recovered-ldso" "$disk" 2>/dev/null || :
cmp "$dynamic" "$work/recovered-busybox" || {
    echo '[M105 FAIL] official dynamic ELF not preserved on ext4' >&2; exit 1;
}
cmp "$ldso" "$work/recovered-ldso" || {
    echo '[M105 FAIL] official musl ELF not preserved on ext4' >&2; exit 1;
}
debugfs -R 'stat /m105/busybox' "$disk" 2>/dev/null | grep -Eq 'Mode:[[:space:]]+0755'
debugfs -R "stat /lib/$loader" "$disk" 2>/dev/null | grep -Eq 'Mode:[[:space:]]+0755'

for mode in ext4 tmpfs; do
    mode_work="$work/$mode"
    mkdir -p "$mode_work"
    sh scripts/stage-busybox-rootfs.sh "$work/static-busybox" "$mode_work"
    # The CPIO carries a symlink only: musl ELF's inode lives on the ext4
    # disk in mode=ext4, or is copied into writable tmpfs in mode=tmpfs.
    # The Alpine program ALWAYS lives on the real physical ext4 disk.
    mkdir -p "$mode_work/rootfs/lib"
    case "$mode" in
        ext4)
            target="/mnt/lib/$loader"
            args="sh -c 'set -e; /bin/busybox readlink /lib/$loader; exec /mnt/m105/busybox echo M105-EXT4-DYN-OK'"
            expected=M105-EXT4-DYN-OK ;;
        tmpfs)
            target="/tmp/$loader"
            args="sh -c 'set -e; /bin/busybox readlink /lib/$loader; /bin/busybox cp /mnt/lib/$loader /tmp/$loader; /bin/busybox cmp /mnt/lib/$loader /tmp/$loader; exec /mnt/m105/busybox echo M105-TMPFS-DYN-OK'"
            expected=M105-TMPFS-DYN-OK ;;
    esac
    link="$mode_work/rootfs/lib/$loader"
    if [ -e "$link" ] && [ ! -L "$link" ]; then
        echo "[M105 FAIL] regular CPIO interpreter payload already exists: $link" >&2
        exit 1
    fi
    ln -sfn "$target" "$link"
    test -L "$mode_work/rootfs/lib/$loader"
    test "$(readlink "$mode_work/rootfs/lib/$loader")" = "$target"
    (
        cd "$mode_work/rootfs"
        find . -print | LC_ALL=C sort | cpio -o -H newc --quiet --owner=0:0
    ) > "$mode_work/rootfs.cpio"
    # Verify the ACTUAL archive, not merely the source staging directory.
    # GNU cpio can render its verbose symlink listing differently depending
    # on version and whether entries begin with './'. Inspect exact archive
    # member names and independently extract ONLY the interpreter symlink.
    if ! cpio -it < "$mode_work/rootfs.cpio" > "$mode_work/cpio-names.txt" 2> "$mode_work/cpio-errors.txt"; then
        echo "[M105 FAIL] cannot enumerate packed CPIO $arch/$mode" >&2
        sed -n '1,60p' "$mode_work/cpio-errors.txt" >&2
        exit 1
    fi
    if ! awk -v expected="lib/$loader" '
        {
            name=$0;
            sub(/^\.\//,"",name);
            if (name==expected) count++;
            if (name=="m105/busybox" || name=="mnt/m105/busybox" ||
                name=="busybox" || name=="dynamic-busybox" ||
                name=="mnt/dynamic-busybox" || name=="bin/dynamic-busybox") bad=1;
        }
        END { if(count!=1 || bad) exit 1 }
    ' "$mode_work/cpio-names.txt"; then
        echo "[M105 FAIL] CPIO interpreter member absent/duplicated or contains dynamic main ELF: $arch/$mode" >&2
        sed -n '1,100p' "$mode_work/cpio-names.txt" >&2
        exit 1
    fi
    verify_dir=$(mktemp -d "$mode_work/verify-archive.XXXXXX")
    archive_abs="$(pwd)/$mode_work/rootfs.cpio"
    if ! (
        cd "$verify_dir"
        cpio -id --quiet "lib/$loader" "./lib/$loader" \
            bin/busybox ./bin/busybox < "$archive_abs"
    ) > "$mode_work/cpio-verify.log" 2>&1; then
        echo "[M105 FAIL] cannot extract packed interpreter member $arch/$mode" >&2
        sed -n '1,60p' "$mode_work/cpio-verify.log" >&2
        exit 1
    fi
    extracted_link="$verify_dir/lib/$loader"
    if [ ! -L "$extracted_link" ] || [ "$(readlink "$extracted_link")" != "$target" ]; then
        echo "[M105 FAIL] CPIO interpreter is not an exact symlink to $target ($arch/$mode)" >&2
        ls -ld "$extracted_link" >&2 || :
        sed -n '1,100p' "$mode_work/cpio-names.txt" >&2
        exit 1
    fi
    cmp "$work/static-busybox" "$verify_dir/bin/busybox" || {
        echo "[M105 FAIL] CPIO /bin/busybox differs from the verified static Debian ELF $arch/$mode" >&2
        exit 1
    }

    id="m105-$mode"
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$id" cpio "$args" "$expected" - /bin/busybox > "$mode_work/cases.tsv"
    # Dedicated one-case manifest prevents a different CPIO interpreter
    # symlink from leaking into the next boot. Every QEMU exit is enforced
    # by the existing architecture-specific parity harness.
    NAS_ENABLE_LWEXT4=1 NAS_ENABLE_VIRTIO_BLK=1 \
    NAS_ACCEPTANCE_KERNEL="$kernel" NAS_VIRTIO_BLK_IMAGE="$disk" \
    NAS_ACCEPTANCE_CPIO="$mode_work/rootfs.cpio" \
    BUSYBOX_PARITY_BINARY="$work/static-busybox" \
    BUSYBOX_PARITY_MANIFEST="$mode_work/cases.tsv" \
    BUSYBOX_PARITY_CASE="$id" sh "$harness"

    clean="$log_dir/$id.clean.log"
    test -s "$clean"
    grep -Fxq "$target" "$clean" || {
        echo "[M105 FAIL] wrong /lib musl symlink in guest $arch/$mode" >&2
        exit 1
    }
    grep -Fq '[DYN] real musl interpreter loaded, entry=' "$clean" || {
        echo "[M105 FAIL] no genuine PT_INTERP musl loading $arch/$mode" >&2
        exit 1
    }
    grep -Fxq '[LWEXT4 GUEST] real external VirtIO-Blk ext4 disk mounted at /mnt' "$clean" || {
        echo "[M105 FAIL] genuine external VirtIO-backed ext4 mount absent $arch/$mode" >&2
        exit 1
    }
    grep -Fxq '[VIRTIO-BLK] persistent block device write+flush enabled' "$clean" || {
        echo "[M105 FAIL] real writable VirtIO-Blk device unproven $arch/$mode" >&2
        exit 1
    }
    grep -Fq '[EXEC] replaced process with lwext4 ELF: /mnt/m105/busybox' "$clean" || {
        echo "[M105 FAIL] Alpine ELF did not exec from ext4 $arch/$mode" >&2
        exit 1
    }
    grep -Fxq "$expected" "$clean" || {
        echo "[M105 FAIL] actual Alpine dynamic ELF stdout absent $arch/$mode" >&2
        exit 1
    }
    sh scripts/check-ext4-fsck-clean.sh "$disk"
    echo "[M105 REAL QEMU PASS] $arch: original Alpine ET_DYN on ext4 + musl via $mode read_at"
done

# Guest only reads the real block disk. Verify the exact upstream file bytes
# and ext4 metadata again AFTER both QEMU processes have exited.
: > "$work/after-busybox"
: > "$work/after-ldso"
debugfs -R "dump /m105/busybox $work/after-busybox" "$disk" 2>/dev/null || :
debugfs -R "dump /lib/$loader $work/after-ldso" "$disk" 2>/dev/null || :
cmp "$dynamic" "$work/after-busybox"
cmp "$ldso" "$work/after-ldso"
echo "[PASS] M105 $arch: two REAL native QEMU boots, ext4/tmpfs musl provenance, e2fsck"
