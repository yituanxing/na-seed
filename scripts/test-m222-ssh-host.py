#!/usr/bin/env python3
"""M222 fail-closed host+TCP+SHA static negatives; never a QEMU PASS claim."""
from importlib.util import module_from_spec, spec_from_file_location
import base64
import hashlib
import ipaddress
import json
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
source = ROOT / 'scripts/m222-ssh-wire-check.py'
spec = spec_from_file_location('test_m222_wire', source)
assert spec and spec.loader
wire = module_from_spec(spec)
spec.loader.exec_module(wire)


def frame(src: str, dst: str, sport: int, dport: int,
          flags: int, payload: bytes = b'') -> bytes:
    tcp = struct.pack('!HHIIHHHH', sport, dport, 1, 1, (5 << 12) | flags, 4096, 0, 0) + payload
    ipv4 = struct.pack('!BBHHHBBH4s4s', 0x45, 0, len(tcp) + 20, 0,
                       0, 64, 6, 0, ipaddress.IPv4Address(src).packed,
                       ipaddress.IPv4Address(dst).packed)
    return b'\xaa' * 6 + b'\xbb' * 6 + b'\x08\x00' + ipv4 + tcp


def capture(*, flow_count: int = 5, wrong_host: str = '10.0.2.2',
            wrong_port: int = 15423, omit_host_id: bool = False,
            omit_guest_data: bool = False, omit_host_data: bool = False) -> bytes:
    raw = struct.pack('<IHHIIII', 0xA1B2C3D4, 2, 4, 0, 0, 65535, 1)
    for connection in range(flow_count):
        guest = '10.0.2.15'
        gport = 34000 + connection
        entries = [
            frame(guest, wrong_host, gport, wrong_port, 0x02),
            frame(wrong_host, guest, wrong_port, gport, 0x12),
            frame(guest, wrong_host, gport, wrong_port, 0x10),
            frame(guest, wrong_host, gport, wrong_port, 0x18,
                  b'SSH-2.0-OpenSSH_10.0\r\n' if not omit_guest_data else b''),
            frame(wrong_host, guest, wrong_port, gport, 0x18,
                  b'SSH-2.0-OpenSSH_10.0\r\n' if not omit_host_id else b'not-an-ssh-server'),
            frame(guest, wrong_host, gport, wrong_port, 0x18,
                  b'encrypted-client-packet' if not omit_guest_data else b''),
            frame(wrong_host, guest, wrong_port, gport, 0x18,
                  b'encrypted-server-packet' if not omit_host_data else b''),
        ]
        if omit_host_data:
            entries[4] = frame(wrong_host, guest, wrong_port, gport, 0x10)
        for entry in entries:
            raw += struct.pack('<IIII', 0, 0, len(entry), len(entry)) + entry
    return raw


def setup_host(directory: Path, phase: str) -> tuple[str, bytes]:
    public_bytes = (b'\x00\x00\x00\x0bssh-ed25519\x00\x00\x00\x20' +
                    bytes(range(32)))
    (directory / 'client_id_ed25519.pub').write_text(
        'ssh-ed25519 ' + base64.b64encode(public_bytes).decode() + ' temporary\n')
    fingerprint = 'SHA256:' + base64.b64encode(
        hashlib.sha256(public_bytes).digest()).decode().rstrip('=')
    originals = {}
    for name, hex_pair in (('ssh', 'a1'), ('scp', 'b2'), ('sftp', 'c3')):
        original = f'M222_{name.upper()}_{phase}_SOURCE_{hex_pair * 24}\n'.encode()
        (directory / f'{name}-source').write_bytes(original)
        originals[name] = original
    (directory / 'scp-echo').write_bytes(originals['scp'])
    (directory / 'sftp-echo').write_bytes(originals['sftp'])
    (directory / 'scp-up').write_text(f'M222_REAL_SCP_GUEST_UPLOAD {phase}\n')
    (directory / 'sftp-up').write_text(f'M222_REAL_SFTP_GUEST_UPLOAD {phase}\n')
    log = (f'Accepted publickey for runner from 127.0.0.1 port 33578 '
           f'ssh2: ED25519 {fingerprint}\n') * 5
    return log, originals['ssh']


def negative_host_wire() -> int:
    negatives = 0
    for phase in ('first', 'reopen'):
        with tempfile.TemporaryDirectory(prefix='m222-host-static-') as temporary:
            host = Path(temporary)
            log, stdout = setup_host(host, phase)
            good = capture()
            result = wire.verify(phase, good, log, host, stdout)
            assert result['pcap_real_ssh_connections'] == 5
            assert result['host_authenticated_pubkey_sessions'] == 5
            cases = {
                'only four SSH TCP flows': (capture(flow_count=4), log, stdout),
                'wrong SSH port': (capture(wrong_port=15424), log, stdout),
                'wrong SSH destination': (capture(wrong_host='10.0.2.3'), log, stdout),
                'fake SSH server ID': (capture(omit_host_id=True), log, stdout),
                'missing guest payload': (capture(omit_guest_data=True), log, stdout),
                'missing host payload': (capture(omit_host_data=True), log, stdout),
                'no host auth': (good, '', stdout),
                'one host auth': (good, log.splitlines()[0] + '\n', stdout),
                'wrong host public key fingerprint':
                    (good, log.replace('SHA256:', 'SHA256:INVALID', 1), stdout),
                'host password auth': (good, log + 'Accepted password for runner\n', stdout),
                'wrong guest real SSH stdout': (good, log, b'faked-return'),
            }
            for label, (packet, transcript, output) in cases.items():
                try:
                    wire.verify(phase, packet, transcript, host, output)
                except ValueError:
                    negatives += 1
                else:
                    raise AssertionError(f'M222 accepted false-positive {phase}: {label}')
            for label in ('scp-echo', 'sftp-echo', 'scp-up', 'sftp-up'):
                witness = host / label
                original = witness.read_bytes()
                witness.write_bytes(b'tampered')
                try:
                    wire.verify(phase, good, log, host, stdout)
                except ValueError:
                    negatives += 1
                else:
                    raise AssertionError(f'M222 accepted tampered {phase}/{label}')
                witness.write_bytes(original)
    return negatives


def guest_ledger() -> None:
    with tempfile.TemporaryDirectory(prefix='m222-ledger-static-') as folder:
        temp = Path(folder)
        first, reopen, report = (temp / 'first', temp / 'reopen', temp / 'report.json')
        entries = [('openssh-client-default', 'usr/bin/ssh'),
                   ('openssh-client-common', 'usr/bin/scp'),
                   ('openssh-client-common', 'usr/bin/sftp')]
        def body(phase: str) -> str:
            states = ('install', 'functional') if phase == 'first' else ('reopen',)
            return ''.join(f'M222_RESULT\t{owner}\t{elf}\t{state}\tPASS\n'
                           for owner, elf in entries for state in states) + \
                f'M222_{phase.upper()}_DISCOVERY_COMPLETE\n'
        def run(a: str, b: str) -> int:
            first.write_text(a)
            reopen.write_text(b)
            out = subprocess.run(['python3', str(ROOT / 'scripts/m222-compat-report.py'),
                str(ROOT / 'scripts/m222-candidate-elfs.tsv'), str(first), str(reopen),
                str(report), '--require-all'], cwd=ROOT, capture_output=True,
                text=True, timeout=5)
            return out.returncode
        a, b = body('first'), body('reopen')
        assert run(a, b) == 0
        result = json.loads(report.read_text())
        assert result['newly_installed_full_pass_entrypoints'] == 3
        assert result['preexisting'] == 0
        for variant, first_text, reopen_text in (
            ('preinstalled ELF', a.replace('install\tPASS', 'install\tPREEXISTING', 1), b),
            ('nonfunctional SCP', a.replace('functional\tPASS', 'functional\tFAIL', 1), b),
            ('missing genuine SFTP', a.replace('usr/bin/sftp', 'usr/bin/fake'), b),
            ('reopen SSH failed', a, b.replace('reopen\tPASS', 'reopen\tFAIL', 1)),
            ('reopen marker missing', a, b.replace('M222_REOPEN_DISCOVERY_COMPLETE', '')),
        ):
            assert run(first_text, reopen_text) != 0, variant


def contract() -> None:
    guest = (ROOT / 'tests/m222-ssh-workload.sh').read_text()
    m205 = (ROOT / 'tests/m205-software-workload.sh').read_text()
    runner = (ROOT / 'scripts/probe-m197-alpine-software-cohort.sh').read_text()
    host = (ROOT / 'scripts/m222-ssh-host.sh').read_text()
    names = ('/usr/bin/ssh', '/usr/bin/scp', '/usr/bin/sftp')
    for name in names:
        assert name in guest, name
    for marker in ('StrictHostKeyChecking=yes', 'IdentityAgent=none',
                   'UserKnownHostsFile="$known"', 'M222_BOOT_PHASE',
                   "'..'*", '>= 90', 'busy rm -f "$key"',
                   '/proc/net/naseed-resolv.conf'):
        if marker == '>= 90':
            assert '[ "$waited" -ge 90 ]' in guest
        else:
            assert marker in guest, marker
    for marker in ('milestone=M222', 'expected_rows=3',
                   '"$milestone" = M222 ] && exercise_budget=240',
                   'export NAS_M222_BOOT_PHASE="$phase"'):
        assert marker in m205, marker
    # Installed-as-a-dependency is not an explicit APK world request.
    # M222 AND the later M223 require genuine offline add even for signed
    # dependency-installed owners, preserving both world-oracle contracts.
    assert ('elif [ "$milestone" != M222 ] && [ "$milestone" != M223 ] && '
            'apk info -e "$package" >/dev/null 2>&1; then') in m205
    assert 'apk --cache-dir /var/cache/apk --no-network -v add "$package"' in m205
    for marker in ('m222_prepare_host first', 'm222_prepare_host reopen',
                   'm222_start_host first', 'm222_start_host reopen',
                   'm222_stop_host', 'm222-first-wire.json', 'm222-reopen-wire.json',
                   'm222-compat-report.py', 'm222-alpine-disk-check.py',
                   'm222-alpine-reopen-cases.tsv'):
        assert marker in runner, marker
    for marker in ('ListenAddress=127.0.0.1', 'PasswordAuthentication=no',
                   'UsePAM=no', 'KbdInteractiveAuthentication=no',
                   'AllowTcpForwarding=no', 'AllowAgentForwarding=no',
                   'AllowStreamLocalForwarding=no', 'm222_cleanup',
                   'm222_exit_status', 'rm -f "$disk"'):
        assert marker in host, marker
    for path in (ROOT / 'scripts/probe-m197-alpine-software-cohort.sh',
                 ROOT / 'scripts/m222-ssh-host.sh', ROOT / 'tests/m205-software-workload.sh',
                 ROOT / 'tests/m222-ssh-workload.sh'):
        subprocess.run(['sh', '-n', str(path)], check=True, cwd=ROOT, timeout=5)
    subprocess.run(['python3', 'scripts/check-m141-candidates.py', '--profile', 'm222'],
                   check=True, cwd=ROOT, capture_output=True, timeout=5)


def main() -> None:
    count = negative_host_wire()
    guest_ledger()
    contract()
    print(f'[M222 STATIC HOST PASS] {count} fail-closed SSH host/PCAP/SHA negatives, '
          '3/3 signed guest ledger negatives, two-phase 3ISA integration; '
          'NO signed APK download, native QEMU or public HTTPS PASS claimed')


if __name__ == '__main__':
    main()
