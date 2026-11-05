#!/usr/bin/env python3
"""Regression for physical Alpine serial markers across three native ISAs.

Original RV64 getty/ash emitted CR CR LF in the first M201 CI while the
original fbi itself returned 0. This test forbids interpreting echoed shell
commands as real lifecycle markers and accepts actual CR, LF or repeated CR.
"""

import runpy


qmp = runpy.run_path('scripts/m201-qmp-alpine-fbi.py', run_name='m201_uart_test')
started = qmp['guest_started_fbi']
exited = qmp['guest_exited_fbi']

echo = (b'naseed:~# printf "M201_FBI_START\\n"; /usr/bin/fbi '
        b'--nointeractive -t 15; printf "M201_FBI_EXEC_RC=%d\\n" "$?"\r\n')
assert not started(echo)
assert not exited(echo)

for line_break in (b'\n', b'\r\n', b'\r\r\n', b'\r', b'\r\r'):
    stream = echo + b'M201_FBI_START' + line_break
    assert started(stream), repr(line_break)
    assert not exited(stream)
    stream += b'[EXEC] original dynamic ELF\r\nM201_FBI_EXEC_RC=0' + line_break
    assert exited(stream), repr(line_break)

assert not started(b'prefix-M201_FBI_START\r\n')
assert not exited(b'printf "M201_FBI_EXEC_RC=0"\r\n')
assert not exited(b'M201_FBI_EXEC_RC=not-a-number\r\n')
print('[M201 PHYSICAL UART HOST PASS] 3 ISA CR/LF/CRCRLF, '
      'no echoed-shell false positives')
