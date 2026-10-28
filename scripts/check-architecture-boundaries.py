#!/usr/bin/env python3
"""Cross-layer ownership checks for the architecture-first NaSeed contract.

This is a narrow REGRESSION guard, not a substitute for code review. It
forbids known circular dependencies; it does not enforce the not-yet-built
Dentry/Path, device resources, shared mapping, or permission subsystems.
"""
import argparse
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parent.parent
# Only dependencies with one authoritative owner belong here. In particular
# do not ban general file-backed VM reads before MM has a real address_space.
INCLUDE_DRIVER = re.compile(
    r'^\s*#\s*include\s*["<][^">]*(?:fs/|task/|syscall/|'
    r'mm/usercopy\.h|arch/linux_)[^">]*[">]', re.M)
INCLUDE_VFS = re.compile(
    r'^\s*#\s*include\s*["<][^">]*(?:arch/linux_|'
    r'mm/usercopy\.h|syscall/dispatch)[^">]*[">]', re.M)
DIRECT_ABI = re.compile(r'\b(?:syscall_dispatch|nas_user_write_bytes|'
                        r'nas_user_read_bytes|__NR_[A-Za-z0-9_]+)\s*\(')
BOOT_EXEC = re.compile(r'\bbootfs_(?:mounted|find|mount)\s*\(')
BOOT_EXEC_INCLUDE = re.compile(r'^\s*#\s*include\s*["<][^">]*bootfs\.h[">]', re.M)
ARCH_MM = re.compile(r'^\s*#\s*include\s*["<][^">]*(?:'
                     r'fs/|drivers/|bootfs\.h)[^">]*[">]', re.M)

DRIVER_FILES = (
    'kernel/block/device.c',
    'kernel/drivers/core/bus.c',
    'kernel/drivers/virtio/transport.c',
    'kernel/drivers/port/block.c',
    'kernel/drivers/port/dma.c',
)
VFS_FILES = ('kernel/fs/vfs.c', 'kernel/fs/lookup.c',
             'kernel/fs/vfs_providers.c')
MM_FILES = ('kernel/mm/mm.c', 'kernel/mm/fault.c')
EXEC_FILES = ('kernel/task/exec.c',)
FILES = DRIVER_FILES + VFS_FILES + MM_FILES + EXEC_FILES


def violations(path, source):
    """Return forbidden dependencies; SOURCE is text to allow mutation tests."""
    errors = []
    if path in DRIVER_FILES and INCLUDE_DRIVER.search(source):
        errors.append('block/bus/driver core imported FS, Task, syscall or usercopy')
    if path in VFS_FILES:
        if INCLUDE_VFS.search(source):
            errors.append('VFS core imported Linux ABI or usercopy')
        if DIRECT_ABI.search(source):
            errors.append('VFS core called Linux syscall or user-copy service')
    if path in MM_FILES and ARCH_MM.search(source):
        errors.append('MM owner imported filesystem, boot archive or driver')
    if path in EXEC_FILES:
        if BOOT_EXEC.search(source) or BOOT_EXEC_INCLUDE.search(source):
            errors.append('runtime execve depends on bootstrap CPIO/bootfs')
    return errors


def self_test():
    examples = [
        ('kernel/task/exec.c', 'if (bootfs_mounted()) return 0;'),
        ('kernel/task/exec.c', '#include "../bootfs.h"'),
        ('kernel/block/device.c', '#include "../fs/vfs.h"'),
        ('kernel/drivers/core/bus.c', '#include "../../mm/usercopy.h"'),
        ('kernel/fs/vfs.c', 'syscall_dispatch(args);'),
        ('kernel/fs/lookup.c', '#include "../arch/linux_stat_abi.h"'),
        ('kernel/mm/fault.c', '#include "../drivers/virtio/mmio.h"'),
    ]
    for path, source in examples:
        if not violations(path, source):
            raise SystemExit('[ARCH FAIL] self-test missed ' + path + ': ' + source)
    for path in FILES:
        if violations(path, '/* no forbidden dependencies */\n#include <stdint.h>\n'):
            raise SystemExit('[ARCH FAIL] self-test false positive: ' + path)
    print('[ARCH PASS] negative and positive boundary mutation fixtures')


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--self-test', action='store_true')
    args = p.parse_args()
    if args.self_test:
        self_test()
        return
    issues = []
    for path in FILES:
        full = ROOT / path
        if not full.is_file():
            issues.append(f'{path}: required source missing')
            continue
        for detail in violations(path, full.read_text(encoding='utf-8')):
            issues.append(f'{path}: {detail}')
    if issues:
        raise SystemExit('[ARCH FAIL] layer boundary regression:\n' +
                         '\n'.join(issues))
    print('[ARCH PASS] runtime exec / VFS / MM / BlockDevice / driver boundaries')


if __name__ == '__main__':
    main()
