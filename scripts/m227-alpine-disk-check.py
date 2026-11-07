#!/usr/bin/env python3
"""Host-only exact physical ext4 oracle after TWO original Alpine boots.

Native QEMU is stopped by the proven M194 UART runner, not guest umount.
The inherited read-only strict fsck is required; do not claim clean shutdown.
"""
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile


def load(name: str, alias: str):
    source = Path(__file__).with_name(name)
    spec = importlib.util.spec_from_file_location(alias, source)
    if spec is None or spec.loader is None:
        raise RuntimeError('missing original Alpine host oracle ' + name)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


M197 = load('m197-alpine-disk-check.py', 'm227_m197_host')
M195 = load('m195-alpine-disk-check.py', 'm227_m195_host')
M191 = load('m191-alpine-root-disk.py', 'm227_m191_host')
PACKAGES = M197.PACKAGES + ('wget', 'aria2', 'lighttpd', 'lighttpd-openrc')
BINARIES = M197.BINARIES + ('/usr/bin/wget', '/usr/bin/aria2c', '/usr/sbin/lighttpd')
DOCUMENT = b'M227_OPENRC_HTTP_DURABLE_2026\n'


def verify(port: str, source: Path, disk: Path) -> None:
    source = source.resolve(strict=True)
    disk = disk.resolve(strict=True)
    for path in ('/var/www/m227/index.txt', '/etc/runlevels/default/lighttpd',
                 '/etc/init.d/lighttpd'):
        original = M191.guest_path(source, path)
        if original.exists() or original.is_symlink():
            raise ValueError('[M227 HOST FAIL] official baseline preseeded ' + path)
    # M197 independently verifies exact persisted Bash/Lua/SQLite/Git work,
    # signed public WAN witness, correct package ISA and full read-only fsck.
    M197.verify(port, source, disk)
    M195.verify(port, source, disk, stage='M227', witness_path='/root/m197-wan.txt',
                witness=M197.WITNESS, packages=PACKAGES, executables=BINARIES)
    with tempfile.TemporaryDirectory(prefix='m227-openrc-') as tmp:
        partition = Path(tmp) / 'root.ext4'
        M191.extract_partition(disk, partition)
        read = lambda path: M191.debugfs_cat(partition, path)
        if read('/var/www/m227/index.txt') != DOCUMENT:
            raise ValueError('[M227 HOST FAIL] real HTTP response not durable on ext4')
        config = read('/etc/lighttpd/lighttpd.conf')
        for rule in (b'server.bind = "127.0.0.1"', b'server.port = 15432',
                     b'server.document-root = "/var/www/m227"'):
            if rule not in config:
                raise ValueError('[M227 HOST FAIL] genuine service configuration lost')
        if not read('/etc/init.d/lighttpd').startswith(b'#!'):
            raise ValueError('[M227 HOST FAIL] authentic signed OpenRC script absent')
        link = subprocess.run(
            ['debugfs', '-R', 'stat /etc/runlevels/default/lighttpd', str(partition)],
            check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if 'Type: symlink' not in link.stdout or 'lighttpd' not in link.stdout:
            raise ValueError('[M227 HOST FAIL] rc-update default symlink not present')
    print(f'[M227 OPENRC EXT4 HOST PASS] {port} original signed package DB and '
          'lighttpd-openrc, guest-written HTTP, default symlink, complete '
          'M197 SQLite/Git, strict read-only fsck; no guest clean unmount claimed')


if __name__ == '__main__':
    if len(sys.argv) != 4:
        raise SystemExit('usage: m227-alpine-disk-check.py PORT SIGNED_ROOT DISK')
    verify(sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]))
