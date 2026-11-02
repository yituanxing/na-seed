#!/usr/bin/env python3
"""Independent physical-QEMU oracle for two real userspace desktop clients.

Input is injected only through QEMU VirtIO keyboard/tablet hardware.  The
desktop application's serial markers are synchronization edges, never pixel
evidence: five independent screendumps must match the expected composition.
"""

import argparse
from pathlib import Path
import runpy
import socket
import sys
import time

qmp = runpy.run_path('scripts/m161-qmp-verify.py',
                     run_name='m167_qmp_library')

BG = (16, 32, 48)
IMAGE = (255, 0, 255)
TERM = (20, 68, 204)
TERM_KEY = (255, 255, 0)

# Samples intentionally cover background, each independent client surface,
# and the true overlapping region whose ownership changes on focus/raise.
FRAMES = {
    'initial': ((20, 20, BG), (100, 150, IMAGE),
                (340, 210, TERM), (550, 240, TERM)),
    'image-focus': ((20, 20, BG), (100, 150, IMAGE),
                    (340, 210, IMAGE), (550, 240, TERM)),
    'image-drag': ((20, 20, BG), (100, 150, BG),
                   (230, 170, IMAGE), (340, 210, IMAGE)),
    'terminal-focus': ((20, 20, BG), (230, 170, IMAGE),
                       (340, 210, TERM), (550, 240, TERM)),
    'terminal-key': ((20, 20, BG), (230, 170, IMAGE),
                     (340, 210, TERM_KEY), (550, 240, TERM_KEY)),
}

MARKERS = {
    'initial': 'M167_DESKTOP_INITIAL_VISIBLE',
    'image-focus': 'M167_IMAGE_FOCUS_VISIBLE',
    'image-drag': 'M167_IMAGE_MOVED_VISIBLE',
    'terminal-focus': 'M167_TERMINAL_FOCUS_VISIBLE',
    'terminal-key': 'M167_TERMINAL_KEY_VISIBLE',
}


def wait_marker(serial: Path, marker: str, deadline: float):
    token = marker.encode('ascii')
    while time.monotonic() < deadline:
        if serial.is_file():
            raw = serial.read_bytes()
            if b'[M167 APP FAIL]' in raw or b'[FAIL]' in raw:
                raise RuntimeError(f'guest failed before {marker}')
            if raw.count(token) > 1:
                raise RuntimeError(f'guest duplicated {marker}')
            if token in raw:
                return
        time.sleep(0.1)
    raise RuntimeError(f'timed out waiting for native desktop {marker}')


def connect(path: Path, deadline: float):
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.settimeout(2)
    while time.monotonic() < deadline:
        try:
            sock.connect(str(path))
            break
        except (ConnectionRefusedError, FileNotFoundError):
            time.sleep(0.1)
    else:
        sock.close()
        raise RuntimeError('QEMU QMP socket unavailable')
    reader = sock.makefile('rb')
    if 'QMP' not in qmp['read_json'](reader, time.monotonic() + 10):
        raise RuntimeError('missing QMP greeting')
    qmp['qmp_command'](sock, reader, 'qmp_capabilities')
    return sock, reader


def check_frame(sock, reader, outdir: Path, label: str):
    destination = outdir / f'm167-{label}.ppm'
    deadline = time.monotonic() + 8
    while True:
        destination.unlink(missing_ok=True)
        qmp['qmp_command'](sock, reader, 'screendump',
                           {'filename': str(destination.resolve())})
        if not destination.is_file():
            raise RuntimeError(f'QEMU did not produce {destination}')
        try:
            tokens, _ = qmp['ppm_tokens'](destination.read_bytes())
            if tokens[1:3] != [b'800', b'600']:
                raise RuntimeError(f'not actual 800x600 desktop scanout: {tokens}')
            qmp['verify_ppm'](destination, f'M167 {label}', FRAMES[label])
            return
        except RuntimeError:
            if time.monotonic() >= deadline:
                raise
            time.sleep(0.15)


def send(sock, reader, events):
    qmp['qmp_command'](sock, reader, 'input-send-event', {'events': events})


def key(name: str, down: bool):
    return {'type': 'key', 'data': {
        'down': down, 'key': {'type': 'qcode', 'data': name}}}


def button(down: bool):
    return {'type': 'btn', 'data': {'button': 'left', 'down': down}}


def pointer(x: int, y: int):
    # QMP normalizes absolute coordinates to 0..32767, independent of the
    # guest's 800x600 display.  Stay inside wide solid-color pixel areas.
    if not (0 <= x < 800 and 0 <= y < 600):
        raise ValueError('pointer outside 800x600 guest surface')
    return [
        {'type': 'abs', 'data': {'axis': 'x',
                                 'value': (x * 32767 + 399) // 799}},
        {'type': 'abs', 'data': {'axis': 'y',
                                 'value': (y * 32767 + 299) // 599}},
    ]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--serial', type=Path, required=True)
    ap.add_argument('--qmp', type=Path, required=True)
    ap.add_argument('--outdir', type=Path, required=True)
    ap.add_argument('--timeout', type=int, default=700)
    args = ap.parse_args()
    deadline = time.monotonic() + args.timeout
    args.outdir.mkdir(parents=True, exist_ok=True)

    wait_marker(args.serial, MARKERS['initial'], deadline)
    sock, reader = connect(args.qmp, deadline)
    with sock:
        check_frame(sock, reader, args.outdir, 'initial')

        # Raise the image window by clicking body pixels (no titlebar drag).
        send(sock, reader, pointer(150, 150))
        # QEMU's absolute tablet and BTN_LEFT can arrive through different
        # evdev devices, without a cross-device delivery-order guarantee.
        # Model a physical move-then-click with a short settle interval.
        time.sleep(0.6)
        send(sock, reader, [button(True)])
        wait_marker(args.serial, MARKERS['image-focus'], deadline)
        check_frame(sock, reader, args.outdir, 'image-focus')
        send(sock, reader, [button(False)])
        time.sleep(0.6)

        # Press titlebar, move with hardware button held, release at (240,120).
        send(sock, reader, pointer(150, 90))
        time.sleep(0.6)
        send(sock, reader, [button(True)])
        wait_marker(args.serial, 'M167_IMAGE_DRAG_GRABBED', deadline)
        send(sock, reader, pointer(240, 120))
        time.sleep(0.6)
        send(sock, reader, [button(False)])
        wait_marker(args.serial, MARKERS['image-drag'], deadline)
        check_frame(sock, reader, args.outdir, 'image-drag')

        # Only the terminal receives this focused key, through its real
        # client-process IPC; no direct framebuffer change is authorized.
        send(sock, reader, pointer(550, 220))
        time.sleep(0.6)
        send(sock, reader, [button(True)])
        wait_marker(args.serial, MARKERS['terminal-focus'], deadline)
        check_frame(sock, reader, args.outdir, 'terminal-focus')
        send(sock, reader, [button(False)])
        time.sleep(0.6)
        send(sock, reader, [key('a', True)])
        wait_marker(args.serial, MARKERS['terminal-key'], deadline)
        check_frame(sock, reader, args.outdir, 'terminal-key')
        send(sock, reader, [key('a', False)])

        # The native application exits after Q down.  It is incorrect to
        # inject the release after its PASS marker: QEMU may already exit.
        send(sock, reader, [key('q', True)])
        wait_marker(args.serial, 'M167_DESKTOP_PASS', deadline)

    print('[M167 QMP DESKTOP PASS] native guest: actual keyboard/tablet '
          'input, independent image and terminal processes, AF_UNIX/memfd '
          'surfaces, five QEMU framebuffer screenshots', flush=True)


if __name__ == '__main__':
    try:
        main()
    except Exception as exc:
        print(f'[M167 QMP DESKTOP FAIL] {exc}', file=sys.stderr, flush=True)
        raise SystemExit(1)
