#!/usr/bin/env python3
"""Original Alpine interactive fbi: physical two-frame + two-key QMP oracle.

No guest stdout image or host PNG fixture can satisfy either frame: both
are sampled from separate live `screendump` commands on system-QEMU GPU.
"""

import argparse
from pathlib import Path
import runpy
import sys
import time


m201 = runpy.run_path('scripts/m201-qmp-alpine-fbi.py', run_name='m202_m201_lib')
keyboard = runpy.run_path('scripts/m167-qmp-verify.py', run_name='m202_key_lib')
qmp = m201['qmp']

FRAMES = {
    'first': ((100, 300, (255, 0, 0)),
              (300, 300, (0, 255, 0)),
              (500, 300, (0, 0, 255)),
              (700, 300, (255, 255, 255))),
    'second': ((100, 300, (0, 255, 255)),
               (300, 300, (255, 0, 255)),
               (500, 300, (255, 255, 0)),
               (700, 300, (0, 0, 0))),
}


def raw_lines(path: Path) -> list[bytes]:
    return path.read_bytes().splitlines() if path.is_file() else []


def whole_line(lines: list[bytes], value: bytes) -> bool:
    # Original native Alpine shell produces LF, CRLF or CRCRLF. A command
    # echo with quoted 'M202_FBI_START' is not a whole-line event.
    return value in lines


def observed_exit(lines: list[bytes]) -> bool:
    return any(line.startswith(b'M202_FBI_EXEC_RC=') and
               line[len(b'M202_FBI_EXEC_RC='):].isdigit() for line in lines)


def check_physical_frame(screenshot: Path, expected_name: str) -> None:
    tokens, pixels = qmp['ppm_tokens'](screenshot.read_bytes())
    if tokens != [b'P6', b'800', b'600', b'255']:
        raise RuntimeError(f'not an 800x600 physical QEMU PPM: {tokens}')
    if len(pixels) != 800 * 600 * 3:
        raise RuntimeError(f'QEMU physical scanout plane has {len(pixels)} bytes')
    for x, y, expected in FRAMES[expected_name]:
        at = (y * 800 + x) * 3
        actual = tuple(pixels[at:at + 3])
        if actual != expected:
            raise RuntimeError(f'{expected_name}: QEMU ({x},{y}) '
                               f'{actual} != {expected}')


def capture_until(sock, reader, screenshot: Path, expected_name: str,
                  serial: Path, deadline: float) -> None:
    last = 'none'
    count = 0
    screenshot.parent.mkdir(parents=True, exist_ok=True)
    while time.monotonic() < deadline:
        screenshot.unlink(missing_ok=True)
        qmp['qmp_command'](sock, reader, 'screendump',
                           {'filename': str(screenshot.resolve())})
        if not screenshot.is_file():
            raise RuntimeError('real QEMU did not create physical PPM')
        try:
            check_physical_frame(screenshot, expected_name)
            return
        except RuntimeError as exc:
            last = str(exc)
            count += 1
            lines = raw_lines(serial)
            if observed_exit(lines):
                raise RuntimeError('original fbi exited before physical '
                                   f'{expected_name} frame: {last}')
            if b'[FAIL]' in serial.read_bytes():
                raise RuntimeError('real guest kernel reported failure')
            time.sleep(0.12)
    raise RuntimeError(f'physical QMP {expected_name} image timeout after '
                       f'{count} captures; last mismatch: {last}')


def hardware_key(sock, reader, name: str) -> None:
    # Real QMP input goes through QEMU VirtIO keyboard IRQ/input-core/VT.
    keyboard['send'](sock, reader, [keyboard['key'](name, True)])
    time.sleep(0.10)
    keyboard['send'](sock, reader, [keyboard['key'](name, False)])
    time.sleep(0.14)


def run(args: argparse.Namespace) -> None:
    if args.success_file.exists():
        raise RuntimeError('refusing stale physical hardware success sentinel')
    deadline = time.monotonic() + args.timeout
    sock, reader = m201['m200']['connect'](args.qmp, deadline)
    try:
        while time.monotonic() < deadline:
            lines = raw_lines(args.serial)
            if whole_line(lines, b'M202_FBI_START'):
                break
            if b'[FAIL]' in args.serial.read_bytes():
                raise RuntimeError('original Alpine/kernel reported failure')
            time.sleep(.09)
        else:
            raise RuntimeError('original authenticated Alpine shell did not run interactive fbi')

        if observed_exit(raw_lines(args.serial)):
            raise RuntimeError('fbi exited before first live physical framebuffer')
        capture_until(sock, reader, args.first, 'first', args.serial, deadline)
        print(f'[M202 QMP FIRST IMAGE PASS] {args.port}: four exact physical RGB samples',
              flush=True)
        if observed_exit(raw_lines(args.serial)):
            raise RuntimeError('fbi exited before QMP hardware next-image key')
        hardware_key(sock, reader, 'spc')
        capture_until(sock, reader, args.second, 'second', args.serial, deadline)
        print(f'[M202 QMP SECOND IMAGE PASS] {args.port}: different independent '
              'QEMU physical RGB pixels after real VirtIO Space key', flush=True)
        if observed_exit(raw_lines(args.serial)):
            raise RuntimeError('fbi exited before QMP hardware quit key')
        hardware_key(sock, reader, 'q')
        while time.monotonic() < deadline:
            lines = raw_lines(args.serial)
            if whole_line(lines, b'M202_FBI_EXEC_RC=0'):
                args.success_file.write_text('M202_ORIGINAL_INTERACTIVE_FBI_QMP_PASS\n')
                print(f'[M202 QMP ORIGINAL INTERACTIVE FBI PASS] {args.port}: '
                      'signed unmodified fbi, 2 exact QMP GPU images, physical '
                      'VirtIO keyboard Space+q, real foreground exit 0', flush=True)
                return
            if observed_exit(lines):
                raise RuntimeError('original fbi failed after QMP physical quit key')
            time.sleep(.11)
        raise RuntimeError('original interactive fbi did not exit after physical QMP q')
    finally:
        reader.close()
        sock.close()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', choices=('amd64', 'riscv64', 'arm64'), required=True)
    parser.add_argument('--serial', required=True, type=Path)
    parser.add_argument('--qmp', required=True, type=Path)
    parser.add_argument('--first', required=True, type=Path)
    parser.add_argument('--second', required=True, type=Path)
    parser.add_argument('--success-file', required=True, type=Path)
    parser.add_argument('--failure-file', required=True, type=Path)
    parser.add_argument('--timeout', type=int, default=260)
    args = parser.parse_args()
    try:
        run(args)
    except Exception as exc:
        args.failure_file.write_text(str(exc) + '\n')
        print('[M202 QMP ORIGINAL INTERACTIVE FBI FAIL]', exc,
              file=sys.stderr, flush=True)
        raise SystemExit(1)


if __name__ == '__main__':
    main()
