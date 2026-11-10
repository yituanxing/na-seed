#!/usr/bin/env python3
"""Final M230 original signed Alpine Dropbear/OpenRC SSH + physical ext4 oracle.

Requires two real native-QEMU SSH remote commands, independent actual guest
NIC captures and persistent original OpenRC/host-key files on physical ext4.
SSH application contents are encrypted: the NIC PCAP proves *real transport*,
not the remote command plaintext. The host OpenSSH exit/output and persistent
host-key pinning independently attest command execution across reboots.
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
    if spec is None or spec.loader is None:
        raise RuntimeError('M230 missing original Alpine/real wire oracle: ' + name)
    obj = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(obj)
    return obj


M227 = load('m227-alpine-disk-check.py', 'm230_original_openrc_ext4')
M223 = load('m223-http-wire-check.py', 'm230_original_guest_ethernet')
HOST = load('m230-host-ssh-check.py', 'm230_host_ssh_receipt')
M191 = M227.M191
HOST_IP = ipaddress.IPv4Address('10.0.2.2')
GUEST_NET = ipaddress.IPv4Network('10.0.2.0/24')
PACKAGES = ('dropbear', 'dropbear-openrc')
NATIVE_BINARIES = ('/usr/sbin/dropbear',)
TIME_PATTERN = re.compile(r'20[0-9]{2}-[01][0-9]-[0-3][0-9]T[0-2][0-9]:[0-5][0-9]:[0-5][0-9]Z\Z')


def fail(message: str) -> None:
    raise ValueError('[M230 ORIGINAL SSH FAIL] ' + message)


def read_receipt(path: Path, phase: str) -> dict:
    if path.is_symlink() or not path.is_file():
        fail(phase + ' missing or symlinked independent SSH host receipt')
    try:
        data = json.loads(path.read_text(encoding='utf-8'))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        fail(phase + ' malformed SSH host receipt: ' + str(exc))
    if not isinstance(data, dict):
        fail(phase + ' host receipt is not an object')
    fixed = {'phase': phase, 'transport': HOST.TRANSPORT,
             'host': '127.0.0.1', 'guest_port': HOST.GUEST_PORT,
             'remote_command_success': True, 'public_key_only': True,
             'root_only_isolated_test_no_DAC': True, 'pty_allocated': False}
    for field, value in fixed.items():
        if type(data.get(field)) is not type(value) or data[field] != value:
            fail(phase + ' original SSH host proof invalid ' + field)
    for field in ('host_port', 'attempts'):
        val = data.get(field)
        if type(val) is not int or not 1 <= val <= (65535 if field == 'host_port' else 2**31):
            fail(phase + ' original SSH proof has invalid ' + field)
    nonce = data.get('nonce')
    if not isinstance(nonce, str) or HOST.HEX_48.fullmatch(nonce) is None:
        fail(phase + ' original SSH proof lacks fresh host challenge')
    expected = {
        'nonce_sha256': hashlib.sha256(nonce.encode()).hexdigest(),
        'remote_command_sha256': hashlib.sha256(HOST.remote_command(nonce).encode()).hexdigest(),
        'remote_output_sha256': hashlib.sha256(('M230-SSH-REMOTE-EXEC:' + nonce + '\n').encode()).hexdigest(),
    }
    for field, value in expected.items():
        if data.get(field) != value:
            fail(phase + ' SSH challenge/output digest mismatch: ' + field)
    for field in ('client_pubkey_sha256', 'host_key_fingerprint'):
        val = data.get(field)
        regex = HOST.HEX_64 if field == 'client_pubkey_sha256' else HOST.FINGERPRINT
        if not isinstance(val, str) or regex.fullmatch(val) is None:
            fail(phase + ' SSH host key/public key attestation missing: ' + field)
    ts = data.get('timestamp_utc')
    if not isinstance(ts, str) or TIME_PATTERN.fullmatch(ts) is None:
        fail(phase + ' host receipt timestamp invalid')
    return data


def tcp_segment(raw: bytes):
    if len(raw) < 20:
        return None
    sport, dport, sequence = struct.unpack_from('!HHI', raw)
    offset = (raw[12] >> 4) * 4
    if offset < 20 or offset > len(raw):
        return None
    return sport, dport, sequence, raw[13], raw[offset:]


def physical_ssh_flow(capture: bytes, phase: str) -> dict:
    flows: dict[tuple[str, int], dict] = {}
    for index, frame in enumerate(M223.pcap.pcap_frames(capture)):
        ipv4 = M223.pcap.ipv4_packet(frame)
        if ipv4 is None:
            continue
        src, dst, protocol, payload = ipv4
        segment = tcp_segment(payload) if protocol == 6 else None
        if segment is None:
            continue
        sport, dport, seq, flags, body = segment
        into_guest = (src == HOST_IP and dst in GUEST_NET and dst != HOST_IP and
                      dport == HOST.GUEST_PORT and sport >= 1024)
        into_host = (src in GUEST_NET and src != HOST_IP and dst == HOST_IP and
                     sport == HOST.GUEST_PORT and dport >= 1024)
        if not (into_guest or into_host):
            continue
        guest, remote = ((str(dst), sport) if into_guest else (str(src), dport))
        flow = flows.setdefault((guest, remote),
                                {'syn': [], 'synack': [], 'ack': [],
                                 'client': [], 'server': []})
        if into_guest:
            if flags & 0x02 and not flags & 0x10:
                flow['syn'].append(index)
            if flags & 0x10 and not flags & 0x02:
                flow['ack'].append(index)
            if body:
                flow['client'].append((seq, body))
        else:
            if flags & 0x12 == 0x12:
                flow['synack'].append(index)
            if body:
                flow['server'].append((seq, body))
    matches = []
    for (guest, remote), row in flows.items():
        if not (row['syn'] and row['synack'] and row['ack'] and
                min(row['syn']) < min(row['synack']) < min(row['ack'])):
            continue
        try:
            client = M223.contiguous_payload(row['client'])
            server = M223.contiguous_payload(row['server'])
        except ValueError as exc:
            fail(phase + ' contradictory SSH TCP retransmission: ' + str(exc))
        # Only the identification string is plaintext. Key exchange, SSH
        # authentication and remote command are encrypted; never pretend the
        # PCAP directly confirms an application response or key-only auth.
        if (re.match(rb'SSH-2\.0-OpenSSH_[^\r\n]{1,100}\r\n', client) is None or
                re.match(rb'SSH-2\.0-dropbear_[^\r\n]{1,100}\r\n', server,
                         flags=re.IGNORECASE) is None or
                len(client) < 256 or len(server) < 256):
            continue
        matches.append({'guest_ip': guest, 'host_source_tcp_port': remote,
                        'guest_port': HOST.GUEST_PORT,
                        'client_ssh_identification': True,
                        'original_dropbear_server_identification': True,
                        'encrypted_ssh_session_traffic': True})
    if not matches:
        fail(phase + ' physical guest NIC lacks real host TCP handshake, '
             'OpenSSH/Dropbear banners and exchanged encrypted SSH packets')
    # OpenSSH can retry failed KEX/connection attempts before its final valid
    # public-key remote command. Encryption prevents identifying that command
    # directly in PCAP; require at least one real wire SSH session alongside
    # exact independent host OpenSSH success and pinned host key receipts.
    return {'matching_guest_ssh_tcp_sessions': len(matches),
            'guest_ssh_tcp_flows': matches}


def verify_transport(first: Path, reopen: Path, first_pcap: Path,
                     reopen_pcap: Path) -> tuple[dict, dict]:
    files = (first, reopen, first_pcap, reopen_pcap)
    if any(p.is_symlink() or not p.is_file() for p in files) or len({
            str(p.resolve(strict=False)) for p in files}) != len(files):
        fail('missing, symlinked or overlapping independent SSH receipts/PCAPs')
    a, b = read_receipt(first, 'first'), read_receipt(reopen, 'reopen')
    if (a['host_port'] != b['host_port'] or
            a['client_pubkey_sha256'] != b['client_pubkey_sha256'] or
            a['host_key_fingerprint'] != b['host_key_fingerprint'] or
            a['nonce'] == b['nonce'] or
            a['timestamp_utc'] >= b['timestamp_utc']):
        fail('two SSH sessions do not use independent challenges and persistent '
             'public client key/pinned original Dropbear host identity')
    captures = (first_pcap.read_bytes(), reopen_pcap.read_bytes())
    if not all(captures) or hashlib.sha256(captures[0]).digest() == hashlib.sha256(captures[1]).digest():
        fail('two original native-QEMU guest NIC captures missing or identical')
    flow1 = physical_ssh_flow(captures[0], 'first')
    flow2 = physical_ssh_flow(captures[1], 'reopen')
    for flow, pcap_bytes in zip((flow1, flow2), captures):
        flow['pcap_sha256'] = hashlib.sha256(pcap_bytes).hexdigest()
    return a, {'transport_only_not_ext4_acceptance': True,
               'actual_ssh_physical_eth0': {'first': flow1, 'reopen': flow2},
               'host_pinned_dropbear_key_reused': True,
               'genuine_ssh_public_key_remote_commands': 2,
               'client_pubkey_sha256': a['client_pubkey_sha256'],
               'host_key_fingerprint': a['host_key_fingerprint']}


def check_artifacts(port: str, read, stat, first_receipt: dict) -> dict:
    if port not in M191.ARCH_MACHINE:
        fail('unsupported original Alpine architecture')
    installed = read('/lib/apk/db/installed')
    world = read('/etc/apk/world').decode('utf-8')
    for package in PACKAGES:
        if package not in world.splitlines() or b'\nP:' + package.encode() + b'\n' not in b'\n' + installed:
            fail('original signed APK DB lacks ' + package)
    for path in NATIVE_BINARIES:
        binary = read(path)
        if (len(binary) < 20 or binary[:6] != b'\x7fELF\x02\x01' or
                struct.unpack_from('<H', binary, 18)[0] != M191.ARCH_MACHINE[port]):
            fail('original upstream Dropbear ELF missing or wrong ISA: ' + path)
    if not read('/etc/init.d/dropbear').startswith(b'#!'):
        fail('original signed dropbear-openrc service absent')
    runlevel = stat('/etc/runlevels/default/dropbear')
    if 'Type: symlink' not in runlevel or 'dropbear' not in runlevel:
        fail('original Dropbear OpenRC default-runlevel link absent')
    authorized = read('/root/.ssh/authorized_keys').strip().splitlines()
    if len(authorized) != 1:
        fail('guest contains missing or multiple root client authorized_keys')
    fields = authorized[0].split()
    # Preserve Dropbear/OpenSSH's per-key session restrictions. The signing
    # identity is still the canonical OpenSSH ed25519 public key, without its
    # authorized_keys restrictions and optional human-readable comment.
    if len(fields) != 3 or fields[:2] != [b'restrict', b'ssh-ed25519']:
        fail('guest authorized_keys lacks mandatory restrict Ed25519 key')
    normalized = b' '.join(fields[1:3]) + b'\n'
    if hashlib.sha256(normalized).hexdigest() != first_receipt['client_pubkey_sha256']:
        fail('original host authenticated Ed25519 client public key not on ext4')
    host_key = read('/etc/dropbear/dropbear_ed25519_host_key')
    if len(host_key) < 80:
        fail('original Dropbear generated persistent Ed25519 host private key missing')
    mode = stat('/etc/dropbear/dropbear_ed25519_host_key')
    if ('Type: regular' not in mode or
            re.search(r'Mode:\s+0*600\b', mode) is None):
        fail('persisted Dropbear private host key missing secure 0600 metadata')
    return {'signed_original_apks': sorted(PACKAGES),
            'correct_ISA_original_dropbear_ELF': list(NATIVE_BINARIES),
            'original_openrc_dropbear_default': True,
            'guest_public_key_only_sha256': first_receipt['client_pubkey_sha256'],
            'persistent_dropbear_ed25519_host_private_key_sha256': hashlib.sha256(host_key).hexdigest(),
            'root_only_isolated_test_not_secure_multiuser_DAC': True}


def verify(port: str, root: Path, disk: Path, first: Path, reopen: Path,
           first_pcap: Path, reopen_pcap: Path) -> dict:
    files = (root, disk, first, reopen, first_pcap, reopen_pcap)
    if (any(p.is_symlink() for p in files) or len({
            str(p.resolve(strict=False)) for p in files}) != len(files)):
        fail('original signed source, physical ext4 and host proofs overlap')
    first_receipt, wire = verify_transport(first, reopen, first_pcap, reopen_pcap)
    source, physical = root.resolve(strict=True), disk.resolve(strict=True)
    if not source.is_dir() or not physical.is_file():
        fail('original Alpine rootfs/disk must be real directory/physical image')
    for path in ('/root/.ssh/authorized_keys',
                 '/etc/dropbear/dropbear_ed25519_host_key',
                 '/etc/runlevels/default/dropbear'):
        baseline = M191.guest_path(source, path)
        if baseline.exists() or baseline.is_symlink():
            fail('signed Alpine baseline improperly preseeded M230 generated file: ' + path)
    M227.verify(port, source, physical)
    with tempfile.TemporaryDirectory(prefix='m230-original-dropbear-physical-ext4-') as temp:
        partition = Path(temp) / 'root.ext4'
        M191.extract_partition(physical, partition)
        def read(path: str) -> bytes:
            return M191.debugfs_cat(partition, path)
        def stat(path: str) -> str:
            result = subprocess.run(['debugfs', '-R', 'stat ' + path, str(partition)],
                                    capture_output=True, text=True, check=True)
            return result.stdout
        original = check_artifacts(port, read, stat, first_receipt)
    return {'acceptance': 'original-Alpine-two-PID1-OpenRC-real-host-public-key-SSH-remote-commands-physical-ext4',
            'port': port, 'real_host_to_original_dropbear_two_native_QEMUs': wire,
            'real_original_signed_dropbear_ext4': original,
            'inherited_M227_PID1_OpenRC_and_M197_physical_fsck': True,
            'security_scope': 'disposable root-only network test, no production DAC/security claim'}


def main() -> None:
    if len(sys.argv) != 9:
        raise SystemExit('usage: m230-dropbear-disk-check.py ISA SIGNED_ROOT DISK '
                         'FIRST.json REOPEN.json FIRST.pcap REOPEN.pcap OUT.json')
    port = sys.argv[1]
    root, disk, first, reopen, pcap1, pcap2, output = map(Path, sys.argv[2:])
    if output.exists() or output.is_symlink() or any(
            output.resolve(strict=False) == item.resolve(strict=False)
            for item in (root, disk, first, reopen, pcap1, pcap2)):
        fail('refusing stale/overlapping final SSH oracle output')
    result = verify(port, root, disk, first, reopen, pcap1, pcap2)
    output.write_text(json.dumps(result, sort_keys=True, indent=2) + '\n')
    print(f'[M230 ORIGINAL DROPBEAR SSH TWO-BOOT PASS] {port}: '
          'two native QEMUs, original Alpine OpenRC, real host public-key SSH '
          'remote commands and pinned host identity, independent NIC PCAPs, '
          'original signed ELF/APK and physical ext4+read-only fsck; '
          'DISPOSABLE ROOT-ONLY test, not production security')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, RuntimeError, subprocess.CalledProcessError) as exc:
        raise SystemExit('[M230 FINAL SSH ORACLE FAIL] ' + str(exc)) from exc
