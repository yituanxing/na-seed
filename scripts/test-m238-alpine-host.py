#!/usr/bin/env python3
"""Offline M238 strict 15 ELF ledger + signed physical SHA negative contracts."""
import csv
import hashlib
from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import tempfile
import unittest


BASE = Path(__file__).resolve().parent


def load(name: str):
    spec = spec_from_file_location('m238_host_' + name.replace('-', '_'), BASE / name)
    assert spec and spec.loader
    target = module_from_spec(spec)
    spec.loader.exec_module(target)
    return target


report = load('m238-compat-report.py')
disk = load('m238-alpine-disk-check.py')


class M238SignedNativeTests(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory(prefix='m238-physical-contract-')
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name)
        self.cases = report.owner.manifest_cases(BASE / 'm238-candidate-elfs.tsv')

    def ledgers(self, altered=None):
        first = ['M238_FIRST_DISCOVERY_COMPLETE']
        reopen = ['M238_REOPEN_DISCOVERY_COMPLETE']
        for index, row in enumerate(self.cases):
            for phase in ('install', 'functional', 'reopen'):
                value = ('PREEXISTING' if altered == ('preexisting', index, phase) else
                         'FAIL' if altered == (index, phase) else 'PASS')
                line = f'M238_RESULT\t{row["package"]}\t{row["elf_path"]}\t{phase}\t{value}'
                (reopen if phase == 'reopen' else first).append(line)
        return '\n'.join(first) + '\n', '\n'.join(reopen) + '\n'

    def test_fifteen_exact_genuine_results_and_fail_closed(self):
        self.assertEqual(len(self.cases), 15)
        self.assertEqual(len({item['elf_path'] for item in self.cases}), 15)
        self.assertEqual(len({item['package'] for item in self.cases}), 6)
        first, reopen = self.ledgers()
        proved = report.owner.build_report(self.cases, first, reopen)
        self.assertEqual(proved['newly_installed_full_pass_entrypoints'], 15)
        self.assertEqual(proved['preexisting'], 0)
        for bad in ((0, 'install'), (3, 'functional'), (12, 'reopen'),
                    ('preexisting', 0, 'install')):
            with self.subTest(bad=bad):
                f, r = self.ledgers(bad)
                try:
                    outcome = report.owner.build_report(self.cases, f, r)
                except ValueError:
                    continue
                self.assertNotEqual(outcome['newly_installed_full_pass_entrypoints'], 15)
        with self.assertRaises(ValueError):
            report.owner.build_report(self.cases, first + first.splitlines()[1] + '\n', reopen)
        with self.assertRaises(ValueError):
            report.owner.build_report(self.cases,
                                      first.replace('M238_FIRST_DISCOVERY_COMPLETE\n', ''), reopen)
        with self.assertRaises(ValueError):
            report.owner.build_report(self.cases, first, reopen.replace('\tPASS', '\tBLOCKED', 1))

    def test_signed_archive_sha_must_equal_actual_ext4_and_all_unique(self):
        original = {case['elf_path']: b'\x7fELF-M238-' + case['elf_path'].encode()
                    for case in self.cases}
        census = self.root / 'build/external/m238-candidate-inventory/amd64/candidates.tsv'
        census.parent.mkdir(parents=True)

        def contents():
            return ('package\telf_path\tstatus\tsha256\n' + ''.join(
                f'{case["package"]}\t{case["elf_path"]}\tNATIVE_ELF\t'
                f'{hashlib.sha256(original[case["elf_path"]]).hexdigest()}\n'
                for case in self.cases))

        class M191:
            @staticmethod
            def debugfs_cat(_partition, path):
                return original[path.lstrip('/')]

        census.write_text(contents())
        result = disk.signed_sha('amd64', self.root / 'original.ext4', M191, self.root)
        self.assertEqual(len(result), 15)
        for broken in ('wrong-signature', 'duplicate', 'wrong-type', 'wrong-hash'):
            with self.subTest(broken=broken):
                if broken == 'wrong-signature':
                    original[self.cases[0]['elf_path']] += b'changed-after-original-APK'
                elif broken == 'duplicate':
                    census.write_text(contents() + contents().splitlines()[1] + '\n')
                elif broken == 'wrong-type':
                    census.write_text(contents().replace('NATIVE_ELF', 'NOT_ELF', 1))
                else:
                    census.write_text(contents().replace('\t' + result[self.cases[0]['elf_path']],
                                                        '\t' + '0' * 64, 1))
                with self.assertRaises(ValueError):
                    disk.signed_sha('amd64', self.root / 'original.ext4', M191, self.root)
                original[self.cases[0]['elf_path']] = b'\x7fELF-M238-' + self.cases[0]['elf_path'].encode()
                census.write_text(contents())

    def test_semantic_oracle_cannot_accept_missing_real_ext4_witnesses(self):
        class M191:
            @staticmethod
            def debugfs_cat(_partition, _path):
                return b''
        with self.assertRaises(ValueError):
            disk.real_semantics(self.root / 'missing.ext4', M191)

    def test_spirv_cfg_legend_alone_must_not_pass(self):
        # Upstream spirv-cfg emits this legend even with no function graph.
        legend = b'digraph { legend_merge_src [shape=plaintext, label=""]; }'
        with self.assertRaises(ValueError):
            disk.require_real_function_cfg(legend)
        disk.require_real_function_cfg(
            b'digraph { block [label="1\\nFn %main entry", shape=box]; }')


if __name__ == '__main__':
    unittest.main(verbosity=1)
