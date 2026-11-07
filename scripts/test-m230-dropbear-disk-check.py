#!/usr/bin/env python3
"""M230 original signed package/ext4 metadata UNIT fixtures; no QEMU PASS."""

import hashlib
import importlib.util
from pathlib import Path
import struct


def load(filename, name):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    assert spec and spec.loader
    obj = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(obj)
    return obj


disk = load('m230-dropbear-disk-check.py', 'm230_disk_unit')


def reject(fn):
    try:
        fn()
    except (ValueError, KeyError):
        return
    raise AssertionError('invalid signed Dropbear/ext4 evidence unexpectedly accepted')


def check():
    keyline = b'ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIGZha2Uta2V5LWZvci11bml0LXRlc3Q=\n'
    canonical = b' '.join(keyline.split()[:2]) + b'\n'
    receipt = {'client_pubkey_sha256': hashlib.sha256(canonical).hexdigest()}
    correct = bytearray(b'\x7fELF\x02\x01' + b'\x00' * 32)
    struct.pack_into('<H', correct, 18, 62)
    base = {
        '/etc/apk/world': b'dropbear\ndropbear-openrc\n',
        '/lib/apk/db/installed': b'P:dropbear\nV:2025.88-r1\n\nP:dropbear-openrc\nV:2025.88-r1\n',
        '/usr/sbin/dropbear': bytes(correct),
        '/etc/init.d/dropbear': b'#!/sbin/openrc-run\n',
        '/root/.ssh/authorized_keys': b'restrict ' + keyline,
        '/etc/dropbear/dropbear_ed25519_host_key': b'\x9a' * 160,
    }
    state = {'data': base}
    def read(path):
        return state['data'][path]
    def stat(path):
        if path == '/etc/runlevels/default/dropbear':
            return 'Inode: 400 Type: symlink Mode: 0777\nFast link dest: /etc/init.d/dropbear\n'
        return 'Inode: 401 Type: regular Mode: 0600\n'
    good = disk.check_artifacts('amd64', read, stat, receipt)
    assert good['original_openrc_dropbear_default'] is True
    assert good['root_only_isolated_test_not_secure_multiuser_DAC'] is True
    for bad in ('arm64', 'unknown'):
        reject(lambda p=bad: disk.check_artifacts(p, read, stat, receipt))
    bad = dict(base)
    bad['/etc/apk/world'] = b'dropbear\n'
    state['data'] = bad
    reject(lambda: disk.check_artifacts('amd64', read, stat, receipt))
    bad = dict(base)
    binary = bytearray(base['/usr/sbin/dropbear'])
    struct.pack_into('<H', binary, 18, 183)
    bad['/usr/sbin/dropbear'] = bytes(binary)
    state['data'] = bad
    reject(lambda: disk.check_artifacts('amd64', read, stat, receipt))
    bad = dict(base)
    bad['/root/.ssh/authorized_keys'] = b'ssh-ed25519 wrong\n'
    state['data'] = bad
    reject(lambda: disk.check_artifacts('amd64', read, stat, receipt))
    bad = dict(base)
    bad['/root/.ssh/authorized_keys'] = keyline  # correct key without `restrict` is unsafe
    state['data'] = bad
    reject(lambda: disk.check_artifacts('amd64', read, stat, receipt))
    bad = dict(base)
    bad['/etc/dropbear/dropbear_ed25519_host_key'] = b'\x00'
    state['data'] = bad
    reject(lambda: disk.check_artifacts('amd64', read, stat, receipt))
    state['data'] = base
    reject(lambda: disk.check_artifacts('amd64', read, stat,
                                        dict(receipt, client_pubkey_sha256='0' * 64)))
    print('[M230 DROPBEAR EXT4 UNIT PASS] signed APK/ISA, original OpenRC, '
          'client public key and persistent Ed25519 host private key; '
          'UNIT FIXTURES ONLY, no physical QEMU/security acceptance claim')


if __name__ == '__main__':
    check()
