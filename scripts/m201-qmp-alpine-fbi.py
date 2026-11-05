#!/usr/bin/env python3
"""Independently prove original signed Alpine fbi pixels on real QEMU QMP.

The guest prints only an invocation marker. Acceptance requires four exact
physical 800x600 framebuffer pixels from QEMU, during the original fbi run.
Never accept guest stdout, an input PNG or a synthetic framebuffer as proof.
"""

import argparse
from pathlib import Path
import runpy
import sys
import time


m200 = runpy.run_path('scripts/m200-qmp-verify.py', run_name='m201_reused_m200_qmp')
qmp = m200['qmp']
SAMPLES = ((100, 300, (255, 0, 0)),
           (300, 300, (0, 255, 0)),
           (500, 300, (0, 0, 255)),
           (700, 300, (255, 255, 255)))


def exact_physical_pixels(frame: Path) -> None:
    tokens, pixels = qmp['ppm_tokens'](frame.read_bytes())
    if tokens != [b'P6', b'800', b'600', b'255']:
        raise RuntimeError(f'not real QEMU 800x600 RGB PPM: {tokens!r}')
    if len(pixels) != 800 * 600 * 3:
        raise RuntimeError(f'QEMU scanout plane malformed: {len(pixels)} bytes')
    for x, y, expected in SAMPLES:
        idx = (y * 800 + x) * 3
        observed = tuple(pixels[idx:idx + 3])
        if observed != expected:
            raise RuntimeError(f'QEMU pixel ({x},{y}) {observed} != {expected}')


def run(args: argparse.Namespace) -> None:
    if args.success_file.exists():
        raise RuntimeError('refusing stale prior physical-QMP success sentinel')
    deadline = time.monotonic() + args.timeout
    sock, reader = m200['connect'](args.qmp, deadline)
    failures = 0
    last_error = ''
    try:
        # Prepare QMP *before* original fbi launches. A short real fbi
        # slideshow interval is enough when screenshots start immediately.
        while time.monotonic() < deadline:
            serial = args.serial.read_bytes() if args.serial.is_file() else b''
            if b'[FAIL]' in serial:
                raise RuntimeError('real guest reported kernel/boot failure')
            if b'M201_FBI_START\r\n' in serial or b'M201_FBI_START\n' in serial:
                break
            time.sleep(0.075)
        else:
            raise RuntimeError('original authenticated Alpine ash never invoked fbi')

        # One original full-screen 800x600 PNG, four independent exact RGB
        # pixels; no dependence on the guest declaring its own picture pass.
        args.screenshot.parent.mkdir(parents=True, exist_ok=True)
        while time.monotonic() < deadline:
            args.screenshot.unlink(missing_ok=True)
            qmp['qmp_command'](sock, reader, 'screendump',
                               {'filename': str(args.screenshot.resolve())})
            if not args.screenshot.is_file():
                raise RuntimeError('QEMU did not create physical screendump')
            try:
                exact_physical_pixels(args.screenshot)
                args.success_file.write_text('M201_ORIGINAL_FBI_QMP_PHYSICAL_PASS\n')
                print(f'[M201 QMP PHYSICAL PIXEL PASS] {args.port} original '
                      'signed Alpine fbida-fbi rendered 800x600 native PNG; '
                      'four independent exact QEMU RGB scanout samples', flush=True)
                return
            except RuntimeError as error:
                last_error = str(error)
                failures += 1
                # The genuine original fbi --nointeractive -t 15 -1 exits
                # once its single 15s slide has displayed; screen evidence
                # MUST be captured before this exit/cleanup.
                serial = args.serial.read_bytes() if args.serial.is_file() else b''
                if b'M201_FBI_EXEC_RC=' in serial:
                    raise RuntimeError('original fbi exited before QMP pixel '
                                       f'witness; {last_error}')
                time.sleep(0.10)
        raise RuntimeError(f'physical QEMU pixel timeout after {failures} samples; '
                           f'last sample: {last_error}')
    finally:
        reader.close()
        sock.close()


def main() -> None:
    cli = argparse.ArgumentParser()
    cli.add_argument('--port', choices=('amd64', 'riscv64', 'arm64'), required=True)
    cli.add_argument('--serial', required=True, type=Path)
    cli.add_argument('--qmp', required=True, type=Path)
    cli.add_argument('--screenshot', required=True, type=Path)
    cli.add_argument('--success-file', required=True, type=Path)
    cli.add_argument('--failure-file', required=True, type=Path)
    cli.add_argument('--timeout', type=int, default=180)
    args = cli.parse_args()
    try:
        run(args)
    except Exception as exc:
        args.failure_file.write_text(f'{exc}\n')
        print(f'[M201 QMP PHYSICAL PIXEL FAIL] {exc}', file=sys.stderr,
              flush=True)
        raise SystemExit(1)


if __name__ == '__main__':
    main()
