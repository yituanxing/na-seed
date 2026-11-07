#!/usr/bin/env python3
"""Inject actual serial input then independently screenshot three QEMU frames.

No guest-side success token substitutes for pixel verification. This watcher
never edits guest memory or fabricates an input event: it writes to the FIFO
that backs QEMU's real UART / OpenSBI console character device.
"""
import argparse
import json
import os
from pathlib import Path
import runpy
import socket
import sys
import time

qmp = runpy.run_path('scripts/m161-qmp-verify.py', run_name='m164_qmp_library')
first = qmp['SAMPLES']
last = ((32, 0, (255, 255, 255)),
        (160, 0, (0, 0, 255)),
        (288, 0, (0, 255, 0)),
        (416, 0, (255, 0, 0)))


def wait_marker(serial, marker, deadline):
    raw_marker = marker.encode('ascii')
    while time.monotonic() < deadline:
        if serial.is_file():
            log = serial.read_bytes()
            if b'[FAIL]' in log or b'[M164 APP FAIL]' in log:
                raise RuntimeError(f'guest error before {marker}')
            count = log.count(raw_marker)
            if count > 1:
                raise RuntimeError(f'duplicate guest marker {marker}')
            if count == 1:
                return
        time.sleep(0.1)
    raise RuntimeError(f'timed out awaiting genuine UART guest marker {marker}')


def connect_qmp(path, deadline):
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.settimeout(2)
    while time.monotonic() < deadline:
        try:
            sock.connect(str(path))
            break
        except (ConnectionRefusedError, FileNotFoundError):
            time.sleep(0.1)
    else:
        raise RuntimeError('real QEMU QMP socket not available')
    reader = sock.makefile('rb')
    greeting = qmp['read_json'](reader, time.monotonic() + 10)
    if 'QMP' not in greeting:
        raise RuntimeError('missing QMP greeting')
    qmp['qmp_command'](sock, reader, 'qmp_capabilities')
    return sock, reader


def visible_frame(sock, reader, destination, samples, label):
    qmp['qmp_command'](sock, reader, 'screendump', {
        'filename': str(destination.resolve())})
    if not destination.is_file():
        raise RuntimeError(f'QMP did not produce {destination}')
    qmp['verify_ppm'](destination, label, samples)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--fifo', required=True, type=Path)
    parser.add_argument('--serial', required=True, type=Path)
    parser.add_argument('--qmp', required=True, type=Path)
    parser.add_argument('--outdir', required=True, type=Path)
    parser.add_argument('--timeout', type=int, default=700)
    args = parser.parse_args()
    if not args.fifo.is_fifo():
        raise RuntimeError('test serial input path is not a genuine FIFO')
    deadline = time.monotonic() + args.timeout
    # RDWR is a Linux FIFO keepalive. QEMU remains the only reader; this
    # process writes exclusively after the matching physical guest output.
    fd = os.open(args.fifo, os.O_RDWR | os.O_NONBLOCK)
    try:
        wait_marker(args.serial, 'M164_FRAME_A_VISIBLE', deadline)
        sock, reader = connect_qmp(args.qmp, deadline)
        with sock:
            visible_frame(sock, reader, args.outdir / 'm164-first.ppm', first, 'M164 A')
            os.write(fd, b'wrong\r')
            wait_marker(args.serial, 'M164_INVALID_INPUT_REJECTED', deadline)
            visible_frame(sock, reader, args.outdir / 'm164-unchanged.ppm', first, 'M164 unchanged')
            os.write(fd, b'flip\r')
            wait_marker(args.serial, 'M164_FRAME_B_VISIBLE', deadline)
            visible_frame(sock, reader, args.outdir / 'm164-flipped.ppm', last, 'M164 B')
            os.write(fd, b'quit\r')
            wait_marker(args.serial, 'M164_INTERACTIVE_PASS', deadline)
    finally:
        os.close(fd)
    print('[M164 SERIAL->QMP PASS] three genuine UART commands, '
          'rejected invalid input, three independent hardware screenshots', flush=True)


if __name__ == '__main__':
    try:
        main()
    except Exception as exc:
        print(f'[M164 SERIAL/QMP FAIL] {exc}', file=sys.stderr, flush=True)
        raise SystemExit(1)
