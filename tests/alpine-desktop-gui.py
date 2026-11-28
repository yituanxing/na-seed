#!/usr/bin/env python3
"""Original Alpine GUI round trip: QMP VirtIO pointer and keyboard, never XTEST.

Serial is used only for read-only observations, console login and clean poweroff.
All GUI navigation, file opening, editing, saving and closing use physical input.
"""
import json
import pathlib
import pexpect
import socket
import sys
import time

work, ovmf, disk, phase = sys.argv[1:]
assert phase in ("edit", "reopen", "fileops", "fileops-reopen"), phase
work = pathlib.Path(work)
sockpath = str(work / f"gui-{phase}-qmp.sock")
args = [
    "-M", "q35", "-m", "1024M", "-cpu", "qemu64", "-display", "none",
    "-monitor", "none", "-serial", "stdio", "-no-reboot", "-no-shutdown",
    "-nic", "none", "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04",
    "-drive", f"if=pflash,unit=0,format=raw,file={ovmf},readonly=on",
    "-drive", f"if=none,id=root,format=raw,file={disk}",
    "-device", "virtio-blk-pci,drive=root,disable-legacy=on",
    "-vga", "none", "-device", "virtio-gpu-pci,disable-legacy=on",
    "-device", "virtio-keyboard-pci,disable-legacy=on",
    "-device", "virtio-mouse-pci,disable-legacy=on",
    "-qmp", f"unix:{sockpath},server=on,wait=off",
    "-cdrom", str(work / "init.iso"),
]

def qmp(command):
    with socket.socket(socket.AF_UNIX) as sock:
        sock.settimeout(20)
        sock.connect(sockpath)
        with sock.makefile("rwb") as stream:
            json.loads(stream.readline())
            for request in ({"execute": "qmp_capabilities"}, command):
                stream.write(json.dumps(request).encode() + b"\n")
                stream.flush()
                while True:
                    message = json.loads(stream.readline())
                    if "event" in message:
                        continue
                    if "error" in message:
                        raise RuntimeError(message)
                    if "return" in message:
                        break

def scanout(label):
    path = work / f"gui-{phase}-{label}.ppm"
    qmp({"execute": "screendump", "arguments": {"filename": str(path)}})
    magic, w, h, maximum, pixels = path.read_bytes().split(None, 4)
    assert magic == b"P6" and maximum == b"255"
    w, h = int(w), int(h)
    assert len(pixels) == w * h * 3
    return w, h, pixels

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
    # The guest uses an ordinary REL mouse; 24 moves to the negative edge
    # clamp it at (0,0) from ANY previous position. No invented pointer state.
    for _ in range(24):
        rel(-50, -50)
    # On this fixed 800x600 QEMU/Weston fixture, libinput accelerates
    # 35-unit REL steps. The first real failure screenshot measured the
    # pointer hotspot near (370,267) for the requested (330,230), missing
    # the documents icon entirely. Calibrate the *physical input deltas*
    # to the resulting screen pixels; do not inject synthetic GUI events.
    dx, dy = round(x * 0.89), round(y * 0.86)
    while dx or dy:
        step_x = min(35, dx)
        step_y = min(35, dy)
        rel(step_x, step_y)
        dx -= step_x
        dy -= step_y
    time.sleep(0.35)

def click():
    for down in (True, False):
        input_events([{"type": "btn", "data": {"button": "left", "down": down}}])

def double_click():
    click()
    # Keep the second press comfortably inside GTK's double-click interval.
    # The previous 0.14s gap plus two 45ms QMP waits and connection latency
    # could put the press-to-press interval near GTK's default threshold.
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

def type_text(text):
    for ch in text:
        keycode = {" ": "spc", "\n": "ret", ".": "dot"}.get(ch, ch)
        key(keycode, True)
        key(keycode, False)
        time.sleep(0.06)

def pixels_changed(a, b):
    assert len(a) == len(b)
    return sum(a[i:i+3] != b[i:i+3] for i in range(0, len(a), 3))

def pixels_changed_rect(a, b, width, left, top, right, bottom):
    # The location entry changes only after navigation; selecting an icon
    # changes thousands of other pixels but must never satisfy this check.
    changed = 0
    for y in range(top, bottom):
        for x in range(left, right):
            pos = (y * width + x) * 3
            changed += a[pos:pos + 3] != b[pos:pos + 3]
    return changed

def observe(guest, shell, success):
    guest.sendline(shell + "; echo " + success)
    guest.expect(r"\r*\n" + success + r"\r*\n", timeout=95)
    guest.expect(r"# ")


def fileops_observe(guest, expr, marker, retries=15):
    """Only read guest state; mutating file operations MUST use QMP inputs."""
    guest.sendline(
        f'for i in $(seq 1 {retries}); do {expr} && break; sleep 1; done; '
        f'if {expr}; then echo {marker}_PASS; else echo {marker}_FAIL; fi'
    )
    passed = guest.expect([r"\r*\n" + marker + r"_PASS\r*\n",
                           r"\r*\n" + marker + r"_FAIL\r*\n"],
                          timeout=retries + 50) == 0
    guest.expect(r"# ")
    if not passed:
        raise RuntimeError(f"GUI file operation {marker} did not change actual guest filesystem")
    print(f"PASS: GUI file operation {marker}, observed read-only", flush=True)


def thunar_window_count(guest):
    guest.sendline(
        'printf "THUNAR_TOPLEVELS=%s\\n" '
        '"$(grep -c get_toplevel /var/log/naseed-desktop/thunar.log 2>/dev/null || :)"'
    )
    guest.expect(r"\r*\nTHUNAR_TOPLEVELS=([0-9]+)\r*\n", timeout=30)
    count = int(guest.match.group(1))
    guest.expect(r"# ")
    return count


def fileops_navigate(phase, baseline, w, to_documents):
    """Screen-path proof prevents selection-only false positives."""
    move_to(330, 230)
    double_click()
    _, _, after = scanout("fileops-documents" if to_documents else "fileops-root")
    address_change = pixels_changed_rect(baseline, after, w, 360, 131, 520, 154)
    if address_change < 80:
        raise RuntimeError(f"Thunar fileops navigation did not change location: "
                           f"address_pixels={address_change}")
    print(f"PASS: GUI fileops navigated {'documents' if to_documents else 'root'} "
          f"by physical input, address_pixels={address_change}", flush=True)
    return after


def fileops_select_last():
    # End is handled by the original Thunar icon view, not the guest shell.
    chord("end")
    time.sleep(0.3)


def fileops_mousepad(guest, baseline):
    fileops_select_last()
    chord("ret")
    editor = None
    changed = 0
    for _ in range(55):
        time.sleep(0.7)
        _, _, sample = scanout("fileops-reopened-editor")
        changed = pixels_changed(baseline, sample)
        if changed > 18000:
            editor = sample
            break
    guest.sendline(
        'if pidof mousepad >/dev/null; then echo FILEOPS_EDITOR_ALIVE; '
        'else echo FILEOPS_EDITOR_MISSING; fi'
    )
    alive = guest.expect([r"\r*\nFILEOPS_EDITOR_ALIVE\r*\n",
                          r"\r*\nFILEOPS_EDITOR_MISSING\r*\n"],
                         timeout=35) == 0
    guest.expect(r"# ")
    if not alive or editor is None:
        raise RuntimeError(f"GUI reopen of renamed persisted file failed: "
                           f"mousepad_alive={alive} changed_pixels={changed}")
    print(f"PASS: restarted Thunar opened renamed file in original Mousepad, "
          f"visible_pixels={changed}", flush=True)
    chord("ctrl", "w")
    time.sleep(0.5)


def run_fileops(guest, phase, root_frame, w, h):
    assert (w, h) == (800, 600)
    saved = 'test "$(cat /root/desktop/documents/note.txt)" = "naseed gui roundtrip"'
    archived = 'test "$(cat /root/desktop/archived.txt)" = "naseed gui roundtrip"'
    absent = 'test ! -e /root/desktop/note.txt'
    fileops_observe(guest, saved, "ORIGINAL_INTACT", retries=1)
    if phase == "fileops-reopen":
        # A completely fresh QEMU boot: the retained copy exists, but the
        # permanently deleted second copy must remain absent.
        fileops_observe(guest, archived, "ARCHIVE_SURVIVED_REBOOT", retries=1)
        fileops_observe(guest, absent, "DELETE_SURVIVED_REBOOT", retries=1)
        fileops_mousepad(guest, root_frame)
        fileops_observe(guest, archived, "ARCHIVE_REOPEN_CONTENT", retries=1)
        print("PASS: GUI fileops second independent boot reopened renamed file; "
              "permanent deletion survived", flush=True)
        return

    # 1. Original Thunar: real double-click and real Ctrl+C / Alt+Up / Ctrl+V.
    documents = fileops_navigate(phase, root_frame, w, True)
    move_to(330, 230)
    click()
    chord("ctrl", "c")
    chord("alt", "up")
    time.sleep(0.8)
    _, _, parent = scanout("fileops-parent")
    assert pixels_changed_rect(documents, parent, w, 360, 131, 520, 154) > 80, (
        "Thunar Alt+Up failed to navigate to parent folder")
    chord("ctrl", "v")
    fileops_observe(guest,
                    'test "$(cat /root/desktop/note.txt)" = "naseed gui roundtrip"',
                    "COPY_TO_DESKTOP")
    scanout("fileops-copied")

    # 2. Rename the copied file in Thunar; original source is untouched.
    fileops_select_last()
    chord("f2")
    time.sleep(0.65)
    scanout("fileops-rename-dialog")
    chord("ctrl", "a")
    type_text("archived.txt")
    chord("ret")
    fileops_observe(guest, archived + ' && ' + absent, "RENAME")
    scanout("fileops-renamed")

    # 3. Open a second original Thunar window (not another app). Its
    # xdg_toplevel must be a genuinely new Wayland request, then give it
    # physical focus. The old Thunar window remains open underneath.
    before_windows = thunar_window_count(guest)
    chord("ctrl", "n")
    time.sleep(2.0)
    after_windows = thunar_window_count(guest)
    if after_windows <= before_windows:
        scanout("fileops-second-window-failed")
        raise RuntimeError(f"Thunar Ctrl+N failed to create second Wayland window: "
                           f"{before_windows=} {after_windows=}")
    move_to(520, 340)
    click()
    time.sleep(0.65)
    _, _, new_window = scanout("fileops-second-window")
    print(f"PASS: second independent Thunar Wayland toplevel via QMP Ctrl+N: "
          f"{before_windows=} {after_windows=}", flush=True)

    # 4. Create a second copy via the new window; then permanently delete
    # THAT copy via Shift+Delete (not rm, unlink, or a guest shell command).
    new_documents = fileops_navigate(phase, new_window, w, True)
    move_to(330, 230)
    click()
    chord("ctrl", "c")
    chord("alt", "up")
    time.sleep(0.8)
    _, _, second_parent = scanout("fileops-second-parent")
    assert pixels_changed_rect(new_documents, second_parent, w, 360, 131, 520, 154) > 80, (
        "Second Thunar window Alt+Up failed")
    chord("ctrl", "v")
    fileops_observe(guest,
                    'test "$(cat /root/desktop/note.txt)" = "naseed gui roundtrip"',
                    "SECOND_COPY")
    fileops_select_last()
    chord("shift", "delete")
    time.sleep(0.85)
    scanout("fileops-delete-confirmation")
    # Depending on the original Thunar preferences Shift+Delete may show a
    # confirmation dialog or immediately remove the file. Observe only.
    guest.sendline('if test -e /root/desktop/note.txt; then echo DELETE_NEEDS_CONFIRM; '
                   'else echo DELETE_ALREADY_DONE; fi')
    confirm = guest.expect([r"\r*\nDELETE_NEEDS_CONFIRM\r*\n",
                            r"\r*\nDELETE_ALREADY_DONE\r*\n"], timeout=25) == 0
    guest.expect(r"# ")
    if confirm:
        chord("ret")
    fileops_observe(guest, absent + ' && ' + archived, "PERMANENT_DELETE")
    scanout("fileops-deleted")
    chord("ctrl", "w")
    time.sleep(0.6)
    fileops_observe(guest, archived + ' && ' + absent + ' && ' + saved,
                    "TWO_WINDOWS_CLOSED_SAFELY", retries=1)
    print("PASS: original Thunar copy, rename, two-window copy/delete via "
          "real QMP mouse and keyboard; source and archived copy preserved", flush=True)

with (work / f"gui-{phase}.log").open("w") as logfile:
    guest = pexpect.spawn("qemu-system-x86_64", args, encoding="utf-8",
                          codec_errors="replace", timeout=240)
    guest.logfile_read = logfile
    try:
        guest.expect("login:", timeout=240)
        guest.sendline("root")
        guest.expect("Password:")
        guest.sendline("naseed-ci-only")
        guest.expect(r"# ")
        # Readiness is an observation. No console command launches a GUI app.
        guest.sendline(
            'for i in $(seq 1 110); do test -S /run/user/0/wayland-0 && '
            'pidof thunar >/dev/null && grep -q get_toplevel '
            '/var/log/naseed-desktop/thunar.log 2>/dev/null && break; '
            'sleep 1; done; if test -S /run/user/0/wayland-0 && '
            'pidof thunar >/dev/null && grep -q get_toplevel '
            '/var/log/naseed-desktop/thunar.log; then echo GUI_READY; '
            'else echo GUI_NOT_READY; fi'
        )
        if guest.expect([r"\r*\nGUI_READY\r*\n",
                         r"\r*\nGUI_NOT_READY\r*\n"], timeout=225):
            raise RuntimeError("OpenRC did not produce a live Thunar GUI")
        guest.expect(r"# ")
        steady = False
        last = None
        for _ in range(70):
            time.sleep(1)
            w, h, frame = scanout("files-root")
            visible = sum(max(frame[i:i+3]) > 20 for i in range(0, len(frame), 3))
            drift = pixels_changed(last, frame) if last is not None else w*h
            if (w, h) == (800, 600) and visible > 70000 and drift < 3000:
                steady = True
                break
            last = frame
        assert steady, f"original Thunar never painted stable desktop: {visible=}, {drift=}"
        # A newly mapped Weston toplevel is not guaranteed keyboard/mouse
        # focus. Click empty Thunar content first, *then* double-click the
        # folder. This remains genuine QMP -> VirtIO -> Weston input.
        move_to(520, 340)
        click()
        time.sleep(0.7)
        fw, fh, frame = scanout("files-focused")
        assert (fw, fh) == (w, h)
        if phase in ("fileops", "fileops-reopen"):
            run_fileops(guest, phase, frame, w, h)
        else:
            # The actual 800x600 screenshot puts the first icon near (280,230)
            # after libinput acceleration of the requested QMP REL movement.
            # Only a changed path entry proves navigation: icon selection alone
            # changed 5666 pixels and caused a false PASS in the previous test.
            # The real address field is y=121..155, with path glyphs at y=131..154;
            # x=360..520 covers the added /documents suffix, not the menu row.
            address_change = changed = 0
            for attempt in range(1, 4):
                move_to(330, 230)
                double_click()
                dw, dh, documents = scanout(f"documents-attempt-{attempt}")
                assert (dw, dh) == (w, h)
                changed = pixels_changed(frame, documents)
                address_change = pixels_changed_rect(
                    frame, documents, w, 360, 131, 520, 154)
                if address_change > 80:
                    break
                print(f"GUI {phase}: Thunar still shows desktop after real "
                      f"double-click attempt {attempt}, selection_pixels={changed}", flush=True)
            else:
                raise RuntimeError(f"Thunar did not navigate into documents after "
                                   f"three physical double-click attempts: "
                                   f"screen_delta={changed} address_delta={address_change}")
            print(f"PASS: GUI {phase}: Thunar really navigated to documents by "
                  f"QMP double-click, attempts={attempt} "
                  f"screen_pixels={changed} address_pixels={address_change}", flush=True)
            # note.txt is now the first entry in the same icon cell. Launch
            # its installed MIME handler by physical double click.
            move_to(330, 230)
            double_click()
            editor = None
            delta = 0
            for _ in range(60):
                time.sleep(0.7)
                _, _, candidate = scanout("editor-open")
                delta = pixels_changed(documents, candidate)
                if delta > 18000:
                    editor = candidate
                    break
            guest.sendline('if pidof mousepad >/dev/null; then echo GUI_MOUSEPAD_LIVE; else echo GUI_MOUSEPAD_MISSING; fi')
            alive = guest.expect([r"\r*\nGUI_MOUSEPAD_LIVE\r*\n",
                                  r"\r*\nGUI_MOUSEPAD_MISSING\r*\n"], timeout=35) == 0
            guest.expect(r"# ")
            if not alive or editor is None:
                guest.sendline('tail -80 /var/log/naseed-desktop/thunar.log /var/log/naseed-desktop/weston.log 2>/dev/null; echo GUI_DIAGNOSTICS_END')
                guest.expect(r"\r*\nGUI_DIAGNOSTICS_END\r*\n", timeout=35)
                raise RuntimeError(f"Thunar doubleclick did not open visible original Mousepad: alive={alive} changed_pixels={delta}")
            print(f"PASS: GUI {phase}: original Mousepad opened via Thunar physical double-click, pixels={delta}", flush=True)
            if phase == "edit":
                # No serial commands edit the file. Focus the actual GTK editor,
                # select document contents, type fresh data, and Ctrl+S.
                move_to(240, 180)
                click()
                time.sleep(0.6)
                chord("ctrl", "a")
                type_text("naseed gui roundtrip\n")
                chord("ctrl", "s")
                time.sleep(1.3)
                scanout("editor-saved")
                guest.sendline(
                    'for i in $(seq 1 18); do '
                    'test "$(cat /root/desktop/documents/note.txt)" = '
                    '"naseed gui roundtrip" && break; sleep 1; done; '
                    'if test "$(cat /root/desktop/documents/note.txt)" = '
                    '"naseed gui roundtrip"; then echo GUI_SAVED; else echo GUI_NOT_SAVED; fi'
                )
                if guest.expect([r"\r*\nGUI_SAVED\r*\n",
                                 r"\r*\nGUI_NOT_SAVED\r*\n"], timeout=60):
                    raise RuntimeError("Mousepad GUI Ctrl+S did not persist typed content")
                guest.expect(r"# ")
                print("PASS: physical keyboard edited and saved file in original GTK Mousepad", flush=True)
            else:
                # The previous edit is verified from an offline ext4 image before
                # this boot. This new Mousepad window must be opened by GUI again.
                guest.sendline('if test "$(cat /root/desktop/documents/note.txt)" = "naseed gui roundtrip"; then echo GUI_REOPENED_DATA; else echo GUI_REOPENED_BAD_DATA; fi')
                if guest.expect([r"\r*\nGUI_REOPENED_DATA\r*\n",
                                 r"\r*\nGUI_REOPENED_BAD_DATA\r*\n"], timeout=35):
                    raise RuntimeError("reboot lost saved GUI file content")
                guest.expect(r"# ")
                print("PASS: second original Alpine boot reopened the persisted file through Thunar GUI", flush=True)
            chord("ctrl", "w")  # Close the original editor through the real UI.
            time.sleep(1.0)
            scanout("editor-closed")
        guest.sendline("poweroff")
        guest.expect(pexpect.EOF, timeout=200)
        guest.close()
        assert guest.exitstatus == 1, (guest.exitstatus, guest.signalstatus)
    finally:
        if guest.isalive():
            guest.close(force=True)
serial = (work / f"gui-{phase}.log").read_text(errors="replace")
for marker in ("[M191 ROOT] original Alpine PID1 from mounted ext4: /sbin/init",
               "[ALPINE POWER_OFF] original BusyBox; durable ext4 unmount",
               "[PASS] cmdline userspace exit 0"):
    assert marker in serial, marker
assert "[FAIL]" not in serial
print(f"PASS: GUI-only original Alpine desktop {phase} boot and clean poweroff", flush=True)
