#!/bin/sh
# First a genuinely working Linux Make, then *the same* ELF under native QEMU.
set -eu
sh scripts/build-external-gnumake.sh amd64
sh scripts/build-external-tcc.sh amd64
# Real Linux counterpart: identical GNU Make+TinyCC binaries+Makefile.
work=build/external/gnumake/amd64
"$work/gmake" --no-print-directory -f tests/gnumake-real.mk \
  TCC=build/external/tcc/amd64/tcc SOURCE=tests/tcc-first.c \
  OBJECT="$work/first.o" native-c > "$work/linux-native-build.log" 2>&1
grep -Fxq 'naseed-compiled-object' "$work/linux-native-build.log"
readelf -h "$work/first.o" | grep -q 'REL (Relocatable file)'
readelf -s "$work/first.o" | grep -Fq naseed_answer
echo '[GNUMAKE LINUX BUILD PASS] unmodified GNU Make drove original TinyCC'
# Capture actual Linux process creation behavior (clone/vfork/execve).
# Detect which general Linux ABI NaSeed lacks before writing kernel code.
strace -f -qq -e trace=clone,clone3,vfork,fork,execve,execveat \
  -o "$work/linux-process-syscalls.log" \
  "$work/gmake" --no-print-directory -f tests/gnumake-real.mk \
    TCC=build/external/tcc/amd64/tcc SOURCE=tests/tcc-first.c \
    OBJECT="$work/second.o" native-c > "$work/linux-process-oracle.log" 2>&1
grep -Fxq 'naseed-compiled-object' "$work/linux-process-oracle.log"
readelf -h "$work/second.o" | grep -q 'REL (Relocatable file)'
echo '[GNUMAKE LINUX SPAWN] real process creation/syscalls:'
grep -E 'clone|vfork|fork|execve' "$work/linux-process-syscalls.log" | head -30 || :
sh scripts/stage-external-gnumake-rootfs.sh
make kernel build/deps/limine-binary/BOOTX64.EFI
work=build/external/gnumake/amd64
stage="$work/stage"
iso="$work/gnumake.iso"
mkdir -p "$stage/boot/limine" "$stage/EFI/BOOT"
cp build/kernel.elf "$stage/boot/kernel"
cp build/deps/limine-binary/BOOTX64.EFI "$stage/EFI/BOOT/BOOTX64.EFI"
cp build/deps/limine-binary/limine-uefi-cd.bin "$stage/boot/limine/"
cp "$work/rootfs.cpio" "$stage/boot/rootfs.cpio"
cat > "$stage/boot/limine/limine.conf" <<'LIMINE'
timeout: 0
/NaSeed GNU Make native build tool
    protocol: limine
    path: boot():/boot/kernel
    cmdline: naseed.argv=/bin/make-init
    module_path: boot():/boot/rootfs.cpio
LIMINE
xorriso -as mkisofs -R -r -J \
  --efi-boot boot/limine/limine-uefi-cd.bin \
  --efi-boot-part --efi-boot-image -o "$iso" "$stage" >/dev/null 2>&1
python3 scripts/gnumake-serial-check.py "$iso"
