#!/usr/bin/env python3
"""FINAL M229 original Alpine Valkey/OpenRC and physical ext4/RDB oracle.

Requires TWO independently captured original QEMU hostfwd RESP sessions,
random authenticated first-boot SET/GET/SAVE, reopen GET of that *same*
value, original signed APKs, original Alpine PID1/OpenRC and physical ext4.
Neither a green QEMU step nor host JSON alone is accepted.
"""

import hashlib
import importlib.util
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
        raise RuntimeError('M229 requires original ext4/wire oracle: ' + name)
    obj = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(obj)
    return obj


M227 = load('m227-alpine-disk-check.py', 'm229_original_openrc_disk')
M229 = load('m229-resp-oracle.py', 'm229_original_valkey_wire')
M191 = M227.M191
SIGNED_PACKAGES = ('valkey', 'valkey-cli', 'valkey-openrc')
SIGNED_ELFS = ('/usr/bin/valkey-server', '/usr/bin/valkey-cli')
REQUIRED_CONFIG = (
    b'bind 0.0.0.0\n',
    b'protected-mode yes\n',
    b'port 15445\n',
    b'daemonize no\n',
    b'dir /var/lib/valkey\n',
    b'dbfilename m229.rdb\n',
    b'save ""\n',
    b'rdbcompression no\n',
    b'appendonly no\n',
)


def fail(message: str) -> None:
    raise ValueError('[M229 VALKEY EXT4 FAIL] ' + message)


def check_artifacts(port: str, read, stat, receipt: dict) -> dict:
    """Check exact read-only ext4 contents, never run a host stand-in DB."""
    if port not in M191.ARCH_MACHINE:
        fail('unsupported original Alpine ISA')
    world = read('/etc/apk/world').decode('utf-8')
    installed = read('/lib/apk/db/installed')
    for package in SIGNED_PACKAGES:
        if package not in world.splitlines() or (b'\nP:' + package.encode() + b'\n') not in b'\n' + installed:
            fail('original signed APK DB lacks ' + package)
    for path in SIGNED_ELFS:
        binary = read(path)
        if (len(binary) < 20 or binary[:6] != b'\x7fELF\x02\x01' or
                struct.unpack_from('<H', binary, 18)[0] != M191.ARCH_MACHINE[port]):
            fail('wrong-ISA or missing genuine upstream Valkey ELF: ' + path)
    if not read('/etc/init.d/valkey').startswith(b'#!'):
        fail('original valkey-openrc daemon script absent')
    confd = read('/etc/conf.d/valkey')
    if b'cfgfile="/etc/valkey/m229.conf"' not in confd:
        fail('original Alpine OpenRC configuration does not select Valkey service')
    runlevel = stat('/etc/runlevels/default/valkey')
    if 'Type: symlink' not in runlevel or 'valkey' not in runlevel:
        fail('actual default-runlevel Valkey symlink absent')

    cfg = read('/etc/valkey/m229.conf')
    for setting in REQUIRED_CONFIG:
        if setting not in cfg:
            fail('original Valkey config lost required settings: ' +
                 setting.decode('ascii').strip())
    token = read('/root/m229-auth-token').strip()
    if not re.fullmatch(rb'[0-9a-f]{64}', token):
        fail('actual physical ext4 lacks random private 256-bit AUTH file')
    if cfg.splitlines().count(b'requirepass ' + token) != 1:
        fail('original Valkey config does not require host-staged strong AUTH')
    for path, mode in (('/root/m229-auth-token', '0600'),
                       ('/etc/valkey/m229.conf', '0640')):
        info = stat(path)
        if 'Type: regular' not in info or re.search(r'Mode:\s+0*' + mode.lstrip('0') + r'\b', info) is None:
            fail('physical ext4 lacks private authentication/config mode: ' + path)

    snapshot = read('/var/lib/valkey/m229.rdb')
    if len(snapshot) < 32 or re.match(rb'REDIS[0-9]{4}', snapshot[:9]) is None:
        fail('real Valkey RDB snapshot absent/truncated/invalid magic')
    key, value = receipt['key'].encode('ascii'), receipt['value'].encode('ascii')
    # rdbcompression=no explicitly prevents hiding the unpredictable first
    # host SET bytes in a compressed stream. This proves the actual disk,
    # while the second live host GET proves Valkey parsed/reloaded the data.
    if key not in snapshot or value not in snapshot:
        fail('first authenticated HOST SET key/value absent from physical RDB')
    if hashlib.sha256(value).hexdigest() != receipt['value_sha256']:
        fail('actual physical RDB value mismatches host-receipt digest')
    return {'original_signed_valkey_apk': sorted(SIGNED_PACKAGES),
            'valkey_original_native_elf': list(SIGNED_ELFS),
            'original_valkey_openrc_default': True,
            'private_valkey_auth_file_and_config': True,
            'real_physical_valkey_rdb_bytes': len(snapshot),
            'real_physical_valkey_rdb_sha256': hashlib.sha256(snapshot).hexdigest(),
            'first_authenticated_host_key_sha256': hashlib.sha256(key).hexdigest(),
            'first_authenticated_host_value_sha256': hashlib.sha256(value).hexdigest()}


def verify(port: str, root: Path, disk: Path, first: Path, reopen: Path,
           first_pcap: Path, reopen_pcap: Path) -> dict:
    all_inputs = (root, disk, first, reopen, first_pcap, reopen_pcap)
    if any(p.is_symlink() for p in all_inputs) or len({
            str(p.resolve(strict=False)) for p in all_inputs}) != len(all_inputs):
        fail('overlapping or symlinked signed root/disk/receipt/PCAP')
    # Independently verify two real host nonces and exact AUTH/Valkey RESP
    # transactions in two different QEMU Ethernet PCAPs. No fake server or
    # receipt-only acceptance. Physical disk checks MUST follow.
    network = M229.verify_transport(first, reopen, first_pcap, reopen_pcap)
    receipt = M229.read_receipt(first, 'first')
    if network['key'] != receipt['key'] or network['value_sha256'] != receipt['value_sha256']:
        fail('host network and final RDB first-boot key do not agree')
    source, physical = root.resolve(strict=True), disk.resolve(strict=True)
    if not source.is_dir() or not physical.is_file():
        fail('original Alpine signed source or physical disk missing')
    for path in ('/etc/valkey/m229.conf', '/var/lib/valkey/m229.rdb',
                 '/etc/runlevels/default/valkey', '/root/m229-auth-token'):
        initial = M191.guest_path(source, path)
        if initial.exists() or initial.is_symlink():
            fail('signed baseline improperly contains M229 generated file: ' + path)
    # M229 inherits M227's original *loopback-only* lighttpd. Only Valkey
    # opts into eth0; M227 original OpenRC/networking and complete M197
    # signed Bash/Lua/SQLite/Git + strict ext4/fsck remain mandatory.
    M227.verify(port, source, physical)
    with tempfile.TemporaryDirectory(prefix='m229-original-valkey-ext4-') as tmp:
        partition = Path(tmp) / 'root.ext4'
        M191.extract_partition(physical, partition)
        def read(path: str) -> bytes:
            return M191.debugfs_cat(partition, path)
        def stat(path: str) -> str:
            result = subprocess.run(['debugfs', '-R', 'stat ' + path,
                                     str(partition)], stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE, text=True, check=True)
            return result.stdout
        actual = check_artifacts(port, read, stat, receipt)
    return {'acceptance': 'original-Alpine-two-PID1-OpenRC-authenticated-host-RESP-persisted-RDB-ext4',
            'port': port, 'real_host_to_guest_authenticated_valkey_two_boot': network,
            'real_original_alpine_signed_valkey_ext4': actual,
            'original_M227_PID1_OpenRC_and_M197_physical_fsck': True}


def main() -> None:
    if len(sys.argv) != 9:
        raise SystemExit('usage: m229-valkey-disk-check.py ISA SIGNED_ROOT DISK '
                         'FIRST_JSON REOPEN_JSON FIRST_PCAP REOPEN_PCAP OUT.json')
    port = sys.argv[1]
    root, disk, first, reopen, pcap1, pcap2, output = map(Path, sys.argv[2:])
    if output.exists() or output.is_symlink() or any(
            output.resolve(strict=False) == source.resolve(strict=False)
            for source in (root, disk, first, reopen, pcap1, pcap2)):
        fail('refusing stale/overlapping final M229 oracle output')
    result = verify(port, root, disk, first, reopen, pcap1, pcap2)
    output.write_text(json.dumps(result, sort_keys=True, indent=2) + '\n')
    print(f'[M229 ORIGINAL VALKEY OPENRC TWO BOOT HOST INGRESS PASS] {port}: '
          'real original signed Alpine Valkey/OpenRC, two independent PID1 '
          'host-authenticated RESP+NIC captures, identical persisted GET, '
          'real RDB on physical ext4 and strict read-only fsck')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, RuntimeError, subprocess.CalledProcessError) as exc:
        raise SystemExit('[M229 FINAL ORACLE FAIL] ' + str(exc)) from exc
