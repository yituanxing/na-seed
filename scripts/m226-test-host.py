#!/usr/bin/env python3
"""Host-only *genuine* official REST protocol two-server-lifecycle preflight.

No host test may be called signed Alpine/native QEMU PASS. That requires M226
independent guest ELF/QEMU PCAP, signed APK SHA, and physical ext4 afterward.
"""
import hashlib
from importlib.util import module_from_spec, spec_from_file_location
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
from urllib.request import urlopen

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parent.parent


def load(name):
    source = ROOT / 'scripts' / name
    spec = spec_from_file_location('m226_preflight_' + name.replace('-', '_'), source)
    module = module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


proof = load('m226-host-proof.py')
origin = load('m226-host-source.py')
BIN = ROOT / 'build/deps/m226-rest-server/rest-server_0.14.0_linux_amd64/rest-server'
CLI = ROOT / 'build/deps/m226-rest-server/restic_0.19.1_linux_amd64'


def wait_port(port):
    for _ in range(80):
        try:
            with socket.create_connection(('127.0.0.1', port), timeout=0.2):
                return
        except OSError:
            time.sleep(.05)
    raise AssertionError(f'official real server did not listen on {port}')


def run(*args, env):
    result = subprocess.run([str(CLI), '--no-cache', *args], env=env,
                            capture_output=True, text=True, timeout=45)
    if result.returncode:
        raise AssertionError(f'host restic {args} failed: {result.stdout} {result.stderr}')
    return result.stdout


def main():
    if not BIN.is_file() or not CLI.is_file():
        raise AssertionError('[M226 PREFLIGHT FAIL] pin/install official binaries first')
    with tempfile.TemporaryDirectory(prefix='m226-true-rest-server-') as temp:
        root = Path(temp)
        server_dir = root / 'persistent'
        server_dir.mkdir()
        env = dict(os.environ, RESTIC_REPOSITORY='rest:http://127.0.0.1:15452/repo/',
                   RESTIC_PASSWORD='m226-disposable-fixture-only-not-a-real-credential',
                   RESTIC_CACHE_DIR=str(root / 'client-cache'))
        for phase in ('first', 'reopen'):
            origin_dir = root / ('m226-host-' + phase)
            origin.generate(phase, origin_dir)
            source_log = root / f'{phase}-source.log'
            server_log = root / f'{phase}-rest.log'
            with source_log.open('w+') as get_log, server_log.open('w+') as rest_log:
                source_process = subprocess.Popen(
                    [sys.executable, '-u', str(ROOT / 'scripts/m226-host-source.py'),
                     'serve', phase, str(origin_dir)], stdout=get_log, stderr=subprocess.STDOUT)
                server_process = subprocess.Popen(
                    [str(BIN), '--no-auth', '--path', str(server_dir),
                     '--listen', '127.0.0.1:15452', '--log', '-'],
                    stdout=rest_log, stderr=subprocess.STDOUT)
                try:
                    wait_port(15452)
                    wait_port(15453)
                    challenge = origin.source(phase, origin_dir)
                    received = urlopen('http://127.0.0.1:15453/m226.source', timeout=4).read()
                    if received != challenge:
                        raise AssertionError('independent real source transfer mismatch')
                    if phase == 'first':
                        run('init', env=env)
                    else:
                        # The SECOND server must read the FIRST server's actual
                        # unmodified filesystem and existing encrypted snapshot.
                        stored = json.loads(run('snapshots', '--json', env=env))
                        if len(stored) != 1:
                            raise AssertionError('restarted repository lost original snapshot')
                    run('backup', str(origin_dir / 'source.bin'), '--tag', 'm226-' + phase,
                        env=env)
                    stored = json.loads(run('snapshots', '--json', env=env))
                    if len(stored) != (1 if phase == 'first' else 2):
                        raise AssertionError('upstream repository snapshot count mismatch')
                    restore = root / ('restored-' + phase)
                    run('restore', 'latest', '--target', str(restore), env=env)
                    actual = restore / str((origin_dir / 'source.bin').relative_to('/'))
                    if actual.read_bytes() != challenge:
                        raise AssertionError('independent real restic restored wrong bytes')
                    run('check', '--read-data', env=env)
                finally:
                    for process in (source_process, server_process):
                        process.terminate()
                    for process in (source_process, server_process):
                        try:
                            process.wait(timeout=5)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait(timeout=5)
            log = server_log.read_text()
            parsed = proof.rest_server_requests(phase, log)
            if not parsed['rest_requests'] >= 15:
                raise AssertionError('no complete restic server traffic')
            signature = ('M226_SOURCE_GET phase=' + phase + ' bytes=' + str(len(challenge)) +
                         ' sha256=' + hashlib.sha256(challenge).hexdigest())
            if source_log.read_text().count(signature) != 1:
                raise AssertionError('independent source transfer receipt absent')
            if phase == 'first':
                first = origin_dir
            else:
                proof.two_phase_sources(first, origin_dir)
                if not (server_dir / 'repo' / 'config').is_file():
                    raise AssertionError('true upstream repository absent')
            print(f'[M226 HOST-ONLY REAL REST PREFLIGHT] phase={phase} '
                  f"requests={parsed['rest_requests']} snapshots={len(stored)}")
        # The parser must fail closed on a fabricated toy GET/PUT fixture.
        try:
            proof.rest_server_requests('first', 'GET /repo/config HTTP/1.1\nPUT /echo 200')
        except ValueError:
            pass
        else:
            raise AssertionError('toy HTTP fixture incorrectly accepted as restic repo')
    print('[M226 HOST-ONLY COMPLETE] official init/backup/restore/check --read-data, '
          'true persistent server restart, two phase-unique HTTP sources; '
          'NATIVE SIGNED ALPINE ACCEPTANCE NOT CLAIMED')


if __name__ == '__main__':
    main()
