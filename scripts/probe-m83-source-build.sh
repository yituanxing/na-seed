#!/bin/sh
# Original upstream zlib built IN NaSeed by authenticated Alpine GCC/GNU Make.
# One reusable original system-QEMU executor, two true machine boots, ONE
# persistent VirtIO ext4 disk; no fake ELF, host compiler, or disk restoration.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: probe-m83-source-build.sh amd64|riscv64|arm64' >&2; exit 2; }
port=$1
. scripts/native-qemu-target.sh
. scripts/native-source-qemu.sh
sh scripts/stage-m83-source-build.sh "$port"
sh scripts/stage-m83-prebuilt.sh "$port"
work="build/external/m83-source/$port"
nas_source_guest_env "$work/rootfs" 1536M
disk="$work/m83-physical-ext4.img"
nas_source_prepare_disk "$disk" 32
nas_source_build_kernel m83

build_markers='M83_SIGNED_GCC_MAKE_PASS M83_EXT4_ZLIB_CONFIGURE_PASS M83_EXT4_ZLIB_GCC_BUILD_PASS M83_EXT4_ZLIB_UPSTREAM_TEST_PASS M83_EXT4_ZLIB_INCREMENTAL_NOOP_PASS M83_EXT4_ZLIB_SELECTIVE_REBUILD_PASS M83_EXT4_ZLIB_EXEC_PASS M83_TMPFS_NATIVE_TWO_TU_PASS M83_RAMFS_NATIVE_TWO_TU_PASS M83_CROSS_FS_EXEC_PASS M83_EXT4_PERSISTENCE_PASS M83_BUILD_PASS'
nas_source_run_case m83-build scripts/m83-build-cases.tsv "$build_markers"
# debugfs dump writes real host files. `-R dump` stdout is NOT image bytes.
sh scripts/check-ext4-fsck-clean.sh "$disk"
debugfs -R 'cat /m83-result.txt' "$disk" > "$work/m83-build-result.txt"
grep -Fxq 'NaSeed-M83-upstream-zlib-1.3.1-GCC-Make-PASS' "$work/m83-build-result.txt" || exit 1
debugfs -R "dump /m83-upstream-zlib/example $work/m83-built-example.elf" "$disk" > "$work/m83-build-debugfs-elf.log" 2>&1
debugfs -R "dump /m83-upstream-zlib/libz.a $work/m83-built-libz.a" "$disk" > "$work/m83-build-debugfs-ar.log" 2>&1
test -s "$work/m83-built-example.elf" && test -s "$work/m83-built-libz.a"
machine=$(nas_source_elf_machine)
readelf -h "$work/m83-built-example.elf" | grep -Fq "$machine" || {
    echo "[M83 FAIL] $port first-boot compiled ELF ISA mismatch" >&2; exit 1;
}
ar t "$work/m83-built-libz.a" | grep -Fxq deflate.o || {
    echo "[M83 FAIL] $port first-boot original zlib archive lacks deflate.o" >&2; exit 1;
}
sha256sum "$work/m83-built-example.elf" "$work/m83-built-libz.a" \
    > "$work/m83-build-guest-objects.sha256"
echo "[M83 FIRST BOOT PASS] $port original GCC-produced ISA-native ELF and static archive independently dumped from real clean ext4"

# SECOND, independent QEMU boot: same `$disk` block-file by exact path, not
# duplicated image or reboot-within-guest simulation; GNU Make -q is genuine.
replay_markers='M83_SIGNED_GCC_MAKE_PASS M83_REPLAY_ORIGINAL_ZLIB_PASS M83_REPLAY_INCREMENTAL_NOOP_PASS M83_REPLAY_PERSISTENCE_PASS M83_REPLAY_PASS'
nas_source_run_case m83-replay scripts/m83-replay-cases.tsv "$replay_markers"
sh scripts/check-ext4-fsck-clean.sh "$disk"
debugfs -R 'cat /m83-result.txt' "$disk" > "$work/m83-replay-result.txt"
cmp "$work/m83-build-result.txt" "$work/m83-replay-result.txt"
debugfs -R "dump /m83-upstream-zlib/example $work/m83-replay-example.elf" "$disk" > "$work/m83-replay-debugfs-elf.log" 2>&1
debugfs -R "dump /m83-upstream-zlib/libz.a $work/m83-replay-libz.a" "$disk" > "$work/m83-replay-debugfs-ar.log" 2>&1
cmp "$work/m83-built-example.elf" "$work/m83-replay-example.elf"
cmp "$work/m83-built-libz.a" "$work/m83-replay-libz.a"
readelf -h "$work/m83-replay-example.elf" | grep -Fq "$machine"
ar t "$work/m83-replay-libz.a" | grep -Fxq deflate.o
echo "[M83 PASS] $port actual original upstream zlib ./configure/GCC/GNU Make/test, three FS, two real QEMU boots, identical physical ext4 ELF/archive SHA and clean e2fsck"
