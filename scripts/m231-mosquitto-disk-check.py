#!/usr/bin/env python3
"""Strict original Alpine Mosquitto/OpenRC, true host MQTT, two-QEMU ext4.

This final gate requires independent host receipts, both physical NIC PCAPs,
original signed ELF/APK and exact retained bytes in physical ext4 broker DB.
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
    obj = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(obj)
    return obj


M227 = load('m227-alpine-disk-check.py', 'm231_openrc_ext4')
M223 = load('m223-http-wire-check.py', 'm231_actual_nic')
HOST = load('m231-host-mqtt-check.py', 'm231_host_wire')
M191 = M227.M191
HOST_IP = ipaddress.IPv4Address('10.0.2.2')
GUEST_NET = ipaddress.IPv4Network('10.0.2.0/24')
PACKAGES = ('mosquitto', 'mosquitto-clients', 'mosquitto-openrc')
BINARIES = ('/usr/sbin/mosquitto', '/usr/bin/mosquitto_pub', '/usr/bin/mosquitto_sub')
RDB = '/var/lib/mosquitto/m231.db'
TS = re.compile(r'20[0-9]{2}-[01][0-9]-[0-3][0-9]T[0-2][0-9]:[0-5][0-9]:[0-5][0-9]Z\Z')


def fail(message: str) -> None:
    raise ValueError('[M231 ORIGINAL MQTT FAIL] ' + message)


def read_receipt(path: Path, phase: str) -> dict:
    if path.is_symlink() or not path.is_file():
        fail('missing independent ' + phase + ' host receipt')
    try:
        data = json.loads(path.read_text())
    except (OSError, json.JSONDecodeError) as exc:
        fail('bad original host receipt: ' + str(exc))
    if not isinstance(data, dict):
        fail('host receipt is not an object')
    fixed = {'phase': phase, 'transport': HOST.TRANSPORT,
             'host': '127.0.0.1', 'guest_port': HOST.GUEST_PORT,
             'retained_qos1_published': phase == 'first',
             'retained_new_subscription': True,
             'unrestricted_mqtt_test_only_no_auth': True}
    for field, value in fixed.items():
        if type(data.get(field)) is not type(value) or data[field] != value:
            fail('original host MQTT receipt mismatch: ' + field)
    for field in ('host_port', 'attempts'):
        n = data.get(field)
        if type(n) is not int or not (1 <= n <= (65535 if field == 'host_port' else 9999)):
            fail('invalid host network ' + field)
    nonce, topic, value = (data.get(x) for x in ('nonce', 'topic', 'value'))
    if (not isinstance(nonce, str) or HOST.HEX48.fullmatch(nonce) is None or
            not isinstance(topic, str) or not topic.startswith('naseed/m231/') or
            HOST.HEX48.fullmatch(topic[12:]) is None or
            not isinstance(value, str) or HOST.HEX64.fullmatch(value) is None or
            data.get('value_sha256') != hashlib.sha256(value.encode()).hexdigest()):
        fail('invalid host random MQTT challenge')
    if phase == 'first' and nonce != topic[12:]:
        fail('first PUB challenge is not host-owned topic')
    wire = HOST.wire_messages(phase, nonce, topic, value)
    for kind in ('request', 'response'):
        expected = {name: hashlib.sha256(bytes_).hexdigest()
                    for name, bytes_ in wire.items() if kind in name}
        if data.get(kind + '_sha256') != expected:
            fail('host MQTT ' + kind + ' digest forgery')
    if not isinstance(data.get('timestamp_utc'), str) or not TS.fullmatch(data['timestamp_utc']):
        fail('host MQTT timestamp missing')
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
        inbound = src == HOST_IP and dst in GUEST_NET and dst != HOST_IP and \
            dport == HOST.GUEST_PORT and sport >= 1024
        outbound = src in GUEST_NET and src != HOST_IP and dst == HOST_IP and \
            sport == HOST.GUEST_PORT and dport >= 1024
        if not (inbound or outbound):
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
    complete = []
    for flow in flows.values():
        if (flow['syn'] and flow['synack'] and flow['ack'] and
                min(flow['syn']) < min(flow['synack']) < min(flow['ack'])):
            complete.append((M223.contiguous_payload(flow['request']),
                             M223.contiguous_payload(flow['response'])))
    return complete


def wire_verify(first: Path, reopen: Path, pcap1: Path, pcap2: Path) -> tuple[dict, dict, dict]:
    inputs = (first, reopen, pcap1, pcap2)
    if any(p.is_symlink() or not p.is_file() for p in inputs) or len({str(p.resolve()) for p in inputs}) != 4:
        fail('overlapping/missing native host and NIC evidence')
    a, b = read_receipt(first, 'first'), read_receipt(reopen, 'reopen')
    if (a['topic'] != b['topic'] or a['value'] != b['value'] or
            a['nonce'] == b['nonce'] or a['timestamp_utc'] >= b['timestamp_utc'] or
            a['host_port'] != b['host_port']):
        fail('independent retained MQTT two-boot challenge differs')
    caps = (pcap1.read_bytes(), pcap2.read_bytes())
    if not all(caps) or hashlib.sha256(caps[0]).digest() == hashlib.sha256(caps[1]).digest():
        fail('two independent real NIC PCAPs absent or identical')
    for receipt, capture in zip((a, b), caps):
        flows = flow_payloads(capture)
        expected = HOST.wire_messages(receipt['phase'], receipt['nonce'],
                                      receipt['topic'], receipt['value'])
        for name in ('subscriber', 'publisher') if receipt['phase'] == 'first' else ('subscriber',):
            if not any(req == expected[name + '_request'] and
                       resp == expected[name + '_response'] for req, resp in flows):
                fail(receipt['phase'] + ' lacks true physical MQTT ' + name +
                     ' CONNECT, QoS1 RETAIN and SUBACK/delivered PUBLISH')
    return a, b, {'host_real_physical_MQTT_PCAP_SHA256':
                  {'first': hashlib.sha256(caps[0]).hexdigest(),
                   'reopen': hashlib.sha256(caps[1]).hexdigest()}}


def verify(port: str, root: Path, disk: Path, first: Path, reopen: Path,
           pcap1: Path, pcap2: Path) -> dict:
    if port not in M191.ARCH_MACHINE:
        fail('unsupported signed native ISA')
    if any(p.is_symlink() for p in (root, disk)):
        fail('overlapping/symlinked signed original root or disk')
    a, _, wire = wire_verify(first, reopen, pcap1, pcap2)
    root, disk = root.resolve(strict=True), disk.resolve(strict=True)
    for path in ('/etc/mosquitto/m231.conf', '/etc/runlevels/default/mosquitto', RDB):
        original = M191.guest_path(root, path)
        if original.exists() or original.is_symlink():
            fail('signed baseline preseeded Mosquitto test state')
    M227.verify(port, root, disk)
    with tempfile.TemporaryDirectory(prefix='m231-original-mqtt-ext4-') as tmp:
        part = Path(tmp) / 'root.ext4'
        M191.extract_partition(disk, part)
        def read(path): return M191.debugfs_cat(part, path)
        world = read('/etc/apk/world').decode('utf-8')
        installed = b'\n' + read('/lib/apk/db/installed')
        for package in PACKAGES:
            if package not in world.splitlines() or b'\nP:' + package.encode() + b'\n' not in installed:
                fail('missing original signed package: ' + package)
        for binary in BINARIES:
            image = read(binary)
            if (len(image) < 20 or image[:6] != b'\x7fELF\x02\x01' or
                    struct.unpack_from('<H', image, 18)[0] != M191.ARCH_MACHINE[port]):
                fail('missing original target ELF: ' + binary)
        if not read('/etc/init.d/mosquitto').startswith(b'#!'):
            fail('original Alpine Mosquitto OpenRC script absent')
        if b'cfgfile="/etc/mosquitto/m231.conf"' not in read('/etc/conf.d/mosquitto'):
            fail('original OpenRC does not select guest-generated config')
        cfg = read('/etc/mosquitto/m231.conf')
        for line in (b'listener 15447 0.0.0.0', b'protocol mqtt', b'allow_anonymous true',
                     b'user root', b'persistence true', b'persistence_location /var/lib/mosquitto/',
                     b'persistence_file m231.db', b'autosave_interval 1'):
            if line not in cfg.splitlines():
                fail('actual Mosquitto config changed: ' + line.decode())
        stat = subprocess.run(['debugfs', '-R', 'stat /etc/runlevels/default/mosquitto',
                               str(part)], check=True, capture_output=True, text=True)
        if 'Type: symlink' not in stat.stdout or 'mosquitto' not in stat.stdout:
            fail('original default runlevel broker link missing')
        data = read(RDB)
        if len(data) < 64 or a['topic'].encode() not in data or a['value'].encode() not in data:
            fail('real physical ext4 broker DB lacks first HOST retained MQTT bytes')
    return {'port': port, 'original_alpine_signed_MQTT': list(PACKAGES),
            'real_original_openrc_auto_reopen': True,
            'real_persisted_retained_topic_sha256': hashlib.sha256(a['topic'].encode()).hexdigest(),
            'real_persisted_retained_value_sha256': a['value_sha256'],
            'physical_broker_DB_bytes': len(data), 'physical_broker_DB_sha256': hashlib.sha256(data).hexdigest(),
            'isolated_localhost_only_no_MQTT_auth_or_nonroot_DAC': True, **wire}


def main() -> None:
    if len(sys.argv) != 9:
        raise SystemExit('usage: m231-mosquitto-disk-check.py ISA ROOT DISK FIRST.json REOPEN.json FIRST.pcap REOPEN.pcap OUT.json')
    port = sys.argv[1]
    root, disk, first, reopen, cap1, cap2, out = map(Path, sys.argv[2:])
    if out.exists() or out.is_symlink() or out.resolve(strict=False) in \
            {p.resolve(strict=False) for p in (root, disk, first, reopen, cap1, cap2)}:
        fail('stale/overlapping final receipt')
    result = verify(port, root, disk, first, reopen, cap1, cap2)
    out.write_text(json.dumps(result, sort_keys=True, indent=2) + '\n')
    print(f'[M231 ORIGINAL MOSQUITTO OPENRC RETAINED MQTT TWO BOOT PASS] {port} '
          'two native PID1 QEMUs, host MQTT QoS1 retained publish/reboot '
          'subscribe, two independent NIC PCAPs and physical ext4/fsck; '
          'test-only root/anonymous MQTT, NOT production security')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as exc:
        raise SystemExit('[M231 FINAL ORACLE FAIL] ' + str(exc)) from exc
