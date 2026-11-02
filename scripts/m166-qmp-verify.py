#!/usr/bin/env python3
"""Send real QEMU keyboard/tablet/mouse hardware events and verify pixels.

The only inputs originate from QMP input-send-event, which QEMU delivers to
its attached VirtIO devices.  The guest application has no test-only syscall
and cannot supply its own PASS verdict: every transition needs a matching
actual QEMU screendump with four exact physical framebuffer RGB samples.
"""

import argparse
from pathlib import Path
import runpy
import socket
import sys
import time

qmp = runpy.run_path('scripts/m161-qmp-verify.py', run_name='m166_qmp_library')

FRAMES = {
    'initial': ((255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255)),
    'key': ((255, 255, 255), (0, 255, 0), (0, 0, 255), (255, 255, 255)),
    'abs': ((255, 255, 255), (255, 0, 255), (0, 0, 255), (255, 255, 255)),
    'click': ((255, 255, 255), (255, 0, 255), (255, 255, 0), (255, 255, 255)),
    'rel': ((255, 255, 255), (255, 0, 255), (255, 255, 0), (0, 255, 255)),
}

MARKERS = {
    'initial': 'M166_FRAME_INITIAL_VISIBLE',
    'key': 'M166_FRAME_KEY_VISIBLE',
    'abs': 'M166_FRAME_ABS_VISIBLE',
    'click': 'M166_FRAME_CLICK_VISIBLE',
    'rel': 'M166_FRAME_REL_VISIBLE',
}


def wait_marker(serial: Path, marker: str, deadline: float):
    encoded = marker.encode('ascii')
    while time.monotonic() < deadline:
        if serial.is_file():
            raw = serial.read_bytes()
            if b'[M166 APP FAIL]' in raw or b'[FAIL]' in raw:
                raise RuntimeError(f'guest failed before {marker}')
            seen = raw.count(encoded)
            if seen > 1:
                raise RuntimeError(f'duplicate guest marker: {marker}')
            if seen == 1:
                return
        time.sleep(0.1)
    raise RuntimeError(f'timed out waiting for physical guest event {marker}')


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
        raise RuntimeError('real QEMU QMP socket unavailable')
    reader = sock.makefile('rb')
    if 'QMP' not in qmp['read_json'](reader, time.monotonic() + 10):
        raise RuntimeError('missing QEMU QMP greeting')
    qmp['qmp_command'](sock, reader, 'qmp_capabilities')
    return sock, reader


def check_frame(sock, reader, outdir: Path, label: str):
    destination = outdir / f'm166-{label}.ppm'
    samples = tuple((x, 0, color) for x, color in
                    zip((32, 160, 288, 416), FRAMES[label]))
    # RAMFB display scanout is asynchronously refreshed after guest RAM
    # writes. Independently sample QEMU's actual surface until it catches
    # up, without using the guest marker as pixel evidence or accepting a
    # synthesized output image. A stable bad frame still fails the gate.
    deadline = time.monotonic() + 8
    while True:
        destination.unlink(missing_ok=True)
        qmp['qmp_command'](sock, reader, 'screendump',
                           {'filename': str(destination.resolve())})
        if not destination.is_file():
            raise RuntimeError(f'physical QEMU did not create {destination}')
        try:
            qmp['verify_ppm'](destination, f'M166 {label}', samples)
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


def move(kind: str, axis: str, value: int):
    return {'type': kind, 'data': {'axis': axis, 'value': value}}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--serial', type=Path, required=True)
    ap.add_argument('--qmp', type=Path, required=True)
    ap.add_argument('--outdir', type=Path, required=True)
    ap.add_argument('--timeout', type=int, default=700)
    args = ap.parse_args()
    deadline = time.monotonic() + args.timeout
    args.outdir.mkdir(parents=True, exist_ok=True)

    wait_marker(args.serial, 'M166_THREE_INPUT_DEVICES_PASS', deadline)
    wait_marker(args.serial, MARKERS['initial'], deadline)
    sock, reader = connect(args.qmp, deadline)
    with sock:
        check_frame(sock, reader, args.outdir, 'initial')
        send(sock, reader, [key('a', True)])
        wait_marker(args.serial, 'M166_SIGNED_EVTEST_DOWN_PASS', deadline)
        wait_marker(args.serial, MARKERS['key'], deadline)
        check_frame(sock, reader, args.outdir, 'key')
        send(sock, reader, [key('a', False)])
        wait_marker(args.serial, 'M166_SIGNED_EVTEST_UP_PASS', deadline)

        # QMP absolute axes are normalized to [0, 0x7fff].  The guest must
        # receive them as EV_ABS + EV_SYN on a true VirtIO-Tablet inputq.
        send(sock, reader, [move('abs', 'x', 20000),
                            move('abs', 'y', 18000)])
        wait_marker(args.serial, MARKERS['abs'], deadline)
        check_frame(sock, reader, args.outdir, 'abs')

        send(sock, reader, [button(True)])
        wait_marker(args.serial, MARKERS['click'], deadline)
        check_frame(sock, reader, args.outdir, 'click')
        send(sock, reader, [button(False)])

        send(sock, reader, [move('rel', 'x', 21), move('rel', 'y', 17)])
        wait_marker(args.serial, MARKERS['rel'], deadline)
        check_frame(sock, reader, args.outdir, 'rel')

        send(sock, reader, [key('q', True)])
        wait_marker(args.serial, 'M166_REAL_EVDEV_DISPLAY_PASS', deadline)
        send(sock, reader, [key('q', False)])
    print('[M166 INPUT->QMP PASS] QEMU keyboard, tablet absolute/button, '
          'relative mouse through actual Linux evdev into five independently '
          'sampled framebuffer images', flush=True)


if __name__ == '__main__':
    try:
        main()
    except Exception as exc:
        print(f'[M166 QMP INPUT FAIL] {exc}', file=sys.stderr, flush=True)
        raise SystemExit(1)
