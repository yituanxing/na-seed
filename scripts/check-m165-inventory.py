#!/usr/bin/env python3
"""Thirty NEW ELF identities and thirty exact functional images/codec cases."""
from pathlib import Path
import re

root = Path(__file__).resolve().parent.parent
manifest = root / 'scripts/m165-original-elfs.txt'
paths = [s.strip() for s in manifest.read_text().splitlines()
         if s.strip() and not s.startswith('#')]
apps = [Path(p).name for p in paths]
runner = (root / 'tests/m165-batch-run.sh').read_text()
markers = [s.strip() for s in
           (root / 'scripts/m165-expected-markers.txt').read_text().splitlines()
           if s.strip() and not s.startswith('#')]
assert len(paths) == len(set(paths)) == 30
assert len(set(apps)) == 30 and all(p.startswith('usr/bin/') for p in paths)
assert len(markers) == len(set(markers)) == 31
assert set(markers) == {'M165_' + x.upper() + '_PASS' for x in apps} | {'M165_BATCH_PASS'}
loop = re.search(r'^for app in (.*?); do$', runner, re.M | re.S)
assert loop
called = re.findall('[a-z0-9-]+', loop.group(1).replace('\\', ''))
cases = runner.split('case "$app" in', 1)[1].split('      *)', 1)[0]
labels = [app for group in re.findall(r'^      ([a-z0-9|]+)\)', cases, re.M)
          for app in group.split('|')]
assert len(set(labels)) == 30 and set(labels) == set(called) == set(apps)
for prior in (root / 'scripts').glob('m[0-9]*-original-elfs.txt'):
    if prior == manifest:
        continue
    old = {x.strip() for x in prior.read_text().splitlines()
           if x.strip() and not x.startswith('#')}
    assert not set(paths) & old, f'historical duplicate in {prior}'
for pair in (('pamtojpeg2k', 'jpeg2ktopam'),
             ('pgmtosbig', 'sbigtopgm'), ('pnmtojbig', 'jbigtopnm')):
    assert all(p in apps and p in runner for p in pair)
assert 'image_check "$out" "$work"' in runner
assert 'sh scripts/host-configure-m163-batch.sh' in (
    root / 'scripts/host-configure-m165-batch.sh').read_text()
print('[M165 STATIC PASS] 30 historically new signed Alpine ELF, '
      'three actual JPEG2000/SBIG/JBIG round trips, shared PNM oracle')
