#!/usr/bin/env python3
"""M233 host DNS protocol, spoof/replay negatives and strict cohort contract."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parent.parent


def load(name: str, alias: str):
    spec = importlib.util.spec_from_file_location(alias, ROOT / 'scripts' / name)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


H = load('m233-host-dns-check.py', 'm233_host_protocol_test')
D = load('m233-dnsmasq-disk-check.py', 'm233_oracle_test')


def rejects(fn, *args) -> None:
    try:
        fn(*args)
    except (ValueError, OSError):
        return
    raise AssertionError('forged DNS record accepted')


def positive_reply(request: bytes) -> bytes:
    txid = struct.unpack_from('!H', request)[0]
    return (struct.pack('!6H', txid, 0x8400, 1, 1, 0, 0) + request[12:] +
            b'\xc0\x0c' + struct.pack('!HHIH', 1, 1, 60, 4) + H.RECORD)


def receipt(phase: str, udp_nonce: str, tcp_nonce: str, timestamp: str) -> dict:
    rows = {}
    for protocol, nonce, txid in (('udp', udp_nonce, 0x4210),
                                  ('tcp', tcp_nonce, 0x4230)):
        request = H.query(nonce, txid)
        answer = positive_reply(request)
        rows[protocol] = {
            'nonce': nonce, 'transaction_id': f'{txid:04x}', 'attempts': 1,
            'request_hex': request.hex(), 'response_hex': answer.hex(),
            'response_sha256': hashlib.sha256(answer).hexdigest(),
        }
    return {'phase': phase, 'host': '127.0.0.1', 'guest_port': 15453,
            'udp_host_port': 15453, 'tcp_host_port': 15453,
            'authoritative_A': '198.51.100.233', 'utc': timestamp,
            'dns_queries': rows}


def protocol_checks() -> None:
    request = H.query('111122223333444455556666', 0x1234)
    answer = positive_reply(request)
    H.validate_response(request, answer)
    wrong_id = bytearray(answer)
    wrong_id[0] ^= 1
    rejects(H.validate_response, request, bytes(wrong_id))
    wrong_ip = bytearray(answer)
    wrong_ip[-1] ^= 1
    rejects(H.validate_response, request, bytes(wrong_ip))
    for wrong in (answer[:18], answer[:-3], b'',
                  answer[:2] + b'\0\0' + answer[4:],
                  answer[:12] + b'Z' + answer[13:]):
        rejects(H.validate_response, request, wrong)
    rejects(H.query, 'non-random', 0)
    rejects(H.query, '111122223333444455556666', 65536)


def oracle_checks() -> None:
    with tempfile.TemporaryDirectory(prefix='m233-fixtures-') as temporary:
        path = Path(temporary) / 'first.json'
        correct = receipt('first', '111122223333444455556666',
                          'abcdefabcdefabcdefabcdef', '2026-11-07T15:00:00Z')

        def verify(data: dict) -> None:
            path.write_text(json.dumps(data))
            D.read_receipt(path, 'first')

        verify(correct)
        from copy import deepcopy
        for mutate in (
            lambda x: x.update(phase='reopen'),
            lambda x: x.update(guest_port=1),
            lambda x: x.update(host='0.0.0.0'),
            lambda x: x.update(utc='some day'),
            lambda x: x['dns_queries']['udp'].update(attempts=True),
            lambda x: x['dns_queries']['udp'].update(nonce='00'),
            lambda x: x['dns_queries']['tcp'].update(response_sha256='f'*64),
            lambda x: x['dns_queries']['tcp'].update(response_hex=''),
            lambda x: x['dns_queries']['udp'].update(request_hex='00'),
            lambda x: x['dns_queries']['udp'].update(transaction_id='123'),
            lambda x: x['dns_queries']['udp'].update(response_hex='zz'),
            lambda x: x['dns_queries'].pop('tcp'),
        ):
            forged = deepcopy(correct)
            mutate(forged)
            path.write_text(json.dumps(forged))
            rejects(D.read_receipt, path, 'first')
        path.write_text(json.dumps(correct))
        # Actual physical PCAP requirements cannot be replaced by host JSON.
        missing = Path(temporary) / 'missing.pcap'
        rejects(D.verify_wire, path, path, missing, missing)


def contract_checks() -> None:
    guest = (ROOT / 'tests/m233-alpine-dnsmasq-workload.sh').read_text()
    cohort = (ROOT / 'scripts/probe-m197-alpine-software-cohort.sh').read_text()
    workflow = (ROOT / '.github/workflows/m233-dnsmasq-host-dns.yml').read_text()
    for literal in ('dnsmasq dnsmasq-openrc', 'rc-update add dnsmasq default',
                    'rc-service dnsmasq start', 'rc-service dnsmasq status',
                    'M233_FIRST_HOST_DNS_READY', 'M233_REOPEN_HOST_DNS_READY',
                    '/root/m227-alpine-openrc-workload.sh --reopen',
                    'port=15453', 'user=root', 'group=root'):
        assert literal in guest, literal
    assert guest.split('REOPEN)')[1].count('rc-service dnsmasq start') == 0
    for literal in ('NAS_M233_DNSMASQ_HOST',
                    'NAS_VIRTIO_NET_HOSTFWD_UDP_PORT',
                    'NAS_VIRTIO_NET_GUESTFWD_UDP_PORT',
                    'm233_start_host first', 'm233_start_host reopen',
                    'm233-dnsmasq-disk-check.py',
                    'M233_FIRST_HOST_DNS_WINDOW_COMPLETE',
                    'M233_REOPEN_HOST_DNS_WINDOW_COMPLETE'):
        assert literal in cohort, literal
    for literal in ('scripts/test-m233-hostfwd-udp-contract.py',
                    'scripts/test-m233-dnsmasq.py',
                    'NAS_M233_DNSMASQ_HOST', 'matrix.port',
                    'm233-first-host-dns.json', 'm233-reopen-wire.pcap'):
        assert literal in workflow, literal
    for path in ('tests/m233-alpine-dnsmasq-workload.sh',
                 'scripts/probe-m197-alpine-software-cohort.sh'):
        subprocess.run(['sh', '-n', str(ROOT / path)], check=True)


def main() -> None:
    protocol_checks()
    oracle_checks()
    contract_checks()
    print('[M233 DNS CONTRACT PASS] actual host UDP/TCP wire protocol, '
          'forged responses/receipts and OpenRC/cohort/CI source gates')


if __name__ == '__main__':
    main()
