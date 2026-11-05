#!/usr/bin/env python3
"""Run unmodified Alpine /usr/bin/fbi via original OpenRC getty/login/ash.

The guest verifies original APK visibility and fbi's *actual* exit status;
the host QMP must independently capture exact pixels while fbi is running.
"""

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
     'test -s /root/m201-four-bars.png; '
     'printf "M201_APK_FBI_RC=%d\\n" "$?"\r',
     'M201_APK_FBI_RC=0',
     'original signed fbida-fbi package installed in ext4 Alpine root'),
    # Keep fbi in the *foreground*: the following marker is its true
    # process exit code, not merely a successful shell background launch.
    # Upstream --nointeractive and -t 15 -1 avoid VT keyboard dependence.
    ('printf "M201_FBI_START\\n"; '
     '/usr/bin/fbi --nointeractive -d /dev/fb0 -t 15 -1 '
     '/root/m201-four-bars.png >/root/m201-fbi.log 2>&1; '
     'printf "M201_FBI_EXEC_RC=%d\\n" "$?"\r',
     'M201_FBI_EXEC_RC=0',
     'original unmodified Alpine fbi full program completed with exit 0'),
    ('test -f /root/m201-fbi.log && '
     'test -s /root/m201-four-bars.png && '
     'apk info -e fbida-fbi >/dev/null; '
     'printf "M201_FBI_FINAL_RC=%d\\n" "$?"\r',
     'M201_FBI_FINAL_RC=0',
     'physical QMP witnessed original fbi before original shell final check'),
)


def main() -> None:
    if len(sys.argv) != 7:
        raise SystemExit('usage: m201-alpine-uart.py RUNNER SERIAL RUNNER_LOG '
                         'FIFO QMP_SUCCESS QMP_FAILURE')
    runner, serial, runner_log, fifo, success, failure = sys.argv[1:]
    success_path = Path(success)
    if success_path.exists():
        raise RuntimeError('refusing stale QMP success sentinel')
    m194.run(
        runner, Path(serial), Path(runner_log), Path(fifo),
        int(os.environ.get('NAS_M201_DIAG_SECONDS', '240')) + 25,
        commands=COMMANDS, stage='M201',
        success_message='[M201 ORIGINAL ALPINE FBI UART PASS] real original '
                        'Alpine init/OpenRC/getty/login/ash + fbida-fbi package '
                        '+ /usr/bin/fbi actual exit 0 AFTER physical QMP image',
        failure_watch=Path(failure),
        # After the real foreground fbi returns, do not allow its last
        # postcondition until the independent physical QMP oracle succeeded.
        before_command_barriers={2: success_path})


if __name__ == '__main__':
    main()
