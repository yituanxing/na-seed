#!/usr/bin/env python3
"""Independent, read-only host ext4 oracle for guest-created OpenRC state.

Only original Alpine rc-update in the first native QEMU may create the
default-runlevel link. Reopening a second original /sbin/init and checking
the disk independently must agree on its existence and provenance.
"""
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile


def load(name: str, alias: str):
    path = Path(__file__).with_name(name)
    spec = importlib.util.spec_from_file_location(alias, path)
    if spec is None or spec.loader is None:
        raise RuntimeError('missing ext4 disk oracle ' + name)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


M198 = load('m198-alpine-disk-check.py', 'm199_m198_disk')
M191 = load('m191-alpine-root-disk.py', 'm199_m191_disk')


def verify(port: str, root: Path, disk: Path):
    M198.verify(port, root, disk)
    service = '/etc/init.d/naseed-desktop'
    enabled = '/etc/runlevels/default/naseed-desktop'
    if M191.guest_path(root.resolve(strict=True), service).exists():
        raise ValueError('[M199 HOST FAIL] immutable Alpine service unexpectedly modified')
    if M191.guest_path(root.resolve(strict=True), enabled).exists():
        raise ValueError('[M199 HOST FAIL] signed Alpine default runlevel was preseeded')
    with tempfile.TemporaryDirectory(prefix='m199-openrc-ext4-') as tmp:
        part = Path(tmp) / 'root.ext4'
        M191.extract_partition(disk.resolve(strict=True), part)
        for guest, kind in ((service, 'regular'), (enabled, 'symlink')):
            result = subprocess.run(
                ['debugfs', '-R', 'stat ' + guest, str(part)],
                check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                text=True)
            if 'Type: ' + kind not in result.stdout:
                raise ValueError('[M199 HOST FAIL] guest-created ' + guest +
                                 ' is not ' + kind + ': ' + result.stdout[-500:])
        if M191.debugfs_cat(part, service) != Path(
                'rootfs/alpine/naseed-desktop.openrc').read_bytes():
            raise ValueError('[M199 HOST FAIL] OpenRC service bytes differ')
    print(f'[M199 OPENRC HOST DISK PASS] {port} guest rc-update default '
          'runlevel symlink, pristine original Alpine baseline, unchanged '
          'service and M198 signed Vim/GUI bytes verified on physical ext4')


if __name__ == '__main__':
    if len(sys.argv) != 4:
        raise SystemExit('usage: m199-alpine-disk-check.py PORT SIGNED_ROOT DISK')
    verify(sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]))
