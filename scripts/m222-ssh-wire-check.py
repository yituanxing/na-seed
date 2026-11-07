#!/usr/bin/env python3
"""Fail-closed real host OpenSSH, independent QEMU TCP PCAP and SHA oracles.

SSH payloads are encrypted and never decrypted by this verifier. The PCAP
proves multiple genuine guest-initiated TCP sessions with both-direction data;
the host daemon proves publickey authentication, and exact remote source/echo
SHA proves the original signed clients' three independent semantics.
"""
from importlib.util import module_from_spec, spec_from_file_location
import base64
import hashlib
import ipaddress
import json
from pathlib import Path
import re
import struct
import sys
import tempfile

source = Path(__file__).with_name('m196-public-wire-check.py')
spec = spec_from_file_location('m222_m196_pcap', source)
if spec is None or spec.loader is None:
    raise RuntimeError('M222 requires original QEMU Ethernet PCAP parser')
pcap = module_from_spec(spec)
spec.loader.exec_module(pcap)

HOST = ipaddress.IPv4Address('10.0.2.2')
GUEST = ipaddress.IPv4Network('10.0.2.0/24')
PORT = 15423


def ssh_tcp_flows(raw: bytes) -> dict:
    """Conservative real SYN→SYNACK→ACK→duplex SSH-payload per guest port."""
    flows: dict[tuple[str, int], dict] = {}
    for index, frame in enumerate(pcap.pcap_frames(raw)):
        packet = pcap.ipv4_packet(frame)
        if packet is None:
            continue
        source_ip, dest_ip, protocol, data = packet
        if protocol != 6 or len(data) < 20:
            continue
        sport, dport = struct.unpack_from('!HH', data)
        header = (data[12] >> 4) * 4
        if header < 20 or header > len(data):
            continue
        flags = data[13]
        guest_to_host = (source_ip in GUEST and source_ip != HOST and
                         dest_ip == HOST and dport == PORT and sport >= 1024)
        host_to_guest = (source_ip == HOST and dest_ip in GUEST and
                         dest_ip != HOST and sport == PORT and dport >= 1024)
        if not (guest_to_host or host_to_guest):
            continue
        guest_ip = str(source_ip if guest_to_host else dest_ip)
        guest_port = sport if guest_to_host else dport
        flow = flows.setdefault((guest_ip, guest_port),
                                {'syn': [], 'synack': [], 'ack': [],
                                 'guest_data': [], 'host_data': [],
                                 'guest_id': [], 'host_id': []})
        payload = data[header:]
        if guest_to_host:
            if flags & 0x02 and not flags & 0x10:
                flow['syn'].append(index)
            if flags & 0x10 and not flags & 0x02:
                flow['ack'].append(index)
            if payload:
                flow['guest_data'].append((index, len(payload)))
                if payload.startswith(b'SSH-2.0-'):
                    flow['guest_id'].append(index)
        else:
            if flags & 0x12 == 0x12:
                flow['synack'].append(index)
            if payload:
                flow['host_data'].append((index, len(payload)))
                if payload.startswith(b'SSH-2.0-OpenSSH_'):
                    flow['host_id'].append(index)
    verified = []
    for (address, port), flow in flows.items():
        if not (flow['syn'] and flow['synack'] and flow['ack'] and
                flow['guest_data'] and flow['host_data'] and
                flow['guest_id'] and flow['host_id']):
            continue
        if not (min(flow['syn']) < min(flow['synack']) < min(flow['ack']) and
                min(flow['guest_data'])[0] > min(flow['synack']) and
                min(flow['host_data'])[0] > min(flow['syn'])):
            continue
        verified.append((address, port, sum(size for _, size in flow['guest_data']),
                         sum(size for _, size in flow['host_data'])))
    if len(verified) < 5:
        raise ValueError(f'PCAP has {len(verified)} real bidirectional SSH sessions; require >=5')
    return {'pcap_real_ssh_connections': len(verified),
            'pcap_guest_payload_bytes': sum(item[2] for item in verified),
            'pcap_host_payload_bytes': sum(item[3] for item in verified),
            'pcap_unique_guest_addresses': sorted({item[0] for item in verified})}


def host_semantics(phase: str, host_dir: Path, host_log: str,
                   actual_stdout: bytes) -> dict:
    if phase not in ('first', 'reopen'):
        raise ValueError('invalid independent native QEMU phase')
    if any(path.is_symlink() for path in (host_dir,)):
        raise ValueError('refuse symlinked host evidence directory')
    # Match each Accepted line against THIS phase's one-time *public* key.
    # A green sshd auth line for some unrelated key is not our guest proof.
    guest_pub = host_dir / 'client_id_ed25519.pub'
    if guest_pub.is_symlink():
        raise ValueError('symlinked temporary public identity')
    fields = guest_pub.read_text().split()
    if len(fields) < 2 or fields[0] != 'ssh-ed25519':
        raise ValueError('missing expected one-time Ed25519 public identity')
    fingerprint = 'SHA256:' + base64.b64encode(
        hashlib.sha256(base64.b64decode(fields[1], validate=True)).digest()
    ).decode('ascii').rstrip('=')
    accepted = re.findall(
        r'^Accepted publickey for ([A-Za-z0-9_-]+) from 127\.0\.0\.1 '
        r'port [0-9]+ ssh2: ED25519 (SHA256:[A-Za-z0-9+/]+)$',
        host_log, flags=re.MULTILINE)
    if len(accepted) < 5 or len({entry[0] for entry in accepted}) != 1 or \
            any(entry[1] != fingerprint for entry in accepted):
        raise ValueError('host sshd did not authenticate all five sessions with this phase key')
    if 'Accepted password' in host_log or 'Accepted keyboard-interactive' in host_log:
        raise ValueError('host accepted forbidden non-public-key authentication')
    sources = {}
    for label in ('ssh', 'scp', 'sftp'):
        source_path = host_dir / f'{label}-source'
        if source_path.is_symlink():
            raise ValueError('symlinked host random source')
        original = source_path.read_bytes()
        prefix = f'M222_{label.upper()}_{phase}_SOURCE_'.encode('ascii')
        if not original.endswith(b'\n') or not re.fullmatch(
                prefix + b'[0-9a-f]{48}', original[:-1]):
            raise ValueError(f'nonrandom or wrong-phase host {label} source')
        sources[label] = original
    if len(set(sources.values())) != 3:
        raise ValueError('host sources are not independent')
    if actual_stdout != sources['ssh']:
        raise ValueError('signed original SSH remote stdout differs from host source')
    for label in ('scp', 'sftp'):
        for leaf, expected in ((f'{label}-up', f'M222_REAL_{label.upper()}_GUEST_UPLOAD {phase}\n'.encode()),
                               (f'{label}-echo', sources[label])):
            witness = host_dir / leaf
            if witness.is_symlink() or witness.read_bytes() != expected:
                raise ValueError(f'signed original {label} host-side {leaf} mismatch')
    return {'phase': phase, 'host_authenticated_pubkey_sessions': len(accepted),
            'host_authenticated_public_fingerprint': fingerprint,
            'host_remote_stdout_sha256': hashlib.sha256(sources['ssh']).hexdigest(),
            'host_scp_roundtrip_sha256': hashlib.sha256(sources['scp']).hexdigest(),
            'host_sftp_roundtrip_sha256': hashlib.sha256(sources['sftp']).hexdigest(),
            'source_bytes': {label: len(data) for label, data in sources.items()}}


def physical_ssh_stdout(port: str, disk: Path, phase: str) -> bytes:
    # Read the actual first/reopened guest ext4, not a preseeded CLI string.
    from importlib.util import module_from_spec as new_module, spec_from_file_location as new_spec
    disk_owner = Path(__file__).with_name('m191-alpine-root-disk.py')
    spec = new_spec('m222_real_disk', disk_owner)
    assert spec and spec.loader
    owner = new_module(spec)
    spec.loader.exec_module(owner)
    if port not in owner.ARCH_MACHINE:
        raise ValueError('unknown ISA for physical SSH stdout')
    with tempfile.TemporaryDirectory(prefix='m222-ssh-stdout-') as temp:
        extracted = Path(temp) / 'ext4.img'
        owner.extract_partition(disk, extracted)
        data = owner.debugfs_cat(extracted,
            f'/root/m222-work/authenticated-host-command.{phase}/ssh.stdout')
        if len(data) > 2048:
            raise ValueError('unbounded SSH remote stdout')
        return data


def verify(phase: str, capture: bytes, host_log: str,
           host_dir: Path, stdout: bytes) -> dict:
    host = host_semantics(phase, host_dir, host_log, stdout)
    wire = ssh_tcp_flows(capture)
    return {**host, **wire, 'original_signed_clients':
            ['usr/bin/ssh', 'usr/bin/scp', 'usr/bin/sftp'],
            'acceptance': 'real-host-sshd-pubkey-plus-independent-duplex-pcap-and-three-exact-host-hashes'}


def main() -> None:
    if len(sys.argv) != 8 or sys.argv[1] not in ('first', 'reopen'):
        raise SystemExit('usage: m222-ssh-wire-check.py first|reopen ISA PCAP HOST_LOG HOST_DIR DISK PROOF.json')
    phase, port = sys.argv[1:3]
    capture, host_log, host_dir, disk, report = map(Path, sys.argv[3:])
    if len({str(p.resolve(strict=False)) for p in (capture, host_log, host_dir, disk, report)}) != 5:
        raise SystemExit('[M222 HOST FAIL] overlapping evidence paths')
    if any(p.is_symlink() for p in (capture, host_log, disk, report)) or report.exists():
        raise SystemExit('[M222 HOST FAIL] stale/symlinked evidence')
    try:
        result = verify(phase, capture.read_bytes(),
                        host_log.read_text(errors='replace'), host_dir,
                        physical_ssh_stdout(port, disk, phase))
    except (OSError, ValueError) as exc:
        raise SystemExit(f'[M222 HOST+PCAP+SHA FAIL] {phase}: {exc}') from exc
    report.write_text(json.dumps(result, sort_keys=True, indent=2) + '\n')
    print(f'[M222 REAL SSH HOST+PCAP+SHA PASS] {phase} {port}: '
          f"host {result['host_authenticated_pubkey_sessions']} authenticated sessions, "
          f"QEMU {result['pcap_real_ssh_connections']} TCP SSH duplex flows, "
          'SSH stdout and SCP/SFTP exact host SHA confirmed')


if __name__ == '__main__':
    main()
