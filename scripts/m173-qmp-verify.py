#!/usr/bin/env python3
"""Real QMP keyboard -> BusyBox PTY -> independent LVGL -> five/four pixels.

Serial markers are synchronization edges only.  The external GUI's visual
existence, actual button repaint and isolated close must be proved by QEMU's
physical 800x600 screendump bytes, never by guest-written PASS strings.
"""
import argparse
from pathlib import Path
import runpy
import sys
import time

base = runpy.run_path('scripts/m167-qmp-verify.py', run_name='m173_library')
qmp = base['qmp']
BG = (16, 32, 48)
MAGENTA = (255, 0, 255)
BROWSER = (34, 95, 72)
TITLE = (240, 168, 40)
INACTIVE_TITLE = (64, 80, 96)


def pixels(path):
    tokens, data = qmp['ppm_tokens'](path.read_bytes())
    if tokens != [b'P6', b'800', b'600', b'255'] or len(data) != 800 * 600 * 3:
        raise RuntimeError(f'not an actual 800x600 physical QEMU display: {path}')
    return data


def pixel(data, x, y):
    offset = (y * 800 + x) * 3
    return tuple(data[offset:offset + 3])


def screenshot(sock, reader, outdir, name, samples):
    dst = outdir / f'm173-{name}.ppm'
    end = time.monotonic() + 12
    while True:
        dst.unlink(missing_ok=True)
        qmp['qmp_command'](sock, reader, 'screendump',
                           {'filename': str(dst.resolve())})
        try:
            data = pixels(dst)
            for x, y, expected in samples:
                actual = pixel(data, x, y)
                if actual != expected:
                    raise RuntimeError(f'{name} pixel ({x},{y}) {actual} != {expected}')
            return dst
        except RuntimeError:
            if time.monotonic() >= end:
                raise
            time.sleep(.2)


def changed(first, second, left, top, right, bottom, minimum):
    a, b = pixels(first), pixels(second)
    differences = 0
    for y in range(top, bottom):
        start = (y * 800 + left) * 3
        end = (y * 800 + right) * 3
        differences += sum(x != z for x, z in zip(a[start:end], b[start:end]))
    if differences < minimum:
        raise RuntimeError(f'visible changed pixels {differences} < {minimum} '
                           f'in ROI ({left},{top})-({right},{bottom})')
    return differences


def fifth_gui(path):
    data = pixels(path)
    colors = set()
    bright = 0
    for y in range(414, 555, 3):
        for x in range(550, 760, 3):
            c = pixel(data, x, y)
            colors.add(c)
            if min(c) > 215:
                bright += 1
    if len(colors) < 7 or bright < 5:
        raise RuntimeError(f'no real second LVGL GUI glyphs and button: '
                           f'{len(colors)} colors, {bright} bright samples')


def key(sock, reader, code):
    base['send'](sock, reader, [base['key'](code, True)])
    base['send'](sock, reader, [base['key'](code, False)])
    time.sleep(.14)


def pointer_click(sock, reader, x, y):
    base['send'](sock, reader, base['pointer'](x, y))
    time.sleep(.65)
    base['send'](sock, reader, [base['button'](True)])


def pointer_release(sock, reader):
    base['send'](sock, reader, [base['button'](False)])
    time.sleep(.25)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--serial', required=True, type=Path)
    ap.add_argument('--qmp', required=True, type=Path)
    ap.add_argument('--outdir', required=True, type=Path)
    ap.add_argument('--timeout', type=int, default=700)
    args = ap.parse_args()
    deadline = time.monotonic() + args.timeout
    args.outdir.mkdir(parents=True, exist_ok=True)
    base['wait_marker'](args.serial, 'M173_DESKTOP_INITIAL_VISIBLE', deadline)
    sock, reader = base['connect'](args.qmp, deadline)
    with sock:
        four = screenshot(sock, reader, args.outdir, 'four',
                          ((20, 20, BG), (100, 150, MAGENTA),
                           (700, 250, BROWSER), (160, 371, TITLE),
                           (700, 530, BG)))

        # The fourth *existing* client's static startup is not the proof.
        # Focus the already-live independent PTY and type an ordinary shell
        # command.  Only the shell can exec the fifth app.  Its /usr/bin/gui
        # name is a filesystem alias, never a compositor role or launch ID.
        pointer_click(sock, reader, 500, 340)
        pointer_release(sock, reader)
        for code in ('g', 'u', 'i', 'ret'):
            key(sock, reader, code)
        base['wait_marker'](args.serial, 'M173_EXTERNAL_CLIENT_VISIBLE', deadline)
        five = screenshot(sock, reader, args.outdir, 'five',
                          ((20, 20, BG), (100, 150, MAGENTA),
                           (160, 371, INACTIVE_TITLE),
                           (650, 376, TITLE)))
        fifth_gui(five)
        changed(four, five, 640, 414, 774, 568, 3000)

        # A physical VirtIO-tablet move and left-button press/release pass
        # through the compositor into the *external* upstream-LVGL client.
        pointer_click(sock, reader, 650, 485)
        pointer_release(sock, reader)
        base['wait_marker'](args.serial,
                            'M173_SECOND_LVGL_BUTTON_VISIBLE', deadline)
        clicked = screenshot(sock, reader, args.outdir, 'clicked',
                             ((20, 20, BG), (100, 150, MAGENTA)))
        fifth_gui(clicked)
        changed(five, clicked, 570, 440, 750, 545, 300)

        # Close only the external GUI by clicking its own titlebar.  It was
        # launched from the shell, not parented by the compositor; existing
        # four clients and the shell must remain usable after socket EOF.
        pointer_click(sock, reader, 765, 376)
        base['wait_marker'](args.serial,
                            'M173_EXTERNAL_CLIENT_CLOSED_VISIBLE', deadline)
        closed = screenshot(sock, reader, args.outdir, 'closed',
                            ((20, 20, BG), (100, 150, MAGENTA),
                             (700, 250, BROWSER),
                             (160, 371, INACTIVE_TITLE),
                             (700, 530, BG)))
        pointer_release(sock, reader)
        changed(five, closed, 640, 414, 774, 568, 3000)

        # Re-focus the live terminal and execute a second shell command;
        # changed *physical* terminal glyph pixels prove the PTY survived.
        pointer_click(sock, reader, 500, 340)
        pointer_release(sock, reader)
        for code in ('e', 'c', 'h', 'o', 'spc', '3', 'ret'):
            key(sock, reader, code)
        time.sleep(1.3)
        after = screenshot(sock, reader, args.outdir, 'shell-after',
                           ((20, 20, BG), (100, 150, MAGENTA),
                            (700, 530, BG)))
        changed(closed, after, 310, 200, 615, 390, 30)
        # Q-down exits the compositor; a later release would race QEMU's
        # shutdown and can spuriously fail after the actual guest PASS.
        base['send'](sock, reader, [base['key']('q', True)])
        base['wait_marker'](args.serial, 'M173_DESKTOP_PASS', deadline)

    print('[M173 QMP DYNAMIC PASS] real BusyBox PTY command launches external '
          'LVGL, four-to-five-to-four physical QEMU frames, independent '
          'button repaint, closing client, and surviving original shell',
          flush=True)


if __name__ == '__main__':
    try:
        main()
    except Exception as exc:
        print('[M173 QMP FAIL]', exc, file=sys.stderr, flush=True)
        raise SystemExit(1)
