#!/usr/bin/env python3
"""Fast synthetic *negative* fixtures for M224 host, wire, disk oracles.

These tests never pretend to be a real QEMU/Alpine boot: they exercise the
independent rejection paths which the actual physical two-boot CI must pass.
"""

import hashlib
import importlib.util
import ipaddress
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch


def load(name):
    source = Path(__file__).with_name(name)
    spec = importlib.util.spec_from_file_location('test_m224_' + source.stem.replace('-', '_'), source)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


host = load('m224-tcp-server.py')
wire = load('m224-tcp-wire.py')
disk = load('m224-alpine-disk-check.py')


def packet(src, dst, sport, dport, seq, flags, body=b''):
    tcp = struct.pack('!HHIIHHHH', sport, dport, seq, 0,
                      (5 << 12) | flags, 65535, 0, 0) + body
    ipv4 = (b'\x45\x00' + struct.pack('!H', 20 + len(tcp)) +
            b'\x00\x00\x00\x00\x40\x06\x00\x00' +
            ipaddress.IPv4Address(src).packed + ipaddress.IPv4Address(dst).packed)
    return (b'\x00' * 12 + b'\x08\x00' + ipv4 + tcp)


def pcap(frames):
    data = bytearray(b'\xd4\xc3\xb2\xa1' + struct.pack('<HHIIII', 2, 4, 0, 0, 65535, 1))
    for frame in frames:
        data.extend(struct.pack('<IIII', 0, 0, len(frame), len(frame)))
        data.extend(frame)
    return bytes(data)


class Capture:
    def __init__(self, phase='first', source=None):
        self.phase = phase
        self.source = source if source is not None else bytes(range(96))
        self.flows = []

    def flow(self, service, guest_port, client=b'', server=b'', *, closed=False):
        guest = '10.0.2.15'
        gateway = '10.0.2.2'
        port = {'nmap_open': 15441, 'nmap_closed': 15442,
                'ncat': 15443, 'iperf': 15444}[service]
        packets = []
        packets.append(packet(guest, gateway, guest_port, port, 1000, 0x02))
        if closed:
            packets.append(packet(gateway, guest, port, guest_port, 2000, 0x14))
        else:
            packets.append(packet(gateway, guest, port, guest_port, 2000, 0x12))
            packets.append(packet(guest, gateway, guest_port, port, 1001, 0x10))
            if client:
                packets.append(packet(guest, gateway, guest_port, port, 1001, 0x18, client))
            if server:
                packets.append(packet(gateway, guest, port, guest_port, 2001, 0x18, server))
        self.flows.append((service, packets))
        return self

    def valid(self):
        return (self.flow('nmap_open', 12001)
                .flow('nmap_closed', 12002, closed=True)
                .flow('ncat', 12003, host.phase_token(self.phase), self.source)
                .flow('iperf', 12004, b'{"ctrl":"hello"}', b'control-response')
                .flow('iperf', 12005, b'Z' * 1024, b'data-ack'))

    def bytes(self):
        return pcap([frame for _, frames in self.flows for frame in frames])


class HostSourceTests(unittest.TestCase):
    def test_fresh_unpredictable_96_raw_bytes(self):
        with tempfile.TemporaryDirectory() as tmp:
            first = Path(tmp) / 'm224-host-first'
            second = Path(tmp) / 'm224-host-reopen'
            host.generate('first', first)
            host.generate('reopen', second)
            a, b = host.source_bytes('first', first), host.source_bytes('reopen', second)
            self.assertEqual(len(a), 96)
            self.assertEqual(len(b), 96)
            self.assertNotEqual(a, b)
            with self.assertRaises(ValueError):
                host.generate('first', first)  # stale response cannot regenerate
            class PhysicalStub:
                @staticmethod
                def debugfs_cat(_partition, path):
                    if path.endswith('host-random.bin'):
                        return a
                    if path.endswith('nmap.txt'):
                        return b'15441/tcp open tcp\n15442/tcp closed tcp\n'
                    if path.endswith('iperf3.json'):
                        return (b'{"end":{"sum_sent":{"bytes":2048,'
                                b'"bits_per_second":16384},"sum_received":{"bytes":512}}}')
                    raise RuntimeError('unexpected physical stub path: ' + path)
            with self.assertRaises(ValueError):
                disk.physical_outputs(None, PhysicalStub, first, first)  # no shared source

    def test_bad_source_and_symlinks_fail(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            first = root / 'm224-host-first'
            host.generate('first', first)
            linked = root / 'alias'
            linked.symlink_to(first, target_is_directory=True)
            with self.assertRaises(ValueError):
                host.source_bytes('first', linked)
            with self.assertRaises(ValueError):
                host.source_bytes('reopen', first)  # swapped phase directory
            (first / 'ncat.source').write_bytes(b'not-96-random')
            with self.assertRaises(ValueError):
                host.source_bytes('first', first)
            with self.assertRaises(ValueError):
                host.phase_token('fake')


class WireCaptureTests(unittest.TestCase):
    def test_independent_duplex_synthetic_positive(self):
        fixture = Capture().valid()
        proof = wire.real_tcp_proof(fixture.bytes(), 'first', fixture.source)
        self.assertEqual(proof['nmap_real_tcp_closed_resets'], 1)
        self.assertEqual(proof['ncat_real_duplex_sessions'], 1)
        self.assertEqual(proof['iperf3_real_tcp_sessions'], 2)
        self.assertGreaterEqual(proof['iperf3_guest_tcp_payload_bytes'], 1024)

    def test_no_closed_rst_rejected(self):
        fixture = Capture().valid()
        fixture.flows = [entry for entry in fixture.flows if entry[0] != 'nmap_closed']
        with self.assertRaisesRegex(ValueError, 'Nmap'):
            wire.real_tcp_proof(fixture.bytes(), 'first', fixture.source)

    def test_no_open_connect_rejected(self):
        fixture = Capture().valid()
        fixture.flows = [entry for entry in fixture.flows if entry[0] != 'nmap_open']
        with self.assertRaisesRegex(ValueError, 'Nmap'):
            wire.real_tcp_proof(fixture.bytes(), 'first', fixture.source)

    def test_ncat_wrong_phase_or_random_rejected(self):
        fixture = Capture().valid()
        with self.assertRaisesRegex(ValueError, 'Ncat'):
            wire.real_tcp_proof(fixture.bytes(), 'reopen', fixture.source)
        with self.assertRaisesRegex(ValueError, 'Ncat'):
            wire.real_tcp_proof(fixture.bytes(), 'first', b'a' * 96)

    def test_iperf_connect_only_and_no_payload_rejected(self):
        fixture = Capture().valid()
        fixture.flows = [entry for entry in fixture.flows if entry[0] != 'iperf']
        fixture.flow('iperf', 12004).flow('iperf', 12005)
        with self.assertRaisesRegex(ValueError, 'iperf3'):
            wire.real_tcp_proof(fixture.bytes(), 'first', fixture.source)

    def test_one_iperf_session_rejected(self):
        fixture = Capture().valid()
        fixture.flows.pop()
        with self.assertRaisesRegex(ValueError, 'iperf3'):
            wire.real_tcp_proof(fixture.bytes(), 'first', fixture.source)

    def test_tcp_contradictory_and_gapped_segments_fail(self):
        self.assertEqual(wire.contiguous_payload([(100, b'abcdef'), (103, b'def')]), b'abcdef')
        with self.assertRaisesRegex(ValueError, 'missing'):
            wire.contiguous_payload([(100, b'abc'), (105, b'f')])
        with self.assertRaisesRegex(ValueError, 'contradictory'):
            wire.contiguous_payload([(100, b'abcdef'), (103, b'XXX')])

    def test_pcap_bad_header_and_non_guest_traffic_fail(self):
        with self.assertRaises(ValueError):
            wire.real_tcp_proof(b'fake UART PASS', 'first', bytes(range(96)))
        fixture = Capture().valid()
        data = fixture.bytes().replace(ipaddress.IPv4Address('10.0.2.2').packed,
                                       ipaddress.IPv4Address('198.51.100.2').packed)
        with self.assertRaises(ValueError):
            wire.real_tcp_proof(data, 'first', fixture.source)


class IndependentHostLogTests(unittest.TestCase):
    def sample_logs(self, phase='first', source=None):
        source = source if source is not None else bytes(range(96))
        token = host.phase_token(phase)
        tcp = ('[M224 HOST READY] phase=' + phase + ' nmap=15441 closed=15442 ncat=15443\n' +
               'M224_HOST_NMAP_ACCEPT phase=' + phase + ' port=15441 peer=127.0.0.1\n' +
               'M224_HOST_NCAT_EXCHANGE phase=' + phase + ' port=15443 ' +
               f'rx_bytes={len(token)} rx_sha256={hashlib.sha256(token).hexdigest()} ' +
               f'tx_bytes={len(source)} tx_sha256={hashlib.sha256(source).hexdigest()} peer=127.0.0.1\n')
        iperf = ('Server listening on 15444 (test #1)\n' +
                 '[  5]   0.00-1.00   sec   1.00 MBytes  8.39 Mbits/sec  receiver\n')
        return tcp, iperf

    def test_host_nonzero_receiver_and_signed_receipt(self):
        tcp, iperf = self.sample_logs()
        proof = wire.host_log_proof('first', tcp, iperf, bytes(range(96)))
        self.assertTrue(proof['host_original_iperf3_nonzero_receiver'])

    def test_host_zero_receiver_or_no_exchange_rejected(self):
        tcp, iperf = self.sample_logs()
        for bad_tcp, bad_iperf in ((tcp, iperf.replace('1.00 MBytes', '0.00 MBytes')),
                                   (tcp.replace('M224_HOST_NCAT_EXCHANGE', 'DISABLED'), iperf),
                                   (tcp.replace('phase=first', 'phase=reopen'), iperf),
                                   (tcp + '[M224 HOST DENY] invalid\n', iperf)):
            with self.subTest(tcp=bad_tcp[-30:], iperf=bad_iperf[-30:]):
                with self.assertRaises(ValueError):
                    wire.host_log_proof('first', bad_tcp, bad_iperf, bytes(range(96)))


class PhysicalWitnessTests(unittest.TestCase):
    def test_iperf_error_rejected_even_if_real_bytes_were_sent(self):
        # The first repaired QEMU really delivered 5120 bytes to the host,
        # but original iperf3 also returned a top-level JSON error. Host
        # traffic and an unrelated sum_sent must never mask that failure.
        report = (b'{"error":"original client control failed",'
                  b'"end":{"sum_sent":{"bytes":5120,"bits_per_second":40000},'
                  b'"sum_received":{"bytes":5120}}}')
        with self.assertRaisesRegex(ValueError, 'no real transfer'):
            disk.iperf3_bytes(report, 'first')

    def test_nmap_physical_requires_both_original_port_lines(self):
        real = b'PORT     STATE SERVICE\n15441/tcp open  unknown\n15442/tcp closed unknown\n'
        self.assertTrue(disk.nmap_port_contrast(real))
        for fake in (b'', real.replace(b'closed', b'filtered'),
                     real.replace(b'open', b'filtered'),
                     real.replace(b'15442/tcp', b'15445/tcp')):
            with self.subTest(fake=fake):
                self.assertFalse(disk.nmap_port_contrast(fake))

    @staticmethod
    def fake_physical(source_first, source_reopen, *, wrong_random=False):
        class Ext4:
            @staticmethod
            def debugfs_cat(_partition, path):
                if path.endswith('host-random.bin'):
                    data = source_first if '.first/' in path else source_reopen
                    return data[:-1] + b'X' if wrong_random else data
                if path.endswith('nmap.txt'):
                    return b'15441/tcp open tcp\n15442/tcp closed tcp\n'
                if path.endswith('iperf3.json'):
                    return (b'{"end":{"sum_sent":{"bytes":2048,'
                            b'"bits_per_second":16384},"sum_received":{"bytes":512}}}')
                raise RuntimeError('unexpected fake debugfs path')
        return Ext4

    def test_two_separate_physical_boots_require_exact_random_response(self):
        with tempfile.TemporaryDirectory() as tmp:
            first, reopen = Path(tmp) / 'm224-host-first', Path(tmp) / 'm224-host-reopen'
            host.generate('first', first)
            host.generate('reopen', reopen)
            a = host.source_bytes('first', first)
            b = host.source_bytes('reopen', reopen)
            proof = disk.physical_outputs(None, self.fake_physical(a, b), first, reopen)
            self.assertNotEqual(proof['ncat_source_sha256']['first'],
                                proof['ncat_source_sha256']['reopen'])
            with self.assertRaisesRegex(ValueError, 'differs'):
                disk.physical_outputs(None, self.fake_physical(a, b, wrong_random=True),
                                      first, reopen)

    def test_iperf_client_empty_or_fake_json_rejected(self):
        for value in (b'{}', b'{"error":"unable to connect"}',
                      b'{"end":{"sum_sent":{"bytes":0,"bits_per_second":0},"sum_received":{"bytes":0}}}',
                      b'{"end":{"sum_sent":{"bytes":2048,"bits_per_second":NaN},"sum_received":{"bytes":512}}}',
                      b'{"end":{"sum_sent":{"bytes":2048,"bits_per_second":100},"sum_received":{"bytes":true}}}',
                      b'garbage'):
            with self.assertRaises(ValueError):
                disk.iperf3_bytes(value, 'first')

    def test_iperf_real_transfer_shape(self):
        self.assertEqual(disk.iperf3_bytes(b'{"end":{"sum_sent":{"bytes":2048,'
                                           b'"bits_per_second":16384},"sum_received":{"bytes":512}}}',
                                           'first'), 2048)

    def test_official_inventory_must_equal_every_physical_elf_sha(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            table = root / 'build/external/m224-candidate-inventory/amd64/candidates.tsv'
            table.parent.mkdir(parents=True)
            manifest = root / 'scripts/m224-candidate-elfs.tsv'
            manifest.parent.mkdir()
            names = [('nmap', 'usr/bin/nmap', b'\x7fELF_NMAP'),
                     ('iperf3', 'usr/bin/iperf3', b'\x7fELF_IPERF3'),
                     ('nmap-ncat', 'usr/bin/ncat', b'\x7fELF_NCAT')]
            manifest.write_text('package\telf_path\tfamily\tprobe\n' +
                                ''.join(f'{name}\t{path}\ttcp\tprobe\n' for name, path, _ in names))
            rows = ['package\telf_path\tstatus\tsha256'] + [
                f'{name}\t{path}\tNATIVE_ELF\t{hashlib.sha256(blob).hexdigest()}'
                for name, path, blob in names]
            table.write_text('\n'.join(rows) + '\n')
            sources = {'/' + path: data for _, path, data in names}

            class SignedPhysical:
                @staticmethod
                def debugfs_cat(_partition, path):
                    return sources[path]

            disk.signed_inventory_sha('amd64', None, SignedPhysical, root)
            sources['/usr/bin/ncat'] = b'\x7fELF_FORGED'
            with self.assertRaisesRegex(ValueError, 'differs'):
                disk.signed_inventory_sha('amd64', None, SignedPhysical, root)
            sources['/usr/bin/ncat'] = names[2][2]
            table.write_text('\n'.join(rows[:3]) + '\n')
            with self.assertRaisesRegex(ValueError, 'missing/duplicate'):
                disk.signed_inventory_sha('amd64', None, SignedPhysical, root)


if __name__ == '__main__':
    unittest.main(verbosity=2)
