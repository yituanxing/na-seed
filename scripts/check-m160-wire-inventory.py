#!/usr/bin/env python3
"""M160 static ownership gate: genuine host PNG and only old Alpine ELFs.

Static checks do not claim QEMU application success. The native three-ISA
runner additionally requires each guest marker, original exec and TLS GET.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
manifest = ROOT / 'scripts/m160-reused-elfs.txt'
lines = [s.strip() for s in manifest.read_text().splitlines()
         if s.strip() and not s.lstrip().startswith('#')]
assert lines == [
    'usr/bin/curl', 'usr/bin/wget', 'usr/bin/pngtopam',
    'usr/bin/pamfile', 'usr/bin/pamtable', 'usr/bin/chafa',
]
for elf in lines:
    occurrences = []
    for old in sorted((ROOT / 'scripts').glob('m[0-9]*-original-elfs.txt')):
        if old == manifest:
            continue
        previous = [s.strip() for s in old.read_text().splitlines()
                    if s.strip() and not s.lstrip().startswith('#')]
        if elf in previous:
            occurrences.append(old.name)
    assert occurrences, f'M160 cannot claim a historically NEW ELF: {elf}'

markers = (ROOT / 'scripts/m160-expected-markers.txt').read_text().splitlines()
assert markers == [
    'M160_REAL_CURL_IMAGE_TLS_PASS',
    'M160_REAL_WGET_IMAGE_TLS_PASS',
    'M160_REAL_PNG_DECODE_PASS',
    'M160_REAL_CHAR_RENDER_PASS',
    'M160_WIRE_IMAGE_PASS',
]
guest = (ROOT / 'tests/m160-batch-run.sh').read_text()
probe = (ROOT / 'scripts/probe-m160-batch.sh').read_text()
server = (ROOT / 'scripts/m160-image-wire-server.py').read_text()
fixture = (ROOT / 'scripts/host-configure-m160-batch.sh').read_text()
for marker in markers:
    assert f'echo {marker}' in guest
for binary in ('curl', 'wget', 'pngtopam', 'pamfile', 'pamtable', 'chafa'):
    assert f'original "{binary}"' not in guest or binary in lines
for directive in ('--cacert "$ca"', '--ca-certificate="$ca"',
                  '--resolve m156.naseed.test:15443:10.0.2.2'):
    assert directive in guest
for forbidden in ('--insecure', '--no-check-certificate', ' -k ',
                  'check_hostname = False', 'CERT_NONE'):
    assert forbidden not in guest + probe + server
for request in ('/m160-curl.png', '/m160-wget.png'):
    assert request in server and request in guest
assert "runpy.run_path('scripts/m156-https-wire-server.py')" in server
assert 'scripts/host-configure-m156-https.sh' in fixture
assert 'host-m160-image.png' in fixture and 'm160-image.sha256' in fixture
assert '[ "$magic" = P6 ]' in guest, 'RGB PNG must decode to native raw PPM'
assert 'sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m160-batch' in (
    ROOT / 'scripts/stage-m160-prebuilt.sh').read_text()
print('[M160 STATIC PASS] 6 previously signed ELFs, 0 new identities; '
      'TLS SAN checks, host-only PNG, 2 real GET paths, 5 unique markers')
