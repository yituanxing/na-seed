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
        weston weston-backend-x11 weston-shell-desktop weston-clients weston-terminal wayland-utils \
        xorg-server xf86-video-fbdev xf86-input-evdev xkeyboard-config xdpyinfo xsetroot strace xterm xclock xmessage xwininfo xdotool xprop xrandr xinput xset xhost xrdb xmodmap xwd xeyes xcalc openbox pcmanfm mousepad dbus xclip shared-mime-info adwaita-icon-theme font-dejavu font-adobe-75dpi font-adobe-100dpi harfbuzz
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
    fc-cache --system-only
    fc-match sans
    glib-compile-schemas /usr/share/glib-2.0/schemas
    update-mime-database /usr/share/mime
    update-desktop-database /usr/share/applications
    mkdir -p /root/desktop/documents /root/.config
    printf "original text\n" > /root/desktop/documents/note.txt
    printf "[Default Applications]\ntext/plain=org.xfce.mousepad.desktop;\n" > /root/.config/mimeapps.list
    test -f /usr/share/applications/org.xfce.mousepad.desktop
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
# Resolve the actual event nodes by Linux capability bits, not probe order.
sudo tee "$root/root/xorg-input.py" >/dev/null <<'INPUT'
import fcntl, glob, os
devices={}
for path in sorted(glob.glob('/dev/input/event*')):
    with open(path,'rb',buffering=0) as fd:
        name=bytearray(128)
        fcntl.ioctl(fd,0x80804506,name,True)
        keys=bytearray(96)
        rel=bytearray(8)
        fcntl.ioctl(fd,0x80604521,keys,True)
        try:
            fcntl.ioctl(fd,0x80084522,rel,True)
        except OSError:
            pass
        if keys[30//8] & (1<<(30%8)):
            devices['Keyboard']=path
        elif rel[0]&3==3:
            devices['Mouse']=path
        print(path,name.split(b'\0',1)[0].decode(),flush=True)
assert set(devices)=={'Keyboard','Mouse'},devices
with open('/root/xorg.conf','a') as cfg:
    for kind,path in devices.items():
        cfg.write(f'\nSection "InputDevice"\n    Identifier "{kind}"\n    Driver "evdev"\n    Option "Device" "{path}"\n    Option "GrabDevice" "false"\n')
        if kind=='Keyboard':
            cfg.write('    Option "XkbLayout" "us"\n')
        cfg.write('EndSection\n')
    cfg.write('\nSection "ServerLayout"\n    Identifier "Layout"\n    Screen "Screen"\n    InputDevice "Keyboard" "CoreKeyboard"\n    InputDevice "Mouse" "CorePointer"\nEndSection\n')
INPUT
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
    python3 -c 'import json,pathlib,subprocess; p=pathlib.Path("python.json"); p.write_text(json.dumps({"value":42})); assert json.loads(p.read_text())["value"]==42; assert subprocess.check_output(["/bin/busybox","echo","42"]).strip()==b"42"' &&
    python3 -c 'import socket,select; a,b=socket.socketpair(); assert a.getsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR)==0; a.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1); assert a.getsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR)==1; assert b.getsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR)==0; e=select.epoll(); e.register(a,select.EPOLLIN|select.EPOLLOUT|select.EPOLLET); assert e.poll(0); b.sendall(b"x"); assert any(v&select.EPOLLIN for _,v in e.poll(0)); assert a.recv(1)==b"x"; b.sendall(b"y"); assert any(v&select.EPOLLIN for _,v in e.poll(0)), "lost socket edge after drain/refill"; assert not e.poll(0); assert a.recv(1)==b"y"; e.close(); a.close(); b.close()'
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
truncate -s 1534M "$work/partition.ext4"
sudo mke2fs -q -F -t ext4 -b 4096 -O '^metadata_csum,^64bit' -d "$root" "$work/partition.ext4"
truncate -s 1536M "$disk"
printf 'label: dos\nunit: sectors\n\nstart=4096, size=3141632, type=83\n' | sfdisk "$disk"
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
def qmp(command):
    # Input and screenshots traverse the same real QEMU device path.
    with socket.socket(socket.AF_UNIX) as sock:
        sock.settimeout(10)
        sock.connect(f'{work}/qmp.sock')
        with sock.makefile('rwb') as stream:
            json.loads(stream.readline())
            for request in ({'execute':'qmp_capabilities'},command):
                stream.write(json.dumps(request).encode()+b'\n')
                stream.flush()
                while True:
                    reply=json.loads(stream.readline())
                    if 'event' in reply: continue
                    if 'error' in reply: raise RuntimeError(reply)
                    if 'return' in reply: break
    return reply['return']

def capture_scanout(name):
    qmp({'execute':'screendump','arguments':{'filename':f'{work}/{name}.ppm'}})
    magic, width, height, maximum, pixels = pathlib.Path(work,f'{name}.ppm').read_bytes().split(None,4)
    assert magic==b'P6' and maximum==b'255', (magic,maximum)
    count=int(width)*int(height)
    assert len(pixels)==count*3
    return int(width),int(height),pixels

def screenshot():
    width,height,pixels=capture_scanout('xorg')
    count=width*height
    red=sum(pixels[i:i+3]==b'\xff\x00\x00' for i in range(0,len(pixels),3))
    print(f'Xorg QEMU scanout: {int(width)}x{int(height)}, red={red}/{count}')
    assert red>=count*0.9, 'Xorg root-window color not visible on real scanout'
# Exercise independent original X11 clients and inspect the physical scanout.
apps = [
    ('xterm','NaSeed-XTerm',
     "xterm -T NaSeed-XTerm -geometry 44x9+20+20 -bg white -fg black -fn fixed -e /bin/sh -c 'printf \"NaSeed X11 terminal\\nOriginal BusyBox shell\\nPTY and font rendering\\n\"; exec /bin/sh -i'",
     (30,30,250,120),b'\xff\xff\xff'),
    ('xclock','NaSeed-XClock',
     'xclock -title NaSeed-XClock -geometry 200x200+400+20 -bg yellow -fg black -hd black -hl black -update 1',
     (410,30,590,210),b'\xff\xff\x00'),
    ('xmessage','NaSeed-XMessage',
     'xmessage -title NaSeed-XMessage -geometry 300x130+20+300 -bg cyan -fg black -buttons OK:0 "NaSeed native X11 message"',
     (28,308,310,424),b'\x00\xff\xff'),
]
def verify_apps(mapped):
    width,height,pixels=capture_scanout('x11-apps')
    assert (width,height)==(800,600),(width,height)
    passed=True
    for name,_,_,(x0,y0,x1,y1),color in apps:
        region=b''.join(pixels[(y*width+x0)*3:(y*width+x1)*3] for y in range(y0,y1))
        colors=[region[i:i+3] for i in range(0,len(region),3)]
        bg=colors.count(color)
        ink=colors.count(b'\x00\x00\x00')
        ok=mapped[name] and bg>=len(colors)//10 and ink>=30
        print(f'X11 {name}: mapped={mapped[name]}, background={bg}/{len(colors)}, black={ink}, {"PASS" if ok else "FAIL"}')
        passed=passed and ok
    return passed
# xdotool is used only to read the server's pointer position; all motion,
# buttons and keys originate from QMP, never XTEST or a guest-side injection.
def pointer():
    guest.sendline('p=$(xdotool getmouselocation --shell); eval "$p"; printf "POINTER=%s,%s\n" "$X" "$Y"')
    guest.expect(r'\r*\nPOINTER=([0-9]+),([0-9]+)\r*\n',timeout=20)
    return tuple(int(v) for v in guest.match.groups())

def input_events(events):
    qmp({'execute':'input-send-event','arguments':{'events':events}})
    time.sleep(0.08)

def move_to(x,y):
    for _ in range(25):
        px,py=pointer()
        if abs(x-px)<=3 and abs(y-py)<=3:
            return
        events=[]
        for axis,delta in (('x',x-px),('y',y-py)):
            if delta:
                events.append({'type':'rel','data':{'axis':axis,'value':max(-50,min(50,delta))}})
        input_events(events)
    raise RuntimeError(f'pointer could not reach {(x,y)}; last={(px,py)}')

def click():
    for down in (True,False):
        input_events([{'type':'btn','data':{'button':'left','down':down}}])

def type_keys(text):
    for ch in text:
        key={' ':'spc','/':'slash','.':'dot','>':'dot','\n':'ret'}.get(ch,ch)
        keys=['shift',key] if ch=='>' else [key]
        for k in keys:
            input_events([{'type':'key','data':{'key':{'type':'qcode','data':k},'down':True}}])
        for k in reversed(keys):
            input_events([{'type':'key','data':{'key':{'type':'qcode','data':k},'down':False}}])

def verify_input():
    move_to(120,90)
    click()
    type_keys('echo 42 >/tmp/keycheck\n')
    guest.sendline('for i in $(seq 1 10); do test "$(cat /tmp/keycheck 2>/dev/null)" = 42 && break; sleep 1; done; if test "$(cat /tmp/keycheck 2>/dev/null)" = 42; then echo KEYBOARD_OK; else echo KEYBOARD_FAILED; fi')
    if guest.expect([r'\r*\nKEYBOARD_OK\r*\n',r'\r*\nKEYBOARD_FAILED\r*\n'],timeout=30)!=0:
        raise RuntimeError('hardware keyboard did not execute command in xterm')
    print('PASS: QMP keyboard -> VirtIO -> evdev -> Xorg -> xterm -> BusyBox shell')
    capture_scanout('x11-keyboard')
    # The actual round-two scanout places the OK button at (36,418).
    move_to(36,418)
    click()
    guest.sendline('for i in $(seq 1 10); do kill -0 "$xmp" 2>/dev/null || break; sleep 1; done; if kill -0 "$xmp" 2>/dev/null; then echo MOUSE_FAILED; else wait "$xmp"; printf "MOUSE_EXIT=%s\n" "$?"; fi')
    if guest.expect([r'\r*\nMOUSE_EXIT=0\r*\n',r'\r*\nMOUSE_FAILED\r*\n',r'\r*\nMOUSE_EXIT=[0-9]+\r*\n'],timeout=30)!=0:
        raise RuntimeError('hardware mouse did not activate xmessage OK button')
    print('PASS: QMP mouse -> VirtIO -> evdev -> Xorg -> xmessage exit 0')
    capture_scanout('x11-input')
    return True

# Openbox is the signed, unchanged distribution package. Read EWMH and
# geometry to measure outcomes; issue every window operation via QMP.
def geometry(title):
    guest.sendline(f'id=$(xdotool search --name "^{title}$" | head -1); if [ -n "$id" ]; then eval "$(xdotool getwindowgeometry --shell "$id")"; printf "WM_GEOM=%s,%s,%s,%s,%s\\n" "$id" "$X" "$Y" "$WIDTH" "$HEIGHT"; else echo WM_GEOM_FAILED; fi')
    if guest.expect([r'\r*\nWM_GEOM=([0-9]+),(-?[0-9]+),(-?[0-9]+),([0-9]+),([0-9]+)\r*\n',r'\r*\nWM_GEOM_FAILED\r*\n'],timeout=20):
        raise RuntimeError(f'no managed window: {title}')
    return tuple(int(v) for v in guest.match.groups())

def active_window():
    guest.sendline('id=$(xdotool getactivewindow 2>/dev/null); printf "WM_ACTIVE=%s\\n" "$id"')
    guest.expect(r'\r*\nWM_ACTIVE=([0-9]*)\r*\n',timeout=20)
    return int(guest.match.group(1) or 0)

def wait_active(wid):
    for _ in range(10):
        if active_window()==wid:
            return
        time.sleep(0.15)
    raise RuntimeError(f'Openbox did not focus {wid}')

def key_event(key,down):
    input_events([{'type':'key','data':{'key':{'type':'qcode','data':key},'down':down}}])

def chord(key):
    key_event('alt',True)
    try:
        key_event(key,True)
        key_event(key,False)
    finally:
        key_event('alt',False)
    time.sleep(0.35)

def drag(button,start,end):
    move_to(*start)
    key_event('alt',True)
    input_events([{'type':'btn','data':{'button':button,'down':True}}])
    try:
        move_to(*end)
    finally:
        input_events([{'type':'btn','data':{'button':button,'down':False}}])
        key_event('alt',False)
    time.sleep(0.4)

def desktop_chord(keys):
    for k in keys:
        key_event(k,True)
    for k in reversed(keys):
        key_event(k,False)
    time.sleep(0.4)

def wait_desktop_window(kind):
    for _ in range(30):
        guest.sendline(f'id=$(xdotool getactivewindow 2>/dev/null); if xprop -id "$id" WM_CLASS 2>/dev/null | grep -qi "{kind}"; then echo DESKTOP_WINDOW_OK; else echo DESKTOP_WINDOW_WAIT; fi')
        if guest.expect([r'\r*\nDESKTOP_WINDOW_OK\r*\n',r'\r*\nDESKTOP_WINDOW_WAIT\r*\n'],timeout=20)==0:
            return
        if kind=='pcmanfm':
            guest.sendline('if kill -0 "$fp" 2>/dev/null; then echo FILES_ALIVE; else wait "$fp"; printf "FILES_DEAD=%s\\n" "$?"; fi')
            if guest.expect([r'\r*\nFILES_ALIVE\r*\n',r'\r*\nFILES_DEAD=([0-9]+)\r*\n'],timeout=20):
                raise RuntimeError(f'PCManFM exited {guest.match.group(1)}')
        time.sleep(0.25)
    capture_scanout('desktop-focus-failed')
    guest.sendline('xwininfo -root -tree; xprop -root _NET_CLIENT_LIST _NET_ACTIVE_WINDOW; echo DESKTOP_STATE_END')
    guest.expect(r'\r*\nDESKTOP_STATE_END\r*\n',timeout=20)
    raise RuntimeError(f'no focused {kind} window')

def verify_desktop():
    # Use a real private D-Bus session and the original GTK programs.
    guest.sendline('mkdir -p /run/user/0; chmod 700 /run/user/0; export XDG_RUNTIME_DIR=/run/user/0; dbus-daemon --session --fork --print-address=1 --print-pid=1 >/root/desktop-bus; ds=$?; export DBUS_SESSION_BUS_ADDRESS=$(head -1 /root/desktop-bus); bp=$(tail -1 /root/desktop-bus); printf "BUS_STATUS=%s\\n" "$ds"')
    guest.expect(r'\r*\nBUS_STATUS=([0-9]+)\r*\n',timeout=60)
    assert guest.match.group(1)=='0','D-Bus session startup failed'
    guest.sendline('dbus-send --session --print-reply --dest=org.freedesktop.DBus /org/freedesktop/DBus org.freedesktop.DBus.ListNames >/root/desktop-bus.log 2>&1; printf "BUS_QUERY=%s\\n" "$?"')
    guest.expect(r'\r*\nBUS_QUERY=([0-9]+)\r*\n',timeout=30)
    assert guest.match.group(1)=='0','D-Bus session query failed'
    guest.sendline('pcmanfm /root/desktop >/root/pcmanfm.log 2>&1 & fp=$!; app_pids="$app_pids $fp"')
    wait_desktop_window('pcmanfm')
    capture_scanout('desktop-files')
    desktop_chord(['ctrl','shift','n'])
    time.sleep(0.6)
    capture_scanout('desktop-folder-dialog')
    type_keys('newfolder\n')
    guest.sendline('for i in $(seq 1 10); do test -d /root/desktop/newfolder && break; sleep 1; done; if test -d /root/desktop/newfolder; then echo FOLDER_CREATED; else echo FOLDER_FAILED; fi')
    if guest.expect([r'\r*\nFOLDER_CREATED\r*\n',r'\r*\nFOLDER_FAILED\r*\n'],timeout=30):
        raise RuntimeError('PCManFM did not create directory via GUI')
    print('PASS: original GTK file manager creates directory via keyboard')
    def open_note():
        wait_desktop_window('pcmanfm')
        desktop_chord(['ctrl','l'])
        desktop_chord(['ctrl','a'])
        type_keys('/root/desktop/documents\n')
        time.sleep(1)
        # PCManFM navigates directories via its location entry; select
        # the sole file in this directory using the normal folder view.
        desktop_chord(['home'])
        desktop_chord(['ret'])
        wait_desktop_window('mousepad')
        time.sleep(0.5)
    open_note()
    capture_scanout('desktop-editor-original')
    desktop_chord(['ctrl','a'])
    type_keys('naseed desktop works\n')
    desktop_chord(['ctrl','s'])
    guest.sendline('for i in $(seq 1 10); do test "$(cat /root/desktop/documents/note.txt)" = "naseed desktop works" && break; sleep 1; done; if test "$(cat /root/desktop/documents/note.txt)" = "naseed desktop works"; then echo EDIT_SAVED; else echo EDIT_FAILED; fi')
    if guest.expect([r'\r*\nEDIT_SAVED\r*\n',r'\r*\nEDIT_FAILED\r*\n'],timeout=30):
        raise RuntimeError('Mousepad did not save edited text')
    capture_scanout('desktop-editor-saved')
    print('PASS: original GTK editor modifies and saves existing file')
    chord('f4')
    wait_desktop_window('pcmanfm')
    open_note()
    desktop_chord(['ctrl','a'])
    desktop_chord(['ctrl','c'])
    guest.sendline('if test "$(timeout 5 xclip -selection clipboard -o)" = "naseed desktop works"; then echo EDIT_REOPENED; else echo REOPEN_FAILED; fi')
    if guest.expect([r'\r*\nEDIT_REOPENED\r*\n',r'\r*\nREOPEN_FAILED\r*\n'],timeout=20):
        raise RuntimeError('reopened Mousepad document differs from saved content')
    capture_scanout('desktop-editor-reopened')
    print('PASS: file-manager launch, editor reopen and actual X11 clipboard content')
    chord('f4')
    wait_desktop_window('pcmanfm')
    chord('f4')
    guest.sendline('for i in $(seq 1 10); do kill -0 "$fp" 2>/dev/null || break; sleep 1; done; if ! kill -0 "$fp" 2>/dev/null; then wait "$fp"; printf "FILES_EXIT=%s\\n" "$?"; else echo FILES_EXIT_FAILED; fi')
    if guest.expect([r'\r*\nFILES_EXIT=0\r*\n',r'\r*\nFILES_EXIT_FAILED\r*\n',r'\r*\nFILES_EXIT=[0-9]+\r*\n'],timeout=30):
        raise RuntimeError('PCManFM did not exit cleanly')
    guest.sendline('kill "$bp"; echo DESKTOP_DONE')
    guest.expect(r'\r*\nDESKTOP_DONE\r*\n',timeout=20)
    print('PASS: GTK application session exit')

def verify_wm():
    guest.sendline('openbox --sm-disable >/root/openbox.log 2>&1 & wp=$!')
    guest.sendline('for i in $(seq 1 30); do kill -0 "$wp" 2>/dev/null || break; wid=$(xprop -root _NET_SUPPORTING_WM_CHECK | sed "s/.*# //"); xprop -id "$wid" _NET_WM_NAME 2>/dev/null | grep -qi openbox && break; sleep 1; done; if kill -0 "$wp" 2>/dev/null && xprop -id "$wid" _NET_WM_NAME 2>/dev/null | grep -qi openbox; then echo WM_READY; else echo WM_FAILED; fi')
    if guest.expect([r'\r*\nWM_READY\r*\n',r'\r*\nWM_FAILED\r*\n'],timeout=90):
        raise RuntimeError('Openbox did not publish its EWMH identity')
    term='NaSeed-XTerm'
    clock='NaSeed-XClock'
    a=geometry(term)
    b=geometry(clock)
    guest.sendline('xprop -root _NET_CLIENT_LIST; echo WM_CLIENTS_END')
    guest.expect(r'\r*\nWM_CLIENTS_END\r*\n',timeout=20)
    ids=guest.before
    assert hex(a[0]) in ids and hex(b[0]) in ids, 'both clients must be managed by Openbox'
    capture_scanout('openbox-before')
    _,x,y,width,height=a
    dx=85 if x+width+110<800 else -85
    dy=65 if y+height+90<600 else -65
    center=(x+width//2,y+height//2)
    drag('left',center,(center[0]+dx,center[1]+dy))
    moved=geometry(term)
    assert abs(moved[1]-x)+abs(moved[2]-y)>=40,(a,moved)
    print(f'PASS: Openbox physical Alt+drag move {a[1:3]} -> {moved[1:3]}')
    _,x,y,width,height=moved
    start=(x+width-18,y+height-18)
    target=(min(790,start[0]+75),min(590,start[1]+55))
    assert target[0]-start[0]>=25 and target[1]-start[1]>=25,(start,target)
    drag('right',start,target)
    resized=geometry(term)
    assert resized[3]>=width+24 and resized[4]>=height+20,(moved,resized)
    print(f'PASS: Openbox physical Alt+right-drag resize {moved[3:]} -> {resized[3:]}')
    # An Xt clock can opt out of keyboard focus. Record its ICCCM hints,
    # then test keyboard switching between two ordinary interactive clients.
    guest.sendline('xprop -name NaSeed-XClock WM_HINTS WM_PROTOCOLS _NET_WM_STATE; echo WM_CLOCK_HINTS_END')
    guest.expect(r'\r*\nWM_CLOCK_HINTS_END\r*\n',timeout=20)
    print(guest.before)
    guest.sendline("xterm -T NaSeed-WM-Term -geometry 30x8+460+330 -bg cyan -fg black -fn fixed -e /bin/sh -i >/root/wm-term.log 2>&1 & wtp=$!; app_pids=\"$app_pids $wtp\"")
    guest.sendline('for i in $(seq 1 30); do kill -0 "$wtp" 2>/dev/null || break; timeout 3 xwininfo -name NaSeed-WM-Term 2>/dev/null | grep -q "Map State: IsViewable" && break; sleep 1; done; if kill -0 "$wtp" 2>/dev/null && timeout 3 xwininfo -name NaSeed-WM-Term 2>/dev/null | grep -q "Map State: IsViewable"; then echo WM_NEW_READY; else echo WM_NEW_FAILED; fi')
    if guest.expect([r'\r*\nWM_NEW_READY\r*\n',r'\r*\nWM_NEW_FAILED\r*\n'],timeout=90):
        raise RuntimeError('Openbox failed to map a newly launched xterm')
    other=geometry('NaSeed-WM-Term')
    guest.sendline('xprop -root _NET_CLIENT_LIST; echo WM_NEW_CLIENTS_END')
    guest.expect(r'\r*\nWM_NEW_CLIENTS_END\r*\n',timeout=20)
    assert all(hex(wid) in guest.before for wid in (a[0],b[0],other[0])), 'all three clients must remain managed'
    move_to(other[1]+other[3]//2,other[2]+other[4]//2)
    click()
    wait_active(other[0])
    move_to(resized[1]+resized[3]//2,resized[2]+resized[4]//2)
    click()
    wait_active(a[0])
    chord('tab')
    wait_active(other[0])
    print('PASS: Openbox physical Alt+Tab focus switch')
    # The reparented clients must still paint on the real QEMU framebuffer.
    w,h,pixels=capture_scanout('openbox-managed')
    for g,color in ((resized,b'\xff\xff\xff'),(geometry(clock),b'\xff\xff\x00'),(other,b'\x00\xff\xff')):
        _,gx,gy,gw,gh=g
        x0,y0=max(0,gx),max(0,gy)
        x1,y1=min(w,gx+gw),min(h,gy+gh)
        region=b''.join(pixels[(row*w+x0)*3:(row*w+x1)*3] for row in range(y0,y1))
        assert region.count(color)>=200,(g,color)
    chord('f4')
    guest.sendline('for i in $(seq 1 15); do kill -0 "$wtp" 2>/dev/null || break; sleep 1; done; if ! kill -0 "$wtp" 2>/dev/null && ! xdotool search --name "^NaSeed-WM-Term$" >/dev/null 2>&1 && kill -0 "$xtp" 2>/dev/null && kill -0 "$xcp" 2>/dev/null && kill -0 "$wp" 2>/dev/null; then wait "$wtp"; printf "WM_CLOSED=%s\\n" "$?"; else echo WM_CLOSE_FAILED; fi')
    if guest.expect([r'\r*\nWM_CLOSED=0\r*\n',r'\r*\nWM_CLOSE_FAILED\r*\n',r'\r*\nWM_CLOSED=[0-9]+\r*\n'],timeout=40):
        raise RuntimeError('Openbox Alt+F4 did not close the second interactive terminal cleanly')
    wait_active(a[0])
    type_keys('echo 43 >/tmp/wmcheck\n')
    guest.sendline('for i in $(seq 1 10); do test "$(cat /tmp/wmcheck 2>/dev/null)" = 43 && break; sleep 1; done; if test "$(cat /tmp/wmcheck 2>/dev/null)" = 43; then echo WM_SHELL_OK; else echo WM_SHELL_FAILED; fi')
    if guest.expect([r'\r*\nWM_SHELL_OK\r*\n',r'\r*\nWM_SHELL_FAILED\r*\n'],timeout=30):
        raise RuntimeError('surviving managed xterm cannot execute a shell command')
    capture_scanout('openbox-closed')
    print('PASS: Openbox close and surviving managed xterm keyboard/PTY')
    verify_desktop()
    guest.sendline('timeout 5 openbox --exit >/root/openbox-exit.log 2>&1; for i in $(seq 1 15); do if ! kill -0 "$wp" 2>/dev/null || grep -Eq "^State:[[:space:]]*Z" "/proc/$wp/status" 2>/dev/null; then break; fi; sleep 1; done; if ! kill -0 "$wp" 2>/dev/null || grep -Eq "^State:[[:space:]]*Z" "/proc/$wp/status" 2>/dev/null; then wait "$wp"; printf "WM_EXIT=%s\\n" "$?"; else echo WM_EXIT_FAILED; fi')
    if guest.expect([r'\r*\nWM_EXIT=0\r*\n',r'\r*\nWM_EXIT_FAILED\r*\n',r'\r*\nWM_EXIT=[0-9]+\r*\n'],timeout=40):
        raise RuntimeError('Openbox session exit failed')
    print('PASS: Openbox clean window-manager exit')
    return True

# Exercise the original X.Org client suite against the running Xorg server.
# Probe core X11 clients before Openbox, so an unrelated window-manager exit
# issue cannot suppress the direct X.Org results; still require every gate.
def verify_xorg_tools():
    checks=[
        ('xrandr', 'timeout 15 xrandr --query > /root/xorg-xrandr.log 2>&1 && grep -q "Screen 0:" /root/xorg-xrandr.log'),
        ('xinput', 'timeout 15 xinput list --short > /root/xorg-xinput.log 2>&1 && grep -qi pointer /root/xorg-xinput.log && grep -qi keyboard /root/xorg-xinput.log'),
        ('xset', 'timeout 15 xset q > /root/xorg-xset.log 2>&1 && grep -q "Keyboard Control" /root/xorg-xset.log'),
        ('xhost', 'timeout 15 xhost > /root/xorg-xhost.log 2>&1 && test -s /root/xorg-xhost.log'),
        ('xmodmap', 'timeout 15 xmodmap -pm > /root/xorg-xmodmap.log 2>&1 && grep -qi shift /root/xorg-xmodmap.log'),
        ('xkbcomp', 'timeout 15 xkbcomp -xkb :0 /root/xorg-keymap.xkb > /root/xorg-xkbcomp.log 2>&1 && test -s /root/xorg-keymap.xkb'),
        ('xrdb', "printf 'NaSeed.xorgProbe: confirmed\\n' | timeout 15 xrdb -merge > /root/xorg-xrdb.log 2>&1 && timeout 15 xrdb -query | grep -Fq 'NaSeed.xorgProbe:'"),
        ('xwd', 'timeout 15 xwd -root -silent -out /root/xorg-root.xwd > /root/xorg-xwd.log 2>&1 && test "$(wc -c </root/xorg-root.xwd)" -gt 1024'),
    ]
    passed=True
    for name,command in checks:
        guest.sendline(f'if {command}; then echo XORG_TOOL_PASS_{name}; else echo XORG_TOOL_FAIL_{name}; fi')
        ok=guest.expect([rf'\r*\nXORG_TOOL_PASS_{name}\r*\n',rf'\r*\nXORG_TOOL_FAIL_{name}\r*\n'],timeout=90)==0
        print(f'Xorg original tool {name}: {"PASS" if ok else "FAIL"}')
        if not ok:
            guest.sendline(f'tail -30 /root/xorg-{name}.log 2>/dev/null; echo XORG_TOOL_LOG_END')
            guest.expect(r'\r*\nXORG_TOOL_LOG_END\r*\n',timeout=30)
            print(guest.before)
        passed=passed and ok
    for name,title,command in (
        ('xeyes','NaSeed-XEyes','xeyes -title NaSeed-XEyes -geometry 130x100+625+310'),
        ('xcalc','NaSeed-XCalc','xcalc -title NaSeed-XCalc -geometry 210x220+545+310'),
    ):
        width,height,before=capture_scanout(f'x11-{name}-before')
        guest.sendline(f'{command} > /root/{name}.log 2>&1 & ep=$!')
        guest.sendline(f'for i in $(seq 1 20); do kill -0 "$ep" 2>/dev/null || break; xwininfo -name {title} 2>/dev/null | grep -q "Map State: IsViewable" && break; sleep 1; done; if kill -0 "$ep" 2>/dev/null && xwininfo -name {title} 2>/dev/null | grep -q "Map State: IsViewable"; then echo XORG_CLIENT_MAPPED; else echo XORG_CLIENT_FAILED; fi')
        mapped=guest.expect([r'\r*\nXORG_CLIENT_MAPPED\r*\n',r'\r*\nXORG_CLIENT_FAILED\r*\n'],timeout=90)==0
        changed=0
        if mapped:
            time.sleep(0.5)
            w,h,after=capture_scanout(f'x11-{name}')
            assert (w,h)==(width,height),(w,h)
            changed=sum(before[i:i+3]!=after[i:i+3] for i in range(0,len(before),3))
        ok=mapped and changed>=500
        print(f'Xorg original client {name}: mapped={mapped}, framebuffer_changed={changed}, {"PASS" if ok else "FAIL"}')
        if not ok:
            guest.sendline(f'cat /root/{name}.log; echo XORG_CLIENT_LOG_END')
            guest.expect(r'\r*\nXORG_CLIENT_LOG_END\r*\n',timeout=30)
            print(guest.before)
        guest.sendline('kill "$ep" 2>/dev/null; wait "$ep" 2>/dev/null; echo XORG_CLIENT_STOPPED')
        guest.expect(r'\r*\nXORG_CLIENT_STOPPED\r*\n',timeout=30)
        passed=passed and ok
    return passed

# Weston is the original Alpine compositor using its X11/pixman backend.
# Keep Xorg and its complete acceptance intact: a Wayland failure is separate.
def verify_wayland():
    guest.sendline('wlc=; mkdir -p /run/user/0; chmod 700 /run/user/0; export XDG_RUNTIME_DIR=/run/user/0; unset WAYLAND_DISPLAY; weston --backend=x11 --renderer=pixman --no-config --idle-time=0 --socket=wayland-1 --width=600 --height=420 > /root/weston.log 2>&1 & wlp=$!')
    try:
        width,height,baseline=capture_scanout('wayland-before')
        guest.sendline('for i in $(seq 1 45); do test -S /run/user/0/wayland-1 && break; kill -0 "$wlp" 2>/dev/null || break; sleep 1; done; if test -S /run/user/0/wayland-1 && timeout 15 env WAYLAND_DISPLAY=wayland-1 wayland-info >/root/wayland-info.log 2>&1 && grep -q "wl_compositor" /root/wayland-info.log && grep -q "xdg_wm_base" /root/wayland-info.log; then echo WAYLAND_PROTOCOL_PASS; else echo WAYLAND_PROTOCOL_FAIL; fi')
        if guest.expect([r'\r*\nWAYLAND_PROTOCOL_PASS\r*\n',r'\r*\nWAYLAND_PROTOCOL_FAIL\r*\n'],timeout=115):
            raise RuntimeError('original Weston X11 backend did not provide a usable Wayland compositor and xdg_wm_base')
        print('PASS: Alpine Weston X11 backend serves original wayland-info and xdg_wm_base')
        time.sleep(0.7)
        w,h,compositor=capture_scanout('wayland-weston')
        assert (w,h)==(width,height)
        changed=sum(baseline[i:i+3]!=compositor[i:i+3] for i in range(0,len(baseline),3))
        print(f'Weston nested X11 framebuffer_changed={changed}')
        assert changed>=500,'Weston X11 backend did not draw a physical window'
        guest.sendline('env WAYLAND_DISPLAY=wayland-1 weston-simple-shm >/root/weston-simple-shm.log 2>&1 & wlc=$!; echo WAYLAND_CLIENT_STARTED')
        guest.expect(r'\r*\nWAYLAND_CLIENT_STARTED\r*\n',timeout=30)
        time.sleep(2.0)
        guest.sendline('if kill -0 "$wlc" 2>/dev/null && test -S /run/user/0/wayland-1; then echo WAYLAND_CLIENT_ALIVE; else echo WAYLAND_CLIENT_DEAD; fi')
        if guest.expect([r'\r*\nWAYLAND_CLIENT_ALIVE\r*\n',r'\r*\nWAYLAND_CLIENT_DEAD\r*\n'],timeout=30):
            raise RuntimeError('original weston-simple-shm client failed to stay connected')
        w,h,client=capture_scanout('wayland-simple-shm')
        assert (w,h)==(width,height)
        delta=sum(compositor[i:i+3]!=client[i:i+3] for i in range(0,len(client),3))
        print(f'Wayland original shm client framebuffer_changed={delta}')
        assert delta>=500,'Wayland client did not change the real QEMU framebuffer'
        print('PASS: original Wayland SHM client renders through Weston -> Xorg -> VirtIO GPU')
        return True
    finally:
        guest.sendline('if [ -n "$wlc" ]; then kill "$wlc" 2>/dev/null; wait "$wlc" 2>/dev/null; fi; kill "$wlp" 2>/dev/null; wait "$wlp" 2>/dev/null; for f in /root/weston.log /root/wayland-info.log /root/weston-simple-shm.log; do echo "=== $f ==="; tail -70 "$f" 2>/dev/null; done; echo WAYLAND_LOG_END')
        guest.expect(r'\r*\nWAYLAND_LOG_END\r*\n',timeout=90)
        print(guest.before)

xorg_ok=False
apps_ok=False
input_ok=False
wm_ok=False
tools_ok=False
wayland_ok=False
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
        # Host-generated caches can be invalidated by ext4 image creation.
        # Run the real package trigger in the booted distribution.
        guest.sendline("fc-cache --system-only -v && fc-match sans; printf 'FONT_STATUS=%s\\n' \"$?\"")
        guest.expect(r'\r*\nFONT_STATUS=([0-9]+)\r*\n',timeout=900)
        if guest.match.group(1)!='0':
            raise RuntimeError('Alpine font cache initialization failed')
        guest.expect(r'# ')
        guest.sendline('python3 /root/xorg-input.py; printf "INPUT_CONFIG_STATUS=%s\n" "$?"')
        guest.expect(r'\r*\nINPUT_CONFIG_STATUS=([0-9]+)\r*\n',timeout=60)
        if guest.match.group(1)!='0':
            raise RuntimeError('real evdev device discovery failed')
        guest.expect(r'# ')
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
        if xorg_ok:
            guest.sendline('export DISPLAY=:0; app_pids=""; wp=')
            mapped={}
            for name,title,command,_,_ in apps:
                guest.sendline(f'{command} >/root/{name}.log 2>&1 & ap=$!; app_pids="$app_pids $ap"')
                if name=='xterm':
                    guest.sendline('xtp=$ap')
                elif name=='xclock':
                    guest.sendline('xcp=$ap')
                elif name=='xmessage':
                    guest.sendline('xmp=$ap')
                guest.sendline(f'for i in $(seq 1 90); do kill -0 "$ap" 2>/dev/null || break; timeout 3 xwininfo -name {title} >/root/{name}.window 2>&1 && grep -q "Map State: IsViewable" /root/{name}.window && break; sleep 1; done; if kill -0 "$ap" 2>/dev/null && grep -q "Map State: IsViewable" /root/{name}.window; then echo XAPP_MAPPED; else echo XAPP_FAILED; fi')
                mapped[name]=guest.expect([r'\r*\nXAPP_MAPPED\r*\n',r'\r*\nXAPP_FAILED\r*\n'],timeout=240)==0
            try:
                time.sleep(1)
                apps_ok=verify_apps(mapped)
                if apps_ok:
                    input_ok=verify_input()
                if input_ok:
                    tools_ok=verify_xorg_tools()
                    wm_ok=verify_wm()
                    if wm_ok:
                        wayland_ok=verify_wayland()
            except (OSError,ValueError,AssertionError,RuntimeError,pexpect.TIMEOUT) as error:
                print(f'X11 application scanout failed: {error}')
            if not mapped['xterm']:
                # A failure must preserve the original syscall evidence, not
                # be converted into a pass by changing xterm resources.
                guest.sendline('cat /proc/$xtp/status; ls -l /proc/$xtp/fd; echo XTERM_STATE_END')
                guest.expect(r'\r*\nXTERM_STATE_END\r*\n',timeout=60)
                guest.sendline("timeout -s KILL 15 python3 -c 'import ctypes,os,sys; p=int(sys.argv[1]); libc=ctypes.CDLL(None,use_errno=True); libc.ptrace.restype=ctypes.c_long; libc.ptrace.argtypes=[ctypes.c_long,ctypes.c_long,ctypes.c_void_p,ctypes.c_void_p]; r=(ctypes.c_ulonglong*27)(); assert libc.ptrace(16,p,None,None)==0,ctypes.get_errno(); os.waitpid(p,0); assert libc.ptrace(12,p,None,ctypes.byref(r))==0,ctypes.get_errno(); print(\"BLOCKED_REGS\",p,\"rax\",r[10],\"orig_rax\",r[15],\"rip\",hex(r[16]),\"args\",[hex(r[i]) for i in (14,13,12,7,9,8)],flush=True); assert libc.ptrace(17,p,None,None)==0,ctypes.get_errno()' \"$xtp\"; echo XTERM_TRACE_END")
                guest.expect(r'\r*\nXTERM_TRACE_END\r*\n',timeout=60)
            guest.sendline('if [ -n "$wp" ]; then kill "$wp" 2>/dev/null; wait "$wp" 2>/dev/null; fi; if [ -n "$bp" ]; then kill "$bp" 2>/dev/null; fi; cat /root/openbox.log /root/openbox-exit.log /root/wm-term.log /root/desktop-bus.log /root/pcmanfm.log 2>/dev/null; echo WM_LOG_END')
            guest.expect(r'\r*\nWM_LOG_END\r*\n',timeout=60)
            guest.sendline('kill $app_pids 2>/dev/null; for p in $app_pids; do wait "$p"; done; for a in xterm xclock xmessage; do echo "=== $a ==="; cat /root/$a.log /root/$a.window; done; echo XAPP_LOG_END')
            guest.expect(r'\r*\nXAPP_LOG_END\r*\n',timeout=120)
        if result!=0:
            guest.sendline('strace -e trace=socket,connect,getsockname,getpeername,getsockopt,setsockopt,fcntl,poll,ppoll,select,pselect6,read,readv,write,writev,recvmsg,sendmsg,recvfrom,sendto,shutdown,close -o /root/xclient.trace xdpyinfo -display :0 >/root/xclient.log 2>&1; tail -100 /root/xclient.trace; cat /root/xclient.log; echo XCLIENT_TRACE_END')
            guest.expect(r'\r*\nXCLIENT_TRACE_END\r*\n',timeout=60)
        guest.sendline('kill "$xp" 2>/dev/null; wait "$xp"; cat /root/xorg-console.log /root/Xorg.log /root/xdpyinfo.log; echo XORG_LOG_END')
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
start=serial.rfind('XORG_READY')
if start<0: start=serial.rfind('XORG_FAILED')
if start>=0: print(serial[start:])
pathlib.Path(work,'xorg.result').write_text('PASS\n' if xorg_ok and apps_ok and input_ok and wm_ok and tools_ok and wayland_ok else 'FAIL\n')
PY
# Prove that the actual physical disk was left clean, not only a RAM copy.
dd if="$disk" of="$work/partition.ext4" bs=1M skip=2 status=none
e2fsck -fn "$work/partition.ext4"
# Check the independent application results only after shutdown and fsck.
tr -d '\r' < "$work/init.log" | grep -Fx '[APP DONE status=0]' >/dev/null
for app in archive-zstd sqlite lua python git gcc-make; do
    tr -d '\r' < "$work/init.log" | grep -Fx "[APP PASS] $app" >/dev/null
done
grep -Fxq PASS "$work/xorg.result"
# Read persisted content from the clean, unmounted ext4 image itself.
debugfs -R 'cat /root/desktop/documents/note.txt' "$work/partition.ext4" > "$work/desktop-note.txt"
test "$(cat "$work/desktop-note.txt")" = 'naseed desktop works'
debugfs -R 'stat /root/desktop/newfolder' "$work/partition.ext4" | grep -F 'Type: directory'
echo 'PASS: desktop-created directory and edited file survived shutdown'
