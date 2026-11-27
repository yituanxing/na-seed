#!/usr/bin/env python3
"""Native RV64/ARM64 original Alpine GUI edit/save/reboot acceptance.

All Thunar navigation, Mousepad opening, editing and closing are real QMP
input-send-event -> VirtIO-MMIO -> evdev -> Weston/Wayland -> GTK actions.
Serial commands observe file contents and power off; never edit or open GUI.
Requires an already provisioned and booted native-desktop.sh ext4 image.
"""
import json
import pathlib
import pexpect
import socket
import sys
import time

if len(sys.argv) != 3 or sys.argv[1] not in ("riscv64", "aarch64"):
    raise SystemExit("usage: native-gui-roundtrip.py <riscv64|aarch64> <edit|reopen>")
arch, phase = sys.argv[1:]
if phase not in ("edit", "reopen"):
    raise SystemExit("unknown phase: " + phase)
work = pathlib.Path("build/native-boot") / arch
efi, disk = work / "desktop-efi.img", work / "desktop.ext4"
assert efi.is_file() and disk.is_file()
sockpath = str((work / f"gui-{phase}-qmp.sock").resolve())
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
    "-device", "virtio-gpu-device",
    "-device", "virtio-keyboard-device",
    "-device", "virtio-mouse-device",
    "-qmp", f"unix:{sockpath},server=on,wait=off",
    "-cpu", "rv64" if arch == "riscv64" else "cortex-a72",
]
if arch == "aarch64":
    args.extend(["-semihosting-config", "enable=on,target=native"])


def qmp(command):
    with socket.socket(socket.AF_UNIX) as sock:
        sock.settimeout(20)
        sock.connect(sockpath)
        with sock.makefile("rwb") as io:
            greeting = json.loads(io.readline())
            assert "QMP" in greeting, greeting
            for request in ({"execute": "qmp_capabilities"}, command):
                io.write(json.dumps(request).encode() + b"\n")
                io.flush()
                while True:
                    msg = json.loads(io.readline())
                    if "event" in msg:
                        continue
                    if "error" in msg:
                        raise RuntimeError(f"QMP error: {msg}")
                    if "return" in msg:
                        break


def scanout(label):
    path = (work / f"gui-{phase}-{label}.ppm").resolve()
    qmp({"execute": "screendump", "arguments": {"filename": str(path)}})
    magic, w, h, maximum, frame = path.read_bytes().split(None, 4)
    if magic != b"P6" or maximum != b"255":
        raise RuntimeError(f"bad real GPU PPM: {path}")
    w, h = int(w), int(h)
    if len(frame) != w * h * 3:
        raise RuntimeError(f"truncated GPU PPM: {path}")
    return w, h, frame


def input_events(events):
    qmp({"execute": "input-send-event", "arguments": {"events": events}})
    time.sleep(0.045)


def rel(dx, dy):
    events = []
    if dx:
        events.append({"type": "rel", "data": {"axis": "x", "value": dx}})
    if dy:
        events.append({"type": "rel", "data": {"axis": "y", "value": dy}})
    if events:
        input_events(events)


def move_to(x, y):
    # Calibrated using x86-64's same 800x600 Weston/libinput QMP fixture.
    # Always re-zero the actual relative pointer at the negative screen edge.
    for _ in range(24):
        rel(-50, -50)
    dx, dy = round(x * 0.89), round(y * 0.86)
    while dx or dy:
        sx, sy = min(35, dx), min(35, dy)
        rel(sx, sy)
        dx -= sx
        dy -= sy
    time.sleep(0.35)


def click():
    for down in (True, False):
        input_events([{"type": "btn", "data": {"button": "left", "down": down}}])


def double_click():
    click()
    time.sleep(0.07)
    click()
    time.sleep(1.4)


def key(code, down):
    input_events([{"type": "key", "data": {
        "key": {"type": "qcode", "data": code}, "down": down}}])


def chord(*keys):
    for code in keys:
        key(code, True)
    for code in reversed(keys):
        key(code, False)
    time.sleep(0.4)


def type_text(value):
    for char in value:
        code = {" ": "spc", "\n": "ret", ".": "dot"}.get(char, char)
        key(code, True)
        key(code, False)
        time.sleep(0.06)


def pixels_changed(a, b):
    assert len(a) == len(b)
    return sum(a[i:i+3] != b[i:i+3] for i in range(0, len(a), 3))


def pixels_changed_rect(a, b, width, x0, y0, x1, y1):
    return sum(a[(y*width+x)*3:(y*width+x+1)*3] !=
               b[(y*width+x)*3:(y*width+x+1)*3]
               for y in range(y0, y1) for x in range(x0, x1))


def observed(guest, condition, mark, attempts=15):
    # Guest shell is read-only: only GUI QMP input may change the note.
    cmd = (f'for i in $(seq 1 {attempts}); do {condition} && break; '
           f'sleep 1; done; if {condition}; then echo {mark}_PASS; '
           f'else echo {mark}_FAIL; fi')
    guest.sendline(cmd)
    idx = guest.expect([r"\r*\n" + mark + r"_PASS\r*\n",
                        r"\r*\n" + mark + r"_FAIL\r*\n"],
                       timeout=attempts + 65)
    guest.expect(r"# ", timeout=30)
    if idx:
        raise RuntimeError(f"{arch}: real GUI {mark} not observed")
    print(f"PASS: {arch} {phase} {mark} (read-only guest inspection)", flush=True)


with (work / f"gui-{phase}.log").open("w") as outfile:
    guest = pexpect.spawn(f"qemu-system-{arch}", args, encoding="utf-8",
                          codec_errors="replace", timeout=260)
    guest.logfile_read = outfile
    success = False
    try:
        guest.expect("login:", timeout=240)
        guest.sendline("root")
        guest.expect("Password:")
        guest.sendline("naseed-ci-only")
        guest.expect(r"# ")
        guest.sendline(
            "for i in $(seq 1 110); do "
            "test -S /run/user/0/wayland-0 && pidof thunar >/dev/null && "
            "grep -q get_toplevel /var/log/naseed-desktop/thunar.log "
            "2>/dev/null && break; sleep 1; done; "
            "if test -S /run/user/0/wayland-0 && pidof thunar >/dev/null && "
            "grep -q get_toplevel /var/log/naseed-desktop/thunar.log; then "
            "echo GUI_READY; else echo GUI_NOT_READY; fi"
        )
        if guest.expect([r"\r*\nGUI_READY\r*\n",
                         r"\r*\nGUI_NOT_READY\r*\n"], timeout=235):
            raise RuntimeError(f"{arch} original OpenRC/Thunar desktop not ready")
        guest.expect(r"# ")
        steady = False
        last = None
        for _ in range(75):
            time.sleep(1)
            w, h, frame = scanout("files-root")
            visible = sum(max(frame[i:i+3]) > 20 for i in range(0, len(frame), 3))
            drift = pixels_changed(last, frame) if last is not None else w*h
            if (w, h) == (800, 600) and visible > 70000 and drift < 3000:
                steady = True
                break
            last = frame
        if not steady:
            raise RuntimeError(f"{arch} Thunar screen not stable: {visible=} {drift=}")
        move_to(520, 340)
        click()
        time.sleep(0.7)
        _, _, root = scanout("files-focused")
        # Selection alone can change thousands of pixels; verify the REAL
        # Thunar location bar changed, not just the selected folder icon.
        address_delta = 0
        for attempt in range(1, 4):
            move_to(330, 230)
            double_click()
            _, _, documents = scanout(f"documents-attempt-{attempt}")
            address_delta = pixels_changed_rect(root, documents, w, 360, 131, 520, 154)
            if address_delta > 80:
                break
        else:
            raise RuntimeError(f"{arch} Thunar real double-click did not enter "
                               f"documents: address_pixels={address_delta}")
        print(f"PASS: {arch} Thunar navigated using real QMP mouse", flush=True)
        move_to(330, 230)
        double_click()
        delta, opened = 0, False
        for _ in range(60):
            time.sleep(0.7)
            _, _, editor = scanout("editor-open")
            delta = pixels_changed(documents, editor)
            if delta > 18000:
                opened = True
                break
        guest.sendline("if pidof mousepad >/dev/null; then echo GUI_MOUSEPAD_LIVE; "
                       "else echo GUI_MOUSEPAD_MISSING; fi")
        alive = guest.expect([r"\r*\nGUI_MOUSEPAD_LIVE\r*\n",
                              r"\r*\nGUI_MOUSEPAD_MISSING\r*\n"], timeout=45) == 0
        guest.expect(r"# ")
        if not (alive and opened):
            raise RuntimeError(f"{arch} real Thunar could not open Mousepad: "
                               f"{alive=} {delta=}")
        print(f"PASS: {arch} original GTK Mousepad opened through real Thunar", flush=True)
        target = 'test "$(cat /root/desktop/documents/note.txt)" = "naseed gui roundtrip"'
        if phase == "edit":
            move_to(240, 180)
            click()
            chord("ctrl", "a")
            type_text("naseed gui roundtrip\n")
            chord("ctrl", "s")
            time.sleep(1.2)
            scanout("editor-saved")
            observed(guest, target, "NATIVE_GUI_SAVED", attempts=18)
        else:
            observed(guest, target, "NATIVE_GUI_REOPENED", attempts=1)
        chord("ctrl", "w")
        time.sleep(0.7)
        scanout("editor-closed")
        guest.sendline("poweroff")
        guest.expect(r"\[ALPINE POWER_OFF\] original BusyBox; durable ext4 unmount",
                     timeout=220)
        guest.expect(r"\[PASS\] cmdline userspace exit 0", timeout=90)
        success = True
    finally:
        if guest.isalive():
            guest.terminate(force=True)
        guest.close()
if not success:
    raise RuntimeError(f"{arch} native GUI {phase} failed")
serial = (work / f"gui-{phase}.log").read_text(errors="replace")
for marker in ("[M191 ROOT] original Alpine PID1 from mounted ext4: /sbin/init",
               "[LWEXT4 GUEST] clean upstream unmount and durable flush",
               "[ALPINE POWER_OFF] original BusyBox; durable ext4 unmount"):
    if marker not in serial:
        raise RuntimeError(f"{arch} GUI {phase} did not reach clean shutdown: {marker}")
if "[FAIL]" in serial:
    raise RuntimeError(f"{arch} GUI {phase} kernel failure marker")
print(f"PASS: {arch} genuine Thunar -> Mousepad GUI {phase} -> "
      "clean shutdown and durable ext4", flush=True)
