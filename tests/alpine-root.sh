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
make ARCH=x86_64 OUT="$out" NAS_ENABLE_LWEXT4=1 NAS_ENABLE_VIRTIO_BLK=1 NAS_ENABLE_M191_ALPINE_ROOT=1 NAS_ENABLE_VIRTIO_GPU=1 NAS_ENABLE_VIRTIO_INPUT=1 iso
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
        -X https://dl-cdn.alpinelinux.org/alpine/v3.23/main -X https://dl-cdn.alpinelinux.org/alpine/v3.23/community \
        add alpine-base openrc git sqlite lua5.4 python3 zstd build-base \
        xorg-server xf86-video-fbdev xf86-input-evdev xkeyboard-config xdpyinfo xsetroot strace
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
    # Restore the normal Alpine mkfontscale trigger skipped by --no-scripts.
    for dir in /usr/share/fonts/*; do
        [ -d "$dir" ] || continue
        [ "${dir##*/}" != encodings ] || continue
        mkfontdir "$dir"
        mkfontscale "$dir"
    done
'
# Use the ordinary Xorg configuration interface, not a patched X server.
sudo tee "$root/root/xorg.conf" >/dev/null <<'XORG'
Section "ServerFlags"
    Option "AutoAddDevices" "false"
    Option "AutoAddGPU" "false"
EndSection
Section "Device"
    Identifier "Display"
    Driver "fbdev"
    Option "fbdev" "/dev/fb0"
EndSection
Section "Screen"
    Identifier "Screen"
    Device "Display"
    DefaultDepth 24
EndSection
XORG
# One distribution workload, using unchanged signed packages. Each group
# reports its result independently; a failed group never hides a later one.
sudo tee "$root/root/acceptance.sh" >/dev/null <<'GUEST'
#!/bin/sh
set -u
umask 022
mkdir -p /root/acceptance-data || exit 1
cd /root/acceptance-data || exit 1
failures=0
run() {
    name=$1
    shift
    if "$@"; then
        printf '[APP PASS] %s\n' "$name"
    else
        printf '[APP FAIL] %s\n' "$name"
        failures=1
    fi
}
archive() {
    mkdir -p unpacked &&
    printf '%s\n' 'NaSeed real software' > source.txt &&
    tar -cf source.tar source.txt &&
    zstd -q -f source.tar -o source.tar.zst &&
    zstd -q -d -f source.tar.zst -o restored.tar &&
    tar -xf restored.tar -C unpacked &&
    cmp source.txt unpacked/source.txt
}
database() {
    sqlite3 records.db 'CREATE TABLE records (value INTEGER); INSERT INTO records VALUES (19),(23);' &&
    test "$(sqlite3 records.db 'SELECT sum(value) FROM records;')" = 42 &&
    test "$(sqlite3 records.db 'PRAGMA integrity_check;')" = ok
}
lua_runtime() {
    lua5.4 -e 'local t={19,23}; local f=assert(io.open("lua.txt","w")); assert(f:write(t[1]+t[2])); assert(f:close())' &&
    test "$(cat lua.txt)" = 42
}
python_runtime() {
    python3 -c 'import json,pathlib,subprocess; p=pathlib.Path("python.json"); p.write_text(json.dumps({"value":42})); assert json.loads(p.read_text())["value"]==42; assert subprocess.check_output(["/bin/busybox","echo","42"]).strip()==b"42"'
}
git_repository() {
    git init -q repo &&
    git -C repo config user.name NaSeed &&
    git -C repo config user.email naseed@example.invalid &&
    printf 'persistent git\n' > repo/readme.txt &&
    git -C repo add readme.txt &&
    git -C repo commit -qm initial &&
    git -C repo branch checkpoint &&
    git -C repo gc &&
    git -C repo fsck --full &&
    test "$(git -C repo show checkpoint:readme.txt)" = 'persistent git'
}
native_build() {
    mkdir -p project &&
    printf '%s\n' '#include <stdio.h>' 'int main(void) { puts("42"); return 0; }' > project/main.c &&
    printf 'all: app\napp: main.c\n\t$(CC) -O2 -o $@ $<\n' > project/Makefile &&
    make -C project &&
    test "$(project/app)" = 42
}
run archive-zstd archive
run sqlite database
run lua lua_runtime
run python python_runtime
run git git_repository
run gcc-make native_build
printf '[APP DONE status=%s]\n' "$failures"
exit "$failures"
GUEST
# Rescue archive has only the exact original BusyBox/loader; no distro marker.
mkdir -p "$work/rescue/bin" "$work/rescue/lib"
sudo chroot "$root" /bin/busybox cat /bin/busybox > "$work/rescue/bin/busybox"
sudo chroot "$root" /bin/busybox cat /lib/ld-musl-x86_64.so.1 > "$work/rescue/lib/ld-musl-x86_64.so.1"
chmod 755 "$work/rescue/bin/busybox" "$work/rescue/lib/ld-musl-x86_64.so.1"
(cd "$work/rescue" && find . -print | LC_ALL=C sort | cpio -o -H newc --quiet --owner=0:0) > "$work/rescue.cpio"
readelf -l "$work/rescue/bin/busybox" | grep -F '/lib/ld-musl-x86_64.so.1'
sha256sum "$work/rescue/bin/busybox" "$work/rescue/lib/ld-musl-x86_64.so.1"
# MBR partition starts at 2 MiB; leave space for the signed toolchain.
rm -f "$disk" "$work/partition.ext4"
truncate -s 1022M "$work/partition.ext4"
sudo mke2fs -q -F -t ext4 -b 4096 -O '^metadata_csum,^64bit' -d "$root" "$work/partition.ext4"
truncate -s 1024M "$disk"
printf 'label: dos\nunit: sectors\n\nstart=4096, size=2093056, type=83\n' | sfdisk "$disk"
e2fsck -fn "$work/partition.ext4"
# Match the historical disk builder: publish a clean, durable host image
# before QEMU opens it, rather than charging image creation writeback to
# the guest's first VirtIO FLUSH.
dd if="$work/partition.ext4" of="$disk" bs=1M seek=2 conv=notrunc,fsync status=none
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
import json, pathlib, socket, sys, time, pexpect
work, ovmf, disk = sys.argv[1:]
args = ['-M','q35','-m','1024M','-cpu','qemu64','-display','none','-monitor','none',
        '-serial','stdio','-no-reboot','-no-shutdown','-nic','none',
        '-device','isa-debug-exit,iobase=0xf4,iosize=0x04',
        '-drive',f'if=pflash,unit=0,format=raw,file={ovmf},readonly=on',
        '-drive',f'if=none,id=root,format=raw,file={disk}',
        '-device','virtio-blk-pci,drive=root,disable-legacy=on',
        '-vga','none','-device','virtio-gpu-pci,disable-legacy=on',
        '-device','virtio-keyboard-pci,disable-legacy=on',
        '-device','virtio-mouse-pci,disable-legacy=on',
        '-qmp',f'unix:{work}/qmp.sock,server=on,wait=off',
        '-cdrom',f'{work}/init.iso']
def screenshot():
    # Read the real QEMU scanout, not a guest-generated screenshot.
    with socket.socket(socket.AF_UNIX) as sock:
        sock.settimeout(10)
        sock.connect(f'{work}/qmp.sock')
        with sock.makefile('rwb') as stream:
            json.loads(stream.readline())
            for command in ({'execute':'qmp_capabilities'},
                            {'execute':'screendump','arguments':{'filename':f'{work}/xorg.ppm'}}):
                stream.write(json.dumps(command).encode()+b'\n')
                stream.flush()
                while True:
                    reply=json.loads(stream.readline())
                    if 'event' in reply: continue
                    if 'error' in reply: raise RuntimeError(reply)
                    if 'return' in reply: break
    magic, width, height, maximum, pixels = pathlib.Path(work,'xorg.ppm').read_bytes().split(None,4)
    assert magic==b'P6' and maximum==b'255', (magic,maximum)
    count=int(width)*int(height)
    assert len(pixels)==count*3
    red=sum(pixels[i:i+3]==b'\xff\x00\x00' for i in range(0,len(pixels),3))
    print(f'Xorg QEMU scanout: {int(width)}x{int(height)}, red={red}/{count}')
    assert red>=count*0.9, 'Xorg root-window color not visible on real scanout'
xorg_ok=False
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
        guest.sendline('/bin/busybox sh /root/acceptance.sh')
        guest.expect(r'\r*\n\[APP DONE status=[01]\]\r*\n', timeout=900)
        guest.expect(r'# ')
        guest.sendline('ls -l /dev/fb0 /sys/class/graphics/fb0; readlink /sys/class/graphics/fb0; python3 -c \'import os,fcntl; fd=os.open("/dev/fb0",os.O_RDWR); b=bytearray(80); fcntl.ioctl(fd,0x4602,b,True); print("FB_FIX",b.hex()); os.close(fd)\'; echo FB_DIAG_END')
        guest.expect(r'\r*\nFB_DIAG_END\r*\n',timeout=120)
        guest.sendline('Xorg :0 vt1 -keeptty -config /root/xorg.conf -logfile /root/Xorg.log -nolisten tcp -ac -noreset >/root/xorg-console.log 2>&1 & xp=$!')
        guest.sendline('for i in $(seq 1 30); do kill -0 "$xp" 2>/dev/null || break; timeout 3 xdpyinfo -display :0 >/root/xdpyinfo.log 2>&1 && break; sleep 1; done; if kill -0 "$xp" 2>/dev/null && timeout 5 xsetroot -display :0 -solid red; then echo XORG_READY; else echo XORG_FAILED; fi')
        result=guest.expect([r'\r*\nXORG_READY\r*\n',r'\r*\nXORG_FAILED\r*\n'],timeout=180)
        if result==0:
            try:
                time.sleep(1)
                screenshot()
                xorg_ok=True
            except (OSError,ValueError,AssertionError,RuntimeError) as error:
                print(f'Xorg scanout failed: {error}')
        guest.sendline('kill "$xp" 2>/dev/null; wait "$xp"; cat /root/xorg-console.log /root/Xorg.log /root/xdpyinfo.log; echo XORG_LOG_END')
        if result!=0:
            guest.expect(r'\r*\nXORG_LOG_END\r*\n',timeout=180)
            guest.sendline('timeout 100 strace -f -e trace=open,openat,readlink,readlinkat,ioctl,bind,connect -o /root/xorg.trace Xorg :0 vt1 -keeptty -config /root/xorg.conf -logfile /root/Xorg-traced.log -nolisten tcp -ac -noreset >/root/xorg-traced-console.log 2>&1; grep -E "/dev/fb|/sys/class/graphics|/dev/tty|bind\\(|connect\\(|ioctl\\(" /root/xorg.trace | tail -80; cat /root/xorg-traced-console.log; echo XORG_TRACE_END')
            guest.expect(r'\r*\nXORG_TRACE_END\r*\n',timeout=180)
        else:
            guest.expect(r'\r*\nXORG_LOG_END\r*\n',timeout=180)
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
for line in serial.splitlines():
    if line.startswith(('[APP PASS]', '[APP FAIL]', '[APP DONE]','[APP DONE status=')):
        print(line)
print('PASS: original Alpine init, serial login, CLI and PID1 poweroff')
# Emit the full Xorg diagnostic tail, including failed starts, to the CI log.
start=serial.find('FB_FIX')
if start<0: start=serial.rfind('FB_DIAG_END')
if start<0: start=serial.rfind('XORG_FAILED')
if start>=0: print(serial[start:])
pathlib.Path(work,'xorg.result').write_text('PASS\n' if xorg_ok else 'FAIL\n')
PY
# Prove that the actual physical disk was left clean, not only a RAM copy.
dd if="$disk" of="$work/partition.ext4" bs=1M skip=2 status=none
e2fsck -fn "$work/partition.ext4"
# Check the independent application results only after shutdown and fsck.
tr -d '\r' < "$work/init.log" | grep -Fxq '[APP DONE status=0]'
for app in archive-zstd sqlite lua python git gcc-make; do
    tr -d '\r' < "$work/init.log" | grep -Fxq "[APP PASS] $app"
done
grep -Fxq PASS "$work/xorg.result"
