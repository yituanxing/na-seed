#!/usr/bin/env python3
"""Offline M236 fail-closed contract tests; never imply native-QEMU PASS."""
from importlib.util import module_from_spec, spec_from_file_location
import hashlib
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


BASE = Path(__file__).resolve().parent


def load(name: str):
    spec = spec_from_file_location('m236_host_' + name.replace('-', '_'), BASE / name)
    assert spec and spec.loader
    target = module_from_spec(spec)
    spec.loader.exec_module(target)
    return target


report = load('m236-compat-report.py')
disk = load('m236-alpine-disk-check.py')


class M236StrictTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='m236-host-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.cases = report.owner.manifest_cases(BASE / 'm236-candidate-elfs.tsv')

    def ledgers(self, altered=None):
        first = ['M236_FIRST_DISCOVERY_COMPLETE']
        reopen = ['M236_REOPEN_DISCOVERY_COMPLETE']
        for i, item in enumerate(self.cases):
            for phase in ('install', 'functional', 'reopen'):
                status = 'FAIL' if altered == (i, phase) else 'PASS'
                line = f'M236_RESULT\t{item["package"]}\t{item["elf_path"]}\t{phase}\t{status}'
                (reopen if phase == 'reopen' else first).append(line)
        return '\n'.join(first) + '\n', '\n'.join(reopen) + '\n'

    def test_exact_seven_strict_guest_result_parser(self):
        self.assertEqual(len(self.cases), 7)
        self.assertEqual(len({x['elf_path'] for x in self.cases}), 7)
        first, reopen = self.ledgers()
        value = report.owner.build_report(self.cases, first, reopen)
        self.assertEqual(value['newly_installed_full_pass_entrypoints'], 7)
        self.assertEqual(value['preexisting'], 0)
        for altered in ((0, 'install'), (3, 'functional'), (6, 'reopen')):
            with self.subTest(altered=altered):
                f, r = self.ledgers(altered)
                try:
                    altered_report = report.owner.build_report(self.cases, f, r)
                except ValueError:
                    continue
                self.assertNotEqual(altered_report['newly_installed_full_pass_entrypoints'], 7)
        with self.assertRaises(ValueError):
            report.owner.build_report(self.cases, first + first.splitlines()[1] + '\n', reopen)
        with self.assertRaises(ValueError):
            report.owner.build_report(self.cases,
                                      first.replace('M236_FIRST_DISCOVERY_COMPLETE\n', ''), reopen)

    def test_signed_inventory_must_match_original_physical_ext4_bytes(self):
        rows = [('fossil', 'usr/bin/fossil'),
                ('libdeflate-utils', 'usr/bin/libdeflate-gzip'),
                ('dfu-util', 'usr/bin/dfu-prefix'),
                ('dfu-util', 'usr/bin/dfu-suffix'),
                ('ofarc', 'usr/bin/ofarc'),
                ('ofhash', 'usr/bin/ofhash'),
                ('xmlsec', 'usr/bin/xmlsec1')]
        original = {path: b'\x7fELF-M236-' + path.encode() for _, path in rows}
        inventory = self.root / 'build/external/m236-candidate-inventory/amd64/candidates.tsv'
        inventory.parent.mkdir(parents=True)

        def content():
            return ('package\telf_path\tstatus\tsha256\n' + ''.join(
                f'{package}\t{path}\tNATIVE_ELF\t{hashlib.sha256(original[path]).hexdigest()}\n'
                for package, path in rows))

        class M191:
            @staticmethod
            def debugfs_cat(_partition, path):
                return original[path.lstrip('/')]

        inventory.write_text(content())
        disk.verify_signed_inventory('amd64', self.root / 'placeholder.ext4', M191, self.root)
        for corrupt in ('sha', 'duplicate', 'wrong-type'):
            with self.subTest(corrupt=corrupt):
                if corrupt == 'sha':
                    original[rows[0][1]] = b'\x7fELF-corrupted-after-official-apk'
                elif corrupt == 'duplicate':
                    inventory.write_text(content() + content().splitlines()[1] + '\n')
                else:
                    inventory.write_text(content().replace('NATIVE_ELF', 'NOT_ELF', 1))
                with self.assertRaises(ValueError):
                    disk.verify_signed_inventory('amd64', self.root / 'placeholder.ext4', M191, self.root)
                original[rows[0][1]] = b'\x7fELF-M236-' + rows[0][1].encode()
                inventory.write_text(content())


if __name__ == '__main__':
    unittest.main(verbosity=1)
