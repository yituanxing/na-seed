#!/usr/bin/env python3
"""One genuine QEMU physical-frame acceptance of the ORIGINAL Alpine dialog.

The same M174 signed CPIO boots its same devfs/software preflight, then the
existing desktop in M187 mode. QMP only sends physical VirtIO keyboard/tablet
events. We require a real dialog exec, full-screen terminal pixel changes,
arrow-selection repaint, ESC returned to the original BusyBox shell, and a
second independent shell output. No simulated ANSI or test-only compositor.
"""
import argparse
from pathlib import Path
import runpy
import sys
import time

ui = runpy.run_path('scripts/m173-qmp-verify.py',run_name='m187_qmp_library')
base = runpy.run_path('scripts/m167-qmp-verify.py',run_name='m187_base_library')
qmp = base['qmp']


def screenshot(sock, reader, outdir, name):
    destination = outdir / f'm187-{name}.ppm'
    destination.unlink(missing_ok=True)
    qmp['qmp_command'](sock, reader, 'screendump',
                       {'filename': str(destination.resolve())})
    ui['pixels'](destination)
    return destination


def send_key(sock, reader, name):
    base['send'](sock, reader, [base['key'](name, True)])
    base['send'](sock, reader, [base['key'](name, False)])
    time.sleep(.12)


def type_command(sock, reader, keys):
    for key in keys:
        send_key(sock, reader, key)
    send_key(sock, reader, 'ret')


def change_until(sock, reader, outdir, name, previous, minimum, deadline):
    while time.monotonic() < deadline:
        destination=screenshot(sock,reader,outdir,name)
        try:
            ui['changed'](previous,destination,309,199,615,410,minimum)
            return destination
        except RuntimeError:
            time.sleep(.25)
    # Fail the native boot immediately; otherwise its outer QEMU watchdog
    # waits another 850 seconds after the physical oracle already knows it
    # cannot pass. This is a failure-only QMP shutdown, never a fake PASS.
    try:
        qmp['qmp_command'](sock, reader, 'quit')
    except Exception:
        pass
    raise RuntimeError(f'{name} never showed distinct physical PTY pixels')


def dialog_ready(sock, reader, outdir, focused, deadline):
    """Wait for a *settled physical TUI frame*, not its first partial repaint.

    On slower ISA emulation the first changed pixels can be only shell echo
    or ncurses clearing the PTY. Sending RIGHT before the final dialog screen
    has been painted lets the shell or partially initialized curses consume
    the key. Repeated QEMU screendumps are the sole readiness evidence.
    """
    previous = None
    steady = 0
    last_delta = None
    while time.monotonic() < deadline:
        shot = screenshot(sock, reader, outdir, 'dialog')
        data = ui['pixels'](shot)
        try:
            ui['changed'](focused, shot, 309, 199, 615, 410, 1000)
            # This is a centered 8x30 yes/no dialog in the existing 53x25
            # terminal. Command echo and an initial ncurses clear can settle
            # without having drawn the box; they must not arm the RIGHT key.
            ui['changed'](focused, shot, 385, 260, 535, 330, 200)
        except RuntimeError:
            steady = 0
        else:
            if previous is not None:
                delta = 0
                for y in range(199, 410):
                    left = (y * 800 + 309) * 3
                    right = (y * 800 + 615) * 3
                    delta += sum(a != b for a, b in
                                 zip(previous[left:right], data[left:right]))
                last_delta = delta
                steady = steady + 1 if delta <= 20 else 0
                if steady >= 2:
                    return shot
        previous = data
        time.sleep(.4)
    try:
        qmp['qmp_command'](sock, reader, 'quit')
    except Exception:
        pass
    raise RuntimeError(f'dialog physical frame did not settle; '
                       f'last pixel delta={last_delta}')


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--serial',required=True,type=Path)
    ap.add_argument('--qmp',required=True,type=Path)
    ap.add_argument('--outdir',required=True,type=Path)
    ap.add_argument('--timeout',type=int,default=850)
    args=ap.parse_args()
    deadline=time.monotonic()+args.timeout
    args.outdir.mkdir(parents=True,exist_ok=True)
    base['wait_marker'](args.serial,'M187_DESKTOP_INITIAL_VISIBLE',deadline)
    sock,reader=base['connect'](args.qmp,deadline)
    with sock:
        initial=screenshot(sock,reader,args.outdir,'initial')
        data=ui['pixels'](initial)
        if ui['pixel'](data,20,20)!=(16,32,48) or \
           ui['pixel'](data,100,150)!=(255,0,255):
            raise RuntimeError('existing GUI windows did not survive M187 boot')

        # The terminal is the ORIGINAL M173 dynamic window manager's PTY
        # client; focus is a real tablet click in its surface, not a fake FD.
        ui['pointer_click'](sock,reader,500,340)
        ui['pointer_release'](sock,reader)
        focused=screenshot(sock,reader,args.outdir,'focused')
        dialog_command=(
          'd','i','a','l','o','g','spc','minus','minus','y','e','s','n','o',
          'spc','m','1','8','7','spc','8','spc','3','0'
        )
        type_command(sock,reader,dialog_command)
        # Actual exec is essential: shell echo of the command alone cannot
        # satisfy this oracle even if it changed PTY glyphs.
        base['wait_marker'](args.serial,
                 '[EXEC] replaced process with CPIO ELF: /usr/bin/dialog',deadline)
        shown=dialog_ready(sock,reader,args.outdir,focused,
                           min(deadline,time.monotonic()+25))

        # Dialog's yes/no selection reacts to VT100 application cursor mode
        # (DECCKM) or ordinary cursor mode. QMP sends a real KEY_RIGHT.
        send_key(sock,reader,'right')
        moved=change_until(sock,reader,args.outdir,'arrow',shown,20,
                           min(deadline,time.monotonic()+12))

        # ESC is a terminal key in M187, not the compositor's old M173 exit.
        # A real `echo 187` on the still-running PTY proves dialog relinquished
        # foreground TTY and BusyBox shell received control again.
        send_key(sock,reader,'esc')
        time.sleep(.75)
        type_command(sock,reader,('e','c','h','o','spc','1','8','7'))
        base['wait_marker'](args.serial,
                          'M187_PTY_SHELL_RESUMED_AFTER_TUI',deadline)
        resumed=screenshot(sock,reader,args.outdir,'shell-resumed')
        ui['changed'](moved,resumed,309,199,615,410,200)

        # Deliberate compositor shortcut, exclusive to the new mode. Old
        # M173 QMP still exits on Q/ESC per its existing acceptance contract.
        base['send'](sock,reader,[base['key']('ctrl',True)])
        base['send'](sock,reader,[base['key']('f12',True)])
        base['wait_marker'](args.serial,'M187_DESKTOP_PASS',deadline)

    print('[M187 QMP VT100 PASS] same signed M174 CPIO, original Alpine dialog '
          'full-screen yes/no PTY rendering, hardware right arrow selection, '
          'ESC exits into real shell, physical QEMU framebuffer screenshots',
          flush=True)


if __name__=='__main__':
    try:main()
    except Exception as exc:
        print('[M187 QMP VT100 FAIL]',exc,file=sys.stderr,flush=True)
        raise SystemExit(1)
