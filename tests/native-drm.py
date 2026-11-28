#!/usr/bin/env python3
"""Native ARM64/RV64 original libdrm/GEM and modetest modeset/pageflip.

A separate real QEMU boot: shut down the original Weston DRM master through
OpenRC, then run unmodified Alpine libdrm-tests and the same upstream GEM
probe that x86-64 already runs. Require changed actual VirtIO-GPU pixels,
pageflip completion events and clean ext4 poweroff. Never emulate ioctls.
"""
import json
import pathlib
import pexpect
import socket
import sys
import time

if len(sys.argv) not in (2, 3) or sys.argv[1] not in ("riscv64", "aarch64"):
    raise SystemExit("usage: native-drm.py <riscv64|aarch64> [xorg]")
arch = sys.argv[1]
mode = sys.argv[2] if len(sys.argv) == 3 else "drm"
if mode not in ("drm", "xorg"):
    raise SystemExit("invalid native graphic profile: " + mode)
work = pathlib.Path("build/native-boot") / arch
efi = work / "desktop-efi.img"
disk = work / ("xorg.ext4" if mode == "xorg" else "desktop.ext4")
assert efi.is_file() and disk.is_file()
qmp_path = str((work / f"native-{mode}-qmp.sock").resolve())
firmware = {
    "riscv64": "/usr/share/qemu-efi-riscv64/RISCV_VIRT_CODE.fd",
    "aarch64": "/usr/share/AAVMF/AAVMF_CODE.fd",
}[arch]
args = [
    "-machine", "virt,acpi=off" if arch == "riscv64" else "virt",
    "-m", "1024M", "-smp", "1", "-display", "none", "-monitor", "none",
    "-serial", "stdio", "-no-reboot", "-no-shutdown", "-nic", "none",
    "-global", "virtio-mmio.force-legacy=false",
    "-drive", f"if=pflash,unit=0,format=raw,file={firmware},readonly=on",
    "-drive", f"if=none,id=bootdisk,format=raw,file={efi.resolve()},readonly=on",
    "-device", "virtio-blk-pci,drive=bootdisk",
    "-drive", f"if=none,id=root,format=raw,file={disk.resolve()}",
    "-device", "virtio-blk-device,drive=root",
    "-device", "virtio-gpu-device", "-device", "virtio-keyboard-device",
    "-device", "virtio-mouse-device",
    "-qmp", f"unix:{qmp_path},server=on,wait=off",
    "-cpu", "rv64" if arch == "riscv64" else "cortex-a72",
]
if arch == "aarch64":
    args.extend(["-semihosting-config", "enable=on,target=native"])


def qmp(command):
    with socket.socket(socket.AF_UNIX) as sock:
        sock.settimeout(20)
        sock.connect(qmp_path)
        with sock.makefile("rwb") as stream:
            assert "QMP" in json.loads(stream.readline())
            for request in ({"execute": "qmp_capabilities"}, command):
                stream.write(json.dumps(request).encode() + b"\n")
                stream.flush()
                while True:
                    reply = json.loads(stream.readline())
                    if "event" in reply:
                        continue
                    if "error" in reply:
                        raise RuntimeError(f"QMP failed: {reply}")
                    if "return" in reply:
                        break


def scanout(label):
    path = (work / f"drm-{label}.ppm").resolve()
    qmp({"execute": "screendump", "arguments": {"filename": str(path)}})
    magic, w, h, maximum, frame = path.read_bytes().split(None, 4)
    if magic != b"P6" or maximum != b"255":
        raise RuntimeError("QMP did not provide genuine VirtIO-GPU PPM")
    width, height = int(w), int(h)
    if len(frame) != width * height * 3:
        raise RuntimeError("QMP provided invalid scanout")
    return width, height, frame


def changed(a, b):
    assert len(a) == len(b)
    return sum(a[i:i+3] != b[i:i+3] for i in range(0, len(a), 3))


def verify(guest, command, marker, timeout=100):
    guest.sendline(command)
    status = guest.expect([r"\r*\n" + marker + r"_PASS\r*\n",
                           r"\r*\n" + marker + r"_FAIL\r*\n"], timeout=timeout)
    guest.expect(r"# ", timeout=30)
    if status:
        raise RuntimeError(f"{arch}: real original-Alpine {marker} failed")
    print(f"PASS: {arch} {marker} from real Alpine software", flush=True)


def run_original_xorg(guest):
    """Run unmodified Xorg/modesetting DRM, Openbox and original X11 clients."""
    verify(guest,
           'python3 /root/xorg-input.py >/root/xorg-input.log 2>&1; '
           'status=$?; if test "$status" = 0 && '
           'grep -q "InputDevice" /root/xorg.conf; then '
           'echo XORG_EVDEV_PASS; else tail -80 /root/xorg-input.log; '
           'echo XORG_EVDEV_FAIL; fi',
           'XORG_EVDEV', timeout=85)
    guest.sendline(
        # The ARM64/RV64 backend is intentionally unaccelerated 2D KMS.
        # Alpine's optional GLX swrast provider is absent, and loading it
        # emits AIGLX errors. X11/Render/MIT-SHM do not require GLX/DRI3.
        'Xorg :0 vt1 -keeptty -config /root/xorg.conf '
        '-logfile /root/Xorg.log -nolisten tcp -ac -noreset '
        '-extension GLX -extension DRI3 '
        '>/root/xorg-console.log 2>&1 & xorg_pid=$!; echo XORG_LAUNCHED')
    guest.expect(r'\r*\nXORG_LAUNCHED\r*\n', timeout=35)
    guest.expect(r'# ')
    try:
        verify(guest,
               'for i in $(seq 1 35); do '
               'timeout 3 xdpyinfo -display :0 >/root/xdpyinfo.log 2>&1 '
               '&& break; sleep 1; done; '
               'if kill -0 "$xorg_pid" 2>/dev/null && '
               'xdpyinfo -display :0 >/root/xdpyinfo.log 2>&1; then '
               'echo XORG_SERVER_PASS; else '
               'echo XORG_SERVER_DIAGNOSTICS_BEGIN; '
               'cat /root/xdpyinfo.log 2>/dev/null; '
               'ls -ld /tmp/.X11-unix /tmp/.X11-unix/* 2>&1; '
               'ps -ef 2>&1 | grep -E "Xorg|xdpyinfo"; '
               'tail -140 /root/Xorg.log /root/xorg-console.log; '
               'echo XORG_SERVER_DIAGNOSTICS_END; '
               'echo XORG_SERVER_FAIL; fi',
               'XORG_SERVER', timeout=140)
    except RuntimeError:
        # Preserve the real Xorg failure. A second original Alpine Xorg
        # run under GDB gives an actual guest backtrace without replacing
        # or weakening the required ordinary X11 acceptance.
        guest.sendline(
            "timeout 160 gdb -q -batch -ex 'set pagination off' "
            "-ex 'set confirm off' -ex 'run' "
            "-ex 'thread apply all bt 14' -ex 'info sharedlibrary' "
            "--args /usr/libexec/Xorg :1 vt1 -keeptty "
            "-config /root/xorg.conf -logfile /root/Xorg-gdb.log "
            "-nolisten tcp -ac -noreset -extension GLX -extension DRI3 "
            ">/root/xorg-gdb-backtrace.log 2>&1; "
            "echo XORG_GDB_DIAGNOSTICS_BEGIN; "
            "tail -180 /root/xorg-gdb-backtrace.log; "
            "echo XORG_GDB_DIAGNOSTICS_END")
        try:
            guest.expect(r'\r*\nXORG_GDB_DIAGNOSTICS_END\r*\n', timeout=190)
        except pexpect.TIMEOUT:
            print(f'{arch}: guest GDB diagnostic timed out', flush=True)
        raise

    _, _, before = scanout('xorg-before-root')
    verify(guest,
           'if DISPLAY=:0 xsetroot -solid red; then echo XORG_ROOT_PASS; '
           'else echo XORG_ROOT_FAIL; fi', 'XORG_ROOT', timeout=50)
    _, _, painted = scanout('xorg-root-red')
    pixels = changed(before, painted)
    if pixels < 80000:
        raise RuntimeError(f'{arch}: Xorg xsetroot did not change real GPU '
                           f'pixels: {pixels}')
    print(f'PASS: {arch} original Xorg drew {pixels} real QMP pixels', flush=True)
    guest.sendline(
        'DISPLAY=:0 openbox --sm-disable >/root/native-openbox.log 2>&1 & wm=$!; '
        'sleep 2; DISPLAY=:0 xterm -T NASEED-NATIVE-XTERM '
        '-geometry 52x12+40+50 -bg white -fg black -fn fixed '
        '-e /bin/sh -i >/root/native-xterm.log 2>&1 & xt=$!; '
        'DISPLAY=:0 xeyes -title NASEED-NATIVE-XEYES '
        '-geometry 150x110+580+350 >/root/native-xeyes.log 2>&1 & xe=$!; '
        'sleep 3; echo XORG_APPS_LAUNCHED')
    guest.expect(r'\r*\nXORG_APPS_LAUNCHED\r*\n', timeout=65)
    guest.expect(r'# ')
    verify(guest,
           'DISPLAY=:0 xwininfo -root -tree >/root/xorg-windows.log 2>&1; '
           'if pidof openbox >/dev/null && pidof xterm >/dev/null && '
           'pidof xeyes >/dev/null && '
           'grep -q NASEED-NATIVE-XTERM /root/xorg-windows.log && '
           'grep -q NASEED-NATIVE-XEYES /root/xorg-windows.log; then '
           'echo XORG_X11_APPS_PASS; else cat /root/xorg-windows.log; '
           'tail -40 /root/native-openbox.log /root/native-xterm.log '
           '/root/native-xeyes.log; echo XORG_X11_APPS_FAIL; fi',
           'XORG_X11_APPS', timeout=100)
    _, _, apps = scanout('xorg-openbox-xterm-xeyes')
    delta = changed(painted, apps)
    if delta < 20000:
        raise RuntimeError(f'{arch}: native X11 windows did not change '
                           f'actual GPU pixels: {delta}')
    print(f'PASS: {arch} original Xorg/Openbox/xterm/xeyes '
          f'real GPU pixels changed={delta}', flush=True)
    verify(guest,
           'kill "$xe" "$xt" "$wm" "$xorg_pid" 2>/dev/null || :; '
           'for i in $(seq 1 20); do pidof Xorg >/dev/null || break; '
           'sleep 1; done; '
           'if ! pidof Xorg >/dev/null; then echo XORG_CLEAN_PASS; '
           'else echo XORG_CLEAN_FAIL; fi',
           'XORG_CLEAN', timeout=90)


with (work / f"{mode}-serial.log").open("w") as log:
    guest = pexpect.spawn(f"qemu-system-{arch}", args, encoding="utf-8",
                          codec_errors="replace", timeout=260)
    guest.logfile_read = log
    ok = False
    try:
        guest.expect("login:", timeout=240)
        guest.sendline("root")
        guest.expect("Password:")
        guest.sendline("naseed-ci-only")
        guest.expect(r"# ")
        if mode == "xorg":
            verify(guest,
                   'rc-service naseed-desktop stop >/root/drm-stop.log 2>&1; '
                   'for i in $(seq 1 30); do pidof weston >/dev/null || break; sleep 1; done; '
                   'if ! pidof weston >/dev/null; then echo DRM_MASTER_FREE_PASS; '
                   'else tail -80 /root/drm-stop.log; echo DRM_MASTER_FREE_FAIL; fi',
                   "DRM_MASTER_FREE", timeout=110)
            run_original_xorg(guest)
        else:
            # Reuse NaSeed's documented trusted UTC boot metadata, NOT a fake
            # monotonic epoch. The RV64 QEMU firmware can supply stale RTC.
            verify(guest,
                   "now=$(date -u +%s); "
                   "if test \"$now\" -ge 1700000000; then "
                   "echo NATIVE_REALTIME_PASS; else echo NATIVE_REALTIME_FAIL; fi",
                   "NATIVE_REALTIME", timeout=40)
            verify(guest,
                   "rc-service naseed-desktop stop >/root/drm-stop.log 2>&1; "
                   "for i in $(seq 1 30); do pidof weston >/dev/null || break; sleep 1; done; "
                   "if ! pidof weston >/dev/null; then echo DRM_MASTER_FREE_PASS; "
                   "else tail -80 /root/drm-stop.log; echo DRM_MASTER_FREE_FAIL; fi",
                   "DRM_MASTER_FREE", timeout=110)
            # Byte-identical upstream x86 libdrm/GEM syscall acceptance, not a
            # hand-written alternative ioctl implementation for native ISAs.
            verify(guest,
                   "python3 /root/drm-probe.py >/root/drm-gem.log 2>&1; result=$?; "
                   "if test \"$result\" = 0 && "
                   "grep -Fxq DRM_GEM_FINAL_CLOSE_PASS /root/drm-gem.log && "
                   "grep -Fxq DRM_KMS_DISCOVERY_PASS /root/drm-gem.log; then "
                   "echo DRM_GEM_NATIVE_PASS; else tail -100 /root/drm-gem.log; "
                   "echo DRM_GEM_NATIVE_FAIL; fi",
                   "DRM_GEM_NATIVE", timeout=95)
            verify(guest,
                   "modetest -M virtio_gpu -c -e -p >/root/drm-enum.log 2>&1; result=$?; "
                   "if test \"$result\" = 0 && grep -q connected /root/drm-enum.log; "
                   "then echo DRM_MODE_ENUM_PASS; else tail -80 /root/drm-enum.log; "
                   "echo DRM_MODE_ENUM_FAIL; fi",
                   "DRM_MODE_ENUM", timeout=85)
            width, height, before = scanout("before-modeset")
            verify(guest,
                   "modetest -M virtio_gpu -s 44@42:800x600 </dev/null "
                   ">/root/drm-modeset.log 2>&1; result=$?; "
                   "if test \"$result\" = 0; then echo DRM_MODE_SET_PASS; "
                   "else tail -80 /root/drm-modeset.log; echo DRM_MODE_SET_FAIL; fi",
                   "DRM_MODE_SET", timeout=90)
            w, h, after = scanout("actual-modeset")
            delta = changed(before, after)
            if (width, height) != (800, 600) or (w, h) != (800, 600) or delta < 80000:
                raise RuntimeError(f"{arch}: modetest did not change actual QMP "
                                   f"GPU scanout: {delta} pixels")
            print(f"PASS: {arch} unmodified modetest KMS modeset changed "
                  f"{delta} real VirtIO-GPU pixels", flush=True)

            guest.sendline("(sleep 25 | modetest -M virtio_gpu -s 44@42:800x600 -v) "
                           ">/root/drm-pageflip.log 2>&1 & flip_pid=$!; "
                           "echo DRM_PAGEFLIP_STARTED")
            guest.expect(r"\r*\nDRM_PAGEFLIP_STARTED\r*\n", timeout=45)
            guest.expect(r"# ")
            time.sleep(8)
            _, _, flip_a = scanout("pageflip-active-a")
            time.sleep(9)
            _, _, flip_b = scanout("pageflip-active-b")
            page_delta = changed(flip_a, flip_b)
            # The unmodified modetest pageflip stream can present one
            # solid pixel pattern for many consecutive frames. QMP samples
            # eight seconds apart may therefore be byte-identical even
            # when genuine PAGE_FLIP_COMPLETE events continue arriving.
            # Require a REAL QMP scanout transition from the preceding
            # upstream modeset, independently of the strict event check.
            phase_delta = changed(after, flip_a)
            verify(guest,
                   "wait \"$flip_pid\"; result=$?; "
                   "if test \"$result\" = 0 && "
                   "grep -Eq 'freq: [1-9][0-9]*' /root/drm-pageflip.log; "
                   "then echo DRM_PAGEFLIP_EVENTS_PASS; "
                   "else tail -100 /root/drm-pageflip.log; "
                   "echo DRM_PAGEFLIP_EVENTS_FAIL; fi",
                   "DRM_PAGEFLIP_EVENTS", timeout=100)
            if max(page_delta, phase_delta) < 10000:
                raise RuntimeError(
                    f"{arch}: original modetest Page Flip had real events "
                    f"but never changed actual VirtIO-GPU scanout: "
                    f"phase={phase_delta}, between={page_delta}"
                )
            print(f"PASS: {arch} real DRM pageflip events and actual "
                  f"QMP scanout transition: phase={phase_delta}, "
                  f"between_samples={page_delta}", flush=True)
        guest.sendline("poweroff")
        guest.expect(r"\[LWEXT4 GUEST\] clean upstream unmount and durable flush",
                     timeout=180)
        guest.expect(r"\[PASS\] cmdline userspace exit 0", timeout=60)
        ok = True
    finally:
        if guest.isalive():
            guest.terminate(force=True)
        guest.close()
if not ok:
    raise RuntimeError(f"{arch} native DRM/GEM/PageFlip acceptance incomplete")
if mode == "xorg":
    print(f"PASS: {arch} original Alpine Xorg modesetting / Openbox / "
          "xterm / xeyes, QMP GPU pixels and clean ext4 shutdown", flush=True)
else:
    print(f"PASS: {arch} original libdrm/GEM + KMS/PageFlip events + "
          "real QMP pixels and clean ext4 shutdown", flush=True)
