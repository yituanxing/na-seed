#!/usr/bin/env python3
"""Fail-closed original M224 TCP wire, host, and physical guest evidence.

The QEMU Ethernet PCAP must show native Nmap connect open/closed, Ncat's
complete duplex request+host-only random body, and iperf3 control plus real
data streams. The independent host process logs and the guest's PHYSICAL
ext4 data are cross-checked; UART markers alone never constitute success.
"""

import hashlib
from importlib.util import module_from_spec, spec_from_file_location
import ipaddress
import json
from pathlib import Path
import re
import struct
import sys
import tempfile


def load(name: str, alias: str):
    target = Path(__file__).with_name(name)
    spec = spec_from_file_location(alias, target)
    if spec is None or spec.loader is None:
        raise RuntimeError('M224 missing original network oracle: ' + name)
    module = module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


pcap = load('m196-public-wire-check.py', 'm224_m196_pcap')
disk_oracle = load('m224-alpine-disk-check.py', 'm224_disk_oracle')
server = load('m224-tcp-server.py', 'm224_ncat_source')
GUEST_NETWORK = ipaddress.IPv4Network('10.0.2.0/24')
HOST_IP = ipaddress.IPv4Address('10.0.2.2')
NMAP_OPEN = 15441
NMAP_CLOSED = 15442
NCAT = 15443
IPERF = 15444
PORTS = (NMAP_OPEN, NMAP_CLOSED, NCAT, IPERF)


def contiguous_payload(parts: list[tuple[int, bytes]]) -> bytes:
    """Reassemble TCP data; reject gaps and contradictory retransmissions."""
    if not parts:
        return b''
    merged = bytearray()
    base = expected = None
    for seq, data in sorted(parts, key=lambda item: (item[0], -len(item[1]))):
        if expected is None:
            base = expected = seq
        if seq > expected:
            raise ValueError('[M224 WIRE FAIL] captured TCP stream has missing bytes')
        overlap = min(len(data), expected - seq)
        if overlap and merged[seq - base:seq - base + overlap] != data[:overlap]:
            raise ValueError('[M224 WIRE FAIL] contradictory TCP retransmission')
        trim = expected - seq
        if trim < len(data):
            merged.extend(data[trim:])
            expected += len(data) - trim
    return bytes(merged)


def tcp_flows(capture: bytes):
    """Only guest 10.0.2.x ↔ real QEMU gateway 10.0.2.2 TCP/ports count."""
    flows = {}
    for index, frame in enumerate(pcap.pcap_frames(capture)):
        packet = pcap.ipv4_packet(frame)
        if packet is None:
            continue
        src, dst, protocol, data = packet
        if protocol != 6 or len(data) < 20:
            continue
        sport, dport, seq = struct.unpack_from('!HHI', data)
        header = (data[12] >> 4) * 4
        if header < 20 or header > len(data):
            continue
        flags = data[13]
        guest_to_host = (src in GUEST_NETWORK and src != HOST_IP and dst == HOST_IP
                         and dport in PORTS and sport >= 1024)
        host_to_guest = (src == HOST_IP and dst in GUEST_NETWORK and dst != HOST_IP
                         and sport in PORTS and dport >= 1024)
        if not (guest_to_host or host_to_guest):
            continue
        port = dport if guest_to_host else sport
        peer = str(src if guest_to_host else dst)
        guest_port = sport if guest_to_host else dport
        flow = flows.setdefault((port, peer, guest_port),
                                {'syn': [], 'synack': [], 'ack': [], 'rst': [],
                                 'client': [], 'server': [], 'client_idx': [], 'server_idx': []})
        if guest_to_host:
            if flags & 0x02 and not flags & 0x10:
                flow['syn'].append(index)
            if flags & 0x10 and not flags & 0x02:
                flow['ack'].append(index)
            if len(data) > header:
                flow['client'].append((seq, data[header:]))
                flow['client_idx'].append(index)
        else:
            if flags & 0x12 == 0x12:
                flow['synack'].append(index)
            if flags & 0x04:
                flow['rst'].append(index)
            if len(data) > header:
                flow['server'].append((seq, data[header:]))
                flow['server_idx'].append(index)
    return flows


def handshake(flow: dict) -> bool:
    return bool(flow['syn'] and flow['synack'] and flow['ack'] and
                min(flow['syn']) < min(flow['synack']) < min(flow['ack']))


def real_tcp_proof(capture: bytes, phase: str, source: bytes) -> dict:
    flows = tcp_flows(capture)
    open_flows = [flow for (port, _, _), flow in flows.items()
                  if port == NMAP_OPEN and handshake(flow)]
    closed = [flow for (port, _, _), flow in flows.items()
              if port == NMAP_CLOSED and flow['syn'] and flow['rst'] and
              min(flow['syn']) < min(flow['rst']) and not flow['synack']]
    if not open_flows or not closed:
        raise ValueError('[M224 WIRE FAIL] real Nmap guest TCP open/closed contrast absent')

    token = server.phase_token(phase)
    ncat = [flow for (port, _, _), flow in flows.items()
            if port == NCAT and handshake(flow) and
            flow['client_idx'] and flow['server_idx'] and
            min(flow['client_idx']) < min(flow['server_idx']) and
            contiguous_payload(flow['client']) == token and
            contiguous_payload(flow['server']) == source]
    if not ncat:
        raise ValueError('[M224 WIRE FAIL] original Ncat missing full duplex '
                         'phase-token and exact host-only 96 bytes')

    iperf_flows = [flow for (port, _, _), flow in flows.items()
                   if port == IPERF and handshake(flow)]
    if len(iperf_flows) < 2:
        raise ValueError('[M224 WIRE FAIL] no separate iperf3 control/data TCP streams')
    sizes = [len(contiguous_payload(flow['client'])) for flow in iperf_flows]
    host_bytes = [len(contiguous_payload(flow['server'])) for flow in iperf_flows]
    if max(sizes, default=0) < 512 or not sum(host_bytes):
        raise ValueError('[M224 WIRE FAIL] iperf3 control without real payload/response')
    return {
        'pcap_sha256': hashlib.sha256(capture).hexdigest(),
        'nmap_real_tcp_open_connects': len(open_flows),
        'nmap_real_tcp_closed_resets': len(closed),
        'ncat_real_duplex_sessions': len(ncat),
        'ncat_real_response_sha256': hashlib.sha256(source).hexdigest(),
        'iperf3_real_tcp_sessions': len(iperf_flows),
        'iperf3_guest_tcp_payload_bytes': sum(sizes),
        'iperf3_host_tcp_payload_bytes': sum(host_bytes),
    }


def host_log_proof(phase: str, tcp_log: str, iperf_log: str,
                   source: bytes) -> dict:
    ready = f'[M224 HOST READY] phase={phase} nmap=15441 closed=15442 ncat=15443'
    if tcp_log.splitlines().count(ready) != 1 or '[M224 HOST DENY]' in tcp_log:
        raise ValueError('[M224 HOST FAIL] independent real host READY missing/denied request')
    nmap = re.compile(r'M224_HOST_NMAP_ACCEPT phase=(first|reopen) port=15441 peer=127\.0\.0\.1\Z')
    accepted = [nmap.fullmatch(line) for line in tcp_log.splitlines()
                if line.startswith('M224_HOST_NMAP_ACCEPT ')]
    if not accepted or any(match is None or match.group(1) != phase for match in accepted):
        raise ValueError('[M224 HOST FAIL] native Nmap open port did not reach host socket')
    exchange = re.compile(r'M224_HOST_NCAT_EXCHANGE phase=(first|reopen) port=15443 '
                          r'rx_bytes=([0-9]+) rx_sha256=([0-9a-f]{64}) '
                          r'tx_bytes=([0-9]+) tx_sha256=([0-9a-f]{64}) peer=127\.0\.0\.1\Z')
    receipts = [exchange.fullmatch(line) for line in tcp_log.splitlines()
                if line.startswith('M224_HOST_NCAT_EXCHANGE ')]
    if len(receipts) != 1 or receipts[0] is None:
        raise ValueError('[M224 HOST FAIL] exactly one real Ncat duplex receipt required')
    record = receipts[0]
    token = server.phase_token(phase)
    if record.group(1) != phase or int(record.group(2)) != len(token) or \
            record.group(3) != hashlib.sha256(token).hexdigest() or \
            int(record.group(4)) != len(source) or \
            record.group(5) != hashlib.sha256(source).hexdigest():
        raise ValueError('[M224 HOST FAIL] native Ncat receipt bytes/SHA mismatch')

    # Reuse the exact host receiver-report form already accepted by M141's
    # real signed iperf3 wire test, never infer transfer from client stdout.
    if 'Server listening on 15444' not in iperf_log:
        raise ValueError('[M224 HOST FAIL] genuine host iperf3 listening absent')
    receiver = re.findall(r'(?m)^\[\s*\d+\]\s+[0-9.]+-[0-9.]+\s+sec\s+'
                          r'([0-9]+(?:\.[0-9]+)?)\s+(Bytes|KBytes|MBytes|GBytes)\s+'
                          r'.*\sreceiver\s*$', iperf_log)
    if not receiver or not any(float(size) > 0 for size, _ in receiver):
        raise ValueError('[M224 HOST FAIL] host original iperf3 receiver sent zero bytes')
    return {'host_nmap_real_accepts': len(accepted),
            'host_ncat_real_receipts': len(receipts),
            'host_ncat_source_sha256': hashlib.sha256(source).hexdigest(),
            'host_original_iperf3_nonzero_receiver': True}


def physical_guest_proof(phase: str, port: str, disk: Path,
                         source: bytes) -> dict:
    m191 = disk_oracle.owner.load('m191-alpine-root-disk.py', 'm224_wire_m191')
    if port not in m191.ARCH_MACHINE:
        raise ValueError('[M224 DISK FAIL] unsupported real guest ISA')
    with tempfile.TemporaryDirectory(prefix='m224-real-network-ext4-') as tmp:
        partition = Path(tmp) / 'root.ext4'
        m191.extract_partition(disk.resolve(strict=True), partition)
        actual = m191.debugfs_cat(partition,
                                  f'/root/m224-work/{disk_oracle.CASES["ncat"]}.{phase}/host-random.bin')
        if actual != source:
            raise ValueError('[M224 DISK FAIL] real native Ncat physical '
                             'ext4 bytes differ from host source')
        nmap_text = m191.debugfs_cat(partition,
                                     f'/root/m224-work/{disk_oracle.CASES["nmap"]}.{phase}/nmap.txt')
        if not disk_oracle.nmap_port_contrast(nmap_text):
            name = f'/root/m224-work/{disk_oracle.CASES["nmap"]}.{phase}/nmap.txt'
            raise ValueError('[M224 DISK FAIL] original Nmap port scan not persisted: '
                             + disk_oracle.nmap_physical_diagnostic(
                                 partition, name, nmap_text))
        report = m191.debugfs_cat(partition,
                                  f'/root/m224-work/{disk_oracle.CASES["iperf3"]}.{phase}/iperf3.json')
        try:
            sent = disk_oracle.iperf3_bytes(report, phase)
        except ValueError as exc:
            # This is the FIRST per-boot physical check, before the final
            # two-phase disk oracle. Retain the actual client error here too;
            # otherwise a failed first boot destroys the only JSON witness.
            raise ValueError(str(exc) + ': ' +
                             disk_oracle.iperf3_physical_diagnostic(
                                 partition, m191, phase, report)) from exc
    return {'physical_ncat_exact_random_bytes': len(source),
            'physical_nmap_port_contrast': True,
            'physical_original_iperf3_sent_bytes': sent}


def verify(phase: str, port: str, capture: bytes, host_log: str,
           host_dir: Path, disk: Path) -> dict:
    if phase not in ('first', 'reopen'):
        raise ValueError('[M224 WIRE FAIL] invalid independent QEMU phase')
    source = server.source_bytes(phase, host_dir)
    iperf_file = host_dir / 'iperf3.log'
    if iperf_file.is_symlink():
        raise ValueError('[M224 HOST FAIL] symlinked external iperf3 log')
    result = host_log_proof(phase, host_log, iperf_file.read_text(), source)
    result.update(real_tcp_proof(capture, phase, source))
    result.update(physical_guest_proof(phase, port, disk, source))
    result.update({'phase': phase, 'original_signed_elfs': list(disk_oracle.BINARIES),
                   'acceptance': 'nmap-open-closed-ncat-random-iperf3-original-host-pcap-ext4'})
    return result


def main() -> None:
    if len(sys.argv) != 8:
        raise SystemExit('usage: m224-tcp-wire.py first|reopen ISA PCAP HOST_LOG HOST_DIR DISK OUT.json')
    phase, port = sys.argv[1:3]
    capture, log, directory, disk, output = map(Path, sys.argv[3:])
    inputs = (capture, log, directory, disk, output)
    if len({str(path.resolve(strict=False)) for path in inputs}) != len(inputs) or \
            any(path.is_symlink() for path in inputs) or output.exists():
        raise SystemExit('[M224 HOST FAIL] stale/symlinked/overlapping real evidence')
    proof = verify(phase, port, capture.read_bytes(), log.read_text(), directory, disk)
    output.write_text(json.dumps(proof, sort_keys=True, indent=2) + '\n')
    print(f'[M224 ORIGINAL TCP WIRE+DISK PASS] {phase} {port}: exact nmap '
          'open/closed, iperf3 control+nonzero data host receiver, Ncat phase '
          'token+96-byte random; independent actual PCAP/physical ext4')


if __name__ == '__main__':
    main()
