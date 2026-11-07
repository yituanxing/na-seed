#!/usr/bin/env python3
"""Independent original Alpine serial observer of real graphical VT login.

The serial login runs only read-only checks and observes real guest-created
ext4 files. All graphical VT keystrokes come from independent physical QMP.
"""

import importlib.util
import os
from pathlib import Path
import sys


spec = importlib.util.spec_from_file_location(
    'm203_m194_uart', Path(__file__).with_name('m194-alpine-uart.py'))
assert spec and spec.loader
m194 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m194)


COMMANDS = (
    ('test -c /dev/tty1 && '
     'grep -Fxq "tty1::respawn:/sbin/getty -L tty1 38400 linux" /etc/inittab && '
     'grep -Fxq tty1 /etc/securetty && '
     'apk info -e fbida-fbi >/dev/null && '
     'test -x /usr/bin/fbi && '
     'test -s /root/m201-four-bars.png && '
     'test -s /root/m202-second-bars.png; '
     'printf "M203_TTY1_GETTY_RC=%d\\n" "$?"\r',
     'M203_TTY1_GETTY_RC=0',
     'real graphical tty1, original inittab getty and signed fbi'),
    ('n=0; while [ ! -s /root/m203-typed-shell.txt ]; do '
     'sleep 1; n=$((n+1)); [ "$n" -lt 180 ] || break; done; '
     'test "$(cat /root/m203-typed-shell.txt 2>/dev/null)" = '
     'm203realtypedinput; '
     'printf "M203_TYPED_FILE_RC=%d\\n" "$?"\r',
     'M203_TYPED_FILE_RC=0',
     'original tty1 authenticated ash wrote ext4 by genuine physical keys'),
    ('n=0; while [ ! -s /root/m203-restored-shell.txt ]; do '
     'sleep 1; n=$((n+1)); [ "$n" -lt 180 ] || break; done; '
     'test "$(cat /root/m203-restored-shell.txt 2>/dev/null)" = '
     'm203restoredtyped && '
     'test "$(cat /root/m203-fbi-rc.txt 2>/dev/null)" = 0 && '
     'test -c /dev/tty1 && '
     'apk info -e fbida-fbi >/dev/null && '
     'test -s /root/m201-four-bars.png; '
     'printf "M203_RESTORED_FILE_RC=%d\\n" "$?"\r',
     'M203_RESTORED_FILE_RC=0',
     'real tty1 shell survived graphics takeover and wrote durable ext4'),
)


def main() -> None:
    if len(sys.argv) != 7:
        raise SystemExit('usage: m203-alpine-uart.py RUNNER SERIAL RUNNER_LOG '
                         'FIFO QMP_SUCCESS QMP_FAILURE')
    runner, serial, runner_log, fifo, success, failure = sys.argv[1:]
    if Path(success).exists():
        raise RuntimeError('refusing stale M203 physical GPU/input proof')
    m194.run(runner, Path(serial), Path(runner_log), Path(fifo),
             int(os.environ.get('NAS_M203_DIAG_SECONDS', '360')) + 25,
             commands=COMMANDS, stage='M203',
             success_message='[M203 ORIGINAL ALPINE GRAPHICAL VT UART PASS] '
                             'physical tty1 getty+login+ash produced real ext4 '
                             'files before and after native fbi KD_GRAPHICS',
             failure_watch=Path(failure))


if __name__ == '__main__':
    main()
