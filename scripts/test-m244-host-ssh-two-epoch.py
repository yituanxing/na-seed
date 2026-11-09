#!/usr/bin/env python3
"""Fast negative/ordering tests; never fake a native M244 guest PASS."""

import argparse
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock


HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('m244_host_checker', HERE / 'm244-host-ssh-two-epoch.py')
assert spec and spec.loader
host = importlib.util.module_from_spec(spec)
spec.loader.exec_module(host)


class HostTwoBootTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='m244-host-test-')
        self.addCleanup(self.tmp.cleanup)
        base = Path(self.tmp.name)
        self.args = argparse.Namespace(
            uart=base / 'guest.serial.log', host_port=12222,
            first_receipt=base / 'first.json', second_receipt=base / 'second.json',
            client_key=base / 'client-privkey', known_hosts=base / 'known_hosts',
            ready_timeout=5, ssh_timeout=20)
        self.pubhash = 'a' * 64
        self.fingerprint = 'SHA256:' + 'A' * 43
        self.valid_session = {
            'session': 1, 'nonce': 'a' * 48, 'nonce_sha256': 'b' * 64,
            'tty_allocated': True, 'canonical_backspace': True,
            'foreground_sigint': True, 'winsize_33x101': True,
            'remote_stdout_sha256': 'c' * 64,
        }

    def test_atomic_receipt_is_guest_ssh_only(self):
        for phase in ('first', 'reopen'):
            path, text = host.ACK[phase]
            remote = host.ack_remote_command(phase)
            self.assertIn('test ! -e ' + path, remote)
            self.assertIn('> ' + path + '.pending', remote)
            self.assertIn('/bin/busybox mv ' + path + '.pending ' + path, remote)
            self.assertIn("'%s\\n' '" + text + "'", remote)
            self.assertNotIn('rm ', remote)
        with self.assertRaises(KeyError):
            host.ack_remote_command('wrong')

    def test_inputs_disallow_stale_host_or_receipts(self):
        with mock.patch.object(host.BASE, 'public_key_sha256', return_value=self.pubhash):
            self.assertEqual(host.check_inputs(self.args), self.pubhash)
            self.args.known_hosts.write_text('stale pinned key')
            with self.assertRaisesRegex(ValueError, 'previously pinned'):
                host.check_inputs(self.args)
            self.args.known_hosts.unlink()
            self.args.first_receipt.write_text('{}')
            with self.assertRaisesRegex(ValueError, 'stale host receipt'):
                host.check_inputs(self.args)

    def test_disallow_same_receipt_and_symlink_known_hosts(self):
        with mock.patch.object(host.BASE, 'public_key_sha256', return_value=self.pubhash):
            self.args.second_receipt = self.args.first_receipt
            with self.assertRaisesRegex(ValueError, 'must differ'):
                host.check_inputs(self.args)
            self.args.second_receipt = Path(self.tmp.name) / 'second.json'
            self.args.known_hosts.symlink_to(self.args.client_key)
            with self.assertRaisesRegex(ValueError, 'symlink'):
                host.check_inputs(self.args)

    def test_authenticated_real_pty_must_precede_guest_ack(self):
        events = []

        def ready(_uart, marker, _timeout):
            events.append(('ready', marker))

        def pty(*_args):
            events.append(('pty', None))
            return dict(self.valid_session)

        def known(_hosts):
            events.append(('host-key', None))
            return self.fingerprint

        def ack(*_args):
            events.append(('ssh-ack', None))
            return 'd' * 64

        with (mock.patch.object(host.BASE, 'wait_for_guest_ready', ready),
              mock.patch.object(host.PTY, 'run_session', pty),
              mock.patch.object(host.BASE, 'known_host_fingerprint', known),
              mock.patch.object(host, 'write_ack_via_ssh', ack)):
            first = host.perform_epoch('first', self.args, client_sha=self.pubhash,
                                       first=None)
            self.assertEqual(first['guest_ssh_ack_path'],
                             '/root/m244-first-host-pty.ok')
            self.assertEqual(first['sessions'][0]['foreground_sigint'], True)
            self.assertEqual(events,
                             [('ready', host.READY['first']), ('pty', None),
                              ('host-key', None), ('ssh-ack', None)])
            events.clear()
            second = host.perform_epoch('reopen', self.args,
                                        client_sha=self.pubhash, first=first)
            self.assertEqual(second['host_key_fingerprint'],
                             first['host_key_fingerprint'])
            self.assertEqual(events[0], ('ready', host.READY['reopen']))

    def test_changed_host_key_never_unblocks_second_boot(self):
        first = {'phase': 'first', 'host_port': 12222,
                 'client_pubkey_sha256': self.pubhash,
                 'host_key_fingerprint': self.fingerprint}
        with (mock.patch.object(host.BASE, 'wait_for_guest_ready'),
              mock.patch.object(host.PTY, 'run_session', return_value=self.valid_session),
              mock.patch.object(host.BASE, 'known_host_fingerprint',
                                return_value='SHA256:' + 'Z' * 43),
              mock.patch.object(host, 'write_ack_via_ssh') as ack):
            with self.assertRaisesRegex(ValueError, 'host key changed'):
                host.perform_epoch('reopen', self.args, client_sha=self.pubhash,
                                   first=first)
            ack.assert_not_called()

    def test_forged_pty_boolean_never_unblocks_reboot(self):
        corrupt = dict(self.valid_session, foreground_sigint=False)
        with (mock.patch.object(host.BASE, 'wait_for_guest_ready'),
              mock.patch.object(host.PTY, 'run_session', return_value=corrupt),
              mock.patch.object(host.BASE, 'known_host_fingerprint',
                                return_value=self.fingerprint),
              mock.patch.object(host, 'write_ack_via_ssh') as ack):
            with self.assertRaisesRegex(ValueError, 'PTY contract incomplete'):
                host.perform_epoch('first', self.args, client_sha=self.pubhash,
                                   first=None)
            ack.assert_not_called()

    def test_wrong_first_boot_receipt_rejected_before_network(self):
        with mock.patch.object(host.BASE, 'wait_for_guest_ready') as waiter:
            for first in (None, {'phase': 'reopen'},
                          {'phase': 'first', 'host_port': 12223,
                           'client_pubkey_sha256': self.pubhash},
                          {'phase': 'first', 'host_port': 12222,
                           'client_pubkey_sha256': 'b' * 64}):
                with self.assertRaises(ValueError):
                    host.perform_epoch('reopen', self.args,
                                       client_sha=self.pubhash, first=first)
            waiter.assert_not_called()

    def test_receipts_are_create_only(self):
        row = {'phase': 'first', 'pty_allocated': True}
        host.write_receipt(self.args.first_receipt, row)
        self.assertEqual(json.loads(self.args.first_receipt.read_text()), row)
        with self.assertRaisesRegex(ValueError, 'replaced'):
            host.write_receipt(self.args.first_receipt, row)

    def test_ack_must_exactly_match_remote_stdout(self):
        with (mock.patch.object(host.BASE, 'ssh_command',
                                return_value=['ssh', 'root@127.0.0.1', 'replace']),
              mock.patch.object(host.subprocess, 'run') as command):
            command.return_value = subprocess_result = mock.Mock(
                returncode=0, stdout=(host.ACK['first'][1] + '\n').encode())
            sha = host.write_ack_via_ssh('first', 12222, self.args.client_key,
                                         self.args.known_hosts, 10)
            self.assertEqual(len(sha), 64)
            argv = command.call_args[0][0]
            self.assertIn('m244-first-host-pty.ok.pending', argv[-1])
            subprocess_result.stdout = b'fake guest ACK\n'
            with self.assertRaisesRegex(ValueError, 'authenticated SSH guest receipt'):
                host.write_ack_via_ssh('first', 12222, self.args.client_key,
                                        self.args.known_hosts, 10)


if __name__ == '__main__':
    unittest.main()
