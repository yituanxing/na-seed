#!/usr/bin/env python3
"""Offline M237 strict 15 ELF ledger + signed physical SHA negative contracts."""
import csv
import hashlib
from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import tempfile
import unittest


BASE = Path(__file__).resolve().parent


def load(name: str):
    spec = spec_from_file_location('m237_host_' + name.replace('-', '_'), BASE / name)
    assert spec and spec.loader
    target = module_from_spec(spec)
    spec.loader.exec_module(target)
    return target


report = load('m237-compat-report.py')
disk = load('m237-alpine-disk-check.py')


class M237SignedNativeTests(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory(prefix='m237-physical-contract-')
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name)
        self.cases = report.owner.manifest_cases(BASE / 'm237-candidate-elfs.tsv')

    def ledgers(self, altered=None):
        first = ['M237_FIRST_DISCOVERY_COMPLETE']
        reopen = ['M237_REOPEN_DISCOVERY_COMPLETE']
        for index, row in enumerate(self.cases):
            for phase in ('install', 'functional', 'reopen'):
                value = ('PREEXISTING' if altered == ('preexisting', index, phase) else
                         'FAIL' if altered == (index, phase) else 'PASS')
                line = f'M237_RESULT\t{row["package"]}\t{row["elf_path"]}\t{phase}\t{value}'
                (reopen if phase == 'reopen' else first).append(line)
        return '\n'.join(first) + '\n', '\n'.join(reopen) + '\n'

    def test_fifteen_exact_genuine_results_and_fail_closed(self):
        self.assertEqual(len(self.cases), 15)
        self.assertEqual(len({item['elf_path'] for item in self.cases}), 15)
        self.assertEqual(len({item['package'] for item in self.cases}), 5)
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
                                      first.replace('M237_FIRST_DISCOVERY_COMPLETE\n', ''), reopen)
        with self.assertRaises(ValueError):
            report.owner.build_report(self.cases, first, reopen.replace('\tPASS', '\tBLOCKED', 1))

    def test_signed_archive_sha_must_equal_actual_ext4_and_all_unique(self):
        original = {case['elf_path']: b'\x7fELF-M237-' + case['elf_path'].encode()
                    for case in self.cases}
        census = self.root / 'build/external/m237-candidate-inventory/amd64/candidates.tsv'
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
                original[self.cases[0]['elf_path']] = b'\x7fELF-M237-' + self.cases[0]['elf_path'].encode()
                census.write_text(contents())

    def test_semantic_oracle_cannot_accept_missing_real_ext4_witnesses(self):
        class M191:
            @staticmethod
            def debugfs_cat(_partition, _path):
                return b''
        with self.assertRaises(ValueError):
            disk.real_semantics(self.root / 'missing.ext4', M191)

    def test_signed_geos_binary_case_uses_persisted_points_and_three_real_operations(self):
        workload = (BASE.parent / 'tests/m237-software-workload.sh').read_text()
        geos = workload.split('    geos-binary-distance-intersects:usr/bin/geosop)', 1)[1].split('        ;;', 1)[0]
        self.assertIn('if [ "$phase" = first ]; then', geos)
        self.assertIn("printf 'POINT (0 0)\\n' > \"$state/geos-origin.wkt\"", geos)
        self.assertIn("printf 'POINT (3 4)\\n' > \"$state/geos-diagonal.wkt\"", geos)
        self.assertIn('test -s "$state/geos-origin.wkt" && test -s "$state/geos-diagonal.wkt"', geos)
        self.assertEqual(geos.count('/usr/bin/geosop -a "$state/geos-origin.wkt"'), 3)
        self.assertIn('-b "$state/geos-diagonal.wkt"', geos)
        self.assertIn('-b "$state/geos-origin.wkt"', geos)
        for expected in ('--format=txt distance > "$target/distance.txt"',
                         '--format=txt intersects > "$target/intersects-true.txt"',
                         '--format=txt intersects > "$target/intersects-false.txt"',
                         "$busy grep -Eq '^5(\\.0+)?$'", '$busy grep -Fxq true',
                         '$busy grep -Fxq false'):
            self.assertIn(expected, geos)
        self.assertNotIn('--format=txt area', geos)
        self.assertNotIn('M237_GEOS_DIAG', geos)

    def test_signed_geos_physical_binary_semantics_fail_closed(self):
        disk.verify_geos_binary('first', b'5\n', b'true\n', b'false\n')
        disk.verify_geos_binary('reopen', b'5.000\n', b'true\n', b'false\n')
        bad_outputs = (
            (b'0\n', b'true\n', b'false\n'),
            (b'5.0001\n', b'true\n', b'false\n'),
            (b'5\nextra\n', b'true\n', b'false\n'),
            (b'5\n', b'false\n', b'false\n'),
            (b'5\n', b'true\n', b'true\n'),
            (b'5\n', b'', b'false\n'),
            (b'5\n', b'true\n', b''),
        )
        for phase in ('first', 'reopen'):
            for values in bad_outputs:
                with self.subTest(phase=phase, values=values):
                    with self.assertRaises(ValueError):
                        disk.verify_geos_binary(phase, *values)


if __name__ == '__main__':
    unittest.main(verbosity=1)
