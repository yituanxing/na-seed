#!/bin/sh
# Shared shell functions for authentic guest-built software on persistent ext4.
# Workload wrappers own source oracles and host ELF/database assertions; this
# file owns the single kernel/QEMU runner, disk setup and serial fail-closed
# contract for both M83 zlib and M84 SQLite/Lua. Source AFTER the canonical
# scripts/native-qemu-target.sh mapping (each wrapper declares it once).

nas_source_guest_env() {
    original_rootfs=$1 original_ram=$2
    export BUSYBOX_PARITY_BINARY="$original_rootfs/bin/busybox"
    export NAS_ACCEPTANCE_CPIO="${original_rootfs}.cpio"
    export BUSYBOX_PARITY_EXPECTED_COUNT=1
    export NAS_ACCEPTANCE_QEMU_RAM="$original_ram"
    export NAS_ACCEPTANCE_DISK_MB=768 NAS_SYSCALL_TRACE=0
    export NAS_ENABLE_RAMFS=1 NAS_ENABLE_LWEXT4=1 NAS_ENABLE_VIRTIO_BLK=1
}

nas_source_prepare_disk() {
    original_disk=$1 original_mib=$2
    # Prove the unmodified upstream lwext4 host implementation before QEMU.
    sh scripts/check-lwext4-ramdisk.sh
    case "$original_mib" in
        32) cp build/external/lwext4/ext4.img "$original_disk" ;;
        256)
            truncate -s 256M "$original_disk"
            mke2fs -q -F -t ext4 -b 4096 -O '^metadata_csum,^64bit' "$original_disk" ;;
        *) echo "[SOURCE QEMU FAIL] unsupported physical ext4 size $original_mib" >&2; return 2 ;;
    esac
    # Assigned ONCE. Neither run_case nor the second QEMU boot copies,
    # reformats, truncates or switches this physical disk.
    export NAS_VIRTIO_BLK_IMAGE="$original_disk"
}

nas_source_build_kernel() {
    original_tag=$1
    if [ "$port" = amd64 ]; then
        make build/deps/limine.h
        original_out="build/$original_tag-virtio-x86"
        mkdir -p "$original_out/deps"
        cp build/deps/limine.h "$original_out/deps/limine.h"
        make -j2 OUT="$original_out" kernel
        export NAS_ACCEPTANCE_KERNEL="$original_out/kernel.elf"
    elif [ "$port" = riscv64 ]; then
        sh scripts/build-riscv64-full.sh
    else
        sh scripts/build-aarch64-full.sh
    fi
}

nas_source_elf_machine() {
    case "$port" in
        amd64) echo 'Advanced Micro Devices X86-64' ;;
        riscv64) echo 'RISC-V' ;;
        arm64) echo 'AArch64' ;;
        *) return 2 ;;
    esac
}

nas_source_run_case() {
    original_case=$1 original_manifest=$2 original_markers=$3
    export BUSYBOX_PARITY_MANIFEST="$original_manifest"
    if ! sh "$runner"; then
        echo "[SOURCE QEMU FAIL] $port $original_case original guest workload" >&2
        test ! -s "$logs/$original_case.clean.log" ||
            tail -300 "$logs/$original_case.clean.log" >&2
        return 1
    fi
    original_log="$logs/$original_case.clean.log"
    test -s "$original_log" || {
        echo "[SOURCE QEMU FAIL] $port missing $original_case serial log" >&2
        return 1
    }
    for original_marker in $original_markers; do
        if ! grep -Fxq "$original_marker" "$original_log"; then
            echo "[SOURCE QEMU FAIL] $port $original_case absent $original_marker" >&2
            tail -250 "$original_log" >&2
            return 1
        fi
    done
    for original_line in '[LWEXT4 GUEST] real external VirtIO-Blk ext4 disk mounted at /mnt' \
                         '[VIRTIO-BLK] persistent block device write+flush enabled' \
                         '[LWEXT4 GUEST] clean upstream unmount and durable flush'; do
        if ! grep -Fq "$original_line" "$original_log"; then
            echo "[SOURCE QEMU FAIL] $port $original_case missing $original_line" >&2
            tail -250 "$original_log" >&2
            return 1
        fi
    done
}
