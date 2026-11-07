#!/usr/bin/env python3
"""Real host TCP MQTT 3.1.1 retained publish/subscribe into one QEMU NIC.

The host, not the guest UART script, owns fresh MQTT challenge bytes. Its
receipt is NOT sufficient without the independent NIC PCAP and ext4 oracle.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import secrets
import socket
import struct
import sys
import time

GUEST_PORT = 15447
TRANSPORT = 'host-localhost-QEMU-hostfwd-guest-eth0-mqtt311'
READY = {'first': b'M231_FIRST_HOST_MQTT_READY',
         'reopen': b'M231_REOPEN_HOST_MQTT_READY'}
HEX48 = re.compile(r'[0-9a-f]{48}\Z')
HEX64 = re.compile(r'[0-9a-f]{64}\Z')


def variable_length(length: int) -> bytes:
    if length < 0 or length >= 268435456:
        raise ValueError('invalid MQTT remaining length')
    answer = bytearray()
    while True:
        digit = length % 128
        length //= 128
        answer.append(digit | (128 if length else 0))
        if not length:
            return bytes(answer)


def packet(header: int, payload: bytes) -> bytes:
    return bytes([header]) + variable_length(len(payload)) + payload


def mqtt_string(value: str) -> bytes:
    encoded = value.encode('ascii')
    return struct.pack('!H', len(encoded)) + encoded


def connect_packet(client: str) -> bytes:
    return packet(0x10, b'\x00\x04MQTT\x04\x02\x00\x1e' + mqtt_string(client))


def publish_packet(topic: str, value: str) -> bytes:
    # QoS1 and RETAIN, packet id=1. No guest-side fabricated publisher.
    return packet(0x33, mqtt_string(topic) + b'\x00\x01' + value.encode('ascii'))


def subscribe_packet(topic: str) -> bytes:
    return packet(0x82, b'\x00\x02' + mqtt_string(topic) + b'\x00')


def retained_packet(topic: str, value: str) -> bytes:
    # Broker's QoS0 retained delivery after a new SUBSCRIBE.
    return packet(0x31, mqtt_string(topic) + value.encode('ascii'))


def read_exact(sock: socket.socket, size: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ValueError('MQTT connection closed before packet completed')
        data.extend(chunk)
    return bytes(data)


def read_packet(sock: socket.socket) -> bytes:
    first = read_exact(sock, 1)
    remaining = 0
    multiplier = 1
    encoded = bytearray()
    for _ in range(4):
        digit = read_exact(sock, 1)[0]
        encoded.append(digit)
        remaining += (digit & 127) * multiplier
        if not digit & 128:
            break
        multiplier *= 128
    else:
        raise ValueError('unbounded MQTT length')
    if remaining > 4096:
        raise ValueError('MQTT response exceeds bounded challenge')
    return first + bytes(encoded) + read_exact(sock, remaining)


def wait_ready(path: Path, marker: bytes, timeout: int) -> None:
    deadline = time.monotonic() + timeout
    offset = 0
    tail = b''
    while time.monotonic() < deadline:
        try:
            with path.open('rb') as stream:
                if stream.seek(0, 2) < offset:
                    offset, tail = 0, b''
                stream.seek(offset)
                chunk = stream.read()
                offset = stream.tell()
        except FileNotFoundError:
            chunk = b''
        if chunk:
            combined = tail + chunk
            if any(line.strip() == marker for line in
                   combined.replace(b'\r', b'').splitlines()):
                return
            tail = combined[-256:]
        time.sleep(.2)
    raise TimeoutError('original PID1 UART never opened MQTT host window')


def wire_messages(phase: str, nonce: str, topic: str, value: str) -> dict:
    subscribe = connect_packet('m231sub' + nonce[:12]) + subscribe_packet(topic)
    subscribe_reply = (b'\x20\x02\x00\x00' + b'\x90\x03\x00\x02\x00' +
                       retained_packet(topic, value))
    result = {'subscriber_request': subscribe,
              'subscriber_response': subscribe_reply}
    if phase == 'first':
        result['publisher_request'] = (connect_packet('m231pub' + nonce[:12]) +
                                       publish_packet(topic, value))
        result['publisher_response'] = b'\x20\x02\x00\x00\x40\x02\x00\x01'
    return result


def transaction(phase: str, port: int, nonce: str, topic: str, value: str,
                timeout: int) -> dict:
    expected = wire_messages(phase, nonce, topic, value)
    deadline = time.monotonic() + timeout
    attempts = 0
    while time.monotonic() < deadline:
        attempts += 1
        try:
            if phase == 'first':
                with socket.create_connection(('127.0.0.1', port), 3) as pub:
                    pub.settimeout(6)
                    pub.sendall(connect_packet('m231pub' + nonce[:12]))
                    if read_packet(pub) != b'\x20\x02\x00\x00':
                        raise ValueError('MQTT real broker refused CONNECT')
                    pub.sendall(publish_packet(topic, value))
                    if read_packet(pub) != b'\x40\x02\x00\x01':
                        raise ValueError('original MQTT QoS1 PUBACK missing')
            with socket.create_connection(('127.0.0.1', port), 3) as sub:
                sub.settimeout(6)
                sub.sendall(connect_packet('m231sub' + nonce[:12]))
                if read_packet(sub) != b'\x20\x02\x00\x00':
                    raise ValueError('real subscriber CONNACK missing')
                sub.sendall(subscribe_packet(topic))
                if read_packet(sub) != b'\x90\x03\x00\x02\x00':
                    raise ValueError('real MQTT SUBACK missing')
                if read_packet(sub) != retained_packet(topic, value):
                    raise ValueError('independent MQTT retained delivery mismatch')
            return {'phase': phase, 'transport': TRANSPORT, 'host': '127.0.0.1',
                    'host_port': port, 'guest_port': GUEST_PORT,
                    'nonce': nonce, 'topic': topic, 'value': value,
                    'value_sha256': hashlib.sha256(value.encode()).hexdigest(),
                    'retained_qos1_published': phase == 'first',
                    'retained_new_subscription': True,
                    'unrestricted_mqtt_test_only_no_auth': True,
                    'request_sha256': {k: hashlib.sha256(v).hexdigest()
                                       for k, v in expected.items() if 'request' in k},
                    'response_sha256': {k: hashlib.sha256(v).hexdigest()
                                        for k, v in expected.items() if 'response' in k},
                    'attempts': attempts,
                    'timestamp_utc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())}
        except (OSError, ValueError):
            time.sleep(.4)
    raise TimeoutError('original broker did not complete independent retained MQTT')


def first_receipt(path: Path) -> tuple[str, str]:
    if path.is_symlink() or not path.is_file():
        raise ValueError('second QEMU requires independent first host receipt')
    data = json.loads(path.read_text(encoding='utf-8'))
    if data.get('phase') != 'first':
        raise ValueError('first host MQTT receipt phase missing')
    topic, value = data.get('topic'), data.get('value')
    if (not isinstance(topic, str) or not topic.startswith('naseed/m231/') or
            HEX48.fullmatch(topic[12:]) is None or not isinstance(value, str) or
            HEX64.fullmatch(value) is None):
        raise ValueError('first real MQTT challenge malformed')
    if data.get('value_sha256') != hashlib.sha256(value.encode()).hexdigest():
        raise ValueError('first value hash mismatch')
    return topic, value


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('phase', choices=('first', 'reopen'))
    parser.add_argument('uart', type=Path)
    parser.add_argument('host_port', type=int)
    parser.add_argument('output', type=Path)
    parser.add_argument('--first-receipt', type=Path)
    parser.add_argument('--ready-timeout', type=int, default=2650)
    parser.add_argument('--mqtt-timeout', type=int, default=35)
    args = parser.parse_args()
    if not 1 <= args.host_port <= 65535 or args.output.exists() or args.output.is_symlink():
        parser.error('bad port or stale output')
    if args.phase == 'reopen':
        if args.first_receipt is None:
            parser.error('second native QEMU requires first receipt')
        topic, value = first_receipt(args.first_receipt)
    else:
        topic, value = 'naseed/m231/' + secrets.token_hex(24), secrets.token_hex(32)
    nonce = secrets.token_hex(24)
    if args.phase == 'first':
        nonce = topic[12:]
    elif nonce == topic[12:]:
        raise ValueError('host nonce reused after reboot')
    print(f'[M231 HOST WAIT] phase={args.phase} real original broker READY', flush=True)
    wait_ready(args.uart, READY[args.phase], args.ready_timeout)
    evidence = transaction(args.phase, args.host_port, nonce, topic, value,
                           args.mqtt_timeout)
    args.output.write_text(json.dumps(evidence, sort_keys=True, indent=2) + '\n')
    print(f'[M231 HOST RETAINED MQTT RECEIPT] phase={args.phase} original broker, '
          'two independent TCP clients and retained value; PCAP/ext4 required', flush=True)


if __name__ == '__main__':
    try:
        main()
    except (OSError, TimeoutError, ValueError, json.JSONDecodeError) as exc:
        raise SystemExit('[M231 HOST MQTT FAIL] ' + str(exc)) from exc
