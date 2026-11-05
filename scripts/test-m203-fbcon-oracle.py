#!/usr/bin/env python3
"""Fast negative/positive M203 contract; native physical QMP still mandatory."""

from pathlib import Path
import runpy


oracle = runpy.run_path('scripts/m203-qmp-alpine-fbcon.py',
                        run_name='m203_host_only_contract')
W, H = 800, 600


def synthetic_n() -> bytes:
    pixel = bytearray(W * H * 3)
    for gy, bits in enumerate(oracle['LOWERCASE_N']):
        for gx in range(5):
            if bits & (1 << (4 - gx)):
                for dy in (0, 1):
                    x = oracle['ORIGIN_X'] + 1 + gx
                    y = oracle['ORIGIN_Y'] + 1 + 2 * gy + dy
                    at = (y * W + x) * 3
                    pixel[at:at + 3] = b'\xff\xff\xff'
    return bytes(pixel)


def main() -> None:
    data = synthetic_n()
    assert oracle['has_n_glyph'](data)
    assert not oracle['has_n_glyph'](bytes(W * H * 3))
    assert oracle['text_pixels'](data) > 0
    altered = bytearray(data)
    altered[(105 * W + 81) * 3:(105 * W + 81) * 3 + 3] = b'\x00\x00\x00'
    assert not oracle['has_n_glyph'](bytes(altered))
    assert oracle['changed_pixels'](data, bytes(altered)) >= 1

    for line_end in (b'\n', b'\r\n', b'\r\r\n'):
        assert oracle['whole_line'](b'M203_TTY1_GETTY_RC=0'+line_end,
                                    b'M203_TTY1_GETTY_RC=0')
        assert not oracle['whole_line'](
            b'printf M203_TTY1_GETTY_RC=0'+line_end,
            b'M203_TTY1_GETTY_RC=0')
    source = Path('scripts/m203-alpine-uart.py').read_text()
    assert 'm203-typed-shell.txt' in source
    assert 'm203-restored-shell.txt' in source
    assert 'm203-fbi-rc.txt' in source
    assert 'm203realtypedinput' in source and 'm203restoredtyped' in source
    qmp = Path('scripts/m203-qmp-alpine-fbcon.py').read_text()
    assert "shell_command(sock, reader, 'root')" in qmp
    assert "shell_command(sock, reader, 'naseed-m193-only')" in qmp
    assert "hardware_key(sock, reader, 'spc')" in qmp
    assert "hardware_key(sock, reader, 'q')" in qmp
    assert "'typed-shell-command'" in qmp
    assert "'echo $? > /root/m203-fbi-rc.txt'" in qmp
    assert 'original_fbi_frame(sock, reader' in qmp
    probe = Path('scripts/probe-m203-alpine-fbcon.sh').read_text()
    assert 'build/m203-$port.qmp' in probe
    assert 'NAS_QEMU_VIRTIO_GPU=1' in probe
    assert 'NAS_QEMU_INPUT_OPTIN=1' in probe
    assert 'NAS_VIRTIO_BLK_IMAGE' in probe
    # Native parity runners name their UART stream after the selected case.
    # A distinct "m203" filename made three real guests appear silent.
    assert 'export BUSYBOX_PARITY_CASE=m202-fbi' in probe
    assert 'serial="$PWD/$logs/$BUSYBOX_PARITY_CASE.serial.log"' in probe
    assert 'clean="$PWD/$logs/$BUSYBOX_PARITY_CASE.clean.log"' in probe
    stage = Path('scripts/m203-disk-fixture.py').read_text()
    assert 'TTY1_GETTY' in stage and 'ORIGINAL_APPS' in stage
    print('[M203 FBICON HOST CONTRACT PASS] exact glyph pixels/negative '
          'oracle, genuine qcode root/password+two ext4 writes, native '
          'signed fbi graphics takeover and text restore; physical QEMU required')


if __name__ == '__main__':
    main()
