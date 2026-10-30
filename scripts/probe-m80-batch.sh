#!/bin/sh
# One real NaSeed QEMU boot per ISA for a batch of original Alpine ELF tools.
# Keep signed APK staging, guest execution and serial evidence separate.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: probe-m80-batch.sh amd64|riscv64|arm64' >&2; exit 2; }
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m80-prebuilt.sh "$port"
work="build/external/m80-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export NAS_ACCEPTANCE_DISK_MB=768
export NAS_SYSCALL_TRACE=0
if [ "${NAS_M81_FS:-0}" = 1 ]; then
  # The same original Alpine APK overlay and parity harness, but with ALL
  # three genuinely mounted VFS providers in one kernel and one QEMU boot.
  # No test-only filesystem substitutes or second application installer.
  export BUSYBOX_PARITY_MANIFEST=scripts/m81-fs-cases.tsv
  export NAS_ENABLE_RAMFS=1 NAS_ENABLE_LWEXT4=1 NAS_ENABLE_VIRTIO_BLK=1
  sh scripts/check-lwext4-ramdisk.sh
  disk="$work/m81-physical-ext4.img"
  cp build/external/lwext4/ext4.img "$disk"
  export NAS_VIRTIO_BLK_IMAGE="$disk"
  if [ "$port" = amd64 ]; then
    # Normal M80 compiled an x86 kernel already: never reuse those .o files
    # with different opt-in flags. This is the existing Makefile OUT feature.
    out=build/m81-virtio-x86
    mkdir -p "$out/deps"
    cp build/deps/limine.h "$out/deps/limine.h"
    make -j2 OUT="$out" kernel
    export NAS_ACCEPTANCE_KERNEL="$out/kernel.elf"
  elif [ "$port" = riscv64 ]; then
    # Existing matrix runners only build if kernel.elf is absent.
    sh scripts/build-riscv64-full.sh
  else
    sh scripts/build-aarch64-full.sh
  fi
  log="$logs/m81-fs.clean.log"
else
  export BUSYBOX_PARITY_MANIFEST=scripts/m80-batch-cases.tsv
  log="$logs/m80-batch.clean.log"
fi
if ! sh "$runner"; then
  echo "[M80/M81 FAIL] $port genuine original Alpine program batch" >&2
  test ! -s "$log" || tail -360 "$log" >&2
  exit 1
fi
test -s "$log"
if [ "${NAS_M81_FS:-0}" = 1 ]; then
  # The guest must independently prove every original program on each
  # provider. The host also verifies actual guest mounts and ON-DISK bytes.
  for provider in TMPFS RAMFS EXT4; do
    for app in GREP SED GAWK FIND COREUTILS JQ RIPGREP FD TREE \
               TAR GZIP XZ BZIP2 ZIP UNZIP ZSTD SQLITE BC RSYNC; do
      marker="M81_${provider}_${app}_PASS"
      if ! grep -Fxq "$marker" "$log"; then
        echo "[M81 FAIL] $port missing authentic software/provider result $marker" >&2
        tail -360 "$log" >&2
        exit 1
      fi
    done
  done
  for marker in M81_MOUNT_TMPFS_PASS M81_MOUNT_RAMFS_PASS M81_MOUNT_EXT4_PASS \
                M81_TMPFS_ALL19_PASS M81_RAMFS_ALL19_PASS M81_EXT4_ALL19_PASS \
                M81_CROSS_RSYNC_PASS M81_CROSS_TAR_PASS \
                M81_CROSS_ZSTD_PASS M81_CROSS_SQLITE_PASS \
                M81_EXT4_PERSISTENCE_PASS M81_FS_BATCH_PASS; do
    grep -Fxq "$marker" "$log" || {
      echo "[M81 FAIL] $port missing guest assertion $marker" >&2
      tail -360 "$log" >&2
      exit 1
    }
  done
  grep -Fq '[LWEXT4 GUEST] real external VirtIO-Blk ext4 disk mounted at /mnt' "$log" || {
    echo "[M81 FAIL] $port never mounted real VirtIO ext4" >&2; exit 1;
  }
  grep -Fq '[PASS] real upstream RamFS: VFS create/IO/rename/open-pin/unlink/rmdir' "$log" || {
    echo "[M81 FAIL] $port never initialized original RamFS" >&2; exit 1;
  }
  grep -Fq '[VIRTIO-BLK] persistent block device write+flush enabled' "$log" || {
    echo "[M81 FAIL] $port ext4 block device is not writable" >&2; exit 1;
  }
  grep -Fq '[LWEXT4 GUEST] clean upstream unmount and durable flush' "$log" || {
    echo "[M81 FAIL] $port real ext4 did not cleanly unmount and flush" >&2; exit 1;
  }
  sh scripts/check-ext4-fsck-clean.sh "$disk"
  debugfs -R 'cat /m81-fs-persisted.txt' "$disk" > "$work/m81-ext4-persisted.txt"
  grep -Fxq 'NaSeed-M81-real-ext4-persistence' "$work/m81-ext4-persisted.txt" || {
    echo "[M81 FAIL] $port real ext4 content missing after QEMU clean unmount" >&2
    exit 1
  }
  # No aggregate-only success: every authentic GNU Make incremental and
  # directory dependency case must be attested by the actual guest serial.
  for provider in TMPFS RAMFS EXT4; do
    for case_id in PIPELINE NOOP SOURCE_CHANGE DIR_CREATE DIR_REMOVE \
                   DIR_RENAME DIR_MKDIR DIR_RMDIR CROSS_RENAME; do
      marker="M82_${provider}_${case_id}_PASS"
      grep -Fxq "$marker" "$log" || {
        echo "[M82 FAIL] $port missing actual GNU Make/VFS result $marker" >&2
        tail -360 "$log" >&2
        exit 1
      }
    done
    grep -Fxq "M82_${provider}_ALL9_PASS" "$log" || {
      echo "[M82 FAIL] $port missing full provider assertion $provider" >&2
      tail -360 "$log" >&2
      exit 1
    }
  done
  for marker in M82_ORIGINAL_MAKE_PASS M82_MAKE_BATCH_PASS; do
    grep -Fxq "$marker" "$log" || {
      echo "[M82 FAIL] $port genuine GNU Make or batch assertion $marker missing" >&2
      tail -360 "$log" >&2
      exit 1
    }
  done
  # The ext4 directory was made by REAL GNU Make inside QEMU. Independently
  # read its bytes and the final result from the physical disk on the host.
  debugfs -R 'cat /m82-make-pipeline/result.txt' "$disk" > "$work/m82-ext4-make-output.txt"
  grep -Fxq 'ALPHA one' "$work/m82-ext4-make-output.txt" || {
    echo "[M82 FAIL] $port original GNU Make output absent from real ext4 disk" >&2
    exit 1
  }
  debugfs -R 'cat /m82-make-persisted.txt' "$disk" > "$work/m82-ext4-persisted.txt"
  grep -Fxq 'NaSeed-M82-GNU-Make-original-3FS-27-PASS' "$work/m82-ext4-persisted.txt" || {
    echo "[M82 FAIL] $port genuine GNU Make ext4 persistence missing" >&2
    exit 1
  }
  echo "[M81 PASS] $port three genuine VFS providers, 57 original Alpine functional cases, physical ext4 persisted and e2fsck clean"
  echo "[M82 PASS] $port original GNU Make 27/27 incremental and directory cases across 3 live FS; ext4 QEMU disk independently verified"
  exit 0
fi
for marker in M80_GREP_PASS M80_SED_PASS M80_GAWK_PASS \
              M80_FIND_PASS M80_COREUTILS_PASS M80_JQ_PASS \
              M80_RIPGREP_PASS M80_FD_PASS M80_TREE_PASS \
              M80_TAR_PASS M80_GZIP_PASS M80_XZ_PASS \
              M80_BZIP2_PASS M80_ZIP_PASS M80_UNZIP_PASS \
              M80_ZSTD_PASS M80_SQLITE_PASS M80_BC_PASS \
              M80_RSYNC_PASS \
              M80_TEXT_TREE_PASS M80_ARCHIVES_PASS \
              M80_SQLITE_BC_PASS \
              M80_NANO_DEFERRED M80_BATCH_PASS; do
  if ! grep -Fxq "$marker" "$log"; then
    echo "[M80 FAIL] $port missing genuine guest assertion: $marker" >&2
    tail -360 "$log" >&2
    exit 1
  fi
done
echo "[M80 PASS] $port original Alpine 19-program functional batch (nano interactive deferred)"
