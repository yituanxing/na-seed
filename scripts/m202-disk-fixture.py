#!/usr/bin/env python3
"""Clone M201 signed Alpine physical disk, add only second image to ext4.

Avoid a second APK installation, a second mkfs of the entire official root,
or guest-side image-generating helper. debugfs owns ext4 metadata; this host
script only extracts/replaces the exact existing DOS-MBR partition bytes.
"""

import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile


PART_OFFSET = 4096 * 512  # M191's verified DOS-MBR physical partition.
DISK_SIZE = 256 * 1024 * 1024


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def exact_debugfs(partition: Path, guest: str) -> bytes:
    result = subprocess.run(['debugfs', '-R', 'cat ' + guest,
                             str(partition)], check=True,
                            capture_output=True)
    if b'File not found' in result.stderr:
        raise RuntimeError('guest ext4 member missing: ' + guest)
    return result.stdout


def copy_bytes(source, destination, total: int) -> None:
    remaining = total
    while remaining:
        part = source.read(min(4 * 1024 * 1024, remaining))
        if not part:
            raise RuntimeError('truncated physical Alpine partition')
        destination.write(part)
        remaining -= len(part)


def stage(source: Path, target: Path, second: Path) -> None:
    if source.stat().st_size != DISK_SIZE:
        raise RuntimeError('M201 source is not expected signed-Alpine MBR disk')
    if target.exists():
        raise RuntimeError('refusing stale M202 output disk')
    second_data = second.read_bytes()
    if not second_data.startswith(b'\x89PNG\r\n\x1a\n'):
        raise RuntimeError('host fixture is not real PNG')
    target.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='m202-ext4-', dir=target.parent) as tmp:
        partition = Path(tmp) / 'original-alpine.ext4'
        with source.open('rb') as original, partition.open('wb') as out:
            original.seek(PART_OFFSET)
            copy_bytes(original, out, DISK_SIZE - PART_OFFSET)
        if not exact_debugfs(partition, '/root/m201-four-bars.png').startswith(b'\x89PNG'):
            raise RuntimeError('source original M201 PNG absent')
        result = subprocess.run([
            'debugfs', '-w', '-R',
            f'write {second.resolve()} /root/m202-second-bars.png',
            str(partition)], check=True, capture_output=True, text=True)
        if 'Allocated inode:' not in result.stdout and 'Allocated inode:' not in result.stderr:
            raise RuntimeError('debugfs did not confirm new ext4 PNG inode')
        if exact_debugfs(partition, '/root/m202-second-bars.png') != second_data:
            raise RuntimeError('second guest PNG failed exact ext4 readback')
        subprocess.run(['sh', 'scripts/check-ext4-fsck-clean.sh',
                        str(partition)], check=True)
        shutil.copyfile(source, target)
        with target.open('r+b') as disk, partition.open('rb') as part:
            disk.seek(PART_OFFSET)
            copy_bytes(part, disk, DISK_SIZE - PART_OFFSET)
    if target.stat().st_size != DISK_SIZE:
        raise RuntimeError('modified Alpine physical disk size')
    with source.open('rb') as original, target.open('rb') as candidate:
        if original.read(PART_OFFSET) != candidate.read(PART_OFFSET):
            raise RuntimeError('M202 modified signed Alpine disk partition table')
    print('[M202 ALPINE DISK PASS] original M201 signed APK disk cloned; '
          'one different PNG added to ext4, exact readback and fsck clean; '
          f'second_fixture_sha256={digest(second)}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('source', type=Path)
    parser.add_argument('target', type=Path)
    parser.add_argument('second', type=Path)
    a = parser.parse_args()
    stage(a.source, a.target, a.second)
