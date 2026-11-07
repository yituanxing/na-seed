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
import re
import runpy
import sys
import time

ui = runpy.run_path('scripts/m173-qmp-verify.py',run_name='m187_qmp_library')
base = runpy.run_path('scripts/m167-qmp-verify.py',run_name='m187_base_library')
qmp = base['qmp']
DIALOG_EXEC = b'[EXEC] replaced process with CPIO ELF: /usr/bin/dialog'
PROCESS_EXEC = re.compile(
    rb'(?m)^\[THREAD EXEC\] (?:worker adopted original process|'
    rb'leader reaped siblings) tgid=(\d+)\r?$')
PROCESS_EXIT = re.compile(
    rb'(?m)^\[PROC\] child exited pid=(\d+) status=(\d+); '
    rb'parent pid=(\d+)\r?$')
PROCESS_REAP = re.compile(
    rb'(?m)^\[PROC\] wait4 reaped pid=(\d+) status=(\d+)\r?$')


def fail_native(sock, reader, reason):
    """Stop the real guest when a physical QMP oracle cannot pass."""
    try:
        qmp['qmp_command'](sock, reader, 'quit')
    except Exception:
        pass
    raise RuntimeError(reason)


def wait_native_marker(sock, reader, serial, marker, deadline):
    try:
        base['wait_marker'](serial, marker, deadline)
    except Exception as exc:
        fail_native(sock, reader, f'{marker}: {exc}')


def dialog_process(serial, sock, reader, deadline):
    """Tie the original signed dialog exec to its kernel process identity.

    Each successful exec prints its thread-group PID immediately before its
    ELF path. Reject an ambiguous sequence instead of accepting a different
    process's later exit as proof that dialog relinquished its PTY.
    """
    while time.monotonic() < deadline:
        raw = serial.read_bytes() if serial.is_file() else b''
        if raw.count(DIALOG_EXEC) > 1:
            fail_native(sock, reader, 'original dialog executed more than once')
        at = raw.find(DIALOG_EXEC)
        if at >= 0:
            end = raw.find(b'\n', at)
            if end >= 0:
                previous = raw.rfind(b'[EXEC] replaced process with ', 0, at)
                groups = list(PROCESS_EXEC.finditer(raw,
                                   previous if previous >= 0 else 0, at))
                if len(groups) != 1:
                    fail_native(sock, reader,
                                'original dialog PID is missing or ambiguous '
                                f'({len(groups)} preceding thread-group execs)')
                return int(groups[0].group(1)), end + 1
        time.sleep(.1)
    fail_native(sock, reader, 'original dialog exec identity unavailable')


def wait_dialog_reaped(serial, sock, reader, pid, after_exec, deadline):
    """Require both actual dialog exit(255) and BusyBox wait4(255 << 8).

    Exit alone is insufficient: on slow native ISA emulation the shell can
    remain asleep and keyboard events intended for `echo 187` reach the old
    foreground dialog. Kernel wait4 is the observable shell handoff point.
    """
    while time.monotonic() < deadline:
        raw = serial.read_bytes() if serial.is_file() else b''
        tail = raw[after_exec:]
        exits = [m for m in PROCESS_EXIT.finditer(tail)
                 if int(m.group(1)) == pid]
        if len(exits) > 1:
            fail_native(sock, reader, f'dialog pid={pid} exited repeatedly')
        if exits:
            exit_record = exits[0]
            if int(exit_record.group(2)) != 255:
                fail_native(sock, reader,
                            f'dialog pid={pid} exited with status '
                            f'{int(exit_record.group(2))}, not ESC status 255')
            reaps = [m for m in PROCESS_REAP.finditer(tail)
                     if int(m.group(1)) == pid and
                        m.start() > exit_record.end()]
            if reaps:
                if len(reaps) != 1 or int(reaps[0].group(2)) != 65280:
                    fail_native(sock, reader,
                                f'dialog pid={pid} had inconsistent wait4')
                return
        time.sleep(.1)
    fail_native(sock, reader,
                f'dialog pid={pid} did not exit(255) and reach shell '
                'wait4(65280) within 20s after ESC')


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


def shell_frame_ready(sock, reader, outdir, moved, deadline):
    """Require stable *physical* terminal restoration after dialog wait4.

    The bottom PTY rows differ from the live dialog even when no keyboard
    input has been sent to the shell. Waiting for two settled QEMU frames
    gives slower ISAs time to repaint their foreground TTY after wait4.
    """
    previous = None
    steady = 0
    while time.monotonic() < deadline:
        shot = screenshot(sock, reader, outdir, 'shell-ready')
        data = ui['pixels'](shot)
        try:
            ui['changed'](moved, shot, 309, 375, 615, 410, 150)
        except RuntimeError:
            steady = 0
        else:
            if previous is not None:
                delta = 0
                for y in range(375, 410):
                    left = (y * 800 + 309) * 3
                    right = (y * 800 + 615) * 3
                    delta += sum(a != b for a, b in
                                 zip(previous[left:right], data[left:right]))
                steady = steady + 1 if delta <= 20 else 0
                if steady >= 2:
                    return
        previous = data
        time.sleep(.3)
    fail_native(sock, reader,
                'dialog exited and shell reaped it, but physical PTY frame '
                'did not restore and settle before keyboard input')


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--serial',required=True,type=Path)
    ap.add_argument('--qmp',required=True,type=Path)
    ap.add_argument('--outdir',required=True,type=Path)
    ap.add_argument('--timeout',type=int,default=850)
    ap.add_argument('--m188',action='store_true',
                    help='also verify live pointer/scroll and original Vim')
    ap.add_argument('--m189',action='store_true',
                    help='also verify repeated Nano/Vim on one living PTY')
    ap.add_argument('--m190-write',action='store_true',
                    help='original editors write real IRQ ext4 in same GUI')
    ap.add_argument('--m190-reopen',action='store_true',
                    help='second kernel boot checks and reopens durable ext4')
    args=ap.parse_args()
    if args.m190_write and args.m190_reopen:
        ap.error('only one M190 physical guest phase per boot')
    if (args.m190_write or args.m190_reopen) and not args.m189:
        ap.error('M190 requires original verified M189 PTY')
    if args.m189 and not args.m188:
        ap.error('--m189 requires --m188 (same running signed-CPIO session)')
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
        wait_native_marker(sock,reader,args.serial,DIALOG_EXEC.decode('ascii'),
                           min(deadline,time.monotonic()+40))
        dialog_pid,dialog_exec_end=dialog_process(
            args.serial,sock,reader,min(deadline,time.monotonic()+5))
        shown=dialog_ready(sock,reader,args.outdir,focused,
                           min(deadline,time.monotonic()+25))

        # Dialog's yes/no selection reacts to VT100 application cursor mode
        # (DECCKM) or ordinary cursor mode. QMP sends a real KEY_RIGHT.
        send_key(sock,reader,'right')
        moved=change_until(sock,reader,args.outdir,'arrow',shown,20,
                           min(deadline,time.monotonic()+12))

        # ESC is a terminal key in M187, not the compositor's old M173 exit.
        # QEMU's native RV64/ARM64 guest may not have scheduled BusyBox yet
        # after a fixed sleep. The exact dialog PID must first exit on ESC,
        # then be reaped by the original shell before physical echo keystrokes.
        # This is kernel process evidence, not test-only terminal repainting.
        previous_log=args.serial.read_bytes()[dialog_exec_end:]
        if any(int(m.group(1))==dialog_pid
               for m in PROCESS_EXIT.finditer(previous_log)):
            fail_native(sock,reader,
                        f'dialog pid={dialog_pid} exited before physical ESC')
        send_key(sock,reader,'esc')
        wait_dialog_reaped(args.serial,sock,reader,dialog_pid,dialog_exec_end,
                           min(deadline,time.monotonic()+20))
        shell_frame_ready(sock,reader,args.outdir,moved,
                          min(deadline,time.monotonic()+20))
        type_command(sock,reader,('e','c','h','o','spc','1','8','7'))
        wait_native_marker(sock,reader,args.serial,
                           'M187_PTY_SHELL_RESUMED_AFTER_TUI',
                           min(deadline,time.monotonic()+20))
        resumed=screenshot(sock,reader,args.outdir,'shell-resumed')
        ui['changed'](moved,resumed,309,199,615,410,200)

        if args.m188:
            # Same QEMU machine, same physical desktop and PTY, same signed
            # M174 CPIO. The additional oracle does not replay M187 or spawn
            # a second profile just to exercise the new input capabilities.
            extra=runpy.run_path('scripts/m188-qmp-extras.py',
                                 run_name='m188_qmp_extension')
            extra['run'](sock,reader,args.serial,args.outdir)

        if args.m189:
            sequence=runpy.run_path('scripts/m189-qmp-sequence.py',
                                    run_name='m189_qmp_extension')
            sequence['run'](sock,reader,args.serial,args.outdir)

        if args.m190_write or args.m190_reopen:
            persistent=runpy.run_path('scripts/m190-qmp-persistent.py',
                                      run_name='m190_qmp_extension')
            phase='write' if args.m190_write else 'reopen'
            persistent[phase](sock,reader,args.serial,args.outdir)

        # Deliberate compositor shortcut, exclusive to the new mode. Old
        # M173 QMP still exits on Q/ESC per its existing acceptance contract.
        base['send'](sock,reader,[base['key']('ctrl',True)])
        base['send'](sock,reader,[base['key']('f12',True)])
        wait_native_marker(sock,reader,args.serial,'M187_DESKTOP_PASS',
                           min(deadline,time.monotonic()+20))

    print('[M187 QMP VT100 PASS] same signed M174 CPIO, original Alpine dialog '
          'full-screen yes/no PTY rendering, hardware right arrow selection, '
          'ESC exits into real shell, physical QEMU framebuffer screenshots',
          flush=True)
    if args.m188:
        print('[M188 QMP REAL INPUT PASS] original upstream LVGL timed input, '
              'physical slider drag/scroll, original Alpine Vim edit and '
              'independent shell file-content verification',flush=True)


    if args.m189:
        print('[M189 QMP MULTI EDITOR PASS] physical original Vim -> Nano '
              '-> Vim session, exact shared-file readback, actual PID/wait4 '
              'and resumed shell',flush=True)


    if args.m190_write or args.m190_reopen:
        phase='write' if args.m190_write else 'reopen'
        print(f'[M190 QMP {phase} PASS] physical original GUI and editors, '
              'real IRQ ext4 file and independent shell readback',flush=True)


if __name__=='__main__':
    try:main()
    except Exception as exc:
        print('[M187 QMP VT100 FAIL]',exc,file=sys.stderr,flush=True)
        raise SystemExit(1)
