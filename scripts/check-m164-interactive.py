#!/usr/bin/env python3
"""Static architecture and ownership gate; true QEMU screenshots are separate."""
from pathlib import Path

root = Path(__file__).resolve().parent.parent
guest = (root / 'tests/m164-input-display.c').read_text()
oracle = (root / 'scripts/m164-serial-qmp-verify.py').read_text()
probe = (root / 'scripts/probe-m164-batch.sh').read_text()
markers = (root / 'scripts/m164-expected-markers.txt').read_text().splitlines()
assert len(markers) == len(set(markers)) == 6
assert all(x.startswith('M164_') for x in markers)
assert all(x in guest for x in markers)
for interface in ('TIOCSCTTY', 'FBIOGET_VSCREENINFO',
                  'FBIOGET_FSCREENINFO', 'lseek(fd, 0, SEEK_SET)',
                  'read(STDIN_FILENO', 'write(fd, bgra',
                  '"/usr/bin/fbgrab"', '"/usr/bin/magick"'):
    assert interface in guest
for element in ('b\'wrong\\r\'', 'b\'flip\\r\'', 'b\'quit\\r\'',
                'm164-first.ppm', 'm164-unchanged.ppm', 'm164-flipped.ppm',
                "qmp['verify_ppm']", "qmp['qmp_command']", 'os.O_RDWR'):
    assert element in oracle, element
for host in ('scripts/probe-x86-busybox-parity.sh',
             'scripts/probe-riscv64-busybox-matrix.sh',
             'scripts/probe-aarch64-busybox-matrix.sh'):
    source = (root / host).read_text()
    assert 'NAS_QEMU_SERIAL_FIFO:-/dev/null' in source
    assert '< "$serial_input"' in source
assert 'NAS_QEMU_RAMFB=1' in probe
assert 'NAS_QEMU_SERIAL_FIFO=' in probe
assert 'NAS_QEMU_QMP_SOCKET=' in probe
assert 'scripts/m161-original-elfs.txt' in (
    root / 'scripts/prebuilt-profile-catalog.tsv').read_text()
assert 'm164-batch' in (
    root / 'scripts/prebuilt-profile-catalog.tsv').read_text()
print('[M164 STATIC PASS] optional actual three-ISA serial stdin, unmodified '
      'TTY/VM, userspace frame changes and three independent QMP screenshots')
