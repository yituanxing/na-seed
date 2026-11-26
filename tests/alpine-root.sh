#!/bin/sh
# One original-distribution acceptance path. No milestone-specific guest binary.
set -eu
[ "$#" -eq 0 ] || { echo 'usage: sh tests/alpine-root.sh' >&2; exit 2; }
work="$PWD/build/alpine-root"
out="$work/kernel"
root="$work/rootfs"
disk="$work/root.img"
version=3.23.7
archive="alpine-minirootfs-$version-x86_64.tar.gz"
url="https://dl-cdn.alpinelinux.org/alpine/v3.23/releases/x86_64/$archive"
mkdir -p "$work"
# Build from the current commit, not a recovered historical kernel.
make ARCH=x86_64 OUT="$out" NAS_ENABLE_LWEXT4=1 NAS_ENABLE_VIRTIO_BLK=1 NAS_ENABLE_M191_ALPINE_ROOT=1 iso
curl -fLsS --retry 3 -o "$work/$archive" "$url"
curl -fLsS --retry 3 -o "$work/$archive.sha256" "$url.sha256"
(cd "$work" && sha256sum -c "$archive.sha256")
sudo rm -rf -- "$root"
mkdir -p "$root"
sudo tar --numeric-owner -xzf "$work/$archive" -C "$root"
sudo chown 0:0 "$root"
# Install official packages using apk signature verification; no untrusted packages.
docker run --rm -v "$root:/target" alpine:3.23 sh -ec '
    cp -a /target/etc/apk/keys/. /etc/apk/keys/
    apk --arch x86_64 --root /target --keys-dir /etc/apk/keys --initdb --no-scripts \
        -X https://dl-cdn.alpinelinux.org/alpine/v3.23/main add alpine-base openrc
'
# Configure only the disposable guest. Preserve original executables.
sudo chroot "$root" /bin/busybox sh -ec '
    mkdir -p /dev/pts /proc /sys /run /tmp /root
    chmod 1777 /tmp
    test -x /sbin/init
    test -s /etc/inittab
    sed -i "/^tty[1-6]:/d; /^ttyS0:/d" /etc/inittab
    echo "ttyS0::respawn:/sbin/getty -L ttyS0 115200 vt100" >> /etc/inittab
    if ! grep -qx ttyS0 /etc/securetty 2>/dev/null; then echo ttyS0 >> /etc/securetty; fi
    echo "root:naseed-ci-only" | chpasswd
'
# Rescue archive has only the exact original BusyBox/loader; no distro marker.
mkdir -p "$work/rescue/bin" "$work/rescue/lib"
sudo chroot "$root" /bin/busybox cat /bin/busybox > "$work/rescue/bin/busybox"
sudo chroot "$root" /bin/busybox cat /lib/ld-musl-x86_64.so.1 > "$work/rescue/lib/ld-musl-x86_64.so.1"
chmod 755 "$work/rescue/bin/busybox" "$work/rescue/lib/ld-musl-x86_64.so.1"
(cd "$work/rescue" && find . -print | LC_ALL=C sort | cpio -o -H newc --quiet --owner=0:0) > "$work/rescue.cpio"
readelf -l "$work/rescue/bin/busybox" | grep -F '/lib/ld-musl-x86_64.so.1'
sha256sum "$work/rescue/bin/busybox" "$work/rescue/lib/ld-musl-x86_64.so.1"
# MBR partition starts at 2 MiB. Use the existing lwext4 feature contract.
rm -f "$disk" "$work/partition.ext4"
truncate -s 254M "$work/partition.ext4"
sudo mke2fs -q -F -t ext4 -b 4096 -O '^metadata_csum,^64bit' -d "$root" "$work/partition.ext4"
truncate -s 256M "$disk"
printf 'label: dos\nunit: sectors\n\nstart=4096, size=520192, type=83\n' | sfdisk "$disk"
dd if="$work/partition.ext4" of="$disk" bs=1M seek=2 conv=notrunc status=none
# Each case uses the same built kernel, disk and genuine rescue archive.
make_iso() {
    name=$1
    cmdline=$2
    cp "$work/rescue.cpio" "$out/iso/boot/rootfs.cpio"
    printf 'timeout: 0\n/NaSeed\n    protocol: limine\n    path: boot():/boot/kernel\n    cmdline: %s\n    module_path: boot():/boot/rootfs.cpio\n' "$cmdline" > "$out/iso/boot/limine/limine.conf"
    xorriso -as mkisofs -R -r -J --efi-boot boot/limine/limine-uefi-cd.bin \
        --efi-boot-part --efi-boot-image -o "$work/$name.iso" "$out/iso"
}
ovmf=
for path in /usr/share/OVMF/OVMF_CODE_4M.fd /usr/share/OVMF/OVMF_CODE.fd; do
    if [ -f "$path" ]; then ovmf=$path; break; fi
done
test -n "$ovmf"
make_iso shell 'naseed.argv=/bin/busybox sh -c "test -x /sbin/init && test -s /etc/apk/world && cat /etc/alpine-release"'
make_iso init 'naseed.argv=/sbin/init'
rc=0
timeout 180s qemu-system-x86_64 -M q35 -m 1024M -cpu qemu64 -display none -monitor none \
    -serial stdio -no-reboot -no-shutdown -nic none \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
    -drive "if=pflash,unit=0,format=raw,file=$ovmf,readonly=on" \
    -drive "if=none,id=root,format=raw,file=$disk" \
    -device virtio-blk-pci,drive=root,disable-legacy=on -cdrom "$work/shell.iso" > "$work/shell.log" 2>&1 || rc=$?
cat "$work/shell.log"
test "$rc" -eq 1
for marker in '[M191 ROOT] physical Alpine ext4 registered at /' \
    '[M191 ROOT] original Alpine PID1 from mounted ext4: /bin/busybox' \
    '[LWEXT4 GUEST] clean upstream unmount and durable flush' \
    '[PASS] cmdline userspace exit 0'; do
    grep -Fq "$marker" "$work/shell.log"
done
tr -d '\r' < "$work/shell.log" | grep -Fxq "$version"
! grep -Fq '[FAIL]' "$work/shell.log"
echo 'PASS: original Alpine BusyBox from physical ext4'
# Run original init and exercise a real serial login. pexpect verifies output
# on its own line (not echoed command text), then asks the real PID1 to shut down.
python3 - "$work" "$ovmf" "$disk" <<'PY'
import pathlib, sys, pexpect
work, ovmf, disk = sys.argv[1:]
args = ['-M','q35','-m','1024M','-cpu','qemu64','-display','none','-monitor','none',
        '-serial','stdio','-no-reboot','-no-shutdown','-nic','none',
        '-device','isa-debug-exit,iobase=0xf4,iosize=0x04',
        '-drive',f'if=pflash,unit=0,format=raw,file={ovmf},readonly=on',
        '-drive',f'if=none,id=root,format=raw,file={disk}',
        '-device','virtio-blk-pci,drive=root,disable-legacy=on','-cdrom',f'{work}/init.iso']
with open(f'{work}/init.log','w') as log:
    guest = pexpect.spawn('qemu-system-x86_64',args,encoding='utf-8',codec_errors='replace',timeout=180)
    guest.logfile_read = log
    try:
        guest.expect('login:')
        guest.sendline('root')
        guest.expect('Password:')
        guest.sendline('naseed-ci-only')
        guest.expect(r'# ')
        # Kernel serial diagnostics can occur between guest process outputs.
        # Check values inside the shell, then emit one standalone success line.
        guest.sendline('test -s /etc/apk/world && test -x /sbin/init && '
                       'test "$(id -u)" = 0 && '
                       'test "$(cat /etc/alpine-release)" = 3.23.7 && '
                       'echo CLI_READY')
        guest.expect(r'\r*\nCLI_READY\r*\n')
        guest.sendline('poweroff')
        guest.expect(pexpect.EOF)
        guest.close()
        assert guest.exitstatus == 1, (guest.exitstatus, guest.signalstatus)
    finally:
        if guest.isalive(): guest.close(force=True)
serial = pathlib.Path(work,'init.log').read_text()
for marker in ('[M191 ROOT] physical Alpine ext4 registered at /',
               '[M191 ROOT] original Alpine PID1 from mounted ext4: /sbin/init',
               '[ALPINE POWER_OFF] original BusyBox; durable ext4 unmount',
               '[PASS] cmdline userspace exit 0'):
    assert marker in serial, marker
assert '[FAIL]' not in serial
print('PASS: original Alpine init, serial login, CLI and PID1 poweroff')
PY
# Prove that the actual physical disk was left clean, not only a RAM copy.
dd if="$disk" of="$work/partition.ext4" bs=1M skip=2 status=none
e2fsck -fn "$work/partition.ext4"
