#!/usr/bin/env python3
"""UNIT fixtures: reject fake retained MQTT evidence; NEVER QEMU PASS."""
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


host = load('m231-host-mqtt-check.py', 'm231_host_unit')
disk = load('m231-mosquitto-disk-check.py', 'm231_disk_unit')
TOPIC = 'naseed/m231/' + 'a' * 48
VALUE = 'b' * 64


def contract(probe, workflow, guest):
    """No success path without real host MQTT, SECOND QEMU and NIC PCAP."""
    required_probe = (
        'm231=${NAS_M231_MOSQUITTO_HOST:-0}',
        'NAS_VIRTIO_NET_GUESTFWD_PORT=15447',
        'm231_start_host first "$serial"',
        'm231_finish_host first',
        'm231_start_host reopen "$serial"',
        'm231_finish_host reopen',
        'scripts/m231-alpine-uart.py reopen',
        'M231_REOPEN_HOST_MQTT_WINDOW_COMPLETE',
        '"$PWD/$work/m231-reopen-wire.pcap"',
        'scripts/m231-mosquitto-disk-check.py',
        'm231-mosquitto-final-oracle.json',
    )
    required_workflow = (
        'NAS_M231_MOSQUITTO_HOST:',
        'scripts/test-m231-mosquitto.py',
        'm231-first-host-mqtt.json',
        'm231-reopen-host-mqtt.json',
        'm231-reopen-wire.pcap',
        'm231-mosquitto-final-oracle.json',
    )
    required_guest = (
        'cfg=/etc/mosquitto/m231.conf',
        'user root',
        'persistence true',
        'autosave_interval 1',
        'cfgfile="/etc/mosquitto/m231.conf"',
        'rc-update add mosquitto default',
        'rc-service mosquitto start',
        'M231_REOPEN_HOST_MQTT_READY',
    )
    for kind, source, lines in [('probe', probe, required_probe),
                                ('workflow', workflow, required_workflow),
                                ('guest', guest, required_guest)]:
        for text in lines:
            if text not in source:
                raise ValueError('M231 ' + kind + ' loses strict evidence: ' + text)
    if 'REOPEN)\n        rc-service mosquitto start' in guest:
        raise ValueError('second boot manually starts broker, not OpenRC')


def reject(work):
    try:
        work()
    except (ValueError, TimeoutError):
        return
    raise AssertionError('fabricated/replayed original MQTT evidence accepted')


def ethernet(src, dst, sport, dport, sequence, flags, payload=b''):
    addresses = [ipaddress.IPv4Address(value).packed for value in (src, dst)]
    tcp = struct.pack('!HHIIHHHH', sport, dport, sequence, 0,
                      (5 << 12) | flags, 65535, 0, 0) + payload
    ipv4 = struct.pack('!BBHHHBBH4s4s', 0x45, 0, 20 + len(tcp), 0,
                       0, 64, 6, 0, *addresses)
    return b'\xff' * 6 + b'\x01' * 6 + b'\x08\x00' + ipv4 + tcp


def capture(frames):
    result = bytearray(struct.pack('<IHHIIII', 0xa1b2c3d4, 2, 4, 0,
                                   0, 65535, 1))
    for frame in frames:
        result.extend(struct.pack('<IIII', 0, 0, len(frame), len(frame)))
        result.extend(frame)
    return bytes(result)


def flow(sport, request, response):
    host_ip, guest_ip, port = '10.0.2.2', '10.0.2.15', host.GUEST_PORT
    return [
        ethernet(host_ip, guest_ip, sport, port, 1000, 0x02),
        ethernet(guest_ip, host_ip, port, sport, 9000, 0x12),
        ethernet(host_ip, guest_ip, sport, port, 1001, 0x10),
        ethernet(host_ip, guest_ip, sport, port, 1001, 0x18, request),
        ethernet(guest_ip, host_ip, port, sport, 9001, 0x18, response),
    ]


def fixture(phase):
    nonce = ('a' if phase == 'first' else 'c') * 48
    wire = host.wire_messages(phase, nonce, TOPIC, VALUE)
    receipt = {
        'phase': phase, 'transport': host.TRANSPORT,
        'host': '127.0.0.1', 'host_port': host.GUEST_PORT,
        'guest_port': host.GUEST_PORT, 'nonce': nonce, 'topic': TOPIC,
        'value': VALUE, 'value_sha256': hashlib.sha256(VALUE.encode()).hexdigest(),
        'retained_qos1_published': phase == 'first',
        'retained_new_subscription': True,
        'unrestricted_mqtt_test_only_no_auth': True,
        'request_sha256': {key: hashlib.sha256(value).hexdigest() for key, value
                           in wire.items() if 'request' in key},
        'response_sha256': {key: hashlib.sha256(value).hexdigest() for key, value
                            in wire.items() if 'response' in key},
        'attempts': 1,
        'timestamp_utc': '2026-11-07T12:00:00Z' if phase == 'first' else
                         '2026-11-07T12:01:00Z',
    }
    traffic = []
    if phase == 'first':
        traffic.extend(flow(49151, wire['publisher_request'],
                            wire['publisher_response']))
    traffic.extend(flow(49152, wire['subscriber_request'],
                        wire['subscriber_response']))
    return receipt, capture(traffic)


def check():
    root = Path(__file__).resolve().parent.parent
    probe = (root / 'scripts/probe-m197-alpine-software-cohort.sh').read_text()
    workflow = (root / '.github/workflows/m231-mosquitto-host-mqtt.yml').read_text()
    guest = (root / 'tests/m231-alpine-mosquitto-workload.sh').read_text()
    contract(probe, workflow, guest)
    reject(lambda: contract(probe.replace('m231_start_host reopen "$serial"',
                                          '# omitted second real host watcher'),
                            workflow, guest))
    reject(lambda: contract(probe.replace('"$PWD/$work/m231-reopen-wire.pcap"',
                                          '"$PWD/$work/replayed-first-wire.pcap"'),
                            workflow, guest))
    reject(lambda: contract(probe, workflow.replace('m231-reopen-wire.pcap',
                                                  'missing-second-NIC.pcap'), guest))
    reject(lambda: contract(probe, workflow, guest.replace('persistence true',
                                                         'persistence false')))
    assert host.connect_packet('ab').startswith(b'\x10')
    assert host.publish_packet(TOPIC, VALUE)[0] == 0x33  # QoS1 + RETAIN
    assert host.retained_packet(TOPIC, VALUE)[0] == 0x31  # QoS0 + RETAIN
    assert host.subscribe_packet(TOPIC)[0] == 0x82
    assert host.variable_length(128) == b'\x80\x01'
    reject(lambda: host.variable_length(268435456))

    first, a = fixture('first')
    second, b = fixture('reopen')
    assert len(disk.flow_payloads(a)) == 2
    assert len(disk.flow_payloads(b)) == 1
    with tempfile.TemporaryDirectory(prefix='m231-mqtt-negative-unit-') as root:
        folder = Path(root)
        paths = [folder / name for name in ('first.json', 'reopen.json', 'first.pcap', 'reopen.pcap')]
        def write(x=first, y=second, cap1=a, cap2=b):
            paths[0].write_text(json.dumps(x))
            paths[1].write_text(json.dumps(y))
            paths[2].write_bytes(cap1)
            paths[3].write_bytes(cap2)
        write()
        r1, r2, evidence = disk.wire_verify(*paths)
        assert r1['phase'] == 'first' and r2['phase'] == 'reopen'
        assert evidence['host_real_physical_MQTT_PCAP_SHA256']['first'] != \
               evidence['host_real_physical_MQTT_PCAP_SHA256']['reopen']
        reject(lambda: disk.wire_verify(paths[0], paths[0], paths[2], paths[3]))
        write(y=dict(second, value='d' * 64))
        reject(lambda: disk.wire_verify(*paths))
        write(y=dict(second, nonce='a' * 48))
        reject(lambda: disk.wire_verify(*paths))
        write(y=dict(second, retained_new_subscription=False))
        reject(lambda: disk.wire_verify(*paths))
        write(x=dict(first, request_sha256={}))
        reject(lambda: disk.wire_verify(*paths))
        write(x=dict(first, timestamp_utc='2099-14-52T99:99:99Z'))
        reject(lambda: disk.wire_verify(*paths))
        write(cap1=capture(flow(49152, host.wire_messages('first', 'a'*48,
                         TOPIC, VALUE)['subscriber_request'],
                         host.wire_messages('first', 'a'*48,
                         TOPIC, VALUE)['subscriber_response'])))
        reject(lambda: disk.wire_verify(*paths))  # no original host PUB
        write(cap2=capture(flow(49152, host.wire_messages('reopen', 'c'*48,
                         TOPIC, VALUE)['subscriber_request'], b'\x20\x02\x00\x00')))
        reject(lambda: disk.wire_verify(*paths))  # no retained delivery
        write(cap2=a)
        reject(lambda: disk.wire_verify(*paths))  # replayed first NIC PCAP
        write()
        assert disk.wire_verify(*paths)[0]['topic'] == TOPIC
    print('[M231 MQTT NEGATIVE UNIT PASS] strict probe/workflow, two QEMU NIC '
          'PCAPs and MQTT3.1.1 retained PUB/SUB; rejects forgery/replay; '
          'UNIT FIXTURES ONLY, no original QEMU PASS')


if __name__ == '__main__':
    check()
