#!/usr/bin/env python3
"""Independent real host -> original Alpine Dropbear SSH command per QEMU boot.

The guest is an isolated root-only test system, NOT a secure multi-user SSH
server.  The host generates and retains the *client private key* outside the
guest disk and artifacts; only its public key is staged in Alpine.  An SSH
receipt alone never proves the physical guest NIC, OpenRC or ext4 persistence.
"""

import argparse
import base64
import hashlib
import json
from pathlib import Path
import re
import secrets
import subprocess
import sys
import time


GUEST_PORT = 15446
TRANSPORT = 'host-localhost-QEMU-user-hostfwd-guest-eth0-dropbear-ssh'
READY = {'first': b'M230_FIRST_HOST_SSH_READY',
         'reopen': b'M230_REOPEN_HOST_SSH_READY'}
HEX_48 = re.compile(r'[0-9a-f]{48}\Z')
HEX_64 = re.compile(r'[0-9a-f]{64}\Z')
FINGERPRINT = re.compile(r'SHA256:[A-Za-z0-9+/]{43}\Z')


def valid_port(raw: str) -> int:
    if not raw.isascii() or not raw.isdecimal() or not 1 <= int(raw) <= 65535:
        raise ValueError('host SSH port must be decimal 1..65535')
    return int(raw)


def wait_for_guest_ready(serial: Path, marker: bytes, seconds: int) -> None:
    deadline = time.monotonic() + seconds
    position = 0
    tail = b''
    while time.monotonic() < deadline:
        try:
            with serial.open('rb') as stream:
                if stream.seek(0, 2) < position:
                    position, tail = 0, b''
                stream.seek(position)
                chunk = stream.read()
                position = stream.tell()
        except FileNotFoundError:
            chunk = b''
        if chunk:
            combined = tail + chunk
            if any(line.strip() == marker for line in
                   combined.replace(b'\r', b'').splitlines()):
                return
            tail = combined[-max(256, len(marker) * 2):]
        time.sleep(0.2)
    raise TimeoutError('original Alpine PID1 did not emit M230 Dropbear-ready UART')


def public_key_sha256(private_key: Path) -> str:
    public = Path(str(private_key) + '.pub')
    if (private_key.is_symlink() or not private_key.is_file() or
            public.is_symlink() or not public.is_file()):
        raise ValueError('host-only Ed25519 private/public keypair missing')
    line = public.read_text(encoding='ascii').strip()
    parts = line.split()
    if len(parts) not in (2, 3) or parts[0] != 'ssh-ed25519':
        raise ValueError('host-only client key is not OpenSSH Ed25519')
    try:
        data = base64.b64decode(parts[1], validate=True)
    except ValueError as exc:
        raise ValueError('invalid host-only client public key') from exc
    if len(data) < 32:
        raise ValueError('truncated host-only client Ed25519 public key')
    return hashlib.sha256((parts[0] + ' ' + parts[1] + '\n').encode()).hexdigest()


def known_host_fingerprint(path: Path) -> str:
    if path.is_symlink() or not path.is_file():
        raise ValueError('missing or symlinked host-only pinned known_hosts')
    lines = [line for line in path.read_text(encoding='ascii').splitlines()
             if line and not line.startswith('#')]
    if len(lines) != 1:
        raise ValueError('expected exactly one original Dropbear pinned host key')
    fields = lines[0].split()
    if len(fields) < 3 or fields[1] != 'ssh-ed25519':
        raise ValueError('expected genuine pinned Dropbear Ed25519 host key')
    result = subprocess.run(['ssh-keygen', '-E', 'sha256', '-lf', str(path)],
                            capture_output=True, text=True, timeout=10, check=True)
    match = re.search(r'\b(SHA256:[A-Za-z0-9+/]{43})\b', result.stdout)
    if match is None or FINGERPRINT.fullmatch(match.group(1)) is None:
        raise ValueError('unparseable pinned original Dropbear host key fingerprint')
    return match.group(1)


def first_receipt(path: Path) -> dict:
    if path.is_symlink() or not path.is_file():
        raise ValueError('second independent QEMU requires first SSH receipt')
    try:
        receipt = json.loads(path.read_text(encoding='utf-8'))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise ValueError('invalid first native-QEMU SSH receipt') from exc
    if not isinstance(receipt, dict) or receipt.get('phase') != 'first':
        raise ValueError('second QEMU supplied a foreign first SSH receipt')
    for field in ('nonce', 'host_key_fingerprint', 'client_pubkey_sha256'):
        val = receipt.get(field)
        regex = HEX_48 if field == 'nonce' else FINGERPRINT if field == 'host_key_fingerprint' else HEX_64
        if not isinstance(val, str) or regex.fullmatch(val) is None:
            raise ValueError('first SSH receipt lacks ' + field)
    if receipt.get('remote_command_success') is not True:
        raise ValueError('first native-QEMU SSH remote command did not pass')
    return receipt


def remote_command(nonce: str) -> str:
    # An unpredictable host command is evaluated by the original guest shell.
    # There is no pre-staged nonce, fake command output or guest-side receipt.
    return "/bin/busybox printf 'M230-SSH-REMOTE-EXEC:%s\\n' " + nonce


def ssh_command(phase: str, port: int, key: Path, hosts: Path, nonce: str,
                ssh_timeout: int) -> list[str]:
    options = [
        '-F', '/dev/null', '-T', '-p', str(port), '-i', str(key),
        '-o', 'BatchMode=yes', '-o', 'IdentitiesOnly=yes',
        '-o', 'PreferredAuthentications=publickey',
        '-o', 'PasswordAuthentication=no',
        '-o', 'KbdInteractiveAuthentication=no',
        '-o', 'GSSAPIAuthentication=no',
        '-o', 'ForwardAgent=no', '-o', 'ClearAllForwardings=yes',
        '-o', 'RequestTTY=no', '-o', 'PermitLocalCommand=no',
        '-o', 'ControlMaster=no', '-o', 'EscapeChar=none',
        '-o', 'LogLevel=ERROR', '-o', 'GlobalKnownHostsFile=/dev/null',
        '-o', 'UserKnownHostsFile=' + str(hosts),
        '-o', 'HashKnownHosts=no', '-o', 'HostKeyAlgorithms=ssh-ed25519',
        '-o', 'StrictHostKeyChecking=' + ('accept-new' if phase == 'first' else 'yes'),
        '-o', 'ConnectTimeout=' + str(min(ssh_timeout, 45)),
        '-o', 'ConnectionAttempts=1',
    ]
    return ['ssh', *options, 'root@127.0.0.1', remote_command(nonce)]


def genuine_ssh(port: int, phase: str, key: Path, hosts: Path,
                nonce: str, window: int, per_attempt: int) -> tuple[int, str]:
    expected = 'M230-SSH-REMOTE-EXEC:' + nonce + '\n'
    deadline = time.monotonic() + window
    attempts = 0
    last_error = 'no SSH session'
    while time.monotonic() < deadline:
        attempts += 1
        try:
            trial = subprocess.run(ssh_command(phase, port, key, hosts, nonce,
                                               per_attempt),
                                   capture_output=True, timeout=per_attempt)
            if trial.returncode == 0 and trial.stdout.decode('ascii') == expected:
                return attempts, hashlib.sha256(trial.stdout).hexdigest()
            last_error = 'SSHExit' + str(trial.returncode)
        except (OSError, subprocess.TimeoutExpired, UnicodeError) as exc:
            # Never print SSH stderr, guest disk contents or host credentials.
            last_error = type(exc).__name__
        time.sleep(0.4)
    raise TimeoutError('genuine host SSH remote execution failed: ' + last_error)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('phase', choices=('first', 'reopen'))
    parser.add_argument('uart', type=Path)
    parser.add_argument('host_port', type=valid_port)
    parser.add_argument('receipt', type=Path)
    parser.add_argument('--client-key', type=Path, required=True)
    parser.add_argument('--known-hosts', type=Path, required=True)
    parser.add_argument('--first-receipt', type=Path)
    parser.add_argument('--ready-timeout', type=int, default=2650)
    parser.add_argument('--ssh-timeout', type=int, default=90)
    args = parser.parse_args()
    if args.ready_timeout < 1 or args.ssh_timeout < 1:
        parser.error('timeouts must be positive')
    if args.receipt.exists() or args.receipt.is_symlink():
        parser.error('refusing to overwrite host SSH receipt')
    if args.known_hosts.is_symlink():
        parser.error('host-only known_hosts must not be a symlink')
    if args.known_hosts.resolve(strict=False) == args.client_key.resolve(strict=False):
        parser.error('host-only key and known_hosts must be independent')
    prior = None
    if args.phase == 'reopen':
        if args.first_receipt is None:
            parser.error('second native QEMU requires --first-receipt')
        prior = first_receipt(args.first_receipt)
        if args.known_hosts.is_symlink() or not args.known_hosts.is_file():
            parser.error('second QEMU requires host-only first-boot pinned host key')
    elif args.known_hosts.exists() and args.known_hosts.stat().st_size:
        parser.error('first QEMU refuses already-pinned host key from another run')
    client_sha = public_key_sha256(args.client_key)
    if prior and (prior['client_pubkey_sha256'] != client_sha or
                  prior.get('host_port') != args.host_port):
        parser.error('second native QEMU host keypair or forwarded port changed')
    nonce = secrets.token_hex(24)
    if prior and prior['nonce'] == nonce:
        raise ValueError('reused SSH remote-command challenge after reboot')
    print(f'[M230 HOST WAIT] phase={args.phase} original Alpine Dropbear ready', flush=True)
    wait_for_guest_ready(args.uart, READY[args.phase], args.ready_timeout)
    print(f'[M230 HOST READY] phase={args.phase} real SSH public-key remote exec', flush=True)
    attempts, output_sha = genuine_ssh(args.host_port, args.phase, args.client_key,
                                      args.known_hosts, nonce, args.ssh_timeout,
                                      min(60, args.ssh_timeout))
    fingerprint = known_host_fingerprint(args.known_hosts)
    if prior and prior['host_key_fingerprint'] != fingerprint:
        raise ValueError('original Dropbear host key changed across persisted reboot')
    record = {
        'phase': args.phase, 'transport': TRANSPORT, 'host': '127.0.0.1',
        'host_port': args.host_port, 'guest_port': GUEST_PORT,
        'nonce': nonce, 'nonce_sha256': hashlib.sha256(nonce.encode()).hexdigest(),
        'remote_command_sha256': hashlib.sha256(remote_command(nonce).encode()).hexdigest(),
        'remote_output_sha256': output_sha, 'remote_command_success': True,
        'public_key_only': True, 'root_only_isolated_test_no_DAC': True,
        'pty_allocated': False, 'host_key_fingerprint': fingerprint,
        'client_pubkey_sha256': client_sha, 'attempts': attempts,
        'timestamp_utc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
    }
    args.receipt.parent.mkdir(parents=True, exist_ok=True)
    with args.receipt.open('x', encoding='utf-8') as output:
        json.dump(record, output, sort_keys=True, indent=2)
        output.write('\n')
    print(f'[M230 HOST SSH RECEIPT] phase={args.phase} original guest command '
          'and persisted host key verified; PCAP/ext4 still required', flush=True)


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError, TimeoutError, subprocess.CalledProcessError) as exc:
        raise SystemExit('[M230 HOST SSH FAIL] ' + str(exc)) from exc
