#!/bin/sh
# Shared host staging/boot plumbing for the signed Debian physical ext4 probes.
# Callers retain package provenance, guest commands, init lifecycle, UART and
# independent before/after disk proofs. Never boot a substitute CPIO userland.

nas_debian_target() {
    deb_label=$1
    case "$port" in
        amd64) triplet=x86_64-linux-gnu; loader=ld-linux-x86-64.so.2; label=X86; cc=gcc ;;
        riscv64) triplet=riscv64-linux-gnu; loader=ld-linux-riscv64-lp64d.so.1; label=RV64; cc=riscv64-linux-gnu-gcc ;;
        arm64) triplet=aarch64-linux-gnu; loader=ld-linux-aarch64.so.1; label=ARM64; cc=aarch64-linux-gnu-gcc ;;
        *) echo "[$deb_label FAIL] unsupported architecture $port" >&2; exit 2 ;;
    esac
    . scripts/native-qemu-target.sh
}

nas_debian_require_tools() {
    [ "$deb_label" = M249 ] || set -- "$cc"
    for tool in python3 apt-get apt-cache dpkg-deb readelf cpio cmp sha256sum \
        mke2fs e2fsck debugfs sudo xorriso sgdisk mformat mmd mcopy "$@"; do
        command -v "$tool" >/dev/null || { echo "[$deb_label FAIL] missing $tool" >&2; exit 1; }
    done
}

nas_debian_copy_original() {
    resolved=$(readlink -f "$1")
    case "$resolved" in "$source"/*) : ;; *) echo "[$deb_label FAIL] $3" >&2; exit 1 ;; esac
    cp -L "$1" "$2"
    chmod 755 "$2"
    cmp "$1" "$2"
}

nas_debian_stage_base() {
    mkdir -p "$work"
    python3 scripts/m248-fetch-debian-multiarch.py --arch "$port" --out "$work/source"
    source=$(cd "$work/source/sysroot" && pwd -P)
    test -s "$manifest"
    # M249 boots dash directly; the later cases add only a test CTTY launcher.
    set -- "$1" "$2" "$root/usr/bin" "$root/usr/lib/$triplet" "$root/lib" "$root/lib64"
    [ "$deb_label" = M249 ] || set -- "$@" "$root/sbin"
    bin_error=$1; lib_error=$2; shift 2
    mkdir -p "$@" "$root/etc" "$root/dev" "$root/proc" "$root/sys" "$root/var/tmp" "$root/tmp"
    chmod 1777 "$root/tmp" "$root/var/tmp"
    for name in dash true cat wc printf; do
        nas_debian_copy_original "$source/usr/bin/$name" "$root/usr/bin/$name" "$bin_error"
    done
    for name in libc.so.6 "$loader"; do
        nas_debian_copy_original "$source/usr/lib/$triplet/$name" "$root/usr/lib/$triplet/$name" "$lib_error"
    done
    ln -sfn "../usr/lib/$triplet" "$root/lib/$triplet"
    if [ "$port" = amd64 ]; then
        ln -sfn "../usr/lib/$triplet/$loader" "$root/lib64/$loader"
    else
        ln -sfn "../usr/lib/$triplet/$loader" "$root/lib/$loader"
    fi
}

nas_debian_stage_launcher() {
    "$cc" -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
        tests/m250-debian-tty-launch.c -o "$root/sbin/m250-tty-launch"
    chmod 755 "$root/sbin/m250-tty-launch"
    if readelf -lW "$root/sbin/m250-tty-launch" | grep -q INTERP; then
        echo "[$deb_label FAIL] $1" >&2; exit 1
    fi
}

nas_debian_chown_disk() {
    # Only the disposable verified image is handed back to unprivileged QEMU.
    sudo chown "$(id -u):$(id -g)" "$disk"
    test -r "$disk" && test -w "$disk" || {
        echo "[$deb_label FAIL] $1" >&2; exit 1;
    }
}

nas_debian_stage_rescue() {
    rescue="$work/rescue"
    mkdir -p "$rescue"
    sh scripts/fetch-busybox-fixture.sh "$port" "$work/rescue-busybox"
    sh scripts/stage-busybox-rootfs.sh "$work/rescue-busybox" "$rescue"
    test -s "$rescue/rootfs.cpio"
    # Do not mistake a failed/corrupt archive listing for a clean rescue CPIO.
    listing=$(cpio -it --quiet < "$rescue/rootfs.cpio") || {
        echo "[$deb_label FAIL] cannot read rescue CPIO inventory" >&2; exit 1;
    }
    if printf '%s\n' "$listing" | grep -Eq "$1"; then
        echo "[$deb_label FAIL] $2" >&2; exit 1
    fi
}

nas_debian_root_env() {
    export NAS_ENABLE_LWEXT4=1 NAS_ENABLE_VIRTIO_BLK=1 NAS_ENABLE_M191_ALPINE_ROOT=1
    export NAS_ENABLE_RAMFS=0 NAS_ENABLE_LITTLEFS=0
    unset NAS_ENABLE_M179_FS_IRQ NAS_ENABLE_M180_FS_USER_IRQ
    unset NAS_ENABLE_VIRTIO_BLK_ASYNC NAS_QEMU_ASYNC_BLK NAS_QEMU_ASYNC_BLK_IMAGE
    export NAS_VIRTIO_BLK_IMAGE="$disk"
    export NAS_ACCEPTANCE_CPIO="$PWD/$rescue/rootfs.cpio"
    export BUSYBOX_PARITY_BINARY="$PWD/$work/rescue-busybox"
    export BUSYBOX_PARITY_CASE="$case_id"
    [ "$deb_label" = M249 ] || export BUSYBOX_PARITY_EXPECTED_COUNT=1
    export BUSYBOX_PARITY_MANIFEST="$case_file"
    export NAS_ACCEPTANCE_QEMU_RAM=1024M
}

nas_debian_build_kernel() {
    . scripts/native-source-qemu.sh
    nas_source_build_kernel "$1"
}

nas_debian_uart_env() {
    fifo="$PWD/$work/$1-serial-input.fifo"
    export NAS_QEMU_SERIAL_FIFO="$fifo"
    serial="$PWD/$logs/$case_id.serial.log"
    runner_log="$PWD/$work/$1-runner.log"
}
