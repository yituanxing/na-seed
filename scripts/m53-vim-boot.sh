#!/bin/sh
# Build the second, actual interactive QEMU boot image from the same signed
# Alpine Vim rootfs as the noninteractive Ex test. No custom editor image.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
work="build/external/m53-vim/$port"
archive="$work/rootfs.cpio"
test -s "$archive"
case "$port" in
 amd64)
    test -s build/kernel.elf
    stage="$work/stage"
    img="$work/m53-live.iso"
    mkdir -p "$stage/boot/limine" "$stage/EFI/BOOT"
    cp build/kernel.elf "$stage/boot/kernel"
    cp build/deps/limine-binary/BOOTX64.EFI "$stage/EFI/BOOT/BOOTX64.EFI"
    cp build/deps/limine-binary/limine-uefi-cd.bin "$stage/boot/limine/"
    cp "$archive" "$stage/boot/rootfs.cpio"
    cat > "$stage/boot/limine/limine.conf" <<'EOF'
timeout: 0
/NaSeed M53 original Vim interactive x86
    protocol: limine
    path: boot():/boot/kernel
    cmdline: naseed.argv=/tests/m53-vim-init
    module_path: boot():/boot/rootfs.cpio
EOF
    xorriso -as mkisofs -R -r -J \
       --efi-boot boot/limine/limine-uefi-cd.bin \
       --efi-boot-part --efi-boot-image -o "$img" "$stage" >/dev/null 2>&1
    ;;
 riscv64|arm64)
    img="$work/m53-live.img"
    if [ "$port" = riscv64 ]; then
        efi=BOOTRISCV64.EFI
        kernel=build/port-riscv64/full/kernel.elf
        fw=/usr/share/qemu-efi-riscv64
    else
        efi=BOOTAA64.EFI
        kernel=build/port-aarch64/full/kernel.elf
    fi
    test -s "$kernel"
    truncate -s 512M "$img"
    sgdisk --clear --new 1:2048:0 --typecode 1:ef00 "$img" >/dev/null
    mformat -i "$img@@1048576" -F ::
    mmd -i "$img@@1048576" ::/EFI ::/EFI/BOOT ::/boot ::/boot/limine
    mcopy -i "$img@@1048576" "build/deps/limine-binary/$efi" "::/EFI/BOOT/$efi"
    mcopy -i "$img@@1048576" "$kernel" ::/boot/kernel
    mcopy -i "$img@@1048576" "$archive" ::/boot/rootfs.cpio
    printf '%s\n' 'timeout: 0' \
        '/NaSeed M53 native original Vim TTY' \
        '    protocol: limine' \
        '    path: boot():/boot/kernel' \
        '    cmdline: naseed.argv=/tests/m53-vim-init' \
        '    module_path: boot():/boot/rootfs.cpio' > "$work/m53-live.conf"
    mcopy -i "$img@@1048576" "$work/m53-live.conf" ::/boot/limine/limine.conf
    if [ "$port" = riscv64 ]; then
        cp "$fw/RISCV_VIRT_VARS.fd" "$work/m53-live.vars.fd"
    fi
    ;;
 *) exit 2 ;;
esac
test -s "$img"
echo "$img"
