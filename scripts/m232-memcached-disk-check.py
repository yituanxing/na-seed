#!/usr/bin/env python3
"""Strict memcached volatile reboot gate: two original Alpine PID1 QEMUs.

Host receipts must match both actual VirtIO-Net NIC TCP streams and the same
signed Alpine ext4/OpenRC configuration after a genuine cold reboot.
"""
import hashlib
import importlib.util
import ipaddress
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile


def load(filename: str, alias: str):
    spec = importlib.util.spec_from_file_location(alias, Path(__file__).with_name(filename))
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


M227 = load('m227-alpine-disk-check.py', 'm232_original_alpine_openrc')
M223 = load('m223-http-wire-check.py', 'm232_original_nic_tcp')
HOST = load('m232-host-cache-check.py', 'm232_original_memcached_host')
M191 = M227.M191
HOST_IP = ipaddress.IPv4Address('10.0.2.2')
GUEST_NET = ipaddress.IPv4Network('10.0.2.0/24')
PACKAGES = ('memcached', 'memcached-openrc')
TS = re.compile(r'20[0-9]{2}-[01][0-9]-[0-3][0-9]T[0-2][0-9]:[0-5][0-9]:[0-5][0-9]Z\Z')


def fail(message: str) -> None:
    raise ValueError('[M232 ORIGINAL CACHE FAIL] ' + message)


def read_receipt(path: Path, phase: str) -> dict:
    if path.is_symlink() or not path.is_file():
        fail('missing independently generated real host ' + phase + ' receipt')
    data = json.loads(path.read_text())
    if not isinstance(data, dict):
        fail('host receipt is not JSON object')
    fixed = {'phase': phase, 'transport': HOST.TRANSPORT,
             'host': '127.0.0.1', 'guest_port': HOST.GUEST_PORT,
             'first_key_vanished_after_cold_reboot': phase == 'reopen',
             'original_cache_first_get_miss': True, 'host_set_stored': True,
             'host_get_exact_value': True,
             'test_only_root_daemon_and_localhost_ingress': True}
    for name, value in fixed.items():
        if type(data.get(name)) is not type(value) or data[name] != value:
            fail('memcached host receipt claim invalid: ' + name)
    for name in ('host_port', 'attempts'):
        n = data.get(name)
        if type(n) is not int or not 1 <= n <= (65535 if name == 'host_port' else 9999):
            fail('invalid original host network integer: ' + name)
    key, value = data.get('key'), data.get('value')
    if not isinstance(key, str) or not isinstance(value, str):
        fail('missing host random cache key/value')
    request, response = HOST.wire_messages(key, value)
    if data.get('request_sha256') != hashlib.sha256(request).hexdigest() or \
            data.get('response_sha256') != hashlib.sha256(response).hexdigest() or \
            data.get('value_sha256') != hashlib.sha256(value.encode()).hexdigest():
        fail('forged cache request/response/value hash')
    if not isinstance(data.get('timestamp_utc'), str) or not TS.fullmatch(data['timestamp_utc']):
        fail('missing original host timestamp')
    return data


def flow_payloads(capture: bytes) -> list[tuple[bytes, bytes]]:
    flows = {}
    for index, frame in enumerate(M223.pcap.pcap_frames(capture)):
        packet = M223.pcap.ipv4_packet(frame)
        if packet is None:
            continue
        src, dst, protocol, tcp = packet
        if protocol != 6 or len(tcp) < 20:
            continue
        sport, dport, sequence = struct.unpack_from('!HHI', tcp)
        start = (tcp[12] >> 4) * 4
        if start < 20 or start > len(tcp):
            continue
        flags = tcp[13]
        inbound = (src == HOST_IP and dst in GUEST_NET and dst != HOST_IP and
                   dport == HOST.GUEST_PORT and sport >= 1024)
        outbound = (src in GUEST_NET and src != HOST_IP and dst == HOST_IP and
                    sport == HOST.GUEST_PORT and dport >= 1024)
        if not inbound and not outbound:
            continue
        key = (str(dst), sport) if inbound else (str(src), dport)
        flow = flows.setdefault(key, {'syn': [], 'synack': [], 'ack': [],
                                      'request': [], 'response': []})
        if inbound:
            if flags & 0x02 and not flags & 0x10:
                flow['syn'].append(index)
            if flags & 0x10 and not flags & 0x02:
                flow['ack'].append(index)
            if len(tcp) > start:
                flow['request'].append((sequence, tcp[start:]))
        else:
            if flags & 0x12 == 0x12:
                flow['synack'].append(index)
            if len(tcp) > start:
                flow['response'].append((sequence, tcp[start:]))
    result = []
    for flow in flows.values():
        if flow['syn'] and flow['synack'] and flow['ack'] and \
                min(flow['syn']) < min(flow['synack']) < min(flow['ack']):
            result.append((M223.contiguous_payload(flow['request']),
                           M223.contiguous_payload(flow['response'])))
    return result


def wire_verify(first: Path, reopen: Path, pcap1: Path,
                pcap2: Path) -> tuple[dict, dict, dict]:
    inputs = (first, reopen, pcap1, pcap2)
    if any(p.is_symlink() or not p.is_file() for p in inputs) or \
            len({str(p.resolve()) for p in inputs}) != 4:
        fail('missing/overlapping host receipts and two native NIC captures')
    a, b = read_receipt(first, 'first'), read_receipt(reopen, 'reopen')
    if a['key'] != b['key'] or a['value'] == b['value'] or \
            a['timestamp_utc'] >= b['timestamp_utc'] or a['host_port'] != b['host_port']:
        fail('replay or non-independent cache challenge across reboot')
    caps = (pcap1.read_bytes(), pcap2.read_bytes())
    if not all(caps) or hashlib.sha256(caps[0]).digest() == hashlib.sha256(caps[1]).digest():
        fail('identical/replayed or missing NIC PCAPs')
    for receipt, capture in zip((a, b), caps):
        expected = HOST.wire_messages(receipt['key'], receipt['value'])
        if not any(flow == expected for flow in flow_payloads(capture)):
            fail(receipt['phase'] + ' has no complete real host memcached TCP '
                 'miss/SET/GET with original response data')
    return a, b, {'original_memcached_physical_PCAP_SHA256':
                  {'first': hashlib.sha256(caps[0]).hexdigest(),
                   'reopen': hashlib.sha256(caps[1]).hexdigest()}}


def verify(port: str, root: Path, disk: Path, first: Path, reopen: Path,
           cap1: Path, cap2: Path) -> dict:
    if port not in M191.ARCH_MACHINE:
        fail('not an accepted native Alpine architecture')
    if root.is_symlink() or disk.is_symlink():
        fail('overlapping or symlinked signed rootfs/physical disk')
    a, b, wire = wire_verify(first, reopen, cap1, cap2)
    root, disk = root.resolve(strict=True), disk.resolve(strict=True)
    for path in ('/etc/runlevels/default/memcached', '/root/m232-cache-record'):
        original = M191.guest_path(root, path)
        if original.exists() or original.is_symlink():
            fail('original Alpine root preseeded memcached state')
    M227.verify(port, root, disk)  # Checks real original Alpine/OpenRC/ext4/fsck.
    with tempfile.TemporaryDirectory(prefix='m232-original-cache-ext4-') as tmp:
        partition = Path(tmp) / 'root.ext4'
        M191.extract_partition(disk, partition)
        def read(path): return M191.debugfs_cat(partition, path)
        world = read('/etc/apk/world').decode('utf-8')
        installed = b'\n' + read('/lib/apk/db/installed')
        for package in PACKAGES:
            if package not in world.splitlines() or b'\nP:' + package.encode() + b'\n' not in installed:
                fail('missing official signed Alpine package: ' + package)
        binary = read('/usr/bin/memcached')
        if (len(binary) < 20 or binary[:6] != b'\x7fELF\x02\x01' or
                struct.unpack_from('<H', binary, 18)[0] != M191.ARCH_MACHINE[port]):
            fail('original Alpine memcached wrong ISA or non-ELF')
        if not read('/etc/init.d/memcached').startswith(b'#!'):
            fail('original Alpine memcached OpenRC script missing')
        config = read('/etc/conf.d/memcached')
        for line in (b'MEMCACHED_RUNAS="root"', b'LISTENON="0.0.0.0"',
                     b'PORT="15448"', b'UDPPORT="0"', b'MEMUSAGE="8"',
                     b'MAXCONN="64"', b'MISC_OPTS="-t 1"'):
            if line not in config.splitlines():
                fail('guest-created original OpenRC service config missing: ' + line.decode())
        stat = subprocess.run(['debugfs', '-R', 'stat /etc/runlevels/default/memcached',
                               str(partition)], capture_output=True, text=True, check=True)
        if 'Type: symlink' not in stat.stdout or 'memcached' not in stat.stdout:
            fail('original OpenRC runlevel service not persisted across cold reboot')
    return {'native_port': port, 'original_signed_apk': list(PACKAGES),
            'original_OpenRC_auto_reopen': True,
            'first_cache_key_SHA256': hashlib.sha256(a['key'].encode()).hexdigest(),
            'first_value_sha256': a['value_sha256'],
            'new_value_after_cold_reboot_sha256': b['value_sha256'],
            'cold_reboot_cache_miss_then_new_host_SET_GET': True,
            'physical_read_only_ext4_fsck': True,
            'test_only_root_localhost_ingress': True, **wire}


def main() -> None:
    if len(sys.argv) != 9:
        raise SystemExit('usage: m232-memcached-disk-check.py ISA ROOT DISK FIRST.json REOPEN.json FIRST.pcap REOPEN.pcap OUT.json')
    port = sys.argv[1]
    root, disk, first, second, cap1, cap2, out = map(Path, sys.argv[2:])
    if out.exists() or out.is_symlink() or out.resolve(strict=False) in \
            {p.resolve(strict=False) for p in (root, disk, first, second, cap1, cap2)}:
        fail('stale/overlapping final independent result')
    result = verify(port, root, disk, first, second, cap1, cap2)
    out.write_text(json.dumps(result, sort_keys=True, indent=2) + '\n')
    print(f'[M232 ORIGINAL ALPINE MEMCACHED HOST VOLATILE TWO BOOT PASS] {port} '
          'two native QEMUs, original signed memcached/OpenRC, external real '
          'host miss/SET/GET then reboot cache miss/new SET/GET, dual PCAPs '
          'and original physical ext4/fsck, root/localhost TEST ONLY')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, RuntimeError, subprocess.CalledProcessError) as exc:
        raise SystemExit('[M232 FINAL ORACLE FAIL] ' + str(exc)) from exc
