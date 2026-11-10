#!/usr/bin/env python3
"""Offline fail-closed tests: original stage aliases, host HTTP, TCP PCAP, SHA."""
from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import hashlib
import ipaddress
import os
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent


def imported(file: str, name: str):
    spec = spec_from_file_location(name, ROOT / file)
    assert spec and spec.loader
    module = module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


wire = imported('m223-http-wire-check.py', 'm223_test_wire')
disk = imported('m223-alpine-disk-check.py', 'm223_test_disk')
server = imported('m223-http-server.py', 'm223_test_server')


def packet(src: str, dst: str, sport: int, dport: int, seq: int,
           flags: int, payload: bytes = b'') -> bytes:
    tcp = struct.pack('!HHIIHHHH', sport, dport, seq, 0, (5 << 12) | flags,
                      8192, 0, 0) + payload
    ipv4 = (b'\x45\x00' + struct.pack('!H', 20 + len(tcp)) +
            b'\x00\x00\x00\x00\x40\x06\x00\x00' +
            ipaddress.IPv4Address(src).packed + ipaddress.IPv4Address(dst).packed)
    return b'\0' * 12 + b'\x08\x00' + ipv4 + tcp


def actual_pcap(sources: dict[str, bytes], *, mutate=None) -> bytes:
    frames = []
    guest = '10.0.2.15'
    host = '10.0.2.2'
    for num, route in enumerate(wire.ROUTES):
        sport = 43200 + num
        request = f'GET /{route} HTTP/1.1\r\nHost: 10.0.2.2\r\n\r\n'.encode()
        response = (b'HTTP/1.1 200 OK\r\nContent-Length: ' +
                    str(len(sources[route])).encode() + b'\r\n\r\n' + sources[route])
        if mutate == f'{route}-response':
            response = response[:-1] + b'X'
        if mutate == f'{route}-status':
            response = response.replace(b'200 OK', b'404 Not Found')
        frame_group = [packet(guest, host, sport, 15431, 7, 0x02),
                       packet(host, guest, 15431, sport, 100, 0x12),
                       packet(guest, host, sport, 15431, 8, 0x10),
                       packet(guest, host, sport, 15431, 8, 0x18, request),
                       packet(host, guest, 15431, sport, 101, 0x18, response)]
        if mutate == f'{route}-missing-syn':
            frame_group.pop(0)
        if mutate == f'{route}-missing-get':
            frame_group.pop(3 if len(frame_group) == 5 else 2)
        frames.extend(frame_group)
    pcap = struct.pack('<IHHIIII', 0xa1b2c3d4, 2, 4, 0, 0, 65535, 1)
    return pcap + b''.join(struct.pack('<IIII', 0, 0, len(frame), len(frame)) + frame
                           for frame in frames)


class StrictHttpTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='m223-negative-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.hosts = {}
        for phase in ('first', 'reopen'):
            directory = self.root / f'host-{phase}'
            server.generate(phase, directory)
            self.hosts[phase] = directory

    def sources(self, phase='first'):
        return {route: disk.source_bytes(phase, self.hosts[phase], route)
                for route in wire.ROUTES}

    def test_phase_random_sources_unique_and_wrong_phase_rejected(self):
        all_sources = []
        for phase in ('first', 'reopen'):
            all_sources.extend(self.sources(phase).values())
        self.assertEqual(4, len(set(all_sources)))
        with self.assertRaises(ValueError):
            disk.source_bytes('reopen', self.hosts['first'], 'wget')
        with self.assertRaises(ValueError):
            server.generate('first', self.hosts['first'])

    def test_host_receipts_fail_closed_wrong_sha_phase_or_missing_get(self):
        sources = self.sources()
        lines = '[M223 HOST READY] phase=first port=15431\n'
        for route in wire.ROUTES:
            body = sources[route]
            lines += (f'M223_HOST_GET phase=first path=/{route} status=200 '
                      f'bytes={len(body)} sha256={hashlib.sha256(body).hexdigest()} '
                      'peer=127.0.0.1\n')
        self.assertEqual(wire.host_log_oracle('first', lines, sources)
                         ['host_real_http_get_count'], {'wget': 1, 'aria2': 1})
        for bad in (lines.replace('sha256=', 'sha256=0', 1),
                    lines.replace('phase=first path=/aria2', 'phase=reopen path=/aria2'),
                    lines.split('M223_HOST_GET phase=first path=/aria2')[0],
                    lines.replace('status=200', 'status=404', 1)):
            with self.assertRaises(ValueError):
                wire.host_log_oracle('first', bad, sources)

    def test_tcp_pcap_real_complete_get_200_body_and_static_negatives(self):
        sources = self.sources()
        self.assertEqual(wire.real_http_flows(actual_pcap(sources), sources)
                         ['pcap_real_http_sessions'], {'wget': 1, 'aria2': 1})
        for negative in ('wget-response', 'aria2-status', 'wget-missing-syn',
                         'aria2-missing-get'):
            with self.subTest(case=negative), self.assertRaises(ValueError):
                wire.real_http_flows(actual_pcap(sources, mutate=negative), sources)
        with self.assertRaises(ValueError):
            wire.contiguous_payload([(100, b'abc'), (100, b'abX')])
        self.assertEqual(wire.contiguous_payload([(100, b'abcdef'), (100, b'abc'),
                                                  (103, b'defghi')]), b'abcdefghi')
        with self.assertRaises(ValueError):
            wire.real_http_flows(b'NOT-A-PCAP', sources)

    def test_staged_wget_only_exact_busybox_alias_not_global(self):
        root = self.root / 'stage'
        (root / 'usr/bin').mkdir(parents=True)
        (root / 'usr/sbin').mkdir(parents=True)
        (root / 'bin').mkdir(parents=True)
        self.assertEqual(disk.staging_aliases('amd64', root), ())
        busybox = root / 'bin/busybox'
        elf = bytearray(32)
        elf[:6] = b'\x7fELF\x02\x01'
        struct.pack_into('<H', elf, 18, 62)
        busybox.write_bytes(elf)
        wget = root / 'usr/bin/wget'
        wget.symlink_to('../../bin/busybox')
        self.assertEqual(disk.staging_aliases('amd64', root), ('/usr/bin/wget',))
        with self.assertRaises(ValueError):
            disk.staging_aliases('arm64', root)
        wget.unlink()
        wget.write_bytes(elf)
        with self.assertRaises(ValueError):
            disk.staging_aliases('amd64', root)
        wget.unlink()
        wget.symlink_to('/etc/passwd')
        with self.assertRaises(ValueError):
            disk.staging_aliases('amd64', root)
        wget.unlink()
        (root / 'usr/bin/aria2c').symlink_to('../../bin/busybox')
        with self.assertRaises(ValueError):
            disk.staging_aliases('amd64', root)

    def test_signed_inventory_strict_elf_sha_cross_check(self):
        home = self.root / 'inventory'
        (home / 'scripts').mkdir(parents=True)
        signed = home / 'build/external/m223-candidate-inventory/amd64'
        signed.mkdir(parents=True)
        entries = [('wget', 'usr/bin/wget'), ('aria2', 'usr/bin/aria2c'),
                   ('lighttpd', 'usr/sbin/lighttpd')]
        blobs = {'/' + path: (b'M223_SIGNED_ELF_' + package.encode())
                 for package, path in entries}
        manifest = 'package\telf_path\tfamily\tprobe\n' + ''.join(
            f'{name}\t{path}\thttp\tsemantic\n' for name, path in entries)
        (home / 'scripts/m223-candidate-elfs.tsv').write_text(manifest)
        lines = ['package\telf_path\tstatus\tsha256']
        for package, path in entries:
            lines.append(f'{package}\t{path}\tNATIVE_ELF\t'
                         f'{hashlib.sha256(blobs["/"+path]).hexdigest()}')
        inventory = signed / 'candidates.tsv'
        inventory.write_text('\n'.join(lines) + '\n')

        class FakeDisk:
            @staticmethod
            def debugfs_cat(_partition, path):
                return blobs[path]

        disk.signed_inventory_sha('amd64', self.root, FakeDisk, home)
        blobs['/usr/bin/wget'] = b'MUTATED_ELF'
        with self.assertRaises(ValueError):
            disk.signed_inventory_sha('amd64', self.root, FakeDisk, home)
        inventory.write_text('\n'.join(lines[:-1]) + '\n')
        with self.assertRaises(ValueError):
            disk.signed_inventory_sha('amd64', self.root, FakeDisk, home)

    def test_exact_two_boot_wiring_and_no_raw_disk_artifact(self):
        runner = (ROOT / 'probe-m197-alpine-software-cohort.sh').read_text()
        guest = (ROOT.parent / 'tests/m205-software-workload.sh').read_text()
        http = (ROOT.parent / 'tests/m223-http-workload.sh').read_text()
        workflow = (ROOT.parent / '.github/workflows/m223-http-alpine.yml').read_text()
        for mark in ('m223_start_host first', 'm223_start_host reopen',
                     'm223_stop_host', 'm223-http-wire-check.py first',
                     'm223-http-wire-check.py reopen', 'm223-host-first',
                     'm223-host-reopen', 'm223-alpine-disk-check.py verify',
                     'm223-alpine-disk-check.py stage',
                     'NAS_M223_REOPEN_SECONDS', 'm223-alpine-uart.py',
                     'm223-alpine-reopen-cases.tsv', 'm223-compat-report.py'):
            self.assertIn(mark, runner)
        for mark in ('milestone=M223', 'export NAS_M223_BOOT_PHASE="$phase"',
                     '"$milestone" != M222 ] && [ "$milestone" != M223 ]',
                     'apk --cache-dir /var/cache/apk --no-network -v add "$package"'):
            self.assertIn(mark, guest)
        for mark in ('/usr/bin/wget -q', '/usr/bin/aria2c --quiet',
                     '/usr/sbin/lighttpd -D', '/usr/bin/wget -q -t 1 -T 3',
                     '$busy cmp -s', 'http://10.0.2.2:15431/',
                     'http://127.0.0.1:15432/'):
            self.assertIn(mark, http)
        self.assertIn('needs: [http-original-amd64]', workflow)
        self.assertIn('m223-reopen-wire.pcap', workflow)
        self.assertNotIn('m197-software-disposable.img', workflow)
        self.assertNotIn('m223-host-first/wget.source', workflow)
        self.assertNotIn('private_key', workflow)


if __name__ == '__main__':
    unittest.main(verbosity=2)
