#!/usr/bin/env python3
"""M225 real socket smoke + fail-closed host/PCAP/ext4 negative fixtures.

Real FTP/HTTP transfers below run over *host* TCP sockets only.  They are
never represented as signed Alpine/native QEMU acceptance.
"""

import contextlib
import ftplib
import io
from importlib.util import module_from_spec, spec_from_file_location
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from threading import Thread
import unittest
from unittest import mock
from urllib.request import Request, urlopen


def load(name):
    spec = spec_from_file_location('m225_test_' + name.replace('-', '_'),
                                   Path(__file__).with_name(name))
    module = module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


host = load('m225-host-services.py')
proof = load('m225-host-proof.py')
ledger = load('m225-compat-report.py')
candidates = load('check-m141-candidates.py')


class RealHostTransactions(unittest.TestCase):
    def test_original_host_lftp_get_put_before_long_native_qemu(self):
        # Host distro lftp is a protocol compatibility preflight only, never
        # proof of original signed guest ELF/ISA execution. The targeted x86
        # workflow MUST install it; local workstations may explicitly skip.
        if shutil.which('lftp') is None:
            if os.environ.get('NAS_M225_REQUIRE_HOST_LFTP') == '1':
                self.fail('[M225 HOST FAIL] mandatory original host lftp missing')
            self.skipTest('host lftp not installed; x86 CI forces installation')
        with tempfile.TemporaryDirectory(prefix='m225-host-lftp-') as tmp:
            directory = Path(tmp) / 'm225-host-first'
            host.generate('first', directory)
            ftp, http = host.servers('first', directory, ftp_port=0, rest_port=0)
            download = Path(tmp) / 'lftp-download.bin'
            script = ('set ftp:ssl-allow no; set ftp:prefer-epsv yes; '
                      'set net:max-retries 1; set net:timeout 8; '
                      f'get /lftp.source -o {download}; '
                      f'put {download} -o /lftp.upload; bye')
            stdout = io.StringIO()
            threads = [Thread(target=server.serve_forever, daemon=True)
                       for server in (ftp, http)]
            try:
                with contextlib.redirect_stdout(stdout):
                    for thread in threads:
                        thread.start()
                    result = subprocess.run(
                        ['lftp', '-p', str(ftp.server_address[1]),
                         '-u', 'anonymous,m225-disposable', '127.0.0.1', '-e', script],
                        capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stderr[-1000:])
                challenge = host.source('first', directory, 'lftp')
                self.assertEqual(download.read_bytes(), challenge)
                self.assertEqual((directory / 'lftp.upload').read_bytes(), challenge)
                self.assertIn('M225_HOST_FTP phase=first op=RETR item=lftp', stdout.getvalue())
                self.assertIn('M225_HOST_FTP phase=first op=STOR item=lftp', stdout.getvalue())
            finally:
                for server in (ftp, http):
                    server.shutdown()
                    server.server_close()

    @unittest.skipUnless(shutil.which('rclone'), 'host rclone optional command compatibility')
    def test_host_rclone_ftp_transfer_commands_not_guest_acceptance(self):
        """Native host rclone is a protocol smoke, never signed Alpine proof."""
        with tempfile.TemporaryDirectory(prefix='m225-host-rclone-') as tmp:
            directory = Path(tmp) / 'm225-host-first'
            host.generate('first', directory)
            ftp, http = host.servers('first', directory, ftp_port=0, rest_port=0)
            obscured = subprocess.run(['rclone', 'obscure', 'm225-disposable'],
                                      capture_output=True, text=True, check=True).stdout.strip()
            remote = (':ftp,host=127.0.0.1,port=%d,user=anonymous,pass=%s:' %
                      (ftp.server_address[1], obscured))
            local = Path(tmp) / 'rclone-download.bin'
            workers = [Thread(target=s.serve_forever, daemon=True) for s in (ftp, http)]
            try:
                for worker in workers:
                    worker.start()
                opts = ['--ftp-disable-mlsd', '--transfers', '1', '--checkers', '1',
                        '--retries', '1', '--low-level-retries', '1', '--contimeout', '5s']
                for source, destination in ((remote + 'rclone.source', str(local)),
                                            (str(local), remote + 'rclone.upload')):
                    result = subprocess.run(['rclone', 'copyto', source, destination, *opts],
                                            capture_output=True, text=True, timeout=25)
                    self.assertEqual(result.returncode, 0, result.stderr[-1200:])
                self.assertEqual(local.read_bytes(), host.source('first', directory, 'rclone'))
                self.assertEqual((directory / 'rclone.upload').read_bytes(), local.read_bytes())
            finally:
                for server in (ftp, http):
                    server.shutdown()
                    server.server_close()

    def test_two_separate_phase_sockets_and_random_replay_rejection(self):
        signatures = {}
        with tempfile.TemporaryDirectory(prefix='m225-real-host-smoke-') as tmp:
            root = Path(tmp)
            for phase in host.PHASES:
                directory = root / ('m225-host-' + phase)
                host.generate(phase, directory)
                signatures[phase] = {name: host.digest(host.source(phase, directory, name))
                                     for name in host.ITEMS}
                with self.assertRaisesRegex(ValueError, 'stale'):
                    host.generate(phase, directory)
                ftp, http = host.servers(phase, directory, ftp_port=0, rest_port=0)
                ftp_port = ftp.server_address[1]
                rest_port = http.server_address[1]
                threads = [Thread(target=server.serve_forever, daemon=True)
                           for server in (ftp, http)]
                captured = io.StringIO()
                try:
                    with contextlib.redirect_stdout(captured):
                        for thread in threads:
                            thread.start()
                        print(f'[M225 HOST READY] phase={phase} ftp={ftp_port} rest={rest_port}')
                        for item in host.FTP_ITEMS:
                            with ftplib.FTP() as client:
                                client.connect('127.0.0.1', ftp_port, timeout=5)
                                client.login('anonymous', 'test@example.invalid')
                                downloaded = bytearray()
                                client.retrbinary(f'RETR /{item}.source', downloaded.extend)
                                self.assertEqual(bytes(downloaded), host.source(phase, directory, item))
                                client.storbinary(f'STOR /{item}.upload', io.BytesIO(downloaded))
                        with urlopen(f'http://127.0.0.1:{rest_port}/restic.source', timeout=5) as response:
                            payload = response.read()
                            self.assertEqual(response.status, 200)
                        self.assertEqual(payload, host.source(phase, directory, 'restic'))
                        req = Request(f'http://127.0.0.1:{rest_port}/restic.upload',
                                      data=payload, method='PUT')
                        with urlopen(req, timeout=5) as response:
                            self.assertEqual(response.status, 201)
                        self.assertEqual(proof.receipts(phase, directory, captured.getvalue(),
                                                        ftp_port=ftp_port,
                                                        rest_port=rest_port)['host_real_receipts'], 6)
                finally:
                    for server in (ftp, http):
                        server.shutdown()
                        server.server_close()
                # A stale upload may not overwrite the first real data receipt.
                with self.assertRaises(FileExistsError):
                    host.upload(phase, directory, 'restic', payload)
                with self.assertRaisesRegex(ValueError, 'source phase'):
                    host.source('reopen' if phase == 'first' else 'first', directory, 'restic')
                for item in host.ITEMS:
                    self.assertEqual((directory / (item + '.upload')).read_bytes(),
                                     host.source(phase, directory, item))
            self.assertNotEqual(signatures['first'], signatures['reopen'])
            paired = proof.two_phase_sources(root / 'm225-host-first',
                                             root / 'm225-host-reopen')
            self.assertEqual(len(paired), 3)
            # Replacing a fresh second-boot challenge with an earlier one is
            # not a valid second independent QEMU transaction.
            for item in host.ITEMS:
                first = root / 'm225-host-first' / (item + '.source')
                second = root / 'm225-host-reopen' / (item + '.source')
                original = second.read_bytes()
                try:
                    second.write_bytes(first.read_bytes())
                    with self.subTest(item=item), self.assertRaisesRegex(ValueError, 'reused'):
                        proof.two_phase_sources(root / 'm225-host-first',
                                                root / 'm225-host-reopen')
                finally:
                    second.write_bytes(original)

    def test_invalid_actual_upload_cannot_become_receipt(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp) / 'm225-host-first'
            host.generate('first', directory)
            for item in host.ITEMS:
                with self.subTest(item=item):
                    with self.assertRaisesRegex(ValueError, 'DENY'):
                        host.upload('first', directory, item, b'fake guest PASS')
                    self.assertFalse((directory / (item + '.upload')).exists())


class IndependentNegativeEvidence(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='m225-oracle-negative-')
        self.directory = Path(self.temp.name) / 'm225-host-first'
        host.generate('first', self.directory)
        for item in host.ITEMS:
            host.upload('first', self.directory, item, host.source('first', self.directory, item))

    def tearDown(self):
        self.temp.cleanup()

    def host_log(self):
        lines = ['[M225 HOST READY] phase=first ftp=15451 rest=15452']
        lines.extend(f'M225_HOST_FTP_PASSIVE phase=first port={p}' for p in (45401, 45402, 45403, 45404))
        for item in host.FTP_ITEMS:
            for op in ('RETR', 'STOR'):
                data = host.source('first', self.directory, item)
                lines.append(f'M225_HOST_FTP phase=first op={op} item={item} '
                             f'bytes={len(data)} sha256={host.digest(data)}')
        for op in ('GET', 'PUT'):
            data = host.source('first', self.directory, 'restic')
            lines.append(f'M225_HOST_REST phase=first op={op} item=restic '
                         f'bytes={len(data)} sha256={host.digest(data)}')
        return '\n'.join(lines) + '\n'

    def test_valid_receipt_fixture_is_not_native_qemu_proof(self):
        self.assertEqual(proof.receipts('first', self.directory, self.host_log())['host_real_receipts'], 6)
        with self.assertRaises(ValueError):
            proof.real_wire(b'guest says PASS', 'first', self.directory, [45401, 45402, 45403, 45404])

    def test_tcp_data_without_advertised_ftp_passive_ports_is_not_accepted(self):
        # Negative-only stub: a forged host-supplied dynamic port with some
        # bytes on it is insufficient without actual EPSV/PASV in guest PCAP.
        paths = [
            {'port': host.FTP_PORT, 'client': b'RETR /lftp.source\r\nSTOR /lftp.upload\r\n'
             b'RETR /rclone.source\r\nSTOR /rclone.upload\r\n',
             'server': b'220 Ready\r\n'},
            {'port': 45401, 'client': host.source('first', self.directory, 'lftp'),
             'server': host.source('first', self.directory, 'lftp')},
            {'port': 45402, 'client': host.source('first', self.directory, 'rclone'),
             'server': host.source('first', self.directory, 'rclone')},
        ]
        with mock.patch.object(proof, 'flows', return_value=paths):
            with self.assertRaisesRegex(ValueError, 'EPSV/PASV'):
                proof.real_wire(b'not an actual native QEMU PCAP', 'first', self.directory,
                                [45401, 45402, 45403, 45404], ftp_only=True)

    def test_host_receipts_reject_phase_duplicate_false_hash_and_missing_transport(self):
        original = self.host_log()
        candidates = (
            original.replace('phase=first', 'phase=reopen', 1),
            original.replace('op=RETR', 'op=STOR', 1),
            original.replace('sha256=', 'sha256=0', 1),
            original.replace('op=GET', 'op=PUT', 1),
            original.replace('M225_HOST_FTP_PASSIVE', 'BROKEN', 1),
            original + '[M225 HOST DENY] tampered upload\n',
            original + original.splitlines()[-1] + '\n',
        )
        for changed in candidates:
            with self.subTest(changed=changed[-65:]):
                with self.assertRaisesRegex(ValueError, 'M225 HOST FAIL'):
                    proof.receipts('first', self.directory, changed)

    def test_physical_ext4_mismatch_rejected_for_each_program(self):
        def read(path):
            item = path.split('/')[3].split('.')[0]
            return host.source('first', self.directory, item)
        self.assertEqual(len(proof.physical_files('first', self.directory, read)
                             ['physical_ext4_exact_challenges']), 3)
        for broken in host.ITEMS:
            def read_bad(path):
                return b'fake' if f'/{broken}.first/' in path else read(path)
            with self.subTest(broken=broken):
                with self.assertRaisesRegex(ValueError, 'M225 DISK FAIL'):
                    proof.physical_files('first', self.directory, read_bad)

    def test_upload_replay_and_symlink_rejected(self):
        copied = self.directory / 'lftp.upload'
        copied.write_bytes(b'wrong')
        with self.assertRaisesRegex(ValueError, 'M225 HOST FAIL'):
            proof.receipts('first', self.directory, self.host_log())
        copied.unlink()
        copied.symlink_to(self.directory / 'lftp.source')
        with self.assertRaisesRegex(ValueError, 'M225 HOST FAIL'):
            proof.receipts('first', self.directory, self.host_log())


class SignedGuestBoundaryNegatives(unittest.TestCase):
    def test_original_ftp_ledger_requires_both_real_clients_each_boot(self):
        path = Path(__file__).with_name('m225-candidate-elfs.tsv')
        cases = ledger.owner.manifest_cases(path)
        self.assertEqual(len(cases), 2)
        first = 'M225_FIRST_DISCOVERY_COMPLETE\n'
        reopen = 'M225_REOPEN_DISCOVERY_COMPLETE\n'
        for item in cases:
            pair = f'{item["package"]}\t{item["elf_path"]}'
            first += f'M225_RESULT\t{pair}\tinstall\tPASS\n'
            first += f'M225_RESULT\t{pair}\tfunctional\tPASS\n'
            reopen += f'M225_RESULT\t{pair}\treopen\tPASS\n'
        result = ledger.owner.build_report(cases, first, reopen)
        self.assertEqual(result['newly_installed_full_pass_entrypoints'], 2)
        self.assertEqual(result['preexisting'], 0)
        with self.assertRaisesRegex(ValueError, 'missing/extra records'):
            ledger.owner.build_report(cases, first, reopen.replace('M225_RESULT\trclone',
                                                                   'M225_MISSING\trclone'))
        with self.assertRaisesRegex(ValueError, 'completion marker'):
            ledger.owner.build_report(cases, first, reopen.replace('M225_REOPEN_DISCOVERY_COMPLETE',
                                                                   'M225_FIRST_DISCOVERY_COMPLETE'))
        # A fake guest PASS but originally preinstalled executable never
        # qualifies; strict --require-all must reject non-new installation.
        old = first.replace('M225_RESULT\trclone\tusr/bin/rclone\tinstall\tPASS',
                            'M225_RESULT\trclone\tusr/bin/rclone\tinstall\tPREEXISTING')
        report = ledger.owner.build_report(cases, old, reopen)
        self.assertEqual(report['newly_installed_full_pass_entrypoints'], 1)
        self.assertEqual(report['preexisting'], 1)

    def test_historical_sha_path_exception_is_exact_not_generic(self):
        original = Path(__file__).resolve().parent.parent
        with tempfile.TemporaryDirectory(prefix='m225-history-gate-') as tmp:
            root = Path(tmp)
            (root / 'scripts').mkdir()
            (root / 'rootfs/alpine').mkdir(parents=True)
            (root / 'rootfs/alpine/packages-m225-candidates.txt').write_text('lftp\nrclone\n')
            for name in ('m225-candidate-elfs.tsv', 'm205-candidate-elfs.tsv',
                         'm204-alpine-cohort.tsv', 'm206-candidate-elfs.tsv',
                         'm215-candidate-elfs.tsv', 'm218-candidate-elfs.tsv',
                         'm219-candidate-elfs.tsv', 'm220-candidate-elfs.tsv',
                         'm221-candidate-elfs.tsv', 'm222-candidate-elfs.tsv',
                         'm223-candidate-elfs.tsv', 'm224-candidate-elfs.tsv'):
                shutil.copyfile(original / 'scripts' / name, root / 'scripts' / name)
            (root / 'scripts/m137-original-elfs.txt').write_text('usr/bin/rclone\n')
            (root / 'scripts/m149-original-elfs.txt').write_text('usr/bin/lftp\n')
            with mock.patch.object(candidates, 'ROOT', root):
                candidates.main('m225', 2)
                # rclone may be promoted ONLY from its specific M137 local
                # baseline, never ignored when another accepted cohort owns it.
                (root / 'scripts/m149-original-elfs.txt').write_text(
                    'usr/bin/lftp\nusr/bin/rclone\n')
                with self.assertRaisesRegex(ValueError, 'already covered'):
                    candidates.main('m225', 2)
                (root / 'scripts/m149-original-elfs.txt').write_text('usr/bin/lftp\n')
                (root / 'scripts/m137-original-elfs.txt').write_text(
                    'usr/bin/rclone\nusr/bin/lftp\n')
                with self.assertRaisesRegex(ValueError, 'already covered'):
                    candidates.main('m225', 2)


if __name__ == '__main__':
    unittest.main(verbosity=2)
