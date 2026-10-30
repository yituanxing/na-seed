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
# Syscalls and device endpoints must not treat a physical-page alias as a
# userspace access policy. Only MM and ISA page-table providers may resolve
# raw aliases; the signal trampoline's resident mapping probe is special.
RAW_USER_ALIAS = re.compile(r'\bnas_mmu_user_(?:byte|writable)\s*\(')
SIGNAL_TRAMPOLINE_PROBE = 'nas_mmu_user_byte(NAS_USER_SIGNAL_TRAMPOLINE)'
TMPFS_TASK_DEPENDENCY = re.compile(
    r'^\s*#\s*include\s*["<][^">]*task\.h[">]|'
    r'\bnas_task_at\s*\(|\bfs->cwd\b', re.M)
TMPFS_LEGACY_NAMES = re.compile(
    r'\btmp_(?:path|find|parent_check|create|mkdir|symlink|link|unlink|'
    r'rename|child_entry|name)\s*\(')
# Bootloader-specific legacy argv selection may remain in boot_image/user_stack;
# the *runtime* Task, syscall and I/O layers must never retain application
# identity or scan user output to decide whether an application passed.
APPLICATION_TEST_STATE = re.compile(
    r'\b(?:running_(?:bash|busybox|alpine)|saw_bash_hello|'
    r'matched_chars|observe_stdout)\b')

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
APPLICATION_RUNTIME_FILES = (
    'kernel/task.h', 'kernel/fs/syscall.c', 'kernel/syscall/dispatch.c',
    'kernel/task/exec.c', 'kernel/init/main.c',
)
FILES = DRIVER_FILES + VFS_FILES + MM_FILES + EXEC_FILES + APPLICATION_RUNTIME_FILES


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
    if path in APPLICATION_RUNTIME_FILES and APPLICATION_TEST_STATE.search(source):
        errors.append('application identity or stdout test oracle leaked into runtime')
    if path == 'kernel/fs/tmp_namespace.c':
        code_only = re.sub(r'/\*.*?\*/|//[^\n]*', '', source, flags=re.S)
        if TMPFS_TASK_DEPENDENCY.search(code_only):
            errors.append('tmpfs backend reaches into task CWD or task table')
    if path in ('kernel/fs/tmp_namespace.c', 'kernel/tmpfs.h'):
        if TMPFS_LEGACY_NAMES.search(source):
            errors.append('obsolete tmpfs absolute-path namespace API returned')
    if (path.startswith(('kernel/fs/', 'kernel/drivers/', 'kernel/task/')) and
            path.endswith(('.c', '.h'))):
        examined = source
        if path == 'kernel/task/signal.c':
            examined = examined.replace(SIGNAL_TRAMPOLINE_PROBE, '')
        if RAW_USER_ALIAS.search(examined):
            errors.append('syscall/device/task bypasses fault-aware usercopy')
    return errors


def self_test():
    examples = [
        ('kernel/task/exec.c', 'if (bootfs_mounted()) return 0;'),
        ('kernel/task/exec.c', '#include "../bootfs.h"'),
        ('kernel/task.h', 'bool running_alpine;'),
        ('kernel/fs/syscall.c', 'observe_stdout(ch);'),
        ('kernel/init/main.c', 'if (saw_bash_hello) pass();'),
        ('kernel/block/device.c', '#include "../fs/vfs.h"'),
        ('kernel/drivers/core/bus.c', '#include "../../mm/usercopy.h"'),
        ('kernel/fs/vfs.c', 'syscall_dispatch(args);'),
        ('kernel/fs/lookup.c', '#include "../arch/linux_stat_abi.h"'),
        ('kernel/mm/fault.c', '#include "../drivers/virtio/mmio.h"'),
        ('kernel/fs/socket_syscall.c', 'nas_mmu_user_byte(user_ptr);'),
        ('kernel/drivers/pty.c', 'nas_mmu_user_writable(user_ptr);'),
        ('kernel/task/process.c', 'nas_mmu_user_byte(uaddr);'),
        ('kernel/fs/tmp_namespace.c', '#include "../task.h"'),
        ('kernel/fs/tmp_namespace.c', 'nas_task_at(0);'),
        ('kernel/fs/tmp_namespace.c', 'fs->cwd[0] = 0;'),
        ('kernel/fs/tmp_namespace.c', 'tmp_find("/tmp/x");'),
        ('kernel/tmpfs.h', 'int tmp_rename(const char *, const char *);'),
    ]
    for path, source in examples:
        if not violations(path, source):
            raise SystemExit('[ARCH FAIL] self-test missed ' + path + ': ' + source)
    for path in FILES + ('kernel/fs/socket_syscall.c',
                         'kernel/drivers/pty.c', 'kernel/task/process.c',
                         'kernel/fs/tmp_namespace.c', 'kernel/tmpfs.h'):
        if violations(path, '/* no forbidden dependencies */\n#include <stdint.h>\n'):
            raise SystemExit('[ARCH FAIL] self-test false positive: ' + path)
    if violations('kernel/task/signal.c',
                  'return nas_mmu_user_byte(NAS_USER_SIGNAL_TRAMPOLINE);'):
        raise SystemExit('[ARCH FAIL] signal trampoline probe rejected')
    print('[ARCH PASS] negative and positive boundary mutation fixtures')


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--self-test', action='store_true')
    args = p.parse_args()
    if args.self_test:
        self_test()
        return
    issues = []
    monitored = set(FILES + ('kernel/tmpfs.h',))
    for root in ('kernel/fs', 'kernel/drivers', 'kernel/task'):
        monitored.update(str(p.relative_to(ROOT)) for p in (ROOT / root).rglob('*')
                         if p.suffix in ('.c', '.h'))
    for path in sorted(monitored):
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
