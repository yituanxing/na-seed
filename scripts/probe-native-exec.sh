#!/bin/sh
# Linux oracle + real NaSeed UEFI QEMU: upstream Make/TCC generate and
# run an executable that did not exist in the initial CPIO image.
set -eu
sh scripts/build-external-gnumake.sh amd64
sh scripts/build-external-tcc.sh amd64
work=build/external/native-exec/amd64
mkdir -p "$work"
# Validate the original TinyCC linker, not host GCC, with exactly the
# freestanding source that the NaSeed guest will compile and execute.
build/external/tcc/amd64/tcc -nostdlib -static \
    -o "$work/hello" tests/native-hello.c
# Print precise ELF metadata so a linker/format issue cannot masquerade
# as a kernel failure. The guest will execute the *same* upstream-TCC output.
readelf -h "$work/hello" > "$work/elf-header.log"
readelf -s "$work/hello" > "$work/elf-symbols.log"
cat "$work/elf-header.log" | grep -E 'Type:|Machine:|Entry point'
if ! grep -Eq 'Type:.*EXEC' "$work/elf-header.log"; then
  echo '[NATIVE EXEC LINUX FAIL] TinyCC did not emit ET_EXEC' >&2
  cat "$work/elf-header.log" >&2; exit 1
fi
# TinyCC may strip the static ELF symbol table. A valid, nonzero entry
# point and successful *execution* are the acceptance contract, not the
# presence of a debugging symbol that the linker may omit.
if ! grep -Eq 'Entry point address:[[:space:]]+0x[1-9a-fA-F][0-9a-fA-F]*' "$work/elf-header.log"; then
  echo '[NATIVE EXEC LINUX FAIL] missing ELF entry address' >&2
  cat "$work/elf-header.log" >&2; exit 1
fi
if readelf -l "$work/hello" | grep -q INTERP; then
  echo '[NATIVE EXEC] TinyCC generated dynamic interpreter unexpectedly' >&2
  exit 1
fi
bytes=$(wc -c < "$work/hello")
if [ "$bytes" -gt 65536 ]; then
  echo "[NATIVE EXEC] linked binary exceeds bounded tmpfs inode: $bytes" >&2
  exit 1
fi
"$work/hello" > "$work/linux-executed.log"
grep -Fxq 'naseed-native-tcc-elf-ran' "$work/linux-executed.log"
echo "[NATIVE EXEC LINUX PASS] TinyCC linked real $bytes-byte ELF; Linux ran it"
sh scripts/stage-external-gnumake-rootfs.sh
make kernel build/deps/limine-binary/BOOTX64.EFI
stage="$work/stage"
iso="$work/native-exec.iso"
mkdir -p "$stage/boot/limine" "$stage/EFI/BOOT"
cp build/kernel.elf "$stage/boot/kernel"
cp build/deps/limine-binary/BOOTX64.EFI "$stage/EFI/BOOT/BOOTX64.EFI"
cp build/deps/limine-binary/limine-uefi-cd.bin "$stage/boot/limine/"
cp build/external/gnumake/amd64/rootfs.cpio "$stage/boot/rootfs.cpio"
cat > "$stage/boot/limine/limine.conf" <<'LIMINE'
timeout: 0
/NaSeed native Make/TinyCC tmpfs executable
    protocol: limine
    path: boot():/boot/kernel
    cmdline: naseed.argv=/bin/native-init
    module_path: boot():/boot/rootfs.cpio
LIMINE
xorriso -as mkisofs -R -r -J \
  --efi-boot boot/limine/limine-uefi-cd.bin \
  --efi-boot-part --efi-boot-image -o "$iso" "$stage" >/dev/null 2>&1
python3 scripts/gnumake-serial-check.py "$iso" native-exec
