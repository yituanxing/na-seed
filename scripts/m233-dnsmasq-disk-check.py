#!/usr/bin/env python3
"""Independent DNS UDP+TCP host queries, two NIC captures, signed ext4 APK.

No UART success, forged host JSON or standalone PCAP can substitute for the
same challenged DNS bytes across the original physical NIC and host socket.
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


def load(name: str, alias: str):
    spec = importlib.util.spec_from_file_location(alias, Path(__file__).with_name(name))
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


M227 = load('m227-alpine-disk-check.py', 'm233_m227')
M223 = load('m223-http-wire-check.py', 'm233_m223')
HOST = load('m233-host-dns-check.py', 'm233_host')
M191 = M227.M191
HOST_IP = ipaddress.IPv4Address('10.0.2.2')
GUEST_NET = ipaddress.IPv4Network('10.0.2.0/24')
TS = re.compile(r'20[0-9]{2}-[01][0-9]-[0-3][0-9]T[0-2][0-9]:[0-5][0-9]:[0-5][0-9]Z\Z')
PACKAGES = ('dnsmasq', 'dnsmasq-openrc')
CONFIG = (b'port=15453', b'listen-address=0.0.0.0', b'bind-interfaces',
          b'no-resolv', b'no-hosts', b'no-poll', b'log-facility=-',
          b'user=root', b'group=root',
          b'local=/m233.naseed.test/',
          b'address=/m233.naseed.test/198.51.100.233')


def fail(message: str) -> None:
    raise ValueError('[M233 ORIGINAL DNS FAIL] ' + message)


def read_receipt(path: Path, phase: str) -> dict:
    if path.is_symlink() or not path.is_file():
        fail('missing independent ' + phase + ' DNS host receipt')
    try:
        data = json.loads(path.read_text())
    except (OSError, json.JSONDecodeError) as exc:
        fail('bad host JSON: ' + str(exc))
    fixed = {'phase': phase, 'host': '127.0.0.1',
             'guest_port': HOST.GUEST_PORT, 'authoritative_A': '198.51.100.233'}
    if not isinstance(data, dict) or any(data.get(k) != v or
             type(data.get(k)) is not type(v) for k, v in fixed.items()):
        fail('host DNS receipt has wrong phase/identity')
    for key in ('udp_host_port', 'tcp_host_port'):
        if type(data.get(key)) is not int or not 1 <= data[key] <= 65535:
            fail('invalid host DNS forward port')
    if not isinstance(data.get('utc'), str) or TS.fullmatch(data['utc']) is None:
        fail('missing independent UTC receipt')
    rows = data.get('dns_queries')
    if not isinstance(rows, dict) or set(rows) != {'udp', 'tcp'}:
        fail('missing real host UDP or TCP DNS')
    for protocol, row in rows.items():
        if not isinstance(row, dict) or set(row) != {
                'nonce', 'transaction_id', 'attempts', 'request_hex',
                'response_hex', 'response_sha256'}:
            fail('bad original DNS ' + protocol + ' wire record')
        nonce, txid = row.get('nonce'), row.get('transaction_id')
        if not isinstance(nonce, str) or HOST.HEX24.fullmatch(nonce) is None or \
           not isinstance(txid, str) or HOST.HEX4.fullmatch(txid) is None:
            fail('non-random host DNS challenge encoding')
        if type(row.get('attempts')) is not int or not 1 <= row['attempts'] <= 100:
            fail('invalid bounded live host DNS attempt count')
        try:
            request = bytes.fromhex(row['request_hex'])
            response = bytes.fromhex(row['response_hex'])
        except (TypeError, ValueError) as exc:
            fail('invalid host DNS packet encoding: ' + str(exc))
        if request != HOST.query(nonce, int(txid, 16)):
            fail('DNS request is not fresh host-owned question')
        HOST.validate_response(request, response)
        if row['response_sha256'] != hashlib.sha256(response).hexdigest():
            fail('host DNS socket response digest differs')
    if rows['udp']['nonce'] == rows['tcp']['nonce'] or \
       rows['udp']['transaction_id'] == rows['tcp']['transaction_id']:
        fail('UDP/TCP reused original host DNS challenge')
    return data


def udp_packets(raw: bytes):
    for index, frame in enumerate(M223.pcap.pcap_frames(raw)):
        pkt = M223.pcap.ipv4_packet(frame)
        if pkt is None:
            continue
        src, dst, proto, data = pkt
        if proto != 17 or len(data) < 8:
            continue
        sport, dport, length, _ = struct.unpack_from('!HHHH', data)
        if length < 8 or length > len(data):
            continue
        payload = data[8:length]
        inbound = (src == HOST_IP and dst in GUEST_NET and dst != HOST_IP and
                   dport == HOST.GUEST_PORT and sport >= 1024)
        outbound = (src in GUEST_NET and src != HOST_IP and dst == HOST_IP and
                    sport == HOST.GUEST_PORT and dport >= 1024)
        if inbound or outbound:
            yield index, inbound, (str(dst), sport) if inbound else (str(src), dport), payload


def tcp_flows(raw: bytes):
    flows = {}
    for index, frame in enumerate(M223.pcap.pcap_frames(raw)):
        pkt = M223.pcap.ipv4_packet(frame)
        if pkt is None:
            continue
        src, dst, proto, tcp = pkt
        if proto != 6 or len(tcp) < 20:
            continue
        sport, dport, seq = struct.unpack_from('!HHI', tcp)
        header = (tcp[12] >> 4) * 4
        if header < 20 or header > len(tcp):
            continue
        flags = tcp[13]
        incoming = (src == HOST_IP and dst in GUEST_NET and dst != HOST_IP and
                    dport == HOST.GUEST_PORT and sport >= 1024)
        outgoing = (src in GUEST_NET and src != HOST_IP and dst == HOST_IP and
                    sport == HOST.GUEST_PORT and dport >= 1024)
        if not (incoming or outgoing):
            continue
        key = (str(dst), sport) if incoming else (str(src), dport)
        flow = flows.setdefault(key, {'syn': [], 'synack': [], 'ack': [],
                                      'request': [], 'response': []})
        if incoming:
            if flags & 0x02 and not flags & 0x10:
                flow['syn'].append(index)
            if flags & 0x10 and not flags & 0x02:
                flow['ack'].append(index)
            if len(tcp) > header:
                flow['request'].append((seq, tcp[header:]))
        else:
            if flags & 0x12 == 0x12:
                flow['synack'].append(index)
            if len(tcp) > header:
                flow['response'].append((seq, tcp[header:]))
    for flow in flows.values():
        if flow['syn'] and flow['synack'] and flow['ack'] and \
           min(flow['syn']) < min(flow['synack']) < min(flow['ack']):
            yield (M223.contiguous_payload(flow['request']),
                   M223.contiguous_payload(flow['response']))


def verify_wire(first: Path, reopen: Path, cap1: Path, cap2: Path):
    paths = (first, reopen, cap1, cap2)
    if any(p.is_symlink() or not p.is_file() for p in paths) or \
       len({str(p.resolve()) for p in paths}) != 4:
        fail('overlapping or missing independent DNS/NIC evidence')
    a, b = read_receipt(first, 'first'), read_receipt(reopen, 'reopen')
    if a['utc'] >= b['utc'] or any(a[x] != b[x] for x in
       ('udp_host_port', 'tcp_host_port')):
        fail('no distinct chronological host DNS QEMU reboots')
    challenges = [(v['nonce'], v['transaction_id']) for data in (a, b)
                  for v in data['dns_queries'].values()]
    if len(set(challenges)) != 4:
        fail('replayed host DNS challenge across reboots')
    caps = (cap1.read_bytes(), cap2.read_bytes())
    if not all(caps) or hashlib.sha256(caps[0]).digest() == hashlib.sha256(caps[1]).digest():
        fail('missing or duplicated physical NIC capture')
    for receipt, cap in zip((a, b), caps):
        udp = list(udp_packets(cap))
        row = receipt['dns_queries']['udp']
        request, response = bytes.fromhex(row['request_hex']), bytes.fromhex(row['response_hex'])
        if not any(inc and payload == request and any(not inc2 and later > before and
             key2 == key and payload2 == response
             for later, inc2, key2, payload2 in udp)
             for before, inc, key, payload in udp):
            fail(receipt['phase'] + ' no complete real host→guest UDP DNS in NIC PCAP')
        row = receipt['dns_queries']['tcp']
        request, response = bytes.fromhex(row['request_hex']), bytes.fromhex(row['response_hex'])
        wire_req, wire_rsp = (struct.pack('!H', len(request)) + request,
                             struct.pack('!H', len(response)) + response)
        if not any(req == wire_req and rsp == wire_rsp for req, rsp in tcp_flows(cap)):
            fail(receipt['phase'] + ' no full TCP three-way DNS framing in NIC PCAP')
    return a, {'first': hashlib.sha256(caps[0]).hexdigest(),
               'reopen': hashlib.sha256(caps[1]).hexdigest()}


def verify(port: str, root: Path, disk: Path, first: Path, reopen: Path,
           cap1: Path, cap2: Path):
    if port not in M191.ARCH_MACHINE:
        fail('unsupported signed Alpine ISA')
    if root.is_symlink() or disk.is_symlink():
        fail('symlinked root or disk input')
    receipt, captures = verify_wire(first, reopen, cap1, cap2)
    root, disk = root.resolve(strict=True), disk.resolve(strict=True)
    for path in ('/etc/runlevels/default/dnsmasq', '/usr/sbin/dnsmasq'):
        original = M191.guest_path(root, path)
        if original.exists() or original.is_symlink():
            fail('baseline preseeded dnsmasq package or runlevel')
    M227.verify(port, root, disk)
    with tempfile.TemporaryDirectory(prefix='m233-original-dns-ext4-') as temp:
        part = Path(temp) / 'root.ext4'
        M191.extract_partition(disk, part)
        read = lambda path: M191.debugfs_cat(part, path)
        world = read('/etc/apk/world').decode()
        installed = b'\n' + read('/lib/apk/db/installed')
        for package in PACKAGES:
            if package not in world.splitlines() or b'\nP:' + package.encode() + b'\n' not in installed:
                fail('original signed APK missing: ' + package)
        image = read('/usr/sbin/dnsmasq')
        if (image[:6] != b'\x7fELF\x02\x01' or len(image) < 20 or
                struct.unpack_from('<H', image, 18)[0] != M191.ARCH_MACHINE[port]):
            fail('missing original target ISA dnsmasq ELF')
        if not read('/etc/init.d/dnsmasq').startswith(b'#!'):
            fail('original dnsmasq OpenRC script missing')
        config = read('/etc/dnsmasq.conf').splitlines()
        if any(line not in config for line in CONFIG):
            fail('guest-created authoritative DNS config lost')
        stat = subprocess.run(['debugfs', '-R', 'stat /etc/runlevels/default/dnsmasq',
                               str(part)], capture_output=True, text=True, check=True)
        if 'Type: symlink' not in stat.stdout or 'dnsmasq' not in stat.stdout:
            fail('original DNS OpenRC link absent on physical ext4')
    return {'port': port, 'original_signed_dns_apks': list(PACKAGES),
            'original_openrc_after_2_native_qemu_boots': True,
            'real_host_random_dns_challenge': True,
            'dns_transport': ['udp', 'tcp'], 'guest_port': HOST.GUEST_PORT,
            'host_udp_port': receipt['udp_host_port'],
            'host_tcp_port': receipt['tcp_host_port'],
            'native_nic_pcap_sha256': captures,
            'host_loopback_test_only_not_production_resolver': True}


def main() -> None:
    if len(sys.argv) != 9:
        raise SystemExit('usage: m233-dnsmasq-disk-check.py ISA ROOT DISK FIRST.json REOPEN.json FIRST.pcap REOPEN.pcap OUT.json')
    port = sys.argv[1]
    root, disk, first, reopen, cap1, cap2, out = map(Path, sys.argv[2:])
    if out.exists() or out.is_symlink() or out.resolve(strict=False) in \
       {p.resolve(strict=False) for p in (root, disk, first, reopen, cap1, cap2)}:
        fail('stale or overlapping physical disk oracle')
    result = verify(port, root, disk, first, reopen, cap1, cap2)
    out.write_text(json.dumps(result, sort_keys=True, indent=2) + '\n')
    print(f'[M233 ORIGINAL DNSMASQ OPENRC HOST UDP+TCP TWO BOOT PASS] {port}: '
          'independent DNS questions and replies in both real NIC PCAPs, '
          'original signed Alpine APK/ELF, original OpenRC and strict ext4 fsck')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as exc:
        raise SystemExit('[M233 FINAL ORACLE FAIL] ' + str(exc)) from exc
