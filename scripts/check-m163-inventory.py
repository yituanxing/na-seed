#!/usr/bin/env python3
"""Strict new 33-ELF identity + real guest operation static gate (not QEMU)."""
from pathlib import Path
import re

root = Path(__file__).resolve().parent.parent
manifest = root / 'scripts/m163-original-elfs.txt'
paths = [line.strip() for line in manifest.read_text().splitlines()
         if line.strip() and not line.startswith('#')]
apps = [Path(p).name for p in paths]
runner = (root / 'tests/m163-batch-run.sh').read_text()
markers = [line.strip() for line in
           (root / 'scripts/m163-expected-markers.txt').read_text().splitlines()
           if line.strip() and not line.startswith('#')]
assert len(paths) == 33 and len(set(paths)) == 33
assert all(x.startswith('usr/bin/') for x in paths)
assert len(set(apps)) == 33
assert len(markers) == len(set(markers)) == 34
assert set(markers) == {'M163_' + app.upper() + '_PASS' for app in apps} | {'M163_BATCH_PASS'}
loop = re.search(r'^for app in (.*?); do$', runner, re.M | re.S)
assert loop
called = re.findall('[a-z0-9-]+', loop.group(1).replace('\\', ''))
case_body = runner.split('case "$app" in', 1)[1].split('      *)', 1)[0]
labels = [app for group in re.findall(r'^      ([a-z0-9|]+)\)', case_body, re.M)
          for app in group.split('|')]
assert len(set(labels)) == 33 and set(labels) == set(called) == set(apps)
for older in sorted((root / 'scripts').glob('m[0-9]*-original-elfs.txt')):
    if older == manifest:
        continue
    old_paths = {x.strip() for x in older.read_text().splitlines()
                 if x.strip() and not x.startswith('#')}
    dup = set(paths) & old_paths
    assert not dup, f'ELFs already admitted by {older}: {sorted(dup)}'
assert 'image_check "$out" "$work"' in runner
for required in ('ppmtopict', 'picttoppm', 'ppmtowinicon', 'winicontoppm',
                 '/usr/bin/pamfile', '/usr/bin/pamcut', '/usr/bin/pamtable'):
    assert required in runner
print('[M163 STATIC PASS] 33 historically NEW Alpine Netpbm ELF identities, '
      '33 exact real-image cases and two true graphics codec round trips')
