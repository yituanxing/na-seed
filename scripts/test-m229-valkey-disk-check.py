#!/usr/bin/env python3
"""UNIT ONLY: fake in-memory guest files for strict M229 physical RDB oracle.

Real M229 PASS must run m229-valkey-disk-check.py on an original Alpine ext4
disk after two genuine QEMUs, not on these fixtures.
"""

import hashlib
import importlib.util
from pathlib import Path
import struct


spec = importlib.util.spec_from_file_location(
    'm229_valkey_disk_unit', Path(__file__).with_name('m229-valkey-disk-check.py'))
assert spec and spec.loader
owner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(owner)


def reject(action):
    try:
        action()
    except ValueError:
        pass
    else:
        raise AssertionError('M229 physical oracle accepted unsafe/fake input')


def check():
    port = 'amd64'
    key, value = 'm229:' + 'a' * 48, 'c' * 64
    token = b'f' * 64
    receipt = {'key': key, 'value': value,
               'value_sha256': hashlib.sha256(value.encode()).hexdigest()}
    original = bytearray(32)
    original[:6] = b'\x7fELF\x02\x01'
    struct.pack_into('<H', original, 18, owner.M191.ARCH_MACHINE[port])
    config = b''.join(owner.REQUIRED_CONFIG) + b'requirepass ' + token + b'\n'
    files = {
        '/etc/apk/world': b'\n'.join(n.encode() for n in owner.SIGNED_PACKAGES) + b'\n',
        '/lib/apk/db/installed': b'\n' + b'\n'.join(
            b'P:' + n.encode() for n in owner.SIGNED_PACKAGES) + b'\n',
        '/usr/bin/valkey-server': bytes(original),
        '/usr/bin/valkey-cli': bytes(original),
        '/etc/init.d/valkey': b'#!/sbin/openrc-run\n',
        '/etc/conf.d/valkey': b'cfgfile="/etc/valkey/m229.conf"\ncommand_user="root:root"\n',
        '/etc/valkey/m229.conf': config,
        '/root/m229-auth-token': token + b'\n',
        '/var/lib/valkey/m229.rdb': b'REDIS0012\x00' + key.encode() +
                                  b'\x00' + value.encode() + b'\x00' * 8,
    }
    metadata = {
        '/etc/runlevels/default/valkey': 'Type: symlink    name /etc/init.d/valkey',
        '/root/m229-auth-token': 'Type: regular   Mode:  0600',
        '/etc/valkey/m229.conf': 'Type: regular   Mode:  0640',
    }
    read = files.__getitem__
    stat = metadata.__getitem__
    result = owner.check_artifacts(port, read, stat, receipt)
    assert result['original_valkey_openrc_default']
    assert result['current_kernel_root_only_daemon_identity_no_unprivileged_DAC']
    assert result['real_physical_valkey_rdb_bytes'] > 64
    assert result['first_authenticated_host_value_sha256'] == receipt['value_sha256']
    for change in [
        lambda: files.__setitem__('/var/lib/valkey/m229.rdb', b'BAD' +
                                  files['/var/lib/valkey/m229.rdb'][3:]),
        lambda: files.__setitem__('/etc/valkey/m229.conf', config.replace(
            b'protected-mode yes', b'protected-mode no')),
        lambda: files.__setitem__('/etc/conf.d/valkey',
                                  b'cfgfile="/etc/valkey/m229.conf"\n'),
        lambda: files.__setitem__('/etc/apk/world', b'alpine-base\n'),
        lambda: files.__setitem__('/root/m229-auth-token', b'a' * 64),
    ]:
        before = dict(files)
        change()
        reject(lambda: owner.check_artifacts(port, read, stat, receipt))
        files.clear()
        files.update(before)
    before = metadata['/etc/runlevels/default/valkey']
    metadata['/etc/runlevels/default/valkey'] = 'Type: regular'
    reject(lambda: owner.check_artifacts(port, read, stat, receipt))
    metadata['/etc/runlevels/default/valkey'] = before
    wrong_receipt = dict(receipt, value_sha256='0' * 64)
    reject(lambda: owner.check_artifacts(port, read, stat, wrong_receipt))
    print('[M229 VALKEY DISK UNIT PASS] original APK ELF ISA/OpenRC link, '
          'private random AUTH, true RDB magic/nonce/SET value and strict '
          'ext4 evidence contracts; UNIT FIXTURES ONLY')


if __name__ == '__main__':
    check()
