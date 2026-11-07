#!/usr/bin/env python3
"""Stage real Alpine tty1 getty on a disposable copy of the M202 ext4 disk.

This changes only distribution configuration. Original BusyBox init/getty,
login, ash, original signed fbi, and both PNGs remain byte-for-byte intact.
"""

import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile


M202 = __import__('runpy').run_path('scripts/m202-disk-fixture.py',
                                  run_name='m203_m202_disk')
PART_OFFSET = M202['PART_OFFSET']
DISK_SIZE = M202['DISK_SIZE']
exact_debugfs = M202['exact_debugfs']
copy_bytes = M202['copy_bytes']

ORIGINAL_APPS = ('/sbin/init', '/sbin/getty', '/bin/login', '/bin/ash',
                 '/bin/sh', '/bin/busybox', '/usr/bin/fbi')
TTY1_GETTY = b'tty1::respawn:/sbin/getty -L tty1 38400 linux'


def update_config(part: Path, guest: str, data: bytes, directory: Path) -> None:
    path = directory / guest.lstrip('/').replace('/', '_')
    path.write_bytes(data)
    for command in (f'rm {guest}', f'write {path.resolve()} {guest}'):
        proc = subprocess.run(['debugfs', '-w', '-R', command, str(part)],
                              capture_output=True, text=True, check=True)
        if ('error' in proc.stderr.lower() or
                ('write ' in command and 'Allocated inode:' not in
                 proc.stdout + proc.stderr)):
            raise RuntimeError(f'debugfs failed {command}: {proc.stderr}')
    if exact_debugfs(part, guest) != data:
        raise RuntimeError(f'ext4 config readback mismatch: {guest}')


def stage(source: Path, target: Path) -> None:
    if source.stat().st_size != DISK_SIZE:
        raise RuntimeError('M202 original Alpine root disk size incorrect')
    if target.exists():
        raise RuntimeError('refusing stale M203 output disk')
    target.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='m203-ext4-', dir=target.parent) as temp:
        temporary = Path(temp)
        partition = temporary / 'alpine-tty1.ext4'
        with source.open('rb') as original, partition.open('wb') as out:
            original.seek(PART_OFFSET)
            copy_bytes(original, out, DISK_SIZE - PART_OFFSET)

        originals = {p: hashlib.sha256(exact_debugfs(partition, p)).digest()
                     for p in ORIGINAL_APPS}
        old_inittab = exact_debugfs(partition, '/etc/inittab')
        old_securetty = exact_debugfs(partition, '/etc/securetty')
        if b'ttyS0::respawn:/sbin/getty' not in old_inittab:
            raise RuntimeError('source lost original serial OpenRC getty')
        if b'ttyS0' not in old_securetty.splitlines():
            raise RuntimeError('source lost original authenticated serial terminal')
        if any(row.strip().startswith(b'tty1::') for row in old_inittab.splitlines()):
            raise RuntimeError('source tty1 already configured, cannot add duplicate getty')
        inittab = old_inittab.rstrip(b'\n') + b'\n' + TTY1_GETTY + b'\n'
        securetty = old_securetty.rstrip(b'\n') + b'\ntty1\n'
        update_config(partition, '/etc/inittab', inittab, temporary)
        update_config(partition, '/etc/securetty', securetty, temporary)
        if any(hashlib.sha256(exact_debugfs(partition, p)).digest() != originals[p]
               for p in ORIGINAL_APPS):
            raise RuntimeError('M203 altered an original Alpine executable')
        for p in ('/root/m201-four-bars.png', '/root/m202-second-bars.png'):
            data = exact_debugfs(partition, p)
            if not data.startswith(b'\x89PNG\r\n\x1a\n'):
                raise RuntimeError('M202 signed root image missing: ' + p)
        subprocess.run(['sh', 'scripts/check-ext4-fsck-clean.sh',
                        str(partition)], check=True)
        shutil.copyfile(source, target)
        with target.open('r+b') as disk, partition.open('rb') as part:
            disk.seek(PART_OFFSET)
            copy_bytes(part, disk, DISK_SIZE - PART_OFFSET)

    with source.open('rb') as old, target.open('rb') as new:
        if old.read(PART_OFFSET) != new.read(PART_OFFSET):
            raise RuntimeError('M203 changed signed Alpine physical disk MBR')
    if target.stat().st_size != DISK_SIZE:
        raise RuntimeError('M203 disk lost exact signed root physical size')
    print('[M203 ALPINE CONSOLE DISK PASS] original signed Alpine getty/login/'
          'ash/fbi unchanged; new tty1 in original inittab+securetty; '
          'physical ext4 clean with both real PNGs')


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('source', type=Path)
    parser.add_argument('target', type=Path)
    args = parser.parse_args()
    stage(args.source, args.target)
