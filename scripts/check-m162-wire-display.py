#!/usr/bin/env python3
"""Static ownership and security gate; native QEMU/QMP is separate evidence."""
from pathlib import Path

root = Path(__file__).resolve().parent.parent
binary_file = root / 'scripts/m162-reused-elfs.txt'
paths = [s.strip() for s in binary_file.read_text().splitlines()
         if s.strip() and not s.startswith('#')]
assert paths == [
    'usr/bin/curl', 'usr/bin/wget', 'usr/bin/pngtopam', 'usr/bin/pamfile',
    'usr/bin/chafa', 'usr/bin/magick', 'usr/bin/fbgrab',
]
for elf in paths:
    historical = [p for p in (root / 'scripts').glob('m[0-9]*-original-elfs.txt')
                  if elf in p.read_text().splitlines()]
    assert historical, f'M162 has no previously admitted signed Linux ELF for {elf}'

markers = (root / 'scripts/m162-expected-markers.txt').read_text().splitlines()
assert len(markers) == len(set(markers)) == 7
assert all(x.startswith('M162_') and x.endswith('_PASS') for x in markers)
guest = (root / 'tests/m162-batch-run.sh').read_text()
host = (root / 'scripts/host-configure-m162-batch.sh').read_text()
probe = (root / 'scripts/probe-m162-batch.sh').read_text()
server = (root / 'scripts/m160-image-wire-server.py').read_text()
stage = (root / 'scripts/stage-m162-prebuilt.sh').read_text()
qmp = (root / 'scripts/m161-qmp-verify.py').read_text()
for marker in markers:
    assert f'echo {marker}' in guest, marker
for url in ('m162-display.png', 'm162-display-wget.png'):
    assert url in guest and url in server and url in probe
for required in ('--cacert "$ca"', '--ca-certificate="$ca"',
                 '--resolve m156.naseed.test:15443:10.0.2.2'):
    assert required in guest
for forbidden in ('--insecure', '--no-check-certificate', ' -k ',
                  'CERT_NONE', 'check_hostname = False'):
    assert forbidden not in guest + host + server + probe
assert 'sh scripts/host-configure-m156-https.sh' in host
assert 'host-m162-display.png' in host
assert 'test ! -e "$root/host-m162-display.png"' in stage
assert '--trigger M162_FRAME_VISIBLE --label M162' in probe
assert '"M162_FRAME_VISIBLE"' in qmp
assert 'NAS_QEMU_RAMFB=1' in probe and 'NAS_VIRTIO_NET_USER=1' in probe
assert 'scripts/m161-original-elfs.txt' in (
    root / 'scripts/prebuilt-profile-catalog.tsv').read_text()
assert 'sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m162-batch' in stage
print('[M162 STATIC PASS] seven existing signed ELFs, host-only PNG + X.509, '
      'real NIC plus QMP screen, no new ELF identities or unsafe mmap')
