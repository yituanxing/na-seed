#!/usr/bin/env python3
"""Host OpenSSH proof for original Alpine Dropbear across ONE real QEMU reset.

The M244 UART owner performs the real reboot and verifies its QEMU PID. This
independent host process cannot advance that owner until authentic Dropbear
OpenSSH -tt sessions work on *both* boots. The only guest acknowledgements are
created through separate authenticated SSH commands, never host disk writes.
"""

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import secrets
import subprocess
import time


def sibling(name: str, alias: str):
    source = Path(__file__).with_name(name)
    spec = importlib.util.spec_from_file_location(alias, source)
    if spec is None or spec.loader is None:
        raise RuntimeError('required original Alpine host SSH oracle missing')
    obj = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(obj)
    return obj


PTY = sibling('m240-host-ssh-pty-check.py', 'm244_m240_pty')
BASE = PTY.BASE
TRANSPORT = PTY.TRANSPORT
READY = {
    'first': b'M244_FIRST_HOST_SSH_READY',
    'reopen': b'M244_SECOND_HOST_SSH_READY',
}
ACK = {
    'first': ('/root/m244-first-host-pty.ok', 'M244_HOST_PTY_FIRST_OK_20261109'),
    'reopen': ('/root/m244-second-host-pty.ok', 'M244_HOST_PTY_SECOND_OK_20261109'),
}
SESSION_COUNT = 1  # M240's 9x2 stress suite remains an independent slow gate.


def ack_remote_command(phase: str) -> str:
    """Fixed shell-only receipt with no attacker-controlled string interpolation."""
    path, proof = ACK[phase]
    stage = path + '.pending'
    # A guest polling the final pathname must never see a truncated receipt.
    return ("umask 077; test ! -e " + path + " && test ! -L " + path +
            " && test ! -e " + stage + " && test ! -L " + stage +
            " && /bin/busybox printf '%s\\n' '" + proof + "' > " +
            stage + " && /bin/busybox mv " + stage + " " + path +
            " && /bin/busybox cat " + path)


def write_ack_via_ssh(phase: str, port: int, key: Path, hosts: Path,
                      timeout: float) -> str:
    """A host filesystem write must never satisfy the guest's reboot gate."""
    _path, expected = ACK[phase]
    argv = BASE.ssh_command(phase, port, key, hosts,
                            secrets.token_hex(24), max(1, int(timeout)))
    argv[-1] = ack_remote_command(phase)
    result = subprocess.run(argv, capture_output=True,
                            timeout=max(1, timeout))
    if result.returncode != 0 or result.stdout != (expected + '\n').encode():
        raise ValueError(f'authenticated SSH guest receipt failed: phase={phase} '
                         f'exit={result.returncode} stdout_bytes={len(result.stdout)}')
    return hashlib.sha256(result.stdout).hexdigest()


def check_inputs(args: argparse.Namespace) -> str:
    if (args.first_receipt == args.second_receipt or
            args.first_receipt.resolve(strict=False) ==
            args.second_receipt.resolve(strict=False)):
        raise ValueError('first and second immutable host receipts must differ')
    for path in (args.first_receipt, args.second_receipt):
        if path.exists() or path.is_symlink():
            raise ValueError('refusing stale host receipt: ' + str(path))
    if args.known_hosts.is_symlink():
        raise ValueError('pinned host key file must not be a symlink')
    if args.known_hosts.exists() and args.known_hosts.stat().st_size:
        raise ValueError('refusing previously pinned host key from another QEMU')
    if args.known_hosts.resolve(strict=False) == args.client_key.resolve(strict=False):
        raise ValueError('host-only private key and known_hosts must differ')
    if args.ready_timeout < 1 or args.ssh_timeout < 1:
        raise ValueError('timeouts must be positive')
    return BASE.public_key_sha256(args.client_key)


def perform_epoch(phase: str, args: argparse.Namespace, *,
                  client_sha: str, first: dict | None) -> dict:
    """Fail closed before delivering the guest reboot/poweroff interlock ACK."""
    if phase not in ACK:
        raise ValueError('unknown original Alpine boot epoch')
    if phase == 'reopen':
        if first is None or first.get('phase') != 'first':
            raise ValueError('second boot requires first genuine PTY receipt')
        if first.get('host_port') != args.host_port or first.get(
                'client_pubkey_sha256') != client_sha:
            raise ValueError('SSH host port/keypair changed across true reboot')
    print(f'[M244 HOST WAIT] {phase}: original Alpine Dropbear + live host PTY',
          flush=True)
    BASE.wait_for_guest_ready(args.uart, READY[phase], args.ready_timeout)
    deadline = time.monotonic() + args.ssh_timeout
    sessions = []
    # On each boot require a real keyboard-interactive original ash session;
    # ssh argv uses first accept-new, then second StrictHostKeyChecking=yes.
    for index in range(1, SESSION_COUNT + 1):
        nonce = secrets.token_hex(24)
        sessions.append(PTY.run_session(phase, args.host_port, args.client_key,
                                        args.known_hosts, nonce, index, deadline))
    fingerprint = BASE.known_host_fingerprint(args.known_hosts)
    if first is not None and first.get('host_key_fingerprint') != fingerprint:
        raise ValueError('original Alpine Dropbear Ed25519 host key changed after reboot')
    if (not all(s.get('tty_allocated') is True and
                s.get('canonical_backspace') is True and
                s.get('foreground_sigint') is True and
                s.get('winsize_33x101') is True for s in sessions)):
        raise ValueError('genuine original Alpine PTY contract incomplete')
    # Never unblock reboot from a guest printf/serial READY; the guest file is
    # written only by an independently authenticated and checked SSH session.
    remaining = deadline - time.monotonic()
    if remaining <= 1:
        raise TimeoutError('SSH PTY exhausted phase budget before ACK')
    ack_sha = write_ack_via_ssh(phase, args.host_port, args.client_key,
                                args.known_hosts, min(45, remaining))
    receipt = {
        'phase': phase, 'transport': TRANSPORT, 'host': '127.0.0.1',
        'host_port': args.host_port, 'guest_port': BASE.GUEST_PORT,
        'session_count': len(sessions), 'sessions': sessions,
        'pty_allocated': True, 'public_key_only': True,
        'root_only_isolated_test_no_DAC': True,
        'client_pubkey_sha256': client_sha,
        'host_key_fingerprint': fingerprint,
        'guest_ssh_ack_sha256': ack_sha,
        'guest_ssh_ack_path': ACK[phase][0],
        'timestamp_utc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
    }
    return receipt


def write_receipt(path: Path, receipt: dict) -> None:
    if path.exists() or path.is_symlink():
        raise ValueError('host SSH receipt was replaced while guest was running')
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('x', encoding='utf-8') as stream:
        json.dump(receipt, stream, sort_keys=True, indent=2)
        stream.write('\n')


def run(args: argparse.Namespace) -> None:
    client_sha = check_inputs(args)
    first = perform_epoch('first', args, client_sha=client_sha, first=None)
    write_receipt(args.first_receipt, first)
    print('[M244 HOST FIRST PTY PASS] authenticated original ash, real PTY '
          'backspace/Ctrl-C and original OpenSSH-written guest reboot receipt',
          flush=True)
    second = perform_epoch('reopen', args, client_sha=client_sha, first=first)
    write_receipt(args.second_receipt, second)
    print('[M244 HOST SECOND PTY PASS] same original Dropbear host Ed25519 key, '
          'second real ash PTY and original OpenSSH-written poweroff receipt; '
          'same-QEMU PID/native UART, NIC and ext4 oracles still REQUIRED',
          flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('uart', type=Path)
    parser.add_argument('host_port', type=BASE.valid_port)
    parser.add_argument('first_receipt', type=Path)
    parser.add_argument('second_receipt', type=Path)
    parser.add_argument('--client-key', type=Path, required=True)
    parser.add_argument('--known-hosts', type=Path, required=True)
    parser.add_argument('--ready-timeout', type=int, default=550)
    parser.add_argument('--ssh-timeout', type=int, default=180)
    args = parser.parse_args()
    run(args)


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError, TimeoutError,
            subprocess.SubprocessError) as exc:
        raise SystemExit('[M244 HOST SSH TWO EPOCH FAIL] ' + str(exc)) from exc
