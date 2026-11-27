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
        weston weston-backend-x11 weston-backend-drm libseat seatd-launch eudev libinput-tools weston-shell-desktop weston-clients weston-terminal wayland-utils libdrm-tests \
        xorg-server xf86-video-fbdev xf86-input-evdev xkeyboard-config xdpyinfo xsetroot strace xterm xclock xmessage xwininfo xdotool xprop xrandr xinput xset xhost xrdb xmodmap xwd xeyes xcalc openbox pcmanfm mousepad thunar dbus xclip shared-mime-info adwaita-icon-theme font-dejavu font-adobe-75dpi font-adobe-100dpi harfbuzz
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
# Verify the original XKB package's versioned alias before imaging it into
# ext4; this comparison distinguishes packaging from guest VFS semantics.
sudo chroot "$root" /bin/busybox sh -ec 'test -r /usr/share/xkeyboard-config-2/rules/evdev && head -c 1 /usr/share/xkeyboard-config-2/rules/evdev >/dev/null'
echo 'PASS: original Alpine canonical XKB path readable in host chroot'
# Use the ordinary Xorg configuration interface, not a patched X server.

# Query NaSeed's live DRM node through Alpine's unmodified libdrm.so.2.
# No copied Linux ioctl implementation or fabricated KMS resources in tests.
sudo tee "$root/root/drm-probe.py" >/dev/null <<'DRM_PROBE'
import ctypes
import os
import stat

class Version(ctypes.Structure):
    _fields_ = [
        ("major", ctypes.c_int), ("minor", ctypes.c_int),
        ("patchlevel", ctypes.c_int),
        # libdrm's public drmVersion is NOT the 64-byte Linux ioctl UAPI
        # struct drm_version: these lengths are int, not native size_t.
        ("name_len", ctypes.c_int), ("name", ctypes.c_void_p),
        ("date_len", ctypes.c_int), ("date", ctypes.c_void_p),
        ("desc_len", ctypes.c_int), ("desc", ctypes.c_void_p),
    ]

assert ctypes.sizeof(Version) == 56
fd = os.open("/dev/dri/card0", os.O_RDWR | os.O_CLOEXEC)
try:
    info = os.fstat(fd)
    assert stat.S_ISCHR(info.st_mode), info
    assert (os.major(info.st_rdev), os.minor(info.st_rdev)) == (226, 0), info
    drm = ctypes.CDLL("libdrm.so.2")
    drm.drmGetVersion.argtypes = [ctypes.c_int]
    drm.drmGetVersion.restype = ctypes.POINTER(Version)
    drm.drmFreeVersion.argtypes = [ctypes.POINTER(Version)]
    drm.drmGetCap.argtypes = [ctypes.c_int, ctypes.c_uint64,
                              ctypes.POINTER(ctypes.c_uint64)]
    drm.drmGetCap.restype = ctypes.c_int
    ptr = drm.drmGetVersion(fd)
    assert ptr, "original libdrm could not identify DRM primary node"
    try:
        ver = ptr.contents
        # Fail safely on a corrupt ABI result instead of reading unbounded
        # guest memory and flooding the serial console.
        assert 0 < ver.name_len <= 128, ver.name_len
        assert ver.name, "libdrm did not return a driver name"
        driver = ctypes.string_at(ver.name, ver.name_len)
        print("DRM_DRIVER", driver.decode(), ver.major, ver.minor, flush=True)
        assert driver == b"virtio_gpu", driver
        assert (ver.major, ver.minor, ver.patchlevel) == (0, 1, 0)
    finally:
        drm.drmFreeVersion(ptr)
    dumb = ctypes.c_uint64(0xffffffffffffffff)
    assert drm.drmGetCap(fd, 1, ctypes.byref(dumb)) == 0
    assert dumb.value == 1, "GEM dumb-buffer capability must track real allocation"
    # Call original libdrm, not a home-grown ioctl implementation.
    drm.drmModeCreateDumbBuffer.argtypes = [
        ctypes.c_int, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32,
        ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32),
        ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_uint64)]
    drm.drmModeCreateDumbBuffer.restype = ctypes.c_int
    drm.drmModeMapDumbBuffer.argtypes = [
        ctypes.c_int, ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint64)]
    drm.drmModeMapDumbBuffer.restype = ctypes.c_int
    drm.drmModeDestroyDumbBuffer.argtypes = [ctypes.c_int, ctypes.c_uint32]
    drm.drmModeDestroyDumbBuffer.restype = ctypes.c_int
    import mmap
    def new_dumb(target, w, h):
        handle, pitch, size = (ctypes.c_uint32(), ctypes.c_uint32(),
                               ctypes.c_uint64())
        rc = drm.drmModeCreateDumbBuffer(
            target, w, h, 32, 0, ctypes.byref(handle),
            ctypes.byref(pitch), ctypes.byref(size))
        assert rc == 0, (rc, w, h)
        assert handle.value and pitch.value >= w * 4
        assert size.value == pitch.value * h
        offset = ctypes.c_uint64()
        assert drm.drmModeMapDumbBuffer(target, handle.value,
                                       ctypes.byref(offset)) == 0
        assert offset.value > 0 and offset.value % 4096 == 0
        return handle.value, size.value, offset.value
    first, length, offset = new_dumb(fd, 80, 48)
    second, length2, offset2 = new_dumb(fd, 32, 32)
    assert second != first and offset2 != offset
    other = os.open("/dev/dri/card0", os.O_RDWR | os.O_CLOEXEC)
    try:
        denied = ctypes.c_uint64()
        assert drm.drmModeMapDumbBuffer(other, first,
                                       ctypes.byref(denied)) != 0
        try:
            mmap.mmap(other, length, flags=mmap.MAP_SHARED,
                      prot=mmap.PROT_READ | mmap.PROT_WRITE, offset=offset)
            raise AssertionError("DRM mmap cookie leaked across DRM Files")
        except OSError:
            pass
    finally:
        os.close(other)
    dupfd = os.dup(fd)
    assert drm.drmModeMapDumbBuffer(dupfd, first,
                                   ctypes.byref(ctypes.c_uint64())) == 0
    os.close(dupfd)
    first_map = mmap.mmap(fd, length, flags=mmap.MAP_SHARED,
                          prot=mmap.PROT_READ | mmap.PROT_WRITE, offset=offset)
    second_map = mmap.mmap(fd, length2, flags=mmap.MAP_SHARED,
                           prot=mmap.PROT_READ | mmap.PROT_WRITE, offset=offset2)
    first_map[:8] = b"NaSeed01"
    second_map[:8] = b"NaSeed02"
    assert first_map[:8] == b"NaSeed01"
    assert second_map[:8] == b"NaSeed02"
    assert drm.drmModeDestroyDumbBuffer(fd, first) == 0
    assert drm.drmModeMapDumbBuffer(fd, first,
                                   ctypes.byref(ctypes.c_uint64())) != 0
    # VMA keeps the original physical pages after GEM handle destruction.
    first_map[8:16] = b"MapAlive"
    assert first_map[:16] == b"NaSeed01MapAlive"
    assert drm.drmModeDestroyDumbBuffer(fd, second) == 0
    # Duplicate mappings and the child have independent VMA references.
    pid = os.fork()
    if pid == 0:
        try:
            assert first_map[:16] == b"NaSeed01MapAlive"
            first_map[16:20] = b"fork"
            os._exit(0)
        except BaseException:
            os._exit(17)
    waited, status = os.waitpid(pid, 0)
    assert waited == pid and os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0
    assert first_map[16:20] == b"fork"
    # Close the FINAL DRM OpenFile while a VMA still owns the backing.
    # This must not release/recycle the mapped PMM pages.
    temporary_fd = os.open("/dev/dri/card0", os.O_RDWR | os.O_CLOEXEC)
    tmp_handle, tmp_size, tmp_offset = new_dumb(temporary_fd, 20, 10)
    temporary_map = mmap.mmap(temporary_fd, tmp_size,
                              flags=mmap.MAP_SHARED,
                              prot=mmap.PROT_READ | mmap.PROT_WRITE,
                              offset=tmp_offset)
    os.close(temporary_fd)
    temporary_map[:8] = b"FDClosed"
    assert temporary_map[:8] == b"FDClosed"
    temporary_map.close()
    # A small bounded churn checks handle/object slots are actually
    # reclaimed after GEM_CLOSE/DESTROY, not merely hidden from lookup.
    for _ in range(40):
        recycled, _, _ = new_dumb(fd, 16, 16)
        assert drm.drmModeDestroyDumbBuffer(fd, recycled) == 0
    print("DRM_GEM_FINAL_CLOSE_PASS", flush=True)
    print("DRM_GEM_MMAP_PASS", flush=True)
    first_map.close()
    second_map.close()
    # Public original libdrm KMS discovery. A real connected connector and
    # an actual mode must exist, not a hardcoded successful ioctl return.
    drm.drmModeGetResources.argtypes=[ctypes.c_int]
    drm.drmModeGetResources.restype=ctypes.c_void_p
    drm.drmModeFreeResources.argtypes=[ctypes.c_void_p]
    res=drm.drmModeGetResources(fd)
    assert res,"original libdrm could not enumerate DRM resources"
    drm.drmModeFreeResources(res)
    print("DRM_KMS_DISCOVERY_PASS",flush=True)
    print("DRM_LIBDRM_PASS", flush=True)
finally:
    os.close(fd)
DRM_PROBE

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
unix_msg_dontwait() {
    # The stock Alpine Python socket module exercises Linux recvmsg/sendmsg
    # on accepted *blocking* AF_UNIX sockets. MSG_DONTWAIT must affect only
    # this syscall, never the shared File's O_NONBLOCK setting.
    python3 - <<'SOCKETCHECK'
import errno, fcntl, os, socket
path='/tmp/naseed-unix-stream.sock'
listener=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM)
listener.bind(path)
listener.listen(1)
client=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM)
client.connect(path)
peer,_=listener.accept()
initial=fcntl.fcntl(peer.fileno(),fcntl.F_GETFL)
assert not initial&os.O_NONBLOCK
try:
    peer.recvmsg(64,0,socket.MSG_DONTWAIT)
except OSError as ex:
    assert ex.errno in (errno.EAGAIN,errno.EWOULDBLOCK),ex
else:
    raise AssertionError('empty AF_UNIX recvmsg(MSG_DONTWAIT) must return EAGAIN')
assert fcntl.fcntl(peer.fileno(),fcntl.F_GETFL)==initial
assert client.sendmsg([b'first'],[],socket.MSG_DONTWAIT)==5
assert peer.recvmsg(64)[0]==b'first'
assert peer.sendmsg([b'second'],[],socket.MSG_DONTWAIT)==6
assert client.recvmsg(64)[0]==b'second'
sent=0
full=False
for _ in range(20):
    try:
        n=client.sendmsg([b'x'*4096],[],socket.MSG_DONTWAIT)
        assert n>0
        sent+=n
    except OSError as ex:
        assert ex.errno in (errno.EAGAIN,errno.EWOULDBLOCK),ex
        full=True
        break
assert full and sent>=4096, ('AF_UNIX nonblocking backpressure',sent)
assert peer.recvmsg(4096)[0]==b'x'*4096
a,b=socket.socketpair()
try:
    b.recvmsg(1,0,socket.MSG_DONTWAIT)
except OSError as ex:
    assert ex.errno in (errno.EAGAIN,errno.EWOULDBLOCK),ex
else:
    raise AssertionError('socketpair recvmsg(MSG_DONTWAIT) must return EAGAIN')
# SCM_RIGHTS and MSG_CMSG_CLOEXEC use ordinary File ownership and
# Linux ancillary-data ABI; Wayland keymaps and wl_shm rely on both.
import array
rights_path='/root/acceptance-data/unix-rights.bin'
rights_fd=os.open(rights_path,os.O_CREAT|os.O_RDWR|os.O_TRUNC,0o600)
assert os.write(rights_fd,b'rights')==6
os.lseek(rights_fd,0,os.SEEK_SET)
assert a.sendmsg([b'R'],[(socket.SOL_SOCKET,socket.SCM_RIGHTS,
                           array.array('i',[rights_fd]).tobytes())],
                 socket.MSG_DONTWAIT)==1
payload,control,_,_=b.recvmsg(1,socket.CMSG_SPACE(4),
                              socket.MSG_CMSG_CLOEXEC|socket.MSG_DONTWAIT)
assert payload==b'R' and len(control)==1, (payload,control)
level,kind,transfer=control[0]
assert (level,kind)==(socket.SOL_SOCKET,socket.SCM_RIGHTS)
fds=array.array('i')
fds.frombytes(transfer)
assert len(fds)==1
received_fd=fds[0]
assert fcntl.fcntl(received_fd,fcntl.F_GETFD)&fcntl.FD_CLOEXEC
assert os.read(received_fd,6)==b'rights'
os.close(received_fd)
os.close(rights_fd)
a.close()
b.close()
peer.close()
client.close()
listener.close()
os.unlink(path)
print('original Alpine Python: AF_UNIX MSG_DONTWAIT RX/TX and shared File flags PASS')
SOCKETCHECK
}
mremap_shared_file() {
    # Genuine distribution GCC program: mremap must move/grow a live inode-
    # pinned MAP_SHARED VMA after its source fd closes. Check both writable
    # and read-only mappings, shared aliases, shrink and error semantics.
    cat > mremap-shared.c <<'MREMAPTEST'
#define _GNU_SOURCE
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
int main(void) {
    int fd=memfd_create("generic-shared-remap",MFD_CLOEXEC);
    if(fd<0 || ftruncate(fd,8192))return 1;
    volatile unsigned char *source=mmap(NULL,4096,PROT_READ|PROT_WRITE,
                                          MAP_SHARED,fd,0);
    if(source==MAP_FAILED)return 2;
    volatile unsigned char *alias=mmap(NULL,8192,PROT_READ,MAP_SHARED,fd,0);
    if(alias==MAP_FAILED)return 3;
    source[0]=0x61;source[4095]=0x62;
    close(fd); /* both VMAs, not a reopened integer fd, own the inode */
    volatile unsigned char *expanded=mremap((void *)source,4096,8192,
                                               MREMAP_MAYMOVE);
    if(expanded==MAP_FAILED){perror("mremap grow");return 4;}
    if(expanded[0]!=0x61 || expanded[4095]!=0x62)return 5;
    expanded[4096]=0x63;
    if(alias[0]!=0x61 || alias[4095]!=0x62 || alias[4096]!=0x63)
        return 6;
    volatile unsigned char *shrunk=mremap((void *)expanded,8192,4096,0);
    if(shrunk==MAP_FAILED || shrunk[0]!=0x61)return 7;
    if(munmap((void *)shrunk,4096) || munmap((void *)alias,8192))
        return 8;
    /* Private, demand-zero anonymous mappings can shrink without any
     * eager frame allocation or change to an unrelated VMA. */
    volatile unsigned char *anon=mmap(NULL,8192,PROT_READ|PROT_WRITE,
                                        MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(anon==MAP_FAILED)return 9;
    anon[0]=0x51;
    volatile unsigned char *small=mremap((void *)anon,8192,4096,0);
    if(small==MAP_FAILED || small[0]!=0x51)return 10;
    if(munmap((void *)small,4096))return 11;
    puts("Linux mremap real pinned MAP_SHARED inode, grow/move, alias and shrink PASS");
    return 0;
}
MREMAPTEST
    cc -O2 -Wall -Werror -o mremap-shared mremap-shared.c &&
    ./mremap-shared
}
sysv_shared_memory() {
    # Genuine Alpine GCC program: separate processes attach the same SysV
    # segment, publish changes through physical aliases, and retain deleted
    # segments until their last VMA owner is gone (MIT-SHM lifecycle).
    cat > sysv-shared.c <<'SHMTEST'
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
int main(void) {
    int id=shmget(IPC_PRIVATE,4096,IPC_CREAT|0600);
    if(id<0){perror("shmget");return 1;}
    volatile unsigned char *a=shmat(id,NULL,0);
    if(a==(void *)-1){perror("shmat");return 2;}
    a[0]=17;
    pid_t child=fork();
    if(child<0){perror("fork");return 3;}
    if(!child) {
        volatile unsigned char *b=shmat(id,NULL,0);
        if(b==(void *)-1 || b[0]!=17)_exit(4);
        b[0]=42;
        if(shmdt((const void *)b))_exit(5);
        _exit(0);
    }
    int status=0;
    if(waitpid(child,&status,0)!=child ||
       !WIFEXITED(status) || WEXITSTATUS(status))return 6;
    if(a[0]!=42)return 7;
    struct shmid_ds info;
    if(shmctl(id,IPC_STAT,&info) || info.shm_segsz!=4096 ||
       info.shm_nattch<1)return 8;
    if(shmctl(id,IPC_RMID,NULL)){perror("shmctl IPC_RMID");return 9;}
    /* Linux allows an attach by ID after IPC_RMID as long as an existing
     * mapping retains the segment (required by actual MIT-SHM clients). */
    volatile unsigned char *late=shmat(id,NULL,SHM_RDONLY);
    if(late==(void *)-1 || late[0]!=42)return 10;
    if(shmdt((const void *)late)||shmdt((const void *)a))return 11;
    puts("SysV shared memory cross-process attach/RMID/lifetime PASS");
    return 0;
}
SHMTEST
    cc -O2 -o sysv-shared sysv-shared.c &&
    ./sysv-shared
}
readonly_shared_mmap() {
    # The same inode frame must be visible through independent mappings,
    # observe ordinary writes and truncate, and never claim unsupported
    # PROT_WRITE MAP_SHARED is implemented.
    python3 - <<'MAPCHECK'
import errno, mmap, os
path = "/root/acceptance-data/readonly-shared.bin"
fd = os.open(path, os.O_RDWR | os.O_CREAT | os.O_TRUNC, 0o644)
assert os.write(fd, b"A" * 4096) == 4096
first = mmap.mmap(fd, 4096, flags=mmap.MAP_SHARED, prot=mmap.PROT_READ)
second = mmap.mmap(fd, 4096, flags=mmap.MAP_SHARED, prot=mmap.PROT_READ)
assert first[:4] == second[:4] == b"AAAA"
os.lseek(fd, 1, os.SEEK_SET)
assert os.write(fd, b"BC") == 2
assert first[:4] == second[:4] == b"ABCA", "shared read mappings lost write(2) coherence"
try:
    invalid = mmap.mmap(fd, 4096, flags=mmap.MAP_SHARED,
                        prot=mmap.PROT_READ | mmap.PROT_WRITE)
except OSError as ex:
    assert ex.errno == errno.ENODEV, ex
else:
    invalid.close()
    raise AssertionError("writable shared ext4 mapping must fail closed")
os.ftruncate(fd, 2)
assert first[2:4] == second[2:4] == b"\x00\x00"
second.close()
first.close()
os.close(fd)
print("MAP_SHARED ext4 readonly page identity, write visibility, EOF and write rejection PASS")
MAPCHECK
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
run unix-msg-dontwait unix_msg_dontwait
run readonly-shared-mmap readonly_shared_mmap
run mremap-shared-file mremap_shared_file
run sysv-shm sysv_shared_memory
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

def type_keys(text, pace=0):
    for ch in text:
        key={' ':'spc','/':'slash','.':'dot','>':'dot','\n':'ret'}.get(ch,ch)
        keys=['shift',key] if ch=='>' else [key]
        for k in keys:
            input_events([{'type':'key','data':{'key':{'type':'qcode','data':k},'down':True}}])
            if pace: time.sleep(pace)
        for k in reversed(keys):
            input_events([{'type':'key','data':{'key':{'type':'qcode','data':k},'down':False}}])
            if pace: time.sleep(pace)

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

def desktop_chord(keys, pace=0):
    for k in keys:
        key_event(k,True)
        if pace: time.sleep(pace)
    for k in reversed(keys):
        key_event(k,False)
        if pace: time.sleep(pace)
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
    guest.sendline('timeout 25 openbox --exit >/root/openbox-exit.log 2>&1; for i in $(seq 1 30); do if ! kill -0 "$wp" 2>/dev/null || grep -Eq "^State:[[:space:]]*Z" "/proc/$wp/status" 2>/dev/null; then break; fi; sleep 1; done; if ! kill -0 "$wp" 2>/dev/null || grep -Eq "^State:[[:space:]]*Z" "/proc/$wp/status" 2>/dev/null; then wait "$wp"; printf "WM_EXIT=%s\\n" "$?"; else echo WM_EXIT_FAILED; fi')
    if guest.expect([r'\r*\nWM_EXIT=0\r*\n',r'\r*\nWM_EXIT_FAILED\r*\n',r'\r*\nWM_EXIT=[0-9]+\r*\n'],timeout=80):
        guest.sendline('cat /root/openbox-exit.log; echo WM_EXIT_DIAG_END')
        guest.expect(r'\r*\nWM_EXIT_DIAG_END\r*\n',timeout=30)
        print('Openbox exit diagnostic:',guest.before)
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
    # Read both unmodified Alpine XKB paths in the real guest. The host
    # chroot check above must also pass, so ENODEV here is a guest VFS bug.
    guest.sendline("for p in /usr/share/xkeyboard-config-2/rules/evdev /usr/share/X11/xkb/rules/evdev; do python3 -c 'import sys; p=sys.argv[1]; f=open(p,\"rb\"); print(\"XKB_READ_OK\",p,f.read(16))' \"$p\" || :; done; echo XKB_DIAG_END")
    guest.expect(r'\r*\nXKB_DIAG_END\r*\n',timeout=60)
    diag=guest.before
    print('Guest XKB alias diagnostics:',diag)
    xkb_alias_ok='XKB_READ_OK /usr/share/xkeyboard-config-2/rules/evdev' in diag
    # A repeated session must never inherit prior physical-input markers.
    guest.sendline('rm -f /tmp/wlkey /tmp/wl6a /tmp/wl6b /tmp/wl6survive; echo WAYLAND_FRESH_INPUT_MARKERS')
    guest.expect(r'\r*\nWAYLAND_FRESH_INPUT_MARKERS\r*\n',timeout=30)
    guest.expect(r'# ')
    width,height,baseline=capture_scanout('wayland-before')
    # Explicitly select the official legacy data path for an A/B diagnostic,
    # not a hardcoded keyboard map or a patch to upstream Weston.
    guest.sendline('wlc=; wtp=; wtp2=; mkdir -p /run/user/0; chmod 700 /run/user/0; export XDG_RUNTIME_DIR=/run/user/0 XKB_CONFIG_ROOT=/usr/share/X11/xkb; unset WAYLAND_DISPLAY; weston --backend=x11 --renderer=pixman --no-config --idle-time=0 --socket=wayland-1 --width=600 --height=420 > /root/weston.log 2>&1 & wlp=$!')
    try:
        guest.sendline('for i in $(seq 1 45); do test -S /run/user/0/wayland-1 && break; kill -0 "$wlp" 2>/dev/null || break; sleep 1; done; if test -S /run/user/0/wayland-1; then echo WAYLAND_SOCKET_READY; else echo WAYLAND_SOCKET_MISSING; fi')
        socket_ok=guest.expect([r'\r*\nWAYLAND_SOCKET_READY\r*\n',r'\r*\nWAYLAND_SOCKET_MISSING\r*\n'],timeout=90)==0
        guest.sendline('timeout 15 env WAYLAND_DEBUG=client WAYLAND_DISPLAY=wayland-1 wayland-info >/root/wayland-info.log 2>&1; is=$?; if test "$is" = 0 && grep -q wl_compositor /root/wayland-info.log && grep -q xdg_wm_base /root/wayland-info.log; then echo WAYLAND_PROTOCOL_PASS; else printf "WAYLAND_INFO_STATUS=%s\\n" "$is"; echo WAYLAND_PROTOCOL_FAIL; fi')
        protocol_ok=guest.expect([r'\r*\nWAYLAND_PROTOCOL_PASS\r*\n',r'\r*\nWAYLAND_PROTOCOL_FAIL\r*\n'],timeout=60)==0
        if not protocol_ok:
            guest.sendline('ls -la /run/user/0; test -S /run/user/0/wayland-1 && echo WAYLAND_SOCKET_STILL_PRESENT; test -r /root/wayland-info.log && { wc -c /root/wayland-info.log; tail -100 /root/wayland-info.log; }; echo WAYLAND_DIAG_END')
            guest.expect(r'\r*\nWAYLAND_DIAG_END\r*\n',timeout=40)
            print('Wayland protocol diagnostics:',guest.before)
            # Trace the original distribution client (not a custom test
            # protocol) to preserve the actual Linux socket/epoll failures.
            guest.sendline('timeout 18 strace -f -s 96 -o /root/wayland-client.trace -e trace=socket,connect,accept4,fcntl,epoll_ctl,epoll_wait,epoll_pwait,poll,ppoll,sendmsg,recvmsg,sendto,recvfrom,read,write,close env WAYLAND_DISPLAY=wayland-1 WAYLAND_DEBUG=client wayland-info >/root/wayland-traced-info.log 2>&1; printf "WAYLAND_TRACE_STATUS=%s\\n" "$?"')
            guest.expect(r'\r*\nWAYLAND_TRACE_STATUS=([0-9]+)\r*\n',timeout=45)
            print('Original wayland-info syscall trace status:',guest.match.group(1))
            guest.sendline('tail -75 /root/wayland-client.trace /root/wayland-traced-info.log 2>/dev/null; echo WAYLAND_TRACE_END')
            guest.expect(r'\r*\nWAYLAND_TRACE_END\r*\n',timeout=40)
            print('Original wayland-info syscall trace:',guest.before)
        print(f'Wayland X11 compositor socket={socket_ok} client_registry={protocol_ok}')
        time.sleep(0.7)
        w,h,compositor=capture_scanout('wayland-weston')
        assert (w,h)==(width,height)
        changed=sum(baseline[i:i+3]!=compositor[i:i+3] for i in range(0,len(baseline),3))
        print(f'Weston nested X11 framebuffer_changed={changed}')
        assert changed>=500,'Weston X11 backend did not draw a physical window'
        # Locate the *actual nested compositor* client window. Clock ticks
        # elsewhere on the Xorg root window are not evidence of Wayland
        # drawing: an earlier full-screen diff produced that false positive.
        # Weston itself prints the X11 output window's server-assigned XID.
        # Query that real X11 object instead of assuming a window title.
        guest.sendline("wid=$(awk '/x11 output/ && /window id/ {print $NF}' /root/weston.log | tail -1); if [ -n \"$wid\" ] && eval \"$(xdotool getwindowgeometry --shell \"$wid\" 2>/dev/null)\"; then printf 'WESTON_GEOMETRY=%s,%s,%s,%s\\n' \"$X\" \"$Y\" \"$WIDTH\" \"$HEIGHT\"; else echo WESTON_GEOMETRY_FAIL; fi")
        if guest.expect([r'\r*\nWESTON_GEOMETRY=(-?[0-9]+),(-?[0-9]+),([0-9]+),([0-9]+)\r*\n',r'\r*\nWESTON_GEOMETRY_FAIL\r*\n'],timeout=30):
            raise RuntimeError('Weston X11 output XID is not mapped to a valid X Server window')
        wx,wy,ww,wh=(int(v) for v in guest.match.groups())
        x0=max(0,wx+25);x1=min(width,wx+ww-25)
        y0=max(0,wy+35);y1=min(height,wy+wh-25)
        assert x1-x0>=100 and y1-y0>=100,(wx,wy,ww,wh)
        print(f'Weston interior verification bounds: {x0},{y0} to {x1},{y1}')
        guest.sendline('env WAYLAND_DISPLAY=wayland-1 WAYLAND_DEBUG=client weston-simple-shm >/root/weston-simple-shm.log 2>&1 & wlc=$!; echo WAYLAND_CLIENT_STARTED')
        guest.expect(r'\r*\nWAYLAND_CLIENT_STARTED\r*\n',timeout=30)
        time.sleep(2.0)
        guest.sendline('if kill -0 "$wlc" 2>/dev/null && test -S /run/user/0/wayland-1; then echo WAYLAND_CLIENT_ALIVE; else echo WAYLAND_CLIENT_DEAD; fi')
        if guest.expect([r'\r*\nWAYLAND_CLIENT_ALIVE\r*\n',r'\r*\nWAYLAND_CLIENT_DEAD\r*\n'],timeout=30):
            raise RuntimeError('original weston-simple-shm client failed to stay connected')
        delta=0
        painted=0
        for attempt in range(5):
            w,h,client=capture_scanout('wayland-simple-shm')
            assert (w,h)==(width,height)
            delta=0;painted=0
            for y in range(y0,y1):
                for x in range(x0,x1):
                    p=(y*width+x)*3
                    before_pixel=compositor[p:p+3]
                    after_pixel=client[p:p+3]
                    if before_pixel!=after_pixel:delta+=1
                    if max(after_pixel)>=40:painted+=1
            if delta>=500 and painted>=500:break
            time.sleep(0.5)
        print(f'Wayland SHM interior: changed={delta}, painted={painted}')
        assert delta>=500 and painted>=500,(
            'original Wayland client did not paint the nested compositor content',
            (delta,painted))
        print('PASS: original Wayland SHM client renders through Weston -> Xorg -> VirtIO GPU')
        # Exercise a real Wayland PTY terminal and physical keyboard, not
        # only the simple-shm graphics demonstration.
        guest.sendline('env tag=alpha WAYLAND_DISPLAY=wayland-1 weston-terminal --shell=/bin/sh >/root/weston-terminal.log 2>&1 & wtp=$!; echo WAYLAND_TERMINAL_STARTED')
        guest.expect(r'\r*\nWAYLAND_TERMINAL_STARTED\r*\n',timeout=30)
        time.sleep(2)
        guest.sendline('if kill -0 "$wtp" 2>/dev/null && test -S /run/user/0/wayland-1; then echo WAYLAND_TERMINAL_ALIVE; else echo WAYLAND_TERMINAL_DEAD; fi')
        if guest.expect([r'\r*\nWAYLAND_TERMINAL_ALIVE\r*\n',r'\r*\nWAYLAND_TERMINAL_DEAD\r*\n'],timeout=30):
            guest.sendline('cat /root/weston-terminal.log; echo WAYLAND_TERMINAL_LOG_END')
            guest.expect(r'\r*\nWAYLAND_TERMINAL_LOG_END\r*\n',timeout=30)
            raise RuntimeError('original Weston terminal did not create a lasting Wayland session')
        # The compositor itself is the X11 window: QMP input must cross
        # VirtIO -> evdev -> Xorg -> Weston -> terminal's native Wayland seat.
        # Real, paced QMP input with the physical Weston focus path.
        # Every trial verifies the newly written PTY file, never a stale
        # marker from another compositor session.
        terminal_input=False
        for attempt in range(4):
            move_to(wx+ww//2,wy+wh//2)
            click()
            time.sleep(1.1)
            type_keys('echo 47 >/tmp/wlkey\n',pace=0.08)
            guest.sendline('if test "$(cat /tmp/wlkey 2>/dev/null)" = 47; then echo WAYLAND_TERMINAL_INPUT_PASS; else echo WAYLAND_TERMINAL_INPUT_WAIT; fi')
            terminal_input=guest.expect([r'\r*\nWAYLAND_TERMINAL_INPUT_PASS\r*\n',r'\r*\nWAYLAND_TERMINAL_INPUT_WAIT\r*\n'],timeout=30)==0
            guest.expect(r'# ')
            if terminal_input:
                break
        capture_scanout('wayland-terminal')
        if not terminal_input:
            guest.sendline('tail -60 /root/weston-terminal.log; echo WAYLAND_TERMINAL_LOG_END')
            guest.expect(r'\r*\nWAYLAND_TERMINAL_LOG_END\r*\n',timeout=30)
            print('Weston terminal input diagnostics:',guest.before)
            raise RuntimeError('QMP keyboard input did not reach the real Wayland terminal shell')
        print('PASS: original Weston terminal executes command through real QMP keyboard -> Xorg -> Wayland -> PTY')
        # Round 6: real, independent original Alpine Wayland terminal PTYs.
        # After verifying simple-shm rendering, retire that extra toplevel so
        # Super+Tab in upstream Weston switches only the two terminals.
        guest.sendline('kill "$wlc" 2>/dev/null; wait "$wlc" 2>/dev/null; wlc=; echo WAYLAND_SHM_STOPPED')
        guest.expect(r'\r*\nWAYLAND_SHM_STOPPED\r*\n',timeout=30)
        guest.sendline('env tag=beta WAYLAND_DISPLAY=wayland-1 weston-terminal --shell=/bin/sh >/root/weston-terminal-second.log 2>&1 & wtp2=$!; echo WAYLAND_SECOND_STARTED')
        guest.expect(r'\r*\nWAYLAND_SECOND_STARTED\r*\n',timeout=30)
        time.sleep(2)
        guest.sendline('if kill -0 "$wtp" 2>/dev/null && kill -0 "$wtp2" 2>/dev/null && test -S /run/user/0/wayland-1; then echo WAYLAND_BOTH_ALIVE; else echo WAYLAND_BOTH_DEAD; fi')
        if guest.expect([r'\r*\nWAYLAND_BOTH_ALIVE\r*\n',r'\r*\nWAYLAND_BOTH_DEAD\r*\n'],timeout=30):
            raise RuntimeError('two original Wayland terminal clients did not coexist')
        capture_scanout('wayland-two-terminals')
        # Commands are typed through physical QMP/VirtIO devices. Their
        # inherited tag identifies the actual PTY that received each command.
        move_to(wx+ww//2,wy+wh//2)
        click()
        time.sleep(1)
        type_keys('env >/tmp/wl6b\n',pace=0.08)
        guest.sendline('for i in $(seq 1 12); do grep -Fxq tag=beta /tmp/wl6b 2>/dev/null && break; sleep 1; done; if grep -Fxq tag=beta /tmp/wl6b 2>/dev/null; then echo WAYLAND_BETA_FOCUS_PASS; else echo WAYLAND_BETA_FOCUS_FAIL; fi')
        if guest.expect([r'\r*\nWAYLAND_BETA_FOCUS_PASS\r*\n',r'\r*\nWAYLAND_BETA_FOCUS_FAIL\r*\n'],timeout=45):
            raise RuntimeError('physical pointer failed to focus the second Wayland PTY')
        desktop_chord(['meta_l','tab'],pace=0.08)
        type_keys('env >/tmp/wl6a\n',pace=0.08)
        guest.sendline('for i in $(seq 1 12); do grep -Fxq tag=alpha /tmp/wl6a 2>/dev/null && break; sleep 1; done; if grep -Fxq tag=alpha /tmp/wl6a 2>/dev/null; then echo WAYLAND_ALPHA_SWITCH_PASS; else echo WAYLAND_ALPHA_SWITCH_FAIL; fi')
        if guest.expect([r'\r*\nWAYLAND_ALPHA_SWITCH_PASS\r*\n',r'\r*\nWAYLAND_ALPHA_SWITCH_FAIL\r*\n'],timeout=45):
            raise RuntimeError('Weston Super+Tab did not switch to the first original terminal PTY')
        desktop_chord(['meta_l','tab'],pace=0.08)
        type_keys('exit\n',pace=0.08)
        guest.sendline('for i in $(seq 1 15); do if ! kill -0 "$wtp2" 2>/dev/null || grep -Eq "^State:[[:space:]]*Z" "/proc/$wtp2/status" 2>/dev/null; then break; fi; sleep 1; done; if ! kill -0 "$wtp2" 2>/dev/null || grep -Eq "^State:[[:space:]]*Z" "/proc/$wtp2/status" 2>/dev/null; then wait "$wtp2"; printf "WAYLAND_SECOND_EXIT=%s\n" "$?"; wtp2=; else echo WAYLAND_SECOND_STUCK; fi')
        if guest.expect([r'\r*\nWAYLAND_SECOND_EXIT=0\r*\n',r'\r*\nWAYLAND_SECOND_STUCK\r*\n',r'\r*\nWAYLAND_SECOND_EXIT=[0-9]+\r*\n'],timeout=65):
            raise RuntimeError('second Weston PTY failed clean shell exit')
        move_to(wx+ww//2,wy+wh//2)
        click()
        time.sleep(0.6)
        type_keys('env >/tmp/wl6survive\n',pace=0.08)
        guest.sendline('for i in $(seq 1 12); do grep -Fxq tag=alpha /tmp/wl6survive 2>/dev/null && break; sleep 1; done; if kill -0 "$wtp" 2>/dev/null && kill -0 "$wlp" 2>/dev/null && grep -Fxq tag=alpha /tmp/wl6survive 2>/dev/null; then echo WAYLAND_SURVIVOR_PASS; else echo WAYLAND_SURVIVOR_FAIL; fi')
        if guest.expect([r'\r*\nWAYLAND_SURVIVOR_PASS\r*\n',r'\r*\nWAYLAND_SURVIVOR_FAIL\r*\n'],timeout=45):
            raise RuntimeError('surviving Wayland PTY failed after other client exit')
        capture_scanout('wayland-survivor')
        print('PASS: two original Wayland PTYs, physical mouse focus, Super+Tab isolation, clean close and surviving terminal')
        assert protocol_ok,'Wayland client registry lacks wl_compositor or xdg_wm_base'
        assert xkb_alias_ok,'canonical xkeyboard-config alias is unreadable in NaSeed guest, even if explicit XKB_CONFIG_ROOT worked'
        return True
    finally:
        # Run the unmodified compositor directly, without a ptrace parent
        # retaining its process tree. Check real process termination; cap
        # teardown rather than letting a stuck waiter obscure the pass.
        guest.sendline('if [ -n "$wtp2" ]; then kill "$wtp2" 2>/dev/null; fi; if [ -n "$wtp" ]; then kill "$wtp" 2>/dev/null; fi; if [ -n "$wlc" ]; then kill "$wlc" 2>/dev/null; fi; kill "$wlp" 2>/dev/null; for i in $(seq 1 15); do if ! kill -0 "$wlp" 2>/dev/null || grep -Eq "^State:[[:space:]]*Z" "/proc/$wlp/status" 2>/dev/null; then break; fi; sleep 1; done; if ! kill -0 "$wlp" 2>/dev/null || grep -Eq "^State:[[:space:]]*Z" "/proc/$wlp/status" 2>/dev/null; then wait "$wlp"; printf "WESTON_STOP=%s\\n" "$?"; else echo WESTON_STOP_FAILED; kill -9 "$wlp" 2>/dev/null; fi; for f in /root/weston.log /root/wayland-info.log /root/weston-simple-shm.log /root/weston-terminal.log /root/weston-terminal-second.log; do echo "=== $f ==="; tail -70 "$f" 2>/dev/null; done; echo WAYLAND_LOG_END')
        guest.expect(r'\r*\nWAYLAND_LOG_END\r*\n',timeout=100)
        teardown=guest.before
        print(teardown)
        assert 'WESTON_STOP=0' in teardown,'Weston did not exit cleanly after SIGTERM'
        # Round 7: reap every original child and require the old socket gone
        # before the next compositor can bind the same name.
        guest.sendline('for pid in "$wtp" "$wtp2" "$wlc"; do if [ -n "$pid" ]; then kill "$pid" 2>/dev/null; wait "$pid" 2>/dev/null; fi; done; for i in $(seq 1 12); do test ! -S /run/user/0/wayland-1 && break; sleep 1; done; if test ! -S /run/user/0/wayland-1 && ! kill -0 "$wlp" 2>/dev/null; then echo WESTON_LIFECYCLE_PASS; else echo WESTON_LIFECYCLE_FAIL; fi')
        if guest.expect([r'\r*\nWESTON_LIFECYCLE_PASS\r*\n',r'\r*\nWESTON_LIFECYCLE_FAIL\r*\n'],timeout=45):
            raise RuntimeError('Weston compositor process or Unix socket leaked on exit')
        guest.expect(r'# ')

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
        guest.sendline('python3 /root/drm-probe.py; printf "DRM_PROBE_STATUS=%s\\n" "$?"')
        guest.expect(r'\r*\nDRM_PROBE_STATUS=([0-9]+)\r*\n',timeout=90)
        if guest.match.group(1)!='0':
            raise RuntimeError('original Alpine libdrm failed to identify VirtIO GPU')
        guest.expect(r'# ')
        # Original upstream libdrm modetest, not a local ioctl driver-test
        # binary: enumerate the virtual connector and request a real KMS
        # framebuffer modeset. Compare QEMU scanout, not ioctl return values.
        guest.sendline('modetest -M virtio_gpu -c -e -p >/root/modetest-discovery.log 2>&1; printf "MODETEST_ENUM_STATUS=%s\n" "$?"')
        guest.expect(r'\r*\nMODETEST_ENUM_STATUS=([0-9]+)\r*\n',timeout=60)
        if guest.match.group(1)!='0':
            guest.sendline('cat /root/modetest-discovery.log; echo MODETEST_LOG_END')
            guest.expect(r'\r*\nMODETEST_LOG_END\r*\n',timeout=30)
            raise RuntimeError('original Alpine modetest could not enumerate KMS')
        guest.expect(r'# ')
        width,height,before_kms=capture_scanout('kms-before')
        guest.sendline('modetest -M virtio_gpu -s 44@42:800x600 </dev/null >/root/modetest-scanout.log 2>&1; printf "MODETEST_SET_STATUS=%s\n" "$?"')
        guest.expect(r'\r*\nMODETEST_SET_STATUS=([0-9]+)\r*\n',timeout=120)
        if guest.match.group(1)!='0':
            guest.sendline('cat /root/modetest-scanout.log; echo MODETEST_LOG_END')
            guest.expect(r'\r*\nMODETEST_LOG_END\r*\n',timeout=30)
            raise RuntimeError('original Alpine modetest could not set KMS scanout')
        guest.expect(r'# ')
        time.sleep(1)
        dw,dh,after_kms=capture_scanout('kms-modetest')
        assert (width,height)==(dw,dh)==(800,600)
        changed=sum(before_kms[i:i+3]!=after_kms[i:i+3]
                    for i in range(0,len(before_kms),3))
        print(f'DRM KMS real scanout changed pixels: {changed}/{dw*dh}')
        assert changed>80000,'original modetest did not change QEMU VirtIO GPU scanout'
        print('PASS: original Alpine modetest -s -> DRM/KMS -> GEM -> VirtIO GPU changed QEMU pixels')
        # A live input pipe holds unmodified modetest in its DRM event loop.
        # Its page_flip_handler queues the next framebuffer from the real
        # DRM_EVENT_FLIP_COMPLETE records received through libdrm/read/select.
        guest.sendline('(sleep 25 | modetest -M virtio_gpu -s 44@42:800x600 -v) >/root/modetest-flip.log 2>&1 & flip_pid=$!; echo MODETEST_FLIP_STARTED')
        guest.expect(r'\r*\nMODETEST_FLIP_STARTED\r*\n',timeout=30)
        # A single screenshot can land on the exact same phase of two
        # alternating framebuffers. Sample multiple distinct intervals,
        # keeping the first actually different screenshot as evidence.
        moved=0
        sw=sh=0
        for interval in (0.23,0.37,0.61,0.43,0.79,0.31,1.07,
                         0.47,0.91,0.29,0.73,0.53):
            time.sleep(interval)
            sw,sh,flip_frame=capture_scanout('kms-pageflip')
            assert (sw,sh)==(800,600)
            moved=sum(flip_frame[i:i+3]!=after_kms[i:i+3]
                      for i in range(0,len(flip_frame),3))
            if moved>10000:
                break
        print(f'DRM page-flip real QEMU pixels changed: {moved}/{sw*sh}')
        guest.sendline('wait "$flip_pid"; printf "MODETEST_FLIP_STATUS=%s\n" "$?"')
        guest.expect(r'\r*\nMODETEST_FLIP_STATUS=([0-9]+)\r*\n',timeout=100)
        if guest.match.group(1)!='0':
            guest.sendline('cat /root/modetest-flip.log; echo PAGEFLIP_LOG_END')
            guest.expect(r'\r*\nPAGEFLIP_LOG_END\r*\n',timeout=30)
            raise RuntimeError('original Alpine modetest page flipping failed')
        guest.expect(r'# ')
        guest.sendline('if grep -Eq "freq: [1-9][0-9]*" /root/modetest-flip.log; then echo MODETEST_FLIP_EVENTS_PASS; else echo MODETEST_FLIP_EVENTS_FAIL; fi')
        if guest.expect([r'\r*\nMODETEST_FLIP_EVENTS_PASS\r*\n',
                         r'\r*\nMODETEST_FLIP_EVENTS_FAIL\r*\n'],timeout=40)!=0:
            guest.sendline('cat /root/modetest-flip.log; echo PAGEFLIP_LOG_END')
            guest.expect(r'\r*\nPAGEFLIP_LOG_END\r*\n',timeout=30)
            raise RuntimeError('original modetest did not receive 60 completed DRM flips')
        assert moved>10000, 'original page flips did not change real VirtIO GPU pixels'
        print('PASS: original Alpine modetest -v receives 60+ DRM completion events and changes real QEMU scanout')
        guest.expect(r'# ')
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
        # Let original Alpine eudev derive input identity from the actual
        # kernel's evdev/sysfs capabilities. Do not pre-write synthetic
        # ID_INPUT properties or assign keyboard/mouse types in the test.
        guest.sendline('mkdir -p /run/udev; : >/root/udev-input.log; for input in /sys/class/input/event*; do test -e "$input" || continue; udevadm test "$input" >>/root/udev-input.log 2>&1; done; if udevadm info --query=property --name=/dev/input/event0 >/root/udev-event0.log 2>&1; then echo UDEV_INPUT_INSPECTED; else echo UDEV_INPUT_DIAGNOSTIC; fi; grep -E "^(ID_INPUT|SUBSYSTEM|DEVNAME)=" /root/udev-event0.log 2>/dev/null; echo UDEV_INPUT_DIAG_END')
        guest.expect(r'\r*\nUDEV_INPUT_DIAG_END\r*\n',timeout=120)
        guest.expect(r'# ')
        # Probe the real libinput/udev integration independently from Weston.
        # Keep logs even if libinput enumerates zero devices; the native seat
        # and physical keyboard assertions below remain mandatory.
        guest.sendline('timeout -s KILL 12 libinput list-devices >/root/libinput-devices.log 2>&1; printf "LIBINPUT_LIST_STATUS=%s\\n" "$?"; grep -E "^(Device:|Kernel:|Seat:|Capabilities:)" /root/libinput-devices.log 2>/dev/null || :; echo LIBINPUT_LIST_END')
        guest.expect(r'\r*\nLIBINPUT_LIST_END\r*\n',timeout=60)
        guest.expect(r'# ')
        # The original Weston DRM backend must paint and receive native
        # Wayland clients on VirtIO hardware WITHOUT an Xorg process.
        guest.sendline('mkdir -p /run/user/0; chmod 700 /run/user/0; export XDG_RUNTIME_DIR=/run/user/0 XKB_CONFIG_ROOT=/usr/share/X11/xkb; unset DISPLAY WAYLAND_DISPLAY; SEATD_VTBOUND=0 seatd -g root -l debug >/root/seatd.log 2>&1 & seatd_pid=$!; sleep 1; LIBSEAT_BACKEND=seatd strace -f -s 96 -o /root/weston-drm.strace -e trace=socket,bind,setsockopt,getsockopt,epoll_ctl,epoll_wait,poll,open,openat,newfstatat,readlink,recvmsg,sendmsg weston --backend=drm --renderer=pixman --drm-device=card0 --no-config --continue-without-input --idle-time=0 --socket=wayland-drm >/root/weston-drm.log 2>&1 & native_weston_pid=$!; echo WESTON_DRM_LAUNCHED')
        guest.expect(r'\r*\nWESTON_DRM_LAUNCHED\r*\n',timeout=30)
        guest.expect(r'# ')
        native_client_pid=''
        native_terminal_pid=''
        native_terminal2_pid=''
        native_success=False
        try:
            guest.sendline('for i in $(seq 1 40); do test -S /run/user/0/wayland-drm && break; kill -0 "$native_weston_pid" 2>/dev/null || break; sleep 1; done; if test -S /run/user/0/wayland-drm && kill -0 "$native_weston_pid" 2>/dev/null; then echo WESTON_DRM_SOCKET_READY; else echo WESTON_DRM_SOCKET_MISSING; fi')
            if guest.expect([r'\r*\nWESTON_DRM_SOCKET_READY\r*\n',
                             r'\r*\nWESTON_DRM_SOCKET_MISSING\r*\n'],timeout=95):
                raise RuntimeError('original Weston DRM did not publish native Wayland socket')
            guest.expect(r'# ')
            guest.sendline('timeout 18 env WAYLAND_DISPLAY=wayland-drm wayland-info >/root/weston-drm-info.log 2>&1; info_status=$?; if test "$info_status" = 0 && grep -q wl_compositor /root/weston-drm-info.log && grep -q xdg_wm_base /root/weston-drm-info.log; then echo WESTON_DRM_PROTOCOL_PASS; else printf "WESTON_DRM_PROTOCOL_FAIL=%s\n" "$info_status"; fi')
            if guest.expect([r'\r*\nWESTON_DRM_PROTOCOL_PASS\r*\n',
                             r'\r*\nWESTON_DRM_PROTOCOL_FAIL=[0-9]+\r*\n'],timeout=65):
                raise RuntimeError('original wayland-info cannot connect directly to Weston DRM')
            guest.expect(r'# ')
            nw,nh,native_before=capture_scanout('native-weston-before')
            assert (nw,nh)==(800,600)
            guest.sendline('env WAYLAND_DISPLAY=wayland-drm weston-simple-shm >/root/weston-drm-shm.log 2>&1 & native_client_pid=$!; echo WESTON_DRM_CLIENT_STARTED')
            guest.expect(r'\r*\nWESTON_DRM_CLIENT_STARTED\r*\n',timeout=30)
            guest.expect(r'# ')
            time.sleep(2)
            guest.sendline('if kill -0 "$native_client_pid" 2>/dev/null; then echo WESTON_DRM_CLIENT_ALIVE; else echo WESTON_DRM_CLIENT_DEAD; fi')
            if guest.expect([r'\r*\nWESTON_DRM_CLIENT_ALIVE\r*\n',
                             r'\r*\nWESTON_DRM_CLIENT_DEAD\r*\n'],timeout=30):
                raise RuntimeError('original weston-simple-shm exited under native DRM')
            guest.expect(r'# ')
            changed=0
            # The real compositor repaints asynchronously following GPU
            # transfer/flush; give it the same QEMU scheduling tolerance as
            # the existing native modetest flip sampling, without weakening
            # the visible-pixel assertion.
            for _ in range(25):
                nw,nh,native_after=capture_scanout('native-weston-simple-shm')
                assert (nw,nh)==(800,600)
                changed=sum(native_before[i:i+3]!=native_after[i:i+3]
                            for i in range(0,len(native_after),3))
                if changed>=1000:break
                time.sleep(0.65)
            print(f'Native DRM Weston/SHM QEMU pixels changed: {changed}/480000')
            assert changed>=1000,'original Weston DRM client did not paint actual QEMU scanout'
            print('PASS: original Weston DRM + Pixman + Wayland SHM renders directly through VirtIO GPU, without Xorg')
            # SHM rendering is already physically verified. Remove this
            # demonstration toplevel *before* launching the first terminal:
            # overlapping windows must not steal the physical seat click.
            guest.sendline('kill "$native_client_pid" 2>/dev/null; wait "$native_client_pid" 2>/dev/null; native_client_pid=; echo WESTON_DRM_SHM_REAPED')
            guest.expect(r'\r*\nWESTON_DRM_SHM_REAPED\r*\n',timeout=35)
            guest.expect(r'# ')
            guest.sendline('env tag=alpha WAYLAND_DISPLAY=wayland-drm weston-terminal --shell=/bin/sh >/root/weston-drm-terminal.log 2>&1 & native_terminal_pid=$!; echo WESTON_DRM_TERMINAL_STARTED')
            guest.expect(r'\r*\nWESTON_DRM_TERMINAL_STARTED\r*\n',timeout=30)
            guest.expect(r'# ')
            time.sleep(2)
            guest.sendline('if kill -0 "$native_terminal_pid" 2>/dev/null; then echo WESTON_DRM_TERMINAL_ALIVE; else echo WESTON_DRM_TERMINAL_DEAD; fi')
            if guest.expect([r'\r*\nWESTON_DRM_TERMINAL_ALIVE\r*\n',
                             r'\r*\nWESTON_DRM_TERMINAL_DEAD\r*\n'],timeout=30):
                raise RuntimeError('original weston-terminal did not open under native DRM')
            guest.expect(r'# ')
            # Weston intentionally does not focus a new toplevel merely
            # because it is mapped. Exercise the real QMP -> VirtIO mouse
            # path and click the terminal before sending keyboard events.
            # QMP relative motion through the genuine VirtIO pointer does
            # not require an X11 server or xdotool pointer query. Three
            # bounded steps reach the interior of the mapped terminal from
            # either the display origin or the compositing seat's center.
            for _ in range(3):
                input_events([{'type':'rel','data':{'axis':'x','value':50}},
                              {'type':'rel','data':{'axis':'y','value':50}}])
            click()
            time.sleep(1)
            capture_scanout('native-weston-terminal-focused')
            # QMP mouse button events and native libinput focus delivery are
            # asynchronous. Reassert the *same physical seat path* with a
            # bounded click/type/readback cycle rather than racing one click.
            native_focused=False
            for attempt in range(4):
                click()
                time.sleep(1.2)
                type_keys('echo 57 >/tmp/nativewlkey\n',pace=0.08)
                guest.sendline('if test "$(cat /tmp/nativewlkey 2>/dev/null)" = 57; then echo WESTON_DRM_KEYBOARD_PASS; else echo WESTON_DRM_KEYBOARD_WAIT; fi')
                if guest.expect([r'\r*\nWESTON_DRM_KEYBOARD_PASS\r*\n',
                                 r'\r*\nWESTON_DRM_KEYBOARD_WAIT\r*\n'],timeout=30)==0:
                    native_focused=True
                guest.expect(r'# ')
                if native_focused:
                    break
            if not native_focused:
                capture_scanout('native-weston-terminal-focused')
                raise RuntimeError('native QMP VirtIO evdev keyboard did not reach Weston terminal PTY after four genuine focus attempts')
            capture_scanout('native-weston-terminal')
            print('PASS: native QMP keyboard -> VirtIO evdev -> Weston DRM seat -> original terminal PTY')
            # Round 7: two original Alpine DRM/Wayland PTYs without Xorg.
            guest.sendline('env tag=beta WAYLAND_DISPLAY=wayland-drm weston-terminal --shell=/bin/sh >/root/weston-drm-terminal-second.log 2>&1 & native_terminal2_pid=$!; echo WESTON_DRM_SECOND_STARTED')
            guest.expect(r'\r*\nWESTON_DRM_SECOND_STARTED\r*\n',timeout=30)
            guest.expect(r'# ')
            time.sleep(2)
            guest.sendline('if kill -0 "$native_terminal_pid" 2>/dev/null && kill -0 "$native_terminal2_pid" 2>/dev/null && kill -0 "$native_weston_pid" 2>/dev/null; then echo WESTON_DRM_BOTH_ALIVE; else echo WESTON_DRM_BOTH_DEAD; fi')
            if guest.expect([r'\r*\nWESTON_DRM_BOTH_ALIVE\r*\n',r'\r*\nWESTON_DRM_BOTH_DEAD\r*\n'],timeout=30):
                raise RuntimeError('native DRM two original terminal clients did not coexist')
            guest.expect(r'# ')
            capture_scanout('native-weston-two-terminals')
            # Weston does not auto-focus a freshly mapped toplevel: the
            # currently active alpha must switch to beta via its real seat.
            beta_focused=False
            for focus_attempt in range(4):
                desktop_chord(['meta_l','tab'],pace=0.08)
                capture_scanout('native-weston-beta-focused')
                type_keys('env >/tmp/nativewl7beta\n',pace=0.08)
                guest.sendline('for i in $(seq 1 8); do test -s /tmp/nativewl7beta && break; sleep 1; done; if grep -Fxq tag=beta /tmp/nativewl7beta 2>/dev/null; then echo WESTON_DRM_BETA_PASS; else echo WESTON_DRM_BETA_WAIT; fi')
                beta_focused=guest.expect([r'\r*\nWESTON_DRM_BETA_PASS\r*\n',r'\r*\nWESTON_DRM_BETA_WAIT\r*\n'],timeout=40)==0
                guest.expect(r'# ')
                if beta_focused:
                    break
            if not beta_focused:
                guest.sendline('if test -f /tmp/nativewl7beta; then grep "^tag=" /tmp/nativewl7beta; else echo NATIVE_BETA_NO_INPUT_FILE; fi; echo NATIVE_BETA_DIAG_END')
                guest.expect(r'\r*\nNATIVE_BETA_DIAG_END\r*\n',timeout=30)
                print('Native Wayland beta focus evidence:',guest.before)
                raise RuntimeError('four real Weston Super+Tab attempts did not reach second independent PTY')
            desktop_chord(['meta_l','tab'],pace=0.08)
            type_keys('env >/tmp/nativewl7alpha\n',pace=0.08)
            guest.sendline('for i in $(seq 1 15); do grep -Fxq tag=alpha /tmp/nativewl7alpha 2>/dev/null && break; sleep 1; done; if grep -Fxq tag=alpha /tmp/nativewl7alpha 2>/dev/null; then echo WESTON_DRM_ALPHA_PASS; else echo WESTON_DRM_ALPHA_FAIL; fi')
            if guest.expect([r'\r*\nWESTON_DRM_ALPHA_PASS\r*\n',r'\r*\nWESTON_DRM_ALPHA_FAIL\r*\n'],timeout=55):
                raise RuntimeError('native Weston Super+Tab did not reach first PTY')
            guest.expect(r'# ')
            desktop_chord(['meta_l','tab'],pace=0.08)
            type_keys('exit\n',pace=0.08)
            guest.sendline('for i in $(seq 1 15); do if ! kill -0 "$native_terminal2_pid" 2>/dev/null || grep -Eq "^State:[[:space:]]*Z" "/proc/$native_terminal2_pid/status" 2>/dev/null; then break; fi; sleep 1; done; if ! kill -0 "$native_terminal2_pid" 2>/dev/null || grep -Eq "^State:[[:space:]]*Z" "/proc/$native_terminal2_pid/status" 2>/dev/null; then wait "$native_terminal2_pid"; printf "WESTON_DRM_SECOND_EXIT=%s\n" "$?"; native_terminal2_pid=; else echo WESTON_DRM_SECOND_STUCK; fi')
            if guest.expect([r'\r*\nWESTON_DRM_SECOND_EXIT=0\r*\n',r'\r*\nWESTON_DRM_SECOND_STUCK\r*\n',r'\r*\nWESTON_DRM_SECOND_EXIT=[0-9]+\r*\n'],timeout=65):
                raise RuntimeError('native DRM second terminal did not exit cleanly')
            guest.expect(r'# ')
            click()
            time.sleep(1)
            type_keys('env >/tmp/nativewl7survive\n',pace=0.08)
            guest.sendline('for i in $(seq 1 15); do grep -Fxq tag=alpha /tmp/nativewl7survive 2>/dev/null && break; sleep 1; done; if kill -0 "$native_terminal_pid" 2>/dev/null && grep -Fxq tag=alpha /tmp/nativewl7survive 2>/dev/null; then echo WESTON_DRM_SURVIVOR_PASS; else echo WESTON_DRM_SURVIVOR_FAIL; fi')
            if guest.expect([r'\r*\nWESTON_DRM_SURVIVOR_PASS\r*\n',r'\r*\nWESTON_DRM_SURVIVOR_FAIL\r*\n'],timeout=55):
                raise RuntimeError('native DRM first PTY broke after second terminal exit')
            guest.expect(r'# ')
            capture_scanout('native-weston-survivor')
            print('PASS: original DRM Weston dual terminals, physical keyboard focus switching, independent PTYs, exit and survivor')
            # Run actual unmodified GTK3 Alpine programs on the native
            # Wayland/DRM seat, not a nested Xorg-backed Weston session.
            # All dependencies were installed in the HOST-built Alpine
            # rootfs. GDK_BACKEND=wayland forbids X11 fallback.
            guest.sendline('kill "$native_terminal_pid" 2>/dev/null; wait "$native_terminal_pid" 2>/dev/null; native_terminal_pid=; echo NATIVE_GTK_SEAT_CLEARED')
            guest.expect(r'\r*\nNATIVE_GTK_SEAT_CLEARED\r*\n',timeout=35)
            guest.expect(r'# ')
            guest.sendline('dbus-daemon --session --fork --print-address=1 --print-pid=1 >/root/native-gtk-bus.log 2>&1; rc=$?; if test "$rc" = 0; then export DBUS_SESSION_BUS_ADDRESS="$(head -1 /root/native-gtk-bus.log)"; native_gtk_bus_pid="$(tail -1 /root/native-gtk-bus.log)"; echo NATIVE_GTK_BUS_READY; else echo NATIVE_GTK_BUS_FAILED; fi')
            if guest.expect([r'\r*\nNATIVE_GTK_BUS_READY\r*\n',r'\r*\nNATIVE_GTK_BUS_FAILED\r*\n'],timeout=60):
                raise RuntimeError('native Wayland GTK D-Bus session did not start')
            guest.expect(r'# ')
            def native_gtk_window(name,command):
                bw,bh,before=capture_scanout('native-gtk-'+name+'-before')
                guest.sendline(f'env GDK_BACKEND=wayland WAYLAND_DISPLAY=wayland-drm {command} >/root/native-gtk-{name}.log 2>&1 & native_gtk_pid=$!; echo NATIVE_GTK_STARTED')
                guest.expect(r'\r*\nNATIVE_GTK_STARTED\r*\n',timeout=30)
                guest.expect(r'# ')
                delta=0
                try:
                    for attempt in range(12):
                        time.sleep(0.75)
                        aw,ah,after=capture_scanout('native-gtk-'+name)
                        assert (aw,ah)==(bw,bh)
                        delta=sum(before[i:i+3]!=after[i:i+3] for i in range(0,len(after),3))
                        if delta>=1000: break
                    guest.sendline('if kill -0 "$native_gtk_pid" 2>/dev/null && ! grep -Eq "^State:[[:space:]]*Z" "/proc/$native_gtk_pid/status" 2>/dev/null; then echo NATIVE_GTK_CLIENT_LIVE; else echo NATIVE_GTK_CLIENT_DEAD; fi')
                    live=guest.expect([r'\r*\nNATIVE_GTK_CLIENT_LIVE\r*\n',r'\r*\nNATIVE_GTK_CLIENT_DEAD\r*\n'],timeout=30)==0
                    guest.expect(r'# ')
                    if not live or delta<1000:
                        guest.sendline(f'cat /root/native-gtk-{name}.log; echo NATIVE_GTK_ERROR_LOG_END')
                        guest.expect(r'\r*\nNATIVE_GTK_ERROR_LOG_END\r*\n',timeout=30)
                        print(f'Native GTK {name} diagnostics:',guest.before)
                        raise RuntimeError(f'original Alpine GTK {name} failed real Wayland surface rendering: live={live} pixels={delta}')
                    print(f'PASS: original Alpine GTK {name} on direct Weston DRM, GDK Wayland, QEMU changed pixels={delta}')
                finally:
                    guest.sendline('kill "$native_gtk_pid" 2>/dev/null; wait "$native_gtk_pid" 2>/dev/null; native_gtk_pid=; echo NATIVE_GTK_CLIENT_REAPED')
                    guest.expect(r'\r*\nNATIVE_GTK_CLIENT_REAPED\r*\n',timeout=45)
                    guest.expect(r'# ')
            native_gtk_window('mousepad','mousepad /root/desktop/documents/note.txt')
            native_gtk_window('thunar','thunar /root/desktop')
            guest.sendline('kill "$native_gtk_bus_pid" 2>/dev/null; native_gtk_bus_pid=; echo NATIVE_GTK_BUS_CLOSED')
            guest.expect(r'\r*\nNATIVE_GTK_BUS_CLOSED\r*\n',timeout=30)
            guest.expect(r'# ')
            native_success=True
        finally:
            guest.sendline('kill "$native_gtk_pid" "$native_gtk_bus_pid" "$native_terminal_pid" "$native_terminal2_pid" "$native_client_pid" 2>/dev/null; weston_pids="$(pidof weston 2>/dev/null)"; if test -n "$weston_pids"; then kill $weston_pids 2>/dev/null; fi; for i in $(seq 1 30); do if ! kill -0 "$native_weston_pid" 2>/dev/null || grep -Eq "^State:[[:space:]]*Z" "/proc/$native_weston_pid/status" 2>/dev/null; then break; fi; sleep 1; done; if ! kill -0 "$native_weston_pid" 2>/dev/null || grep -Eq "^State:[[:space:]]*Z" "/proc/$native_weston_pid/status" 2>/dev/null; then wait "$native_weston_pid"; printf "WESTON_DRM_STOP=%s\n" "$?"; else echo WESTON_DRM_STOP_FAILED; kill -9 "$native_weston_pid" 2>/dev/null; fi; kill "$seatd_pid" 2>/dev/null; wait "$seatd_pid" 2>/dev/null; for f in /root/weston-drm.log /root/seatd.log /root/weston-drm-info.log /root/weston-drm-shm.log /root/weston-drm-terminal.log /root/weston-drm-terminal-second.log /root/udev-input.log /root/udev-event0.log /root/libinput-devices.log; do echo "=== $f ==="; tail -80 "$f" 2>/dev/null; done; echo === WESTON_NATIVE_SOCKET_TRACE ===; grep -E "AF_NETLINK|NETLINK_KOBJECT|SO_ATTACH|SO_PASSCRED|epoll_ctl|EOPNOTSUPP|ENOSYS|ENOTTY|EPERM|EINVAL" /root/weston-drm.strace | tail -80; echo WESTON_DRM_DIAG_END')
            guest.expect(r'\r*\nWESTON_DRM_DIAG_END\r*\n',timeout=150)
            native_teardown=guest.before
            print('Original native Weston DRM diagnostic:',native_teardown[-16000:])
            if native_success:
                assert 'WESTON_DRM_STOP=0' in native_teardown,'native Weston DRM did not exit cleanly'
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
                    # Retain the full Openbox/GTK acceptance, but isolate its
                    # occasional exit delay from the separate Wayland verdict.
                    # Neither failure may be converted into a global PASS.
                    try:
                        wm_ok=verify_wm()
                    except (OSError,ValueError,AssertionError,RuntimeError,pexpect.TIMEOUT) as error:
                        print(f'Openbox/GTK regression failed: {error}')
                    try:
                        wayland_ok=verify_wayland()
                        if wayland_ok:
                            # Fail closed if session two raises before return.
                            wayland_ok=False
                            wayland_ok=verify_wayland()
                            print('PASS: nested Weston full two-terminal session passed twice with the same socket name')
                    except (OSError,ValueError,AssertionError,RuntimeError,pexpect.TIMEOUT) as error:
                        print(f'Wayland protocol/client acceptance failed: {error}')
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
# Preserve original guest syscall evidence even when the Wayland gate fails.
debugfs -R 'cat /root/wayland-client.trace' "$work/partition.ext4" > "$work/wayland-client.trace" 2>/dev/null || :
debugfs -R 'cat /root/wayland-traced-info.log' "$work/partition.ext4" > "$work/wayland-traced-info.log" 2>/dev/null || :
# Check the independent application results only after shutdown and fsck.
tr -d '\r' < "$work/init.log" | grep -Fx '[APP DONE status=0]' >/dev/null
for app in archive-zstd sqlite lua python unix-msg-dontwait readonly-shared-mmap mremap-shared-file sysv-shm git gcc-make; do
    tr -d '\r' < "$work/init.log" | grep -Fx "[APP PASS] $app" >/dev/null
done
grep -Fxq PASS "$work/xorg.result"
# Read persisted content from the clean, unmounted ext4 image itself.
debugfs -R 'cat /root/desktop/documents/note.txt' "$work/partition.ext4" > "$work/desktop-note.txt"
test "$(cat "$work/desktop-note.txt")" = 'naseed desktop works'
debugfs -R 'stat /root/desktop/newfolder' "$work/partition.ext4" | grep -F 'Type: directory'
echo 'PASS: desktop-created directory and edited file survived shutdown'

# Round 7: a SECOND genuine QEMU boot on the SAME physical ext4 disk,
# with the original Alpine PID1 and files created through the GUI.
python3 - "$work" "$ovmf" "$disk" <<'SECOND_BOOT'
import pathlib, pexpect, sys
work, ovmf, disk = sys.argv[1:]
args = ['-M','q35','-m','1024M','-cpu','qemu64','-display','none',
        '-monitor','none','-serial','stdio','-no-reboot','-no-shutdown',
        '-nic','none','-device','isa-debug-exit,iobase=0xf4,iosize=0x04',
        '-drive',f'if=pflash,unit=0,format=raw,file={ovmf},readonly=on',
        '-drive',f'if=none,id=root,format=raw,file={disk}',
        '-device','virtio-blk-pci,drive=root,disable-legacy=on',
        '-cdrom',f'{work}/init.iso']
with open(f'{work}/second-boot.log','w') as log:
    guest=pexpect.spawn('qemu-system-x86_64',args,encoding='utf-8',
                        codec_errors='replace',timeout=180)
    guest.logfile_read=log
    try:
        guest.expect('login:')
        guest.sendline('root')
        guest.expect('Password:')
        guest.sendline('naseed-ci-only')
        guest.expect(r'# ')
        guest.sendline('test "$(cat /root/desktop/documents/note.txt)" = "naseed desktop works" && test -d /root/desktop/newfolder && test -x /usr/bin/weston && test "$(cat /etc/alpine-release)" = 3.23.7 && echo SECOND_BOOT_PERSISTENCE_PASS')
        guest.expect(r'\r*\nSECOND_BOOT_PERSISTENCE_PASS\r*\n',timeout=75)
        guest.expect(r'# ')
        guest.sendline('poweroff')
        guest.expect(pexpect.EOF,timeout=180)
        guest.close()
        assert guest.exitstatus == 1, (guest.exitstatus,guest.signalstatus)
    finally:
        if guest.isalive(): guest.close(force=True)
serial=pathlib.Path(work,'second-boot.log').read_text()
for marker in ('[M191 ROOT] physical Alpine ext4 registered at /',
               '[M191 ROOT] original Alpine PID1 from mounted ext4: /sbin/init',
               '[ALPINE POWER_OFF] original BusyBox; durable ext4 unmount',
               '[PASS] cmdline userspace exit 0'):
    assert marker in serial, marker
assert '[FAIL]' not in serial
print('PASS: second genuine Alpine PID1 QEMU boot reopens GUI-created ext4 files and cleanly shuts down')
SECOND_BOOT
# The second shutdown must also leave physical ext4 clean and persistent.
dd if="$disk" of="$work/partition.ext4" bs=1M skip=2 status=none
e2fsck -fn "$work/partition.ext4"
debugfs -R 'cat /root/desktop/documents/note.txt' "$work/partition.ext4" > "$work/desktop-note.txt"
test "$(cat "$work/desktop-note.txt")" = 'naseed desktop works'
debugfs -R 'stat /root/desktop/newfolder' "$work/partition.ext4" | grep -F 'Type: directory'
echo 'PASS: two genuine QEMU boots, clean ext4, durable original desktop edits'
