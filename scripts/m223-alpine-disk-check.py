#!/usr/bin/env python3
"""Fail-closed signed original HTTP ELF plus host-random physical ext4 oracle."""
from importlib.util import module_from_spec, spec_from_file_location
import csv
import hashlib
from pathlib import Path
import re
import subprocess
import sys
import tempfile

source = Path(__file__).with_name('m206-alpine-disk-check.py')
spec = spec_from_file_location('m223_m206_disk', source)
if spec is None or spec.loader is None:
    raise RuntimeError('M223 requires existing signed physical-ext4 owner')
owner = module_from_spec(spec)
spec.loader.exec_module(owner)

ROUTES = ('wget', 'aria2')
CASES = {'wget': 'real-host-wget-download', 'aria2': 'real-host-aria2-download'}


def staging_aliases(port: str, signed_root: Path) -> tuple[str, ...]:
    """Only an OBSERVED genuine base BusyBox wget symlink may be replaced.

    This cannot widen the default M206 owner (ping/ip only), and a staged
    preinstalled native ELF, arbitrary symlink or wrong-ISA BusyBox fails.
    """
    m191 = owner.load('m191-alpine-root-disk.py', 'm223_stage_m191')
    signed_root = signed_root.resolve(strict=True)
    aliases = ()
    for path in ('/usr/bin/wget', '/usr/bin/aria2c', '/usr/sbin/lighttpd'):
        # guest_path() resolves the final symlink. Inspect the raw basename
        # first so an actual BusyBox applet alias cannot be mistaken for an
        # already-installed native candidate, nor a broken link for absence.
        raw = m191.guest_path(signed_root, str(Path(path).parent)) / Path(path).name
        if not raw.exists() and not raw.is_symlink():
            continue
        if path == '/usr/bin/wget' and raw.is_symlink() and \
                owner.is_original_busybox_alias(m191, signed_root, path, port,
                                                ('/usr/bin/wget',)):
            aliases = ('/usr/bin/wget',)
            continue
        raise ValueError('[M223 STAGE FAIL] original ELF preseeded or unknown symlink: ' + path)
    return aliases


def source_bytes(phase: str, host_dir: Path, route: str) -> bytes:
    if phase not in ('first', 'reopen') or route not in ROUTES or host_dir.is_symlink():
        raise ValueError('[M223 HOST FAIL] wrong phase/route or symlinked directory')
    witness = host_dir / f'{route}.source'
    if witness.is_symlink():
        raise ValueError('[M223 HOST FAIL] symlinked random source')
    data = witness.read_bytes()
    expected = (b'M223_' + phase.upper().encode() + b'_' +
                route.upper().encode() + b'_SOURCE_[0-9a-f]{192}\n')
    if not re.fullmatch(expected, data):
        raise ValueError('[M223 HOST FAIL] source lacks phase-specific random bytes')
    return data


def physical_http_outputs(partition: Path, m191, first_dir: Path,
                          reopen_dir: Path) -> None:
    read = lambda path: m191.debugfs_cat(partition, path)
    host_sources = []
    for phase, host_dir in (('first', first_dir), ('reopen', reopen_dir)):
        for route in ROUTES:
            host_bytes = source_bytes(phase, host_dir, route)
            guest = f'/root/m223-work/{CASES[route]}.{phase}/download.bin'
            actual = read(guest)
            if actual != host_bytes:
                raise ValueError(f'[M223 DISK FAIL] actual physical ext4 HTTP {phase}/{route} differs from host')
            host_sources.append(host_bytes)
        local = f'/root/m223-work/real-loopback-lighttpd.{phase}'
        expected = f'M223_LIGHTTPD_LOCAL_{phase}_HTTP_GET\n'.encode()
        if read(local + '/www/index.txt') != expected or \
                read(local + '/loopback.txt') != expected:
            raise ValueError(f'[M223 DISK FAIL] real lighttpd/wget loopback phase {phase} mismatch')
    if len(set(host_sources)) != 4:
        raise ValueError('[M223 HOST FAIL] repeated or reused random source across two QEMUs')


def signed_inventory_sha(port: str, partition: Path, m191, home: Path) -> None:
    """The ext4 bytes must match exact signed/ISA-pinned host APK census SHA."""
    table = home / f'build/external/m223-candidate-inventory/{port}/candidates.tsv'
    if table.is_symlink():
        raise ValueError('[M223 SHA FAIL] symlinked original signed inventory')
    with table.open(newline='', encoding='utf-8') as stream:
        rows = list(csv.reader(stream, delimiter='\t'))
    if not rows or rows[0] != ['package', 'elf_path', 'status', 'sha256']:
        raise ValueError('[M223 SHA FAIL] missing signed original candidate SHA header')
    manifest = home / 'scripts/m223-candidate-elfs.tsv'
    expected = {(line.split('\t')[0], line.split('\t')[1])
                for line in manifest.read_text().splitlines()[1:] if line.strip()}
    actual = set()
    hashes = set()
    for record in rows[1:]:
        if len(record) != 4:
            raise ValueError('[M223 SHA FAIL] invalid signed inventory row')
        package, path, status, sha = record
        if status != 'NATIVE_ELF' or not re.fullmatch('[0-9a-f]{64}', sha) or \
                (package, path) not in expected or (package, path) in actual:
            raise ValueError('[M223 SHA FAIL] mismatched signed original ELF census')
        binary = m191.debugfs_cat(partition, '/' + path)
        if hashlib.sha256(binary).hexdigest() != sha:
            raise ValueError('[M223 SHA FAIL] physical ext4 ELF differs from original signed APK ' + path)
        actual.add((package, path))
        hashes.add(sha)
    if actual != expected or len(hashes) != 3:
        raise ValueError('[M223 SHA FAIL] incomplete/duplicate original signed ELF inventory')


def verify(port: str, signed_root: Path, disk: Path,
           first_dir: Path, reopen_dir: Path) -> None:
    aliases = staging_aliases(port, signed_root)
    home = Path(__file__).resolve().parent.parent
    try:
        owner.verify(port, signed_root, disk,
                     candidates=home / 'scripts/m223-candidate-elfs.tsv',
                     packages=home / 'rootfs/alpine/packages-m223-candidates.txt',
                     expected_count=3, label='M223',
                     allowed_staged_busybox_aliases=aliases)
    except subprocess.CalledProcessError as exc:
        # Only a real strict ext4 fsck failure authorizes physical-disk
        # forensics. Always re-raise the ORIGINAL failure: this diagnostic
        # never changes the acceptance result or touches the guest disk.
        argv = exc.cmd if isinstance(exc.cmd, (list, tuple)) else (exc.cmd,)
        if any('check-ext4-fsck-clean.sh' in str(part) for part in argv):
            report = disk.parent / 'm223-ext4-symlink-diagnostic.log'
            command = [sys.executable,
                       str(home / 'scripts/m223-ext4-symlink-diagnostic.py'),
                       str(disk), str(report)]
            result = subprocess.run(command, check=False)
            if result.returncode:
                print(f'[M223 EXT4 DIAGNOSTIC FAIL] code={result.returncode}',
                      file=sys.stderr)
        raise
    m191 = owner.load('m191-alpine-root-disk.py', 'm223_physical_m191')
    with tempfile.TemporaryDirectory(prefix='m223-physical-http-') as tmp:
        partition = Path(tmp) / 'root.ext4'
        m191.extract_partition(disk.resolve(strict=True), partition)
        physical_http_outputs(partition, m191, first_dir, reopen_dir)
        signed_inventory_sha(port, partition, m191, home)
    print('[M223 PHYSICAL HTTP PASS] four independent exact host-random downloads '
          '+ two real signed lighttpd/wget loopback echoes; all three ext4 ELF '
          'SHA256 exactly match official signed APK census')


if __name__ == '__main__':
    if len(sys.argv) == 4 and sys.argv[1] == 'stage':
        aliases = staging_aliases(sys.argv[2], Path(sys.argv[3]))
        print('[M223 SIGNED STAGE PASS] no candidate preinstalled; '
              f'observed exact base BusyBox wget aliases={len(aliases)}')
    elif len(sys.argv) == 7 and sys.argv[1] == 'verify':
        verify(sys.argv[2], Path(sys.argv[3]), Path(sys.argv[4]),
               Path(sys.argv[5]), Path(sys.argv[6]))
    else:
        raise SystemExit('usage: m223-alpine-disk-check.py stage ISA ROOT | '
                         'verify ISA ROOT DISK HOST_FIRST HOST_REOPEN')
