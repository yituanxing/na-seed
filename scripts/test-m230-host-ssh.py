#!/usr/bin/env python3
"""M230 fast synthetic host/PCAP contract UNIT; never a native-QEMU SSH PASS."""

import hashlib
import importlib.util
import ipaddress
import json
from pathlib import Path
import struct
import subprocess
import tempfile
from threading import Thread
import time


def load(filename: str, name: str):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    assert spec and spec.loader
    obj = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(obj)
    return obj


host = load('m230-host-ssh-check.py', 'm230_host_unit')
disk = load('m230-dropbear-disk-check.py', 'm230_wire_unit')


def reject(work):
    try:
        work()
    except (OSError, ValueError, TimeoutError):
        pass
    else:
        raise AssertionError('unsafe/false original Dropbear evidence accepted')


def tcp_frame(src, dst, sport, dport, seq, flags, content=b''):
    src_ip, dst_ip = [ipaddress.IPv4Address(x).packed for x in (src, dst)]
    tcp = struct.pack('!HHIIHHHH', sport, dport, seq, 0,
                      (5 << 12) | flags, 65535, 0, 0) + content
    ipv4 = struct.pack('!BBHHHBBH4s4s', 0x45, 0, 20 + len(tcp),
                       0, 0, 64, 6, 0, src_ip, dst_ip)
    return b'\xff' * 6 + b'\x01' * 6 + b'\x08\x00' + ipv4 + tcp


def pcap(frames):
    raw = bytearray(struct.pack('<IHHIIII', 0xa1b2c3d4, 2, 4, 0, 0, 65535, 1))
    for frame in frames:
        raw.extend(struct.pack('<IIII', 0, 0, len(frame), len(frame)))
        raw.extend(frame)
    return bytes(raw)


def make_capture(client_port, extra):
    host_ip, guest_ip = '10.0.2.2', '10.0.2.15'
    client = b'SSH-2.0-OpenSSH_9.9p1\r\n' + b'\x00' * (310 + extra)
    server = b'SSH-2.0-dropbear_2025.88\r\n' + b'\x01' * (350 + extra)
    frames = [
        tcp_frame(host_ip, guest_ip, client_port, host.GUEST_PORT, 1000, 0x02),
        tcp_frame(guest_ip, host_ip, host.GUEST_PORT, client_port, 9000, 0x12),
        tcp_frame(host_ip, guest_ip, client_port, host.GUEST_PORT, 1001, 0x10),
        tcp_frame(host_ip, guest_ip, client_port, host.GUEST_PORT, 1001, 0x18, client),
        tcp_frame(guest_ip, host_ip, host.GUEST_PORT, client_port, 9001, 0x18, server),
    ]
    return pcap(frames)


def receipt(phase, nonce, keysha, fp, timestamp):
    output = ('M230-SSH-REMOTE-EXEC:' + nonce + '\n').encode()
    return {'phase': phase, 'transport': host.TRANSPORT, 'host': '127.0.0.1',
            'host_port': 12222, 'guest_port': host.GUEST_PORT,
            'nonce': nonce, 'nonce_sha256': hashlib.sha256(nonce.encode()).hexdigest(),
            'remote_command_sha256': hashlib.sha256(host.remote_command(nonce).encode()).hexdigest(),
            'remote_output_sha256': hashlib.sha256(output).hexdigest(),
            'remote_command_success': True, 'public_key_only': True,
            'root_only_isolated_test_no_DAC': True, 'pty_allocated': False,
            'host_key_fingerprint': fp, 'client_pubkey_sha256': keysha,
            'attempts': 1, 'timestamp_utc': timestamp}


def check():
    for invalid in ('', '0', '-1', '65536', '1.1', '22\n', '２２'):
        reject(lambda x=invalid: host.valid_port(x))
    assert host.valid_port('12222') == 12222
    assert host.GUEST_PORT == 15446
    for phase in ('first', 'reopen'):
        options = host.ssh_command(phase, 12222, Path('/tmp/only-host-key'),
                                   Path('/tmp/only-host-known-hosts'), 'a' * 48, 30)
        joined = ' '.join(options)
        assert 'StrictHostKeyChecking=' + ('accept-new' if phase == 'first' else 'yes') in joined
        for rule in ('BatchMode=yes', 'PasswordAuthentication=no',
                     'PreferredAuthentications=publickey', 'RequestTTY=no',
                     'ForwardAgent=no', 'ClearAllForwardings=yes',
                     'HostKeyAlgorithms=ssh-ed25519'):
            assert rule in joined
        assert 'ssh -F /dev/null -T' in joined
        assert options[-1] == host.remote_command('a' * 48)
    with tempfile.TemporaryDirectory(prefix='m230-ssh-unit-only-') as tmp:
        base = Path(tmp)
        private = base / 'host-client-ed25519'
        server = base / 'fake-host-ed25519'
        subprocess.run(['ssh-keygen', '-q', '-t', 'ed25519', '-N', '', '-f', str(private)], check=True)
        subprocess.run(['ssh-keygen', '-q', '-t', 'ed25519', '-N', '', '-f', str(server)], check=True)
        keysha = host.public_key_sha256(private)
        pinned = base / 'known_hosts'
        server_fields = Path(str(server) + '.pub').read_text().split()
        pinned.write_text('m230-na-seed ' + ' '.join(server_fields[:2]) + '\n')
        fingerprint = host.known_host_fingerprint(pinned)
        assert host.FINGERPRINT.fullmatch(fingerprint)
        uart = base / 'uart'
        marker = host.READY['first']
        def append():
            time.sleep(0.1)
            uart.write_bytes(b'fake prefix ' + marker + b' not-ready\n' + marker[:12])
            time.sleep(0.1)
            with uart.open('ab') as out:
                out.write(marker[12:] + b'\r\n')
        worker = Thread(target=append)
        worker.start()
        host.wait_for_guest_ready(uart, marker, 3)
        worker.join()
        nonce1, nonce2 = 'a' * 48, 'b' * 48
        a = receipt('first', nonce1, keysha, fingerprint, '2026-11-07T13:00:00Z')
        b = receipt('reopen', nonce2, keysha, fingerprint, '2026-11-07T13:02:00Z')
        first, reopen = base / 'first.json', base / 'reopen.json'
        pcap1, pcap2 = base / 'first.pcap', base / 'reopen.pcap'
        first.write_text(json.dumps(a))
        reopen.write_text(json.dumps(b))
        pcap1.write_bytes(make_capture(49152, 0))
        pcap2.write_bytes(make_capture(49153, 2))
        assert host.first_receipt(first)['nonce'] == nonce1
        assert disk.read_receipt(first, 'first')['remote_command_success']
        result = disk.verify_transport(first, reopen, pcap1, pcap2)[1]
        assert result['host_pinned_dropbear_key_reused']
        assert result['genuine_ssh_public_key_remote_commands'] == 2
        # An SSH client may retry a failed session; reject a missing PCAP,
        # but do not falsely reject two original handshakes as forged data.
        multiple = pcap(list(disk.M223.pcap.pcap_frames(make_capture(49152, 0))) +
                        list(disk.M223.pcap.pcap_frames(make_capture(49154, 3))))
        assert disk.physical_ssh_flow(multiple, 'first')['matching_guest_ssh_tcp_sessions'] == 2
        reject(lambda: disk.physical_ssh_flow(pcap([]), 'first'))
        reject(lambda: disk.physical_ssh_flow(pcap1.read_bytes()[:-20], 'first'))
        b['host_key_fingerprint'] = 'SHA256:' + 'x' * 43
        reopen.write_text(json.dumps(b))
        reject(lambda: disk.verify_transport(first, reopen, pcap1, pcap2))
        b['host_key_fingerprint'] = fingerprint
        b['remote_output_sha256'] = '0' * 64
        reopen.write_text(json.dumps(b))
        reject(lambda: disk.verify_transport(first, reopen, pcap1, pcap2))
        b['remote_output_sha256'] = hashlib.sha256(('M230-SSH-REMOTE-EXEC:' + nonce2 + '\n').encode()).hexdigest()
        reopen.write_text(json.dumps(b))
        pcap2.write_bytes(pcap1.read_bytes())
        reject(lambda: disk.verify_transport(first, reopen, pcap1, pcap2))
        pcap2.write_bytes(make_capture(49153, 2))
        assert disk.verify_transport(first, reopen, pcap1, pcap2)[1]['host_pinned_dropbear_key_reused']
    print('[M230 HOST SSH UNIT PASS] key-only/TOFU-to-strict pinning, distinct '
          'host challenges and two synthetic NIC SSH handshakes; UNIT ONLY, '
          'no native QEMU or security acceptance claim')


if __name__ == '__main__':
    check()
