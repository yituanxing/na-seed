#!/usr/bin/env python3
"""Negative UNIT fixtures for original signed memcached two-boot host oracle."""
import hashlib
import importlib.util
import ipaddress
import json
from pathlib import Path
import struct
import tempfile


def load(filename, name):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


host = load('m232-host-cache-check.py', 'm232_host_unit')
disk = load('m232-memcached-disk-check.py', 'm232_disk_unit')
KEY = 'naseed_m232_' + 'a' * 48


def reject(fn):
    try:
        fn()
    except (ValueError, TimeoutError):
        return
    raise AssertionError('fake host memcached evidence accepted')


def contract(probe, workflow, guest):
    required_probe = (
        'm232=${NAS_M232_MEMCACHED_HOST:-0}',
        'NAS_VIRTIO_NET_GUESTFWD_PORT=15448',
        'm232_start_host first "$serial"',
        'm232_finish_host first',
        'm232_start_host reopen "$serial"',
        'm232_finish_host reopen',
        'scripts/m232-alpine-uart.py reopen',
        'M232_REOPEN_HOST_CACHE_WINDOW_COMPLETE',
        '"$PWD/$work/m232-reopen-wire.pcap"',
        'scripts/m232-memcached-disk-check.py',
        'm232-memcached-final-oracle.json',
    )
    required_workflow = (
        'NAS_M232_MEMCACHED_HOST:',
        'scripts/test-m232-memcached.py',
        'm232-first-host-cache.json',
        'm232-reopen-host-cache.json',
        'm232-reopen-wire.pcap',
        'm232-memcached-final-oracle.json',
    )
    required_guest = (
        'apk info -e "$package"',
        'MEMCACHED_RUNAS="root"',
        'LISTENON="0.0.0.0"',
        'rc-update add memcached default',
        'rc-service memcached start',
        'M232_REOPEN_HOST_CACHE_READY',
    )
    for kind, source, needed in [('probe', probe, required_probe),
                                 ('workflow', workflow, required_workflow),
                                 ('guest', guest, required_guest)]:
        for line in needed:
            if line not in source:
                raise ValueError('M232 ' + kind + ' loses mandatory native evidence: ' + line)
    if 'REOPEN)\n        rc-service memcached start' in guest:
        raise ValueError('reopen must use original OpenRC auto-start')


def ethernet(src, dst, sport, dport, seq, flags, payload=b''):
    addresses = [ipaddress.IPv4Address(value).packed for value in (src, dst)]
    tcp = struct.pack('!HHIIHHHH', sport, dport, seq, 0,
                      (5 << 12) | flags, 65535, 0, 0) + payload
    ip = struct.pack('!BBHHHBBH4s4s', 0x45, 0, 20+len(tcp), 0, 0,
                     64, 6, 0, *addresses)
    return b'\xff'*6+b'\x01'*6+b'\x08\x00'+ip+tcp


def capture(frames):
    result = bytearray(struct.pack('<IHHIIII', 0xa1b2c3d4, 2, 4, 0, 0, 65535, 1))
    for frame in frames:
        result.extend(struct.pack('<IIII', 0, 0, len(frame), len(frame)))
        result.extend(frame)
    return bytes(result)


def traffic(port, request, response):
    host_ip, guest_ip = '10.0.2.2', '10.0.2.15'
    return capture([
        ethernet(host_ip, guest_ip, port, host.GUEST_PORT, 1000, 0x02),
        ethernet(guest_ip, host_ip, host.GUEST_PORT, port, 9000, 0x12),
        ethernet(host_ip, guest_ip, port, host.GUEST_PORT, 1001, 0x10),
        ethernet(host_ip, guest_ip, port, host.GUEST_PORT, 1001, 0x18, request),
        ethernet(guest_ip, host_ip, host.GUEST_PORT, port, 9001, 0x18, response),
    ])


def fixture(phase):
    value = ('b' if phase == 'first' else 'c') * 64
    request, response = host.wire_messages(KEY, value)
    receipt = {
        'phase': phase, 'transport': host.TRANSPORT,
        'host': '127.0.0.1', 'host_port': host.GUEST_PORT,
        'guest_port': host.GUEST_PORT, 'key': KEY, 'value': value,
        'value_sha256': hashlib.sha256(value.encode()).hexdigest(),
        'first_key_vanished_after_cold_reboot': phase == 'reopen',
        'original_cache_first_get_miss': True, 'host_set_stored': True,
        'host_get_exact_value': True,
        'test_only_root_daemon_and_localhost_ingress': True,
        'request_sha256': hashlib.sha256(request).hexdigest(),
        'response_sha256': hashlib.sha256(response).hexdigest(),
        'attempts': 1,
        'timestamp_utc': '2026-11-07T12:00:00Z' if phase == 'first' else
                         '2026-11-07T12:01:00Z',
    }
    return receipt, traffic(48111, request, response)


def main():
    root = Path(__file__).resolve().parent.parent
    probe = (root/'scripts/probe-m197-alpine-software-cohort.sh').read_text()
    workflow = (root/'.github/workflows/m232-memcached-host-cache.yml').read_text()
    guest = (root/'tests/m232-alpine-memcached-workload.sh').read_text()
    contract(probe, workflow, guest)
    reject(lambda: contract(probe.replace('m232_start_host reopen "$serial"', '# NOOP'),
                            workflow, guest))
    reject(lambda: contract(probe, workflow.replace('m232-reopen-wire.pcap',
                                                  'm232-reused-first-wire.pcap'), guest))
    reject(lambda: contract(probe, workflow, guest.replace('rc-update add memcached default',
                                                         'true # bypass original OpenRC')))
    assert host.wire_messages(KEY, 'f'*64)[0].startswith(b'get naseed_m232_')
    reject(lambda: host.wire_messages('badkey', 'a'*64))
    reject(lambda: host.wire_messages(KEY, '\n'*64))
    first, a = fixture('first')
    second, b = fixture('reopen')
    assert len(disk.flow_payloads(a)) == 1
    assert len(disk.flow_payloads(b)) == 1
    with tempfile.TemporaryDirectory(prefix='m232-cache-fake-negative-') as folder:
        paths = [Path(folder)/name for name in ('first.json', 'reopen.json',
                                               'first.pcap', 'reopen.pcap')]
        def write(x=first, y=second, p=a, q=b):
            paths[0].write_text(json.dumps(x))
            paths[1].write_text(json.dumps(y))
            paths[2].write_bytes(p)
            paths[3].write_bytes(q)
        write()
        assert disk.wire_verify(*paths)[0]['phase'] == 'first'
        reject(lambda: disk.wire_verify(paths[0], paths[0], paths[2], paths[3]))
        write(y=dict(second, value=first['value']))
        reject(lambda: disk.wire_verify(*paths))
        write(y=dict(second, first_key_vanished_after_cold_reboot=False))
        reject(lambda: disk.wire_verify(*paths))
        write(x=dict(first, request_sha256='0'*64))
        reject(lambda: disk.wire_verify(*paths))
        write(q=a)
        reject(lambda: disk.wire_verify(*paths))
        request, response = host.wire_messages(KEY, first['value'])
        write(p=traffic(48111, request, response.replace(b'END\r\n', b'HIT\r\n', 1)))
        reject(lambda: disk.wire_verify(*paths))
        write()
        assert disk.wire_verify(*paths)[1]['phase'] == 'reopen'
    print('[M232 MEMCACHED NEGATIVE UNIT PASS] strict probe/workflow, genuine '
          'host TCP GET-miss/SET/GET on both QEMUs, rejected PCAP replay; '
          'UNIT FIXTURES ONLY, no native QEMU PASS')


if __name__ == '__main__':
    main()
