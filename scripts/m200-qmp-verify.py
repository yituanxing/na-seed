#!/usr/bin/env python3
"""Independent M200 800x600 VirtIO-GPU framebuffer mmap pixel oracle.

The guest's success line is only a synchronization trigger. Every required
color is sampled from a real system-QEMU QMP screendump, not from guest
stdio, kernel memory, a fixture PPM, or a host reconstruction of pixels.
"""

import argparse
from pathlib import Path
import runpy
import socket
import sys
import time


qmp = runpy.run_path('scripts/m161-qmp-verify.py', run_name='m200_qmp_lib')
SAMPLES = ((32, 20, (255, 0, 0)),
           (160, 20, (0, 255, 0)),
           (288, 20, (0, 0, 255)),
           (416, 20, (255, 255, 255)))


def wait_for_drawn(serial, deadline):
    while time.monotonic() < deadline:
        if serial.is_file():
            data = serial.read_bytes()
            if b'[M200 FBDEV MMAP FAIL]' in data or b'[FAIL]' in data:
                raise RuntimeError('guest reported a real failure before display')
            if data.count(b'M200_SCREEN_DRAWN') == 1:
                if b'M200_FBIOGET_AND_SHARED_MMAP_PASS' not in data or \
                   b'M200_DEVICE_FORK_AND_FD_CLOSE_PASS' not in data:
                    raise RuntimeError('missing mmap/fork/fd-close guest evidence')
                return
        time.sleep(0.1)
    raise RuntimeError('guest never reached MAP_SHARED framebuffer draw')


def connect(path, deadline):
    while time.monotonic() < deadline:
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.settimeout(2)
        try:
            sock.connect(str(path))
        except (FileNotFoundError, ConnectionRefusedError, OSError):
            sock.close()
            time.sleep(0.1)
            continue
        reader = sock.makefile('rb')
        greeting = qmp['read_json'](reader, time.monotonic()+10)
        if 'QMP' not in greeting:
            sock.close()
            raise RuntimeError('QMP greeting absent')
        qmp['qmp_command'](sock, reader, 'qmp_capabilities')
        return sock, reader
    raise RuntimeError('real QMP socket never became available')


def check_pixels(path):
    tokens, pixels = qmp['ppm_tokens'](path.read_bytes())
    if tokens != [b'P6', b'800', b'600', b'255']:
        raise RuntimeError(f'not actual native 800x600 QEMU PPM: {tokens!r}')
    if len(pixels) != 800*600*3:
        raise RuntimeError('QMP PPM pixel plane has invalid size')
    for x, y, expected in SAMPLES:
        at = (y*800+x)*3
        actual = tuple(pixels[at:at+3])
        if actual != expected:
            raise RuntimeError(f'QMP pixel ({x},{y}) {actual} != {expected}')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', required=True,
                        choices=('amd64', 'riscv64', 'arm64'))
    parser.add_argument('--serial', required=True, type=Path)
    parser.add_argument('--qmp', required=True, type=Path)
    parser.add_argument('--screenshot', required=True, type=Path)
    parser.add_argument('--timeout', type=int, default=90)
    args = parser.parse_args()
    deadline = time.monotonic() + args.timeout
    wait_for_drawn(args.serial, deadline)
    sock, reader = connect(args.qmp, deadline)
    try:
        end = min(deadline, time.monotonic()+8)
        last_error = None
        while time.monotonic() < end:
            args.screenshot.unlink(missing_ok=True)
            qmp['qmp_command'](sock, reader, 'screendump',
                               {'filename': str(args.screenshot.resolve())})
            if not args.screenshot.is_file():
                raise RuntimeError('QEMU did not create screendump')
            try:
                check_pixels(args.screenshot)
                print(f'[M200 QMP PHYSICAL PIXEL PASS] {args.port} '
                      '800x600 VirtIO-GPU four exact RGB samples from '
                      'MAP_SHARED forked guest bytes', flush=True)
                return
            except RuntimeError as exc:
                last_error = exc
                time.sleep(0.2)
        raise RuntimeError(f'physical display never reached target: {last_error}')
    finally:
        reader.close()
        sock.close()


if __name__ == '__main__':
    try:
        main()
    except Exception as exc:
        print(f'[M200 QMP PHYSICAL PIXEL FAIL] {exc}',
              file=sys.stderr, flush=True)
        raise SystemExit(1)
