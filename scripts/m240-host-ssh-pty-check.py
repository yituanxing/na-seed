#!/usr/bin/env python3
"""Independent HOST OpenSSH -> ORIGINAL Alpine Dropbear interactive PTY probe.

Every proof is observed on the SSH client's own stdout. A serial READY line,
an SSH TCP banner or an echoed shell command is never sufficient for PASS.
Only disposable root credentials are used; this is not a production DAC test.
"""

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import secrets
import select
import subprocess
import time


def load_base():
    source = Path(__file__).with_name('m230-host-ssh-check.py')
    spec = importlib.util.spec_from_file_location('m240_ssh_base', source)
    if spec is None or spec.loader is None:
        raise RuntimeError('missing original M230 SSH witness')
    obj = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(obj)
    return obj


BASE = load_base()
READY = {'first': b'M240_FIRST_HOST_SSH_PTY_READY',
         'reopen': b'M240_REOPEN_HOST_SSH_PTY_READY'}
TTY_RE = re.compile(rb'(?m)^/dev/pts/[0-9]+\r?$')
SESSION_COUNT = 9  # NAS_PTY_LIMIT=8; a ninth fresh PTY must be reusable.
TRANSPORT = 'host-localhost-QEMU-user-hostfwd-guest-eth0-original-dropbear-ssh-pty'


def ssh_argv(phase: str, port: int, key: Path, hosts: Path) -> list[str]:
    # OpenSSH -tt is indispensable here; -T (M230) explicitly forbids PTY.
    return ['ssh', '-F', '/dev/null', '-tt', '-p', str(port), '-i', str(key),
            '-o', 'BatchMode=yes', '-o', 'IdentitiesOnly=yes',
            '-o', 'PreferredAuthentications=publickey',
            '-o', 'PasswordAuthentication=no', '-o', 'KbdInteractiveAuthentication=no',
            '-o', 'GSSAPIAuthentication=no', '-o', 'ForwardAgent=no',
            '-o', 'ClearAllForwardings=yes', '-o', 'PermitLocalCommand=no',
            '-o', 'ControlMaster=no', '-o', 'EscapeChar=none',
            '-o', 'LogLevel=ERROR', '-o', 'GlobalKnownHostsFile=/dev/null',
            '-o', 'UserKnownHostsFile=' + str(hosts),
            '-o', 'HashKnownHosts=no', '-o', 'HostKeyAlgorithms=ssh-ed25519',
            '-o', 'StrictHostKeyChecking=' + ('accept-new' if phase == 'first'
                                               else 'yes'),
            '-o', 'ConnectTimeout=30', '-o', 'ConnectionAttempts=1',
            'root@127.0.0.1', '/bin/ash -i']


def recv_until(process: subprocess.Popen, expected: bytes, timeout: float,
               initial: bytes = b'') -> bytes:
    """Observe an exact newline-delimited remote proof; bound untrusted output."""
    data = bytearray(initial)
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if expected in data.replace(b'\r', b'').split(b'\n'):
            return bytes(data)
        remaining = max(0.0, min(0.25, deadline - time.monotonic()))
        readable, _, _ = select.select([process.stdout], [], [], remaining)
        if readable:
            chunk = os.read(process.stdout.fileno(), 4096)
            if not chunk:
                break
            data.extend(chunk)
            if len(data) > 131072:
                raise ValueError('SSH remote PTY output exceeded bounded buffer')
    raise TimeoutError('original Dropbear PTY remote proof missing')


def confirm_foreground_block(process: subprocess.Popen, natural_exit: bytes,
                             initial: bytes, seconds: float) -> bytes:
    """Reject a missing/instant-failing sleep before attempting PTY Ctrl-C."""
    data = bytearray(initial)
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if process.poll() is not None:
            raise ValueError('SSH session exited before foreground PTY Ctrl-C')
        if natural_exit in data.replace(b'\r', b'').split(b'\n'):
            raise ValueError('foreground /bin/sleep exited before PTY Ctrl-C')
        readable, _, _ = select.select(
            [process.stdout], [], [], max(0, min(0.1, end - time.monotonic())))
        if readable:
            chunk = os.read(process.stdout.fileno(), 4096)
            if not chunk:
                raise ValueError('SSH PTY stream closed before foreground Ctrl-C')
            data.extend(chunk)
            if len(data) > 131072:
                raise ValueError('foreground PTY output exceeded bounded buffer')
    if natural_exit in data.replace(b'\r', b'').split(b'\n'):
        raise ValueError('foreground sleep returned without signal')
    return bytes(data)


def send(process: subprocess.Popen, payload: bytes) -> None:
    if process.poll() is not None or process.stdin is None:
        raise ValueError('original Dropbear SSH PTY session terminated early')
    process.stdin.write(payload)
    process.stdin.flush()


def run_session(phase: str, port: int, key: Path, hosts: Path,
                nonce: str, index: int, deadline: float) -> dict:
    # No shell parsing of user input; challenge is freshly generated hex only.
    marker = ('M240-PTY-READY:' + nonce).encode()
    process = subprocess.Popen(ssh_argv(phase, port, key, hosts), stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                               bufsize=0, start_new_session=True)
    output = b''
    try:
        send(process, b"stty -echo; stty erase '^?'; printf '\\nM240-PTY-READY:%s\\n' " +
             nonce.encode() + b'; tty; stty rows 33 cols 101; stty size; '
             b"test -t 0 && test -t 1 && printf '\\nM240-PTY-FDS:YES\\n'\n")
        output = recv_until(process, b'M240-PTY-FDS:YES', min(40, deadline - time.monotonic()))
        normalized = output.replace(b'\r', b'')
        if (marker not in normalized.split(b'\n') or
                TTY_RE.search(normalized) is None or
                b'33 101' not in normalized.split(b'\n')):
            raise ValueError('SSH remote PTY path, both tty FDs or winsize invalid')

        # A real ICANON DEL must remove X from the actual guest read buffer.
        # With -echo, remote output cannot be mistaken for echoed host input.
        send(process, b"printf '\\nM240-READ-READY:%s\\n' " + nonce.encode() +
             b"; read -r answer; printf '\\nM240-CANON:%s\\n' \"$answer\"\n")
        output = recv_until(process, ('M240-READ-READY:' + nonce).encode(),
                            min(15, deadline - time.monotonic()), output)
        send(process, nonce.encode() + b'X\x7f\n')
        output = recv_until(process, ('M240-CANON:' + nonce).encode(),
                            min(25, deadline - time.monotonic()), output)
        if b'\nM240-CANON:' + nonce.encode() + b'\n' not in output.replace(b'\r', b''):
            raise ValueError('canonical PTY backspace did not erase erroneous byte')

        # The foreground real guest sleep must be interrupted by PTY ISIG.
        send(process, b"printf '\\nM240-SLEEP-START:%s\\n' " + nonce.encode() +
             b"; /bin/sleep 30; printf '\\nM240-SLEEP-NATURAL:%s\\n' " +
             nonce.encode() + b'\n')
        output = recv_until(process, ('M240-SLEEP-START:' + nonce).encode(),
                            min(15, deadline - time.monotonic()), output)
        # The shell MUST remain genuinely blocked by the foreground command;
        # otherwise an absent/failed sleep could fake the later shell marker.
        output = confirm_foreground_block(
            process, ('M240-SLEEP-NATURAL:' + nonce).encode(), output, 1.2)
        interrupt_at = time.monotonic()
        send(process, b'\x03')
        send(process, b"printf '\\nM240-ISIG:%s\\n' " + nonce.encode() + b'\n')
        output = recv_until(process, ('M240-ISIG:' + nonce).encode(),
                            min(15, deadline - time.monotonic()), output)
        if ('M240-SLEEP-NATURAL:' + nonce).encode() in output:
            raise ValueError('foreground sleep completed naturally without SIGINT')
        if time.monotonic() - interrupt_at >= 15:
            raise ValueError('PTY Ctrl-C did not interrupt foreground sleep')
        # Graceful exit exercises PTY close and owner FD cleanup.
        send(process, b'exit\n')
        process.wait(timeout=max(1, min(15, deadline - time.monotonic())))
        if process.returncode != 0:
            raise ValueError('SSH interactive PTY failed to cleanly exit')
        return {'session': index, 'nonce': nonce,
                'nonce_sha256': hashlib.sha256(nonce.encode()).hexdigest(),
                'tty_allocated': True,
                'canonical_backspace': True, 'foreground_sigint': True,
                'winsize_33x101': True,
                'remote_stdout_sha256': hashlib.sha256(output).hexdigest()}
    finally:
        if process.poll() is None:
            process.kill()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            pass
        if process.stdin is not None:
            process.stdin.close()
        if process.stdout is not None:
            process.stdout.close()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('phase', choices=('first', 'reopen'))
    parser.add_argument('uart', type=Path)
    parser.add_argument('host_port', type=BASE.valid_port)
    parser.add_argument('receipt', type=Path)
    parser.add_argument('--client-key', type=Path, required=True)
    parser.add_argument('--known-hosts', type=Path, required=True)
    parser.add_argument('--first-receipt', type=Path)
    parser.add_argument('--ready-timeout', type=int, default=2650)
    parser.add_argument('--ssh-timeout', type=int, default=260)
    args = parser.parse_args()
    if args.ready_timeout < 1 or args.ssh_timeout < 1:
        parser.error('timeouts must be positive')
    if args.receipt.exists() or args.receipt.is_symlink() or args.known_hosts.is_symlink():
        parser.error('refusing stale receipt or symlinked host key file')
    prior = None
    if args.phase == 'reopen':
        if args.first_receipt is None or not args.known_hosts.is_file():
            parser.error('reboot requires first host receipt and pinned host key')
        prior = json.loads(args.first_receipt.read_text(encoding='utf-8'))
        if (not isinstance(prior, dict) or prior.get('phase') != 'first' or
                prior.get('pty_allocated') is not True or
                prior.get('transport') != TRANSPORT or
                prior.get('session_count') != SESSION_COUNT or
                not isinstance(prior.get('host_key_fingerprint'), str) or
                not BASE.FINGERPRINT.fullmatch(prior['host_key_fingerprint'])):
            parser.error('reboot requires genuine first SSH PTY receipt')
    elif args.known_hosts.exists() and args.known_hosts.stat().st_size:
        parser.error('first boot refuses a stale host key')
    client_sha = BASE.public_key_sha256(args.client_key)
    if prior and (prior.get('client_pubkey_sha256') != client_sha or
                  prior.get('host_port') != args.host_port):
        parser.error('SSH host identity or port changed between QEMU boots')
    print(f'[M240 HOST WAIT] {args.phase}: original Alpine Dropbear PTY', flush=True)
    BASE.wait_for_guest_ready(args.uart, READY[args.phase], args.ready_timeout)
    deadline = time.monotonic() + args.ssh_timeout
    sessions = []
    # All 9 fresh sessions are required on *each* QEMU boot; catches eight-slot
    # PTY leaks that a single apparently successful PTY would conceal.
    for index in range(SESSION_COUNT):
        nonce = secrets.token_hex(24)
        sessions.append(run_session(args.phase, args.host_port, args.client_key,
                                    args.known_hosts, nonce, index + 1, deadline))
        print(f'[M240 HOST PTY SESSION] {args.phase}: {index + 1}/{SESSION_COUNT}',
              flush=True)
    fingerprint = BASE.known_host_fingerprint(args.known_hosts)
    if prior and prior.get('host_key_fingerprint') != fingerprint:
        raise ValueError('Dropbear host key changed across physical ext4 reboot')
    summary = {'phase': args.phase, 'transport': TRANSPORT, 'host': '127.0.0.1',
               'host_port': args.host_port, 'guest_port': BASE.GUEST_PORT,
               'pty_allocated': True, 'public_key_only': True,
               'root_only_isolated_test_no_DAC': True,
               'session_count': len(sessions), 'sessions': sessions,
               'client_pubkey_sha256': client_sha,
               'host_key_fingerprint': fingerprint,
               'timestamp_utc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())}
    args.receipt.parent.mkdir(parents=True, exist_ok=True)
    with args.receipt.open('x', encoding='utf-8') as stream:
        json.dump(summary, stream, sort_keys=True, indent=2)
        stream.write('\n')
    print(f'[M240 HOST SSH PTY RECEIPT] {args.phase}: {len(sessions)} real '
          'interactive root-only SSH PTYs; NIC/ext4 still REQUIRED', flush=True)


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError, TimeoutError,
            subprocess.SubprocessError) as exc:
        raise SystemExit('[M240 HOST SSH PTY FAIL] ' + str(exc)) from exc
