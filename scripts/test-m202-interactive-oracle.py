#!/usr/bin/env python3
"""Fast deterministic contracts, NOT a replacement for physical QEMU proof."""

from pathlib import Path
import runpy
import struct
import tempfile
import zlib


oracle = runpy.run_path('scripts/m202-qmp-alpine-interactive-fbi.py',
                        run_name='m202_oracle_host_contract')
fixture1 = runpy.run_path('scripts/m201-image-fixture.py', run_name='m201_input')
fixture2 = runpy.run_path('scripts/m202-image-fixture.py', run_name='m202_input')


def decode_png(data: bytes) -> bytes:
    assert data.startswith(b'\x89PNG\r\n\x1a\n')
    at = 8
    payloads = []
    while at < len(data):
        length = struct.unpack_from('>I', data, at)[0]
        tag = data[at + 4:at + 8]
        payload = data[at + 8:at + 8 + length]
        crc = struct.unpack_from('>I', data, at + 8 + length)[0]
        assert zlib.crc32(tag + payload) & 0xffffffff == crc
        if tag == b'IHDR':
            assert struct.unpack_from('>II', payload) == (800, 600)
        if tag == b'IDAT':
            payloads.append(payload)
        at += 12 + length
        if tag == b'IEND':
            break
    raw = zlib.decompress(b''.join(payloads))
    assert len(raw) == 600 * (1 + 800 * 3)
    assert all(raw[y * 2401] == 0 for y in range(600))
    return b''.join(raw[y * 2401 + 1:(y + 1) * 2401]
                    for y in range(600))


def physical_ppm(pixels: bytes) -> bytes:
    return b'P6\n800 600\n255\n' + pixels


def main() -> None:
    lines = oracle['whole_line']
    for sep in (b'\n', b'\r\n', b'\r\r\n'):
        assert lines((b'prompt' + sep + b'M202_FBI_START' + sep).splitlines(),
                     b'M202_FBI_START')
        assert not lines((b'printf M202_FBI_START' + sep).splitlines(),
                         b'M202_FBI_START')
        assert oracle['observed_exit']((b'M202_FBI_EXEC_RC=0' + sep).splitlines())
        assert not oracle['observed_exit'](
            (b'printf "M202_FBI_EXEC_RC=%d" "$?"' + sep).splitlines())
    first = decode_png(fixture1['make_png']())
    second = decode_png(fixture2['make_png']())
    assert first != second and len(first) == len(second) == 800 * 600 * 3
    with tempfile.TemporaryDirectory(prefix='m202-image-host-contract-') as temp:
        frame = Path(temp) / 'not-real-qmp-unit-only.ppm'
        for name, pixels in (('first', first), ('second', second)):
            frame.write_bytes(physical_ppm(pixels))
            oracle['check_physical_frame'](frame, name)
            try:
                oracle['check_physical_frame'](
                    frame, 'second' if name == 'first' else 'first')
            except RuntimeError:
                pass
            else:
                raise AssertionError('wrong image admitted by frame oracle')
        altered = bytearray(second)
        at = (300 * 800 + 500) * 3
        altered[at:at + 3] = b'\xff\xff\xff'
        frame.write_bytes(physical_ppm(altered))
        try:
            oracle['check_physical_frame'](frame, 'second')
        except RuntimeError:
            pass
        else:
            raise AssertionError('one-pixel corruption admitted as physical proof')
    uart = Path('scripts/m202-alpine-uart.py').read_text()
    assert '--interactive' in uart and '--nointeractive' not in uart
    # BusyBox setsid's new-session leader opens a real graphical tty1,
    # never sending QMP keyboard bytes into the serial authenticated shell.
    assert "setsid sh -c 'exec </dev/tty1; " in uart
    assert '/usr/bin/fbi --interactive' in uart
    # BusyBox setsid can fork; its initial exit 0 MUST NOT be mistaken for
    # real fbi completion. Only the new-session child writes its exit status.
    assert "while [ ! -s /root/m202-fbi-exit.status ]; do sleep 1; done" in uart
    assert '$(cat /root/m202-fbi-exit.status)' in uart
    assert 'exec /usr/bin/fbi' not in uart
    probe = Path('scripts/probe-m202-alpine-interactive-fbi.sh').read_text()
    assert 'build/m202-$port.qmp' in probe and '${#NAS_QEMU_QMP_SOCKET}' in probe
    assert "'spc'" in Path('scripts/m202-qmp-alpine-interactive-fbi.py').read_text()
    print('[M202 HOST CONTRACT PASS] LF/CRLF/CRCRLF whole-line anti-echo, '
          'two distinct CRC-correct PNG inputs, exact two-frame RGB check, '
          'negative cross-frame and one-pixel oracles, signed fbi VT stdin; '
          'full system-QEMU still mandatory')


if __name__ == '__main__':
    main()
