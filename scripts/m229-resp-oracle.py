#!/usr/bin/env python3
"""Strict independent M229 host->guest Valkey RESP physical NIC verifier.

This is the *transport* half of acceptance. Its output MUST be combined with
original signed Alpine package, two PID1/OpenRC boots and physical ext4/RDB
verification before any M229 acceptance PASS is emitted.
"""

import hashlib
import importlib.util
import ipaddress
import json
from pathlib import Path
import re
import struct
import sys


def load(name: str, alias: str):
    spec = importlib.util.spec_from_file_location(alias, Path(__file__).with_name(name))
    if spec is None or spec.loader is None:
        raise RuntimeError('M229 missing original real-NIC helper: ' + name)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


HOST_CHECK = load('m229-host-resp-check.py', 'm229_real_host_check')
M223 = load('m223-http-wire-check.py', 'm229_actual_ethernet_tcp')
PCAP = M223.pcap
HOST_IP = ipaddress.IPv4Address('10.0.2.2')
GUEST_NET = ipaddress.IPv4Network('10.0.2.0/24')
GUEST_PORT = HOST_CHECK.GUEST_PORT
PHASES = ('first', 'reopen')
TS = re.compile(r'20[0-9]{2}-[01][0-9]-[0-3][0-9]T[0-2][0-9]:[0-5][0-9]:[0-5][0-9]Z\Z')
AUTH = re.compile(rb'\A\*2\r\n\$4\r\nAUTH\r\n\$64\r\n([0-9a-f]{64})\r\n')


def fail(msg: str) -> None:
    raise ValueError('[M229 REAL RESP FAIL] ' + msg)


def expected_public_commands(receipt: dict) -> bytes:
    phase, key, value, nonce = (receipt[name] for name in
                                ('phase', 'key', 'value', 'nonce'))
    commands = [('PING',), ('ECHO', nonce)]
    if phase == 'first':
        commands.extend((('SET', key, value), ('GET', key), ('SAVE',)))
    else:
        commands.append(('GET', key))
    return b''.join(HOST_CHECK.command(*args) for args in commands)


def expected_reply(receipt: dict) -> bytes:
    nonce, value, phase = (receipt[k] for k in ('nonce', 'value', 'phase'))
    messages = [b'+OK\r\n', b'+PONG\r\n',
                f'${len(nonce)}\r\n{nonce}\r\n'.encode()]
    if phase == 'first':
        messages.append(b'+OK\r\n')
    messages.append(f'${len(value)}\r\n{value}\r\n'.encode())
    if phase == 'first':
        messages.append(b'+OK\r\n')
    return b''.join(messages)


def read_receipt(path: Path, phase: str) -> dict:
    if path.is_symlink() or not path.is_file():
        fail(phase + ' missing/symlinked independent host RESP receipt')
    try:
        obj = json.loads(path.read_text(encoding='utf-8'))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        fail(phase + ' malformed host RESP JSON: ' + str(exc))
    if not isinstance(obj, dict):
        fail(phase + ' host RESP receipt is not an object')
    fixed = {'phase': phase, 'transport': HOST_CHECK.TRANSPORT,
             'host': '127.0.0.1', 'guest_port': GUEST_PORT,
             'auth_required': True,
             'commands': (['AUTH', 'PING', 'ECHO', 'SET', 'GET', 'SAVE']
                          if phase == 'first' else ['AUTH', 'PING', 'ECHO', 'GET'])}
    for field, expected in fixed.items():
        if type(obj.get(field)) is not type(expected) or obj[field] != expected:
            fail(phase + ' wrong host RESP ' + field)
    for field in ('host_port', 'attempts'):
        n = obj.get(field)
        if type(n) is not int or not (1 <= n <= (65535 if field == 'host_port' else 2**31)):
            fail(phase + ' missing valid real host ' + field)
    nonce, key, value = (obj.get(k) for k in ('nonce', 'key', 'value'))
    if (not isinstance(nonce, str) or HOST_CHECK.HEX_48.fullmatch(nonce) is None or
            (phase == 'first' and key != 'm229:' + nonce)):
        # Reopen must keep the first key, whose independent nonce differs.
        fail(phase + ' invalid first challenge/key binding')
    if (not isinstance(key, str) or not key.startswith('m229:') or
            HOST_CHECK.HEX_48.fullmatch(key[5:]) is None or
            not isinstance(value, str) or HOST_CHECK.HEX_64.fullmatch(value) is None):
        fail(phase + ' invalid original host random key/value')
    if obj.get('value_sha256') != hashlib.sha256(value.encode()).hexdigest():
        fail(phase + ' original host value digest wrong')
    for field in ('request_sha256', 'response_sha256'):
        val = obj.get(field)
        if not isinstance(val, str) or HOST_CHECK.HEX_64.fullmatch(val) is None:
            fail(phase + ' missing hashed actual ' + field)
    if obj['response_sha256'] != hashlib.sha256(expected_reply(obj)).hexdigest():
        fail(phase + ' receipt did not record successful AUTH/RESP exact responses')
    if not isinstance(obj.get('timestamp_utc'), str) or TS.fullmatch(obj['timestamp_utc']) is None:
        fail(phase + ' missing real UTC response timestamp')
    return obj


def tcp_segment(raw: bytes):
    if len(raw) < 20:
        return None
    sport, dport, seq = struct.unpack_from('!HHI', raw)
    offset = (raw[12] >> 4) * 4
    if offset < 20 or offset > len(raw):
        return None
    return sport, dport, seq, raw[13], raw[offset:]


def physical_valkey_flow(capture: bytes, receipt: dict) -> dict:
    """Physical SYN/SYNACK/ACK+AUTH and exact RESP over real guest eth0."""
    flows: dict[tuple[str, int], dict] = {}
    for index, frame in enumerate(PCAP.pcap_frames(capture)):
        packet = PCAP.ipv4_packet(frame)
        if packet is None:
            continue
        src, dst, protocol, payload = packet
        segment = tcp_segment(payload) if protocol == 6 else None
        if segment is None:
            continue
        sport, dport, sequence, flags, tcp_body = segment
        into_guest = (src == HOST_IP and dst in GUEST_NET and dst != HOST_IP and
                      dport == GUEST_PORT and sport >= 1024)
        into_host = (src in GUEST_NET and src != HOST_IP and dst == HOST_IP and
                     sport == GUEST_PORT and dport >= 1024)
        if not (into_guest or into_host):
            continue
        guest_ip, remote_port = ((str(dst), sport) if into_guest else (str(src), dport))
        entry = flows.setdefault((guest_ip, remote_port),
                                 {'syn': [], 'synack': [], 'ack': [],
                                  'request': [], 'response': []})
        if into_guest:
            if flags & 0x02 and not flags & 0x10:
                entry['syn'].append(index)
            if flags & 0x10 and not flags & 0x02:
                entry['ack'].append(index)
            if tcp_body:
                entry['request'].append((sequence, tcp_body))
        else:
            if flags & 0x12 == 0x12:
                entry['synack'].append(index)
            if tcp_body:
                entry['response'].append((sequence, tcp_body))
    matches = []
    for (guest_ip, source_port), flow in flows.items():
        if not (flow['syn'] and flow['synack'] and flow['ack'] and
                min(flow['syn']) < min(flow['synack']) < min(flow['ack'])):
            continue
        try:
            request = M223.contiguous_payload(flow['request'])
            response = M223.contiguous_payload(flow['response'])
        except ValueError as exc:
            fail(receipt['phase'] + ' contradictory TCP retransmission: ' + str(exc))
        match = AUTH.match(request)
        if match is None:
            continue
        # Never serialize the ephemeral AUTH secret into JSON output. Its
        # plaintext is necessarily present in original packet captures.
        if request[match.end():] != expected_public_commands(receipt):
            continue
        if (hashlib.sha256(request).hexdigest() != receipt['request_sha256'] or
                response != expected_reply(receipt) or
                hashlib.sha256(response).hexdigest() != receipt['response_sha256']):
            continue
        matches.append((guest_ip, source_port, match.group(1)))
    if len(matches) != 1:
        fail(receipt['phase'] + ' physical NIC PCAP missing unique authenticated '
             'host SYN/SYNACK/ACK, nonce ECHO, exact Valkey RESP and RDB SAVE')
    guest, port, auth = matches[0]
    return {'phase': receipt['phase'], 'guest_ip': guest,
            'remote_tcp_port': port, 'nonce': receipt['nonce'],
            'valkey_tcp_verified': True, '_auth': auth}


def verify_transport(first: Path, reopen: Path, pcap_first: Path, pcap_reopen: Path) -> dict:
    inputs = (first, reopen, pcap_first, pcap_reopen)
    if (len({str(p.resolve(strict=False)) for p in inputs}) != 4 or
            any(p.is_symlink() or not p.is_file() for p in inputs)):
        fail('overlapping, missing or symlinked receipt/PCAP')
    a, b = read_receipt(first, 'first'), read_receipt(reopen, 'reopen')
    if a['host_port'] != b['host_port']:
        fail('host forwarded port changed between native QEMUs')
    if (a['nonce'] == b['nonce'] or a['key'] != b['key'] or
            a['value'] != b['value'] or
            a['timestamp_utc'] >= b['timestamp_utc']):
        fail('first SET/SAVE and independent reopen GET challenge do not match')
    cap1, cap2 = pcap_first.read_bytes(), pcap_reopen.read_bytes()
    if not cap1 or not cap2 or hashlib.sha256(cap1).digest() == hashlib.sha256(cap2).digest():
        fail('two independent native QEMU physical captures missing or identical')
    p1, p2 = physical_valkey_flow(cap1, a), physical_valkey_flow(cap2, b)
    if p1.pop('_auth') != p2.pop('_auth'):
        fail('Valkey AUTH token changed across persisted service restart')
    p1['pcap_sha256'] = hashlib.sha256(cap1).hexdigest()
    p2['pcap_sha256'] = hashlib.sha256(cap2).hexdigest()
    return {'transport_only_not_ext4_acceptance': True,
            'first_host_set_and_save': True, 'reopen_host_get_persisted_value': True,
            'both_real_valkey_eth_pcap': {'first': p1, 'reopen': p2},
            'value_sha256': a['value_sha256'], 'key': a['key']}


def main() -> None:
    if len(sys.argv) != 6:
        raise SystemExit('usage: m229-resp-oracle.py FIRST.json REOPEN.json FIRST.pcap REOPEN.pcap OUT.json')
    *sources, output = map(Path, sys.argv[1:])
    if output.exists() or output.is_symlink() or any(
            output.resolve(strict=False) == p.resolve(strict=False) for p in sources):
        fail('refusing stale/overlapping physical proof output')
    evidence = verify_transport(*sources)
    output.write_text(json.dumps(evidence, indent=2, sort_keys=True) + '\n')
    print('[M229 RESP PHYSICAL WIRE VERIFIED] two real QEMU NIC captures; '
          'still requires original Alpine/OpenRC and physical ext4/RDB oracle')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, RuntimeError) as exc:
        raise SystemExit('[M229 RESP WIRE FAIL] ' + str(exc)) from exc
