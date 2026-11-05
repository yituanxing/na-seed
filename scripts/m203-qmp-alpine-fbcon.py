#!/usr/bin/env python3
"""Physical GPU+keyboard gate for original Alpine text VT and fbi handover.

No serial keystroke injection and no guest-rendered success image: the host
uses QMP VirtIO hardware events and independent real GPU PPM screendumps.
Original Alpine getty/login/ash must execute and physically render glyphs.
"""

import argparse
from pathlib import Path
import runpy
import sys
import time


m202 = runpy.run_path('scripts/m202-qmp-alpine-interactive-fbi.py',
                      run_name='m203_prior_physical_image')
m167 = runpy.run_path('scripts/m167-qmp-verify.py', run_name='m203_input')
qmp = m202['qmp']

W, H = 800, 600
ROWS, COLS = 25, 80
CELL_W, CELL_H = 8, 16
ORIGIN_X, ORIGIN_Y = 80, 100

# Independent physical glyph oracle for the lowercase 'n' in the *original*
# hostname's "naseed login:". This 5x7 bitmap is encoded as 7 integer rows;
# only real framebuffer writes via the new kernel console can satisfy it.
LOWERCASE_N = (0, 0, 22, 25, 17, 17, 17)


def serial_data(path: Path) -> bytes:
    return path.read_bytes() if path.is_file() else b''


def whole_line(raw: bytes, marker: bytes) -> bool:
    return marker in raw.splitlines()


def wait_serial(path: Path, marker: bytes, deadline: float) -> None:
    while time.monotonic() < deadline:
        raw = serial_data(path)
        if whole_line(raw, marker):
            return
        if b'[FAIL]' in raw:
            raise RuntimeError('original guest kernel failed before '+marker.decode())
        time.sleep(.1)
    raise RuntimeError('original Alpine UART witness timed out: '+marker.decode())


def wait_new_exec(path: Path, exe: bytes, original_count: int,
                  deadline: float) -> None:
    tag = b'[EXEC] replaced process with lwext4 ELF: ' + exe
    while time.monotonic() < deadline:
        raw = serial_data(path)
        if raw.count(tag) > original_count:
            return
        if b'[FAIL]' in raw:
            raise RuntimeError('guest failed while invoking '+exe.decode())
        time.sleep(.11)
    raise RuntimeError('original Alpine executable did not launch: '+exe.decode())


def screenshot(sock, reader, outdir: Path, name: str) -> tuple[Path, bytes]:
    outdir.mkdir(parents=True, exist_ok=True)
    path = outdir / ('m203-' + name + '.ppm')
    path.unlink(missing_ok=True)
    qmp['qmp_command'](sock, reader, 'screendump',
                       {'filename': str(path.resolve())})
    if not path.is_file():
        raise RuntimeError('real QEMU did not create a physical framebuffer')
    tokens, pixels = qmp['ppm_tokens'](path.read_bytes())
    if tokens != [b'P6', b'800', b'600', b'255'] or len(pixels) != W * H * 3:
        raise RuntimeError('not a real 800x600 RGB GPU scanout: '+str(tokens))
    return path, pixels


def pixel(pixels: bytes, x: int, y: int) -> tuple[int, int, int]:
    offset = (y * W + x) * 3
    return tuple(pixels[offset:offset + 3])


def has_n_glyph(pixels: bytes) -> bool:
    # Scan actual 80x25 text-cell positions instead of trusting UART text or
    # a single guessed cursor location. Exclude the cursor's inverted cell.
    for row in range(ROWS):
        top = ORIGIN_Y + row * CELL_H
        for col in range(30):
            left = ORIGIN_X + col * CELL_W
            if all(pixel(pixels, left + 1 + gx, top + 1 + 2 * gy) ==
                   ((255, 255, 255) if LOWERCASE_N[gy] & (1 << (4 - gx))
                    else (0, 0, 0))
                   for gy in range(7) for gx in range(5)):
                return True
    return False


def text_pixels(pixels: bytes) -> int:
    total = 0
    for y in range(ORIGIN_Y, ORIGIN_Y + ROWS * CELL_H, 2):
        for x in range(ORIGIN_X, ORIGIN_X + COLS * CELL_W, 2):
            if pixel(pixels, x, y) != (0, 0, 0):
                total += 1
    return total


def changed_pixels(before: bytes, after: bytes) -> int:
    if len(before) != len(after):
        raise RuntimeError('not the same QEMU framebuffer geometry')
    return sum(before[k:k+3] != after[k:k+3]
               for k in range((ORIGIN_Y * W + ORIGIN_X) * 3,
                              ((ORIGIN_Y + ROWS * CELL_H - 1) * W +
                               ORIGIN_X + COLS * CELL_W) * 3, 3))


def capture_until(sock, reader, outdir: Path, name: str,
                  predicate, deadline: float) -> bytes:
    attempts = 0
    last = 'no GPU image'
    while time.monotonic() < deadline:
        _path, data = screenshot(sock, reader, outdir, name)
        attempts += 1
        try:
            if predicate(data):
                return data
        except RuntimeError as exc:
            last = str(exc)
        time.sleep(.2)
    raise RuntimeError(f'real QMP {name} timeout after {attempts} GPU captures: {last}')


def hardware_key(sock, reader, key: str, *, shift: bool = False) -> None:
    if shift:
        m167['send'](sock, reader, [m167['key']('shift', True)])
    m167['send'](sock, reader, [m167['key'](key, True)])
    time.sleep(.055)
    m167['send'](sock, reader, [m167['key'](key, False)])
    if shift:
        m167['send'](sock, reader, [m167['key']('shift', False)])
    time.sleep(.055)


def type_text(sock, reader, value: str) -> None:
    mapping = {
        ' ': ('spc', False), '/': ('slash', False),
        '-': ('minus', False), '.': ('dot', False),
        '>': ('dot', True), '$': ('4', True), '?': ('slash', True),
    }
    for ch in value:
        key, shifted = mapping.get(ch, (ch.lower(), ch.isupper()))
        if not (ch.isalnum() or ch in mapping):
            raise ValueError(f'unsupported physical QMP key {ch!r}')
        hardware_key(sock, reader, key, shift=shifted)


def shell_command(sock, reader, command: str) -> None:
    type_text(sock, reader, command)
    hardware_key(sock, reader, 'ret')


def original_fbi_frame(sock, reader, outdir: Path, label: str,
                       deadline: float) -> bytes:
    last = 'not yet visible'
    while time.monotonic() < deadline:
        path, data = screenshot(sock, reader, outdir, 'fbi-' + label)
        try:
            m202['check_physical_frame'](path, label)
            return data
        except RuntimeError as exc:
            last = str(exc)
        time.sleep(.13)
    raise RuntimeError(f'original signed fbi {label} physical GPU missing: '+last)


def run(args) -> None:
    if args.success_file.exists():
        raise RuntimeError('refusing stale M203 hardware proof')
    deadline = time.monotonic() + args.timeout
    wait_serial(args.serial, b'M203_TTY1_GETTY_RC=0', deadline)
    sock, reader = m202['m201']['m200']['connect'](args.qmp, deadline)
    try:
        getty = capture_until(sock, reader, args.outdir, 'original-getty',
                              lambda p: has_n_glyph(p) and text_pixels(p) > 25,
                              deadline)
        print(f'[M203 PHYSICAL VT GETTY PASS] {args.port}: real original '
              'Alpine graphical login glyphs in physical GPU', flush=True)
        login_count = serial_data(args.serial).count(
            b'[EXEC] replaced process with lwext4 ELF: /bin/login')
        shell_count = serial_data(args.serial).count(
            b'[EXEC] replaced process with lwext4 ELF: /bin/sh')

        shell_command(sock, reader, 'root')
        wait_new_exec(args.serial, b'/bin/login', login_count, deadline)
        password = capture_until(sock, reader, args.outdir, 'password',
                                 lambda p: changed_pixels(getty, p) > 25,
                                 deadline)
        if not text_pixels(password) > 25:
            raise RuntimeError('original graphical login password prompt not visible')
        shell_command(sock, reader, 'naseed-m193-only')
        wait_new_exec(args.serial, b'/bin/sh', shell_count, deadline)
        capture_until(sock, reader, args.outdir, 'original-shell',
                      lambda p: changed_pixels(password, p) > 20,
                      deadline)
        print(f'[M203 ORIGINAL LOGIN PHYSICAL KEYBOARD PASS] {args.port}: '
              'real tty1 getty/login/password/ash, not serial keystrokes', flush=True)

        shell_command(sock, reader,
                      'echo m203realtypedinput > /root/m203-typed-shell.txt')
        wait_serial(args.serial, b'M203_TYPED_FILE_RC=0', deadline)
        signed_count = serial_data(args.serial).count(
            b'[EXEC] replaced process with lwext4 ELF: /usr/bin/fbi')
        shell_command(sock, reader,
                      'fbi --interactive -d /dev/fb0 -t 0 '
                      '/root/m201-four-bars.png /root/m202-second-bars.png')
        wait_new_exec(args.serial, b'/usr/bin/fbi', signed_count, deadline)
        original_fbi_frame(sock, reader, args.outdir, 'first', deadline)
        hardware_key(sock, reader, 'spc')
        original_fbi_frame(sock, reader, args.outdir, 'second', deadline)
        hardware_key(sock, reader, 'q')
        restored = capture_until(sock, reader, args.outdir,
                                 'restored-graphical-shell',
                                 lambda p: has_n_glyph(p) and
                                 changed_pixels(getty, p) > 30,
                                 deadline)
        if text_pixels(restored) <= 25:
            raise RuntimeError('KD_TEXT did not restore VT console')
        print(f'[M203 FBICON KD TEXT RESTORED PASS] {args.port}: '
              'real fbi two images/Space/q and physical VT glyph repaint',
              flush=True)
        # This is the original Alpine shell's *actual* previous exit status.
        # A shell still blocked in fbi cannot accept this physical command.
        shell_command(sock, reader, 'echo $? > /root/m203-fbi-rc.txt')
        shell_command(sock, reader,
                      'echo m203restoredtyped > /root/m203-restored-shell.txt')
        wait_serial(args.serial, b'M203_RESTORED_FILE_RC=0', deadline)
        args.success_file.write_text('M203_FBICON_PHYSICAL_QMP_PASS\n')
        print(f'[M203 ORIGINAL ALPINE FBICON PHYSICAL PASS] {args.port}: '
              'original getty/login/ash, VT text glyphs, native signed '
              'fbi KD_GRAPHICS two frames and q, KD_TEXT restoration, '
              'two real physical keyboard writes to durable ext4', flush=True)
    finally:
        reader.close()
        sock.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', choices=('amd64', 'riscv64', 'arm64'), required=True)
    parser.add_argument('--serial', type=Path, required=True)
    parser.add_argument('--qmp', type=Path, required=True)
    parser.add_argument('--outdir', type=Path, required=True)
    parser.add_argument('--success-file', type=Path, required=True)
    parser.add_argument('--failure-file', type=Path, required=True)
    parser.add_argument('--timeout', type=int, default=350)
    args = parser.parse_args()
    try:
        run(args)
    except Exception as exc:
        args.failure_file.write_text(str(exc)+'\n')
        print('[M203 ORIGINAL ALPINE FBICON QMP FAIL]', exc,
              file=sys.stderr, flush=True)
        raise SystemExit(1)
