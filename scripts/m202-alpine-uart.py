#!/usr/bin/env python3
"""Original Alpine OpenRC/login/ash invokes native interactive fbi on tty1."""

import importlib.util
import os
from pathlib import Path
import sys


spec = importlib.util.spec_from_file_location(
    'naseed_m194_uart', Path(__file__).with_name('m194-alpine-uart.py'))
assert spec and spec.loader
m194 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m194)


COMMANDS = (
    ('apk info -e fbida-fbi >/dev/null && test -x /usr/bin/fbi && '
     'test -s /root/m201-four-bars.png && '
     'test -s /root/m202-second-bars.png && test -c /dev/tty1 && '
     'stty -F /dev/tty1 -g > /root/m202-vt-before.mode; '
     'printf "M202_APK_VT_RC=%d\\n" "$?"\r',
     'M202_APK_VT_RC=0',
     'original signed fbi, two independent PNGs and real VT chrdev'),
    # An ORIGINAL Alpine serial ash executes a real BusyBox `setsid` process
    # and WAITS in foreground. Its new session reads only the virtual TTY,
    # not serial UART (which still owns authenticated OpenRC/getty/login).
    # Upstream fbi uses stdin's TCGETS/TCSETS + select/read, Linux VT/KD,
    # rather than libinput/udev or a synthetic guest keyboard helper.
    ('printf "M202_FBI_START\\n"; '
     # Original BusyBox setsid has no portable -c option. Its new-session
     # leader opens tty1 after setsid; Linux TTY open acquires that ctty.
     # Serial OpenRC/getty remains an independent physical terminal.
     "setsid sh -c 'exec </dev/tty1; exec /usr/bin/fbi --interactive -d /dev/fb0 -t 0 "
     '/root/m201-four-bars.png /root/m202-second-bars.png '
     ">/root/m202-fbi.log 2>&1'; "
     'printf "M202_FBI_EXEC_RC=%d\\n" "$?"\r',
     'M202_FBI_EXEC_RC=0',
     'original unmodified fbi received physical VT keys and exited normally'),
    ('test -s /root/m202-fbi.log && '
     '! grep -Fq "No vt switching available" /root/m202-fbi.log && '
     '! grep -Fq "failed to open input devices" /root/m202-fbi.log && '
     'test "$(stty -F /dev/tty1 -g)" = '
     '"$(cat /root/m202-vt-before.mode)" && '
     'apk info -e fbida-fbi >/dev/null; '
     'printf "M202_VT_RESTORED_RC=%d\\n" "$?"\r',
     'M202_VT_RESTORED_RC=0',
     'original fbi VT relinquish, normal Alpine shell and VT termios'),
)


def main() -> None:
    if len(sys.argv) != 7:
        raise SystemExit('usage: m202-alpine-uart.py RUNNER SERIAL RUNNER_LOG '
                         'FIFO QMP_SUCCESS QMP_FAILURE')
    runner, serial, runner_log, fifo, success, failure = sys.argv[1:]
    success_path = Path(success)
    if success_path.exists():
        raise RuntimeError('refusing stale QMP physical key/screen proof')
    m194.run(runner, Path(serial), Path(runner_log), Path(fifo),
             int(os.environ.get('NAS_M202_DIAG_SECONDS', '280')) + 25,
             commands=COMMANDS, stage='M202',
             success_message='[M202 ORIGINAL ALPINE INTERACTIVE UART PASS] '
                             'original signed fbi controlled on /dev/tty1, '
                             'physical GPU frames, actual exit 0, VT restored',
             failure_watch=Path(failure),
             before_command_barriers={2: success_path})


if __name__ == '__main__':
    main()
