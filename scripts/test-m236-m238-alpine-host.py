#!/usr/bin/env python3
"""Strict offline native guest evidence for M236–M238 (not QEMU evidence).

One table-driven ledger and signed SHA oracle for two 15-ELF cohorts; M236
preserves its separate original 7-ELF inventory contract. Unique M237 GEOS
and Recutils proofs and M238 SPIR-V CFG witness tests remain independent.
"""
import hashlib
from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import tempfile
import unittest

BASE = Path(__file__).resolve().parent


def load(name: str):
    spec = spec_from_file_location('signed_native_' + name.replace('-', '_'), BASE / name)
    assert spec and spec.loader
    obj = module_from_spec(spec)
    spec.loader.exec_module(obj)
    return obj


class FifteenSignedNativeCommon:
    """Unittest mixin; never collected without an actual cohort subclass."""
    PROFILE = None
    PACKAGE_COUNT = 0

    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix=self.PROFILE + '-physical-contract-')
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.report = load('alpine-compat-report.py').load_owner(self.PROFILE)
        self.disk = load(self.PROFILE + '-alpine-disk-check.py')
        self.cases = self.report.manifest_cases(BASE / (self.PROFILE + '-candidate-elfs.tsv'))

    def ledgers(self, altered=None):
        prefix = self.PROFILE.upper()
        first = [prefix + '_FIRST_DISCOVERY_COMPLETE']
        reopen = [prefix + '_REOPEN_DISCOVERY_COMPLETE']
        for index, row in enumerate(self.cases):
            for phase in ('install', 'functional', 'reopen'):
                value = ('PREEXISTING' if altered == ('preexisting', index, phase) else
                         'FAIL' if altered == (index, phase) else 'PASS')
                line = f'{prefix}_RESULT\t{row["package"]}\t{row["elf_path"]}\t{phase}\t{value}'
                (reopen if phase == 'reopen' else first).append(line)
        return '\n'.join(first) + '\n', '\n'.join(reopen) + '\n'

    def test_fifteen_exact_genuine_results_and_fail_closed(self):
        self.assertEqual(len(self.cases), 15)
        self.assertEqual(len({item['elf_path'] for item in self.cases}), 15)
        self.assertEqual(len({item['package'] for item in self.cases}), self.PACKAGE_COUNT)
        first, reopen = self.ledgers()
        proved = self.report.build_report(self.cases, first, reopen)
        self.assertEqual(proved['newly_installed_full_pass_entrypoints'], 15)
        self.assertEqual(proved['preexisting'], 0)
        for bad in ((0, 'install'), (3, 'functional'), (12, 'reopen'),
                    ('preexisting', 0, 'install')):
            with self.subTest(bad=bad):
                f, r = self.ledgers(bad)
                try:
                    outcome = self.report.build_report(self.cases, f, r)
                except ValueError:
                    continue
                self.assertNotEqual(outcome['newly_installed_full_pass_entrypoints'], 15)
        with self.assertRaises(ValueError):
            self.report.build_report(self.cases, first + first.splitlines()[1] + '\n', reopen)
        with self.assertRaises(ValueError):
            self.report.build_report(self.cases,
                                      first.replace(self.PROFILE.upper() + '_FIRST_DISCOVERY_COMPLETE\n', ''), reopen)
        with self.assertRaises(ValueError):
            self.report.build_report(self.cases, first, reopen.replace('\tPASS', '\tBLOCKED', 1))

    def test_signed_archive_sha_must_equal_actual_ext4_and_all_unique(self):
        original = {case['elf_path']: b'\x7fELF-' + self.PROFILE.upper().encode() + b'-' + case['elf_path'].encode()
                    for case in self.cases}
        census = self.root / Path('build/external/' + self.PROFILE + '-candidate-inventory/amd64/candidates.tsv')
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
        result = self.disk.signed_sha('amd64', self.root / 'original.ext4', M191, self.root)
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
                    self.disk.signed_sha('amd64', self.root / 'original.ext4', M191, self.root)
                original[self.cases[0]['elf_path']] = b'\x7fELF-' + self.PROFILE.upper().encode() + b'-' + self.cases[0]['elf_path'].encode()
                census.write_text(contents())

    def test_semantic_oracle_cannot_accept_missing_real_ext4_witnesses(self):
        class M191:
            @staticmethod
            def debugfs_cat(_partition, _path):
                return b''
        with self.assertRaises(ValueError):
            self.disk.real_semantics(self.root / 'missing.ext4', M191)


class M237SignedNativeTests(FifteenSignedNativeCommon, unittest.TestCase):
    PROFILE = 'm237'
    PACKAGE_COUNT = 5

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
        self.disk.verify_geos_binary('first', b'5\n', b'true\n', b'false\n')
        self.disk.verify_geos_binary('reopen', b'5.000\n', b'true\n', b'false\n')
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
                        self.disk.verify_geos_binary(phase, *values)

    def test_original_recutils_csv_physical_bytes_and_negative_cases(self):
        canonical = b'"Name","Role"\n"Ada","Engineer"\n"Lin","Researcher"\n'
        self.assertEqual(len(canonical), 50)
        for phase in ('first', 'reopen'):
            self.disk.verify_rec_csv_export(phase, canonical)
            for wrong in (b'Name,Role\nAda,Engineer\nLin,Researcher\n',
                          b'"Name","Role"\n"Ada","Scientist"\n"Lin","Researcher"\n',
                          b'"Name","Role"\n"Ada","Engineer"\n',
                          canonical + b'"Injected","User"\n', b''):
                with self.subTest(phase=phase, wrong=wrong):
                    with self.assertRaises(ValueError):
                        self.disk.verify_rec_csv_export(phase, wrong)
        workload = (BASE.parent / 'tests/m237-software-workload.sh').read_text()
        export = workload.split('    rec-csv-export:usr/bin/rec2csv)', 1)[1].split('        ;;', 1)[0]
        for quoted in ('"Name","Role"', '"Ada","Engineer"', '"Lin","Researcher"'):
            self.assertIn("grep -Fxq '" + quoted + "'", export)
        self.assertIn('|| fail', export)

    def test_all_positive_guest_and_lists_fail_closed_under_posix_shell(self):
        lines = (BASE.parent / 'tests/m237-software-workload.sh').read_text().splitlines()
        for index, line in enumerate(lines):
            if '&&' not in line or line.lstrip().startswith(('#', '$busy awk')):
                continue
            if '&& exit 1' in line or '||' in line:
                continue
            if line.rstrip().endswith('&&'):
                # Guard the entire group, not only its last grep: under dash
                # `set -e; false && true; echo PASS` prints false PASS.
                window = '\n'.join(lines[index:index + 5])
                with self.subTest(line=index + 1):
                    self.assertTrue('|| fail' in window or '|| {' in window,
                                    f'unguarded M237 conjunction at line {index + 1}')


class M238SignedNativeTests(FifteenSignedNativeCommon, unittest.TestCase):
    PROFILE = 'm238'
    PACKAGE_COUNT = 6

    def test_spirv_cfg_legend_alone_must_not_pass(self):
        # Upstream spirv-cfg emits this legend even with no function graph.
        legend = b'digraph { legend_merge_src [shape=plaintext, label=""]; }'
        with self.assertRaises(ValueError):
            self.disk.require_real_function_cfg(legend)
        self.disk.require_real_function_cfg(
            b'digraph { block [label="1\\nFn %main entry", shape=box]; }')

    def test_all_positive_original_guest_conjunctions_fail_closed(self):
        # `set -e; false && true; echo PASS` prints PASS in POSIX sh: all
        # multi-part semantic checks must have their own explicit failure.
        lines = (BASE.parent / 'tests/m238-software-workload.sh').read_text().splitlines()
        self.assertIn('fail() {', '\n'.join(lines))
        for index, line in enumerate(lines):
            if '&&' not in line or line.lstrip().startswith('#') or '&& exit 1' in line:
                continue
            if '||' in line:
                continue
            if line.rstrip().endswith('&&'):
                window = '\n'.join(lines[index:index + 5])
                with self.subTest(line=index + 1):
                    self.assertIn('|| fail', window,
                                  f'original M238 ELF positive check can emit false PASS at line {index + 1}')


class M236StrictTests(unittest.TestCase):
    """Independent smaller original signed ELF ABI and disk provenance."""
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='m236-host-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.report = load('alpine-compat-report.py').load_owner('m236')
        self.disk = load('m236-alpine-disk-check.py')
        self.cases = self.report.manifest_cases(BASE / 'm236-candidate-elfs.tsv')

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
        value = self.report.build_report(self.cases, first, reopen)
        self.assertEqual(value['newly_installed_full_pass_entrypoints'], 7)
        self.assertEqual(value['preexisting'], 0)
        for altered in ((0, 'install'), (3, 'functional'), (6, 'reopen')):
            with self.subTest(altered=altered):
                f, r = self.ledgers(altered)
                try:
                    altered_report = self.report.build_report(self.cases, f, r)
                except ValueError:
                    continue
                self.assertNotEqual(altered_report['newly_installed_full_pass_entrypoints'], 7)
        with self.assertRaises(ValueError):
            self.report.build_report(self.cases, first + first.splitlines()[1] + '\n', reopen)
        with self.assertRaises(ValueError):
            self.report.build_report(self.cases,
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
        self.disk.verify_signed_inventory('amd64', self.root / 'placeholder.ext4', M191, self.root)
        for corrupt in ('sha', 'duplicate', 'wrong-type'):
            with self.subTest(corrupt=corrupt):
                if corrupt == 'sha':
                    original[rows[0][1]] = b'\x7fELF-corrupted-after-official-apk'
                elif corrupt == 'duplicate':
                    inventory.write_text(content() + content().splitlines()[1] + '\n')
                else:
                    inventory.write_text(content().replace('NATIVE_ELF', 'NOT_ELF', 1))
                with self.assertRaises(ValueError):
                    self.disk.verify_signed_inventory('amd64', self.root / 'placeholder.ext4', M191, self.root)
                original[rows[0][1]] = b'\x7fELF-M236-' + rows[0][1].encode()
                inventory.write_text(content())


if __name__ == '__main__':
    import sys
    cohorts = {'m236': M236StrictTests, 'm237': M237SignedNativeTests,
               'm238': M238SignedNativeTests}
    if len(sys.argv) == 1:
        unittest.main(verbosity=1)
    elif len(sys.argv) == 3 and sys.argv[1] == '--profile' and sys.argv[2] in cohorts:
        suite = unittest.defaultTestLoader.loadTestsFromTestCase(cohorts[sys.argv[2]])
        if not unittest.TextTestRunner(verbosity=1).run(suite).wasSuccessful():
            sys.exit(1)
    else:
        sys.exit('usage: test-m236-m238-alpine-host.py [--profile m236|m237|m238]')
