#!/usr/bin/env python3
"""Small offline M235 contract: 12 honest ledgers and strict HTTP negatives.

No QEMU/CI is launched. This cannot certify that a new Alpine binary works;
the native pilot, signed SHA census and post-reboot physical disk must do so.
"""
from importlib.util import module_from_spec, spec_from_file_location
import hashlib
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


BASE = Path(__file__).resolve().parent


def load(name: str):
    spec = spec_from_file_location('m235_test_' + name.replace('-', '_'), BASE / name)
    assert spec and spec.loader
    target = module_from_spec(spec)
    spec.loader.exec_module(target)
    return target


report = load('m235-compat-report.py')
http = load('m235-http-proof.py')
historical = load('test-m223-http-host.py')
wire = historical.wire
server = historical.server
disk = historical.disk


class M235StrictTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='m235-test-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.cases = report.owner.manifest_cases(BASE / 'm235-candidate-elfs.tsv')

    def ledgers(self, *, altered=None):
        first = ['M235_FIRST_DISCOVERY_COMPLETE']
        reopen = ['M235_REOPEN_DISCOVERY_COMPLETE']
        for index, item in enumerate(self.cases):
            for phase in ('install', 'functional', 'reopen'):
                status = ('FAIL' if altered == (index, phase) else
                          'PREEXISTING' if altered == ('preexisting', phase) and index == 0 else 'PASS')
                line = f'M235_RESULT\t{item["package"]}\t{item["elf_path"]}\t{phase}\t{status}'
                (reopen if phase == 'reopen' else first).append(line)
        return '\n'.join(first) + '\n', '\n'.join(reopen) + '\n'

    def test_all_twelve_and_corrupt_or_duplicate_rejected(self):
        first, reopen = self.ledgers()
        value = report.owner.build_report(self.cases, first, reopen)
        self.assertEqual(value['newly_installed_full_pass_entrypoints'], 12)
        self.assertEqual(value['preexisting'], 0)
        for altered in ((0, 'functional'), (1, 'reopen'), (2, 'install'),
                        ('preexisting', 'install')):
            with self.subTest(altered=altered):
                data = self.ledgers(altered=altered)
                try:
                    result = report.owner.build_report(self.cases, *data)
                except ValueError:
                    continue  # Contradictory states fail even earlier.
                self.assertNotEqual(12, result['newly_installed_full_pass_entrypoints'])
        with self.assertRaises(ValueError):
            report.owner.build_report(self.cases, first.replace(
                'M235_FIRST_DISCOVERY_COMPLETE\n', ''), reopen)
        duplicate = 'M235_RESULT\t' + self.cases[0]['package'] + '\t' + self.cases[0]['elf_path'] + '\tinstall\tPASS\n'
        with self.assertRaises(ValueError):
            report.owner.build_report(self.cases, first + duplicate, reopen)

    def test_actual_one_route_tcp_host_and_disk_negatives(self):
        source_dir = self.root / 'host-first'
        server.generate('first', source_dir)
        body = disk.source_bytes('first', source_dir, 'wget')
        wire.ROUTES = ('wget',)
        pcap = self.root / 'actual.pcap'
        pcap.write_bytes(historical.actual_pcap({'wget': body}))
        log = self.root / 'server.log'
        log.write_text('[M223 HOST READY] phase=first port=15431\n'
                       'M223_HOST_GET phase=first path=/wget status=200 '
                       f'bytes={len(body)} sha256={hashlib.sha256(body).hexdigest()} '
                       'peer=127.0.0.1\n')
        fake_disk = self.root / 'disk.img'
        fake_disk.write_bytes(b'physical-ext4-backed-in-real-ci')

        class FakeM191:
            ARCH_MACHINE = {'amd64': 62}
            bytes = body

            @staticmethod
            def extract_partition(_disk, _partition):
                return None

            @staticmethod
            def debugfs_cat(_partition, _guest):
                return FakeM191.bytes

        with patch.object(http, 'module', return_value=wire), \
             patch.object(wire.disk_source.owner, 'load', return_value=FakeM191):
            self.assertEqual(http.verify('first', 'amd64', pcap, log,
                                         source_dir, fake_disk)['physical_ext4_exact_body'], True)
            for bad_phase in ('reopen', 'bogus'):
                with self.subTest(phase=bad_phase), self.assertRaises(ValueError):
                    http.verify(bad_phase, 'amd64', pcap, log, source_dir, fake_disk)
            original = pcap.read_bytes()
            pcap.write_bytes(b'NOT-A-PCAP')
            with self.assertRaises(ValueError):
                http.verify('first', 'amd64', pcap, log, source_dir, fake_disk)
            pcap.write_bytes(original)
            FakeM191.bytes = b'wrong guest ext4 download'
            with self.assertRaises(ValueError):
                http.verify('first', 'amd64', pcap, log, source_dir, fake_disk)
            FakeM191.bytes = body
            text = log.read_text()
            log.write_text(text.replace('/wget ', '/aria2 '))
            with self.assertRaises(ValueError):
                http.verify('first', 'amd64', pcap, log, source_dir, fake_disk)
            log.write_text(text.replace('sha256=', 'sha256=0'))
            with self.assertRaises(ValueError):
                http.verify('first', 'amd64', pcap, log, source_dir, fake_disk)


if __name__ == '__main__':
    unittest.main(verbosity=1)
