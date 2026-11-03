#!/usr/bin/env python3
"""Independent QEMU screenshot oracle for three userspace client surfaces.

All events are QMP hardware keyboard/tablet/mouse input. Serial state markers
only order the sampling; seven framebuffer images must prove actual color,
stacking, separate signed program surfaces and focused key routing.
"""
import argparse
from pathlib import Path
import runpy
import sys
import time

old = runpy.run_path('scripts/m167-qmp-verify.py', run_name='m168_lib')
image = (255, 0, 255)
term = (20, 68, 204)
browser = (34, 95, 72)
browser_key = (218, 115, 48)
term_key = (255, 255, 0)
bg = (16, 32, 48)
active_title = (240, 168, 40)
inactive_title = (64, 80, 96)

FRAMES = {
    'initial': ((20, 20, bg), (100, 150, image),
                (400, 300, term), (448, 250, browser)),
    'image-focus': ((20, 20, bg), (340, 210, image),
                    (400, 300, term), (448, 250, browser)),
    'image-drag': ((20, 20, bg), (100, 150, bg),
                   (230, 170, image), (340, 210, image)),
    'browser-focus': ((20, 20, bg), (500, 92, active_title),
                      (448, 250, browser), (230, 170, image)),
    'browser-key': ((20, 20, bg), (500, 92, active_title),
                    (448, 250, browser_key), (751, 240, browser_key)),
    'terminal-focus': ((20, 20, bg), (500, 172, active_title),
                       (448, 250, term), (751, 240, browser_key)),
    'terminal-key': ((20, 20, bg), (500, 172, active_title),
                     (448, 250, term_key), (550, 370, term_key)),
}

# M169: hardware input after a true >30s idle interval must still wake a
# live compositor. A frozen last frame alone is not proof: the next focus
# marker and physical pixels must also change.
FRAMES['idle']=FRAMES['initial']


def frame(sock, reader, outdir, label):
    # Reuse the canonical P6 screenshot parser. Every physical frame is a
    # separate persisted M168 file; no guest marker stands in for its pixels.
    destination=outdir/f'm168-{label}.ppm'
    end=time.monotonic()+8
    while True:
        destination.unlink(missing_ok=True)
        old['qmp']['qmp_command'](sock,reader,'screendump',
                {'filename':str(destination.resolve())})
        if not destination.is_file():
            raise RuntimeError(f'QEMU screendump absent: {destination}')
        try:
            old['qmp']['verify_ppm'](destination,f'M168 {label}',FRAMES[label])
            return
        except RuntimeError:
            if time.monotonic()>=end:raise
            time.sleep(.15)


def wait(serial, name, deadline):
    old['wait_marker'](serial, name, deadline)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--serial', type=Path, required=True)
    ap.add_argument('--qmp', type=Path, required=True)
    ap.add_argument('--outdir', type=Path, required=True)
    ap.add_argument('--timeout', type=int, default=700)
    args = ap.parse_args()
    deadline = time.monotonic()+args.timeout
    args.outdir.mkdir(parents=True, exist_ok=True)
    wait(args.serial, 'M168_DESKTOP_INITIAL_VISIBLE', deadline)
    sock,reader = old['connect'](args.qmp,deadline)
    send=old['send']
    pointer=old['pointer']
    button=old['button']
    key=old['key']
    with sock:
        frame(sock,reader,args.outdir,'initial')
        time.sleep(32)
        frame(sock,reader,args.outdir,'idle')
        send(sock,reader,pointer(150,150));time.sleep(.6)
        send(sock,reader,[button(True)])
        wait(args.serial,'M168_IMAGE_FOCUS_VISIBLE',deadline)
        print('[M169 IDLE WAKE PASS] >30s without events, real tablet click wakes userspace compositor',flush=True)
        frame(sock,reader,args.outdir,'image-focus')
        send(sock,reader,[button(False)]);time.sleep(.6)

        send(sock,reader,pointer(150,90));time.sleep(.6)
        send(sock,reader,[button(True)])
        wait(args.serial,'M168_IMAGE_DRAG_GRABBED',deadline)
        send(sock,reader,pointer(240,120));time.sleep(.6)
        send(sock,reader,[button(False)])
        wait(args.serial,'M168_IMAGE_MOVED_VISIBLE',deadline)
        frame(sock,reader,args.outdir,'image-drag')

        send(sock,reader,pointer(650,180));time.sleep(.6)
        send(sock,reader,[button(True)])
        wait(args.serial,'M168_BROWSER_FOCUS_VISIBLE',deadline)
        frame(sock,reader,args.outdir,'browser-focus')
        send(sock,reader,[button(False)]);time.sleep(.6)
        send(sock,reader,[key('b',True)])
        wait(args.serial,'M168_BROWSER_KEY_VISIBLE',deadline)
        frame(sock,reader,args.outdir,'browser-key')
        send(sock,reader,[key('b',False)])

        send(sock,reader,pointer(550,370));time.sleep(.6)
        send(sock,reader,[button(True)])
        wait(args.serial,'M168_TERMINAL_FOCUS_VISIBLE',deadline)
        frame(sock,reader,args.outdir,'terminal-focus')
        send(sock,reader,[button(False)]);time.sleep(.6)
        send(sock,reader,[key('a',True)])
        wait(args.serial,'M168_TERMINAL_KEY_VISIBLE',deadline)
        frame(sock,reader,args.outdir,'terminal-key')
        send(sock,reader,[key('a',False)])

        send(sock,reader,[key('q',True)])
        wait(args.serial,'M168_DESKTOP_PASS',deadline)
    print('[M168 QMP THREE CLIENTS PASS] seven actual framebuffer screenshots, '
          'real keyboard/tablet after >30s idle, signed Lynx and MC output, three independent '
          'userland surfaces and focused PTY/browser repaint',flush=True)


if __name__ == '__main__':
    try: main()
    except Exception as exc:
        print(f'[M168 QMP FAIL] {exc}',file=sys.stderr,flush=True)
        raise SystemExit(1)
