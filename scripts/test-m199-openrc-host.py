#!/usr/bin/env python3
"""Host-only guardrails for real original Alpine OpenRC desktop integration.

These checks are deliberately structural: only physical three-ISA QEMU/QMP
can prove that OpenRC launched the actual guest display and editor programs.
"""

from pathlib import Path
import importlib.util
import re


def read(name: str) -> str:
    return Path(name).read_text()


def check() -> None:
    # Every M199 run inherits the full M198 genuine distro/GUI contract.
    previous = Path(__file__).with_name('test-m198-alpine-host.py')
    spec = importlib.util.spec_from_file_location('m199_m198_contract', previous)
    assert spec is not None and spec.loader is not None
    m198 = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m198)
    m198.check()

    # Service belongs to Alpine's ORIGINAL OpenRC; never replace /sbin/init,
    # write an ad-hoc CPIO PID1, or run an independent shell startup chain.
    service = read('rootfs/alpine/naseed-desktop.openrc')
    assert service.startswith('#!/sbin/openrc-run\n')
    assert re.search(r'(?m)^command=["\']?/usr/bin/naseed-desktop', service)
    assert 'm187' in service
    assert 'command_background' in service
    assert 'pidfile' in service
    assert 'output_log' in service and 'error_log' in service
    assert 'depend()' in service
    assert 'localmount' in service
    assert not re.search(r'(?m)^command=.*?/sbin/init', service)

    stage = read('scripts/stage-m199-alpine-openrc.sh')
    assert 'rootfs/alpine/naseed-desktop.openrc' in stage
    assert '/etc/init.d/naseed-desktop' in stage
    # Enabling the service is intentionally a GUEST OpenRC operation. The
    # first guest writes the runlevel entry to physical ext4, then reboot 2
    # must discover it. Host staging must not fake that durable transition.
    assert 'test ! -e "$enabled"' in stage
    assert 'test ! -L "$enabled"' in stage
    assert not re.search(r'(?m)^\s*(?:ln\s+-s|sudo\s+ln\s+-s)', stage)
    assert 'M199 STAGE PASS' in stage
    # The source is a disposable copied Alpine tree, not the signed base.
    assert 'OVERLAY' in stage.upper() or 'overlay' in stage
    assert 'NAS_M199' not in service  # genuine service, no test-only init

    compositor = read('user/desktop/naseed-desktop.c')
    assert '#include <signal.h>' in compositor
    assert 'sig_atomic_t' in compositor
    assert 'sigaction' in compositor and 'SIGTERM' in compositor
    assert 'while(s.live_count)' in compositor
    assert 'retire_window(&s,role,1)' in compositor
    # A signal flag without a wakeup strategy loses SIGTERM if it arrives
    # right before the old infinite poll. A finite poll allows termination.
    assert not re.search(r'poll\(fds\s*,\s*base\s*\+\s*watchers\s*,\s*-1\s*\)',
                         compositor)
    assert 'unlink(getenv(NASD_SOCKET_ENV))' not in compositor

    # Real guest login issues genuine OpenRC commands, not a host-created
    # default-runlevel link or hand-invoked compositor standing in for init.
    uart_path = Path(__file__).with_name('m199-alpine-uart.py')
    uart_spec = importlib.util.spec_from_file_location('m199_openrc_uart', uart_path)
    assert uart_spec is not None and uart_spec.loader is not None
    uart = importlib.util.module_from_spec(uart_spec)
    uart_spec.loader.exec_module(uart)
    assert uart.FIRST_COMMANDS[:len(uart.M197.COMMANDS)] == uart.M197.COMMANDS
    first = uart.FIRST_COMMANDS[len(uart.M197.COMMANDS):]
    first_commands = '\n'.join(entry[0] for entry in first)
    first_markers = {entry[1] for entry in first}
    assert 'rc-update add naseed-desktop default' in first_commands
    assert 'test ! -e /etc/runlevels/default/naseed-desktop' in first_commands
    assert 'test -L /etc/runlevels/default/naseed-desktop' in first_commands
    assert 'rc-service naseed-desktop start' in first_commands
    assert 'rc-service naseed-desktop status' in first_commands
    assert 'rc-service naseed-desktop stop' in first_commands
    assert 'rc-service naseed-desktop restart' in first_commands
    assert 'test "$old_pid" != "$new_pid"' in first_commands
    assert ': > /var/log/naseed-desktop.log' in first_commands
    assert 'while [ -e /tmp/naseed-desktop.sock ]' in first_commands
    assert '/var/log/naseed-desktop.log' in first_commands
    assert 'cat /var/log/naseed-desktop.log' not in first_commands
    assert '/run/naseed-desktop.pid' in first_commands
    assert '/tmp/naseed-desktop.sock' in first_commands
    assert '/root/m198-editor.txt' in first_commands
    assert 'M199_GUEST_RC_UPDATE_RC=0' in first_markers
    assert 'M199_SERVICE_STOP_RC=0' in first_markers
    assert 'M199_OPENRC_RESTART_RC=0' in first_markers
    assert 'M199_STOP_CLEAN_RC=0' in first_markers
    assert 'M199_FINAL_STOP_RC=0' in first_markers
    assert 'M199_FIRST_DURABLE_RC=0' in first_markers
    # Final stop must also wait for asynchronous OpenRC child/socket teardown,
    # not assume that the stop command returning means resources vanished.
    final_stop = next(entry[0] for entry in first
                      if entry[1] == 'M199_FINAL_STOP_RC=0')
    for gate in ('stop_rc=$?', 'n=0; while',
                 'test ! -e /tmp/naseed-desktop.sock',
                 'test ! -e /run/naseed-desktop.pid',
                 '! rc-service naseed-desktop status'):
        assert gate in final_stop
    assert not re.search(r'(?:/usr/bin/)?naseed-desktop\s+m187', first_commands)

    second = uart.REBOOT_COMMANDS
    second_commands = '\n'.join(entry[0] for entry in second)
    second_markers = {entry[1] for entry in second}
    assert 'rc-service naseed-desktop status' in second[0][0]
    assert 'test -L /etc/runlevels/default/naseed-desktop' in second[0][0]
    assert 'test -s /run/naseed-desktop.pid' in second[0][0]
    assert 'M199_REBOOT_AUTO_STATUS_RC=0' in second_markers
    assert 'M199_REBOOT_LOG_RC=0' in second_markers
    assert 'M199_REBOOT_APK_RC=0' in second_markers
    assert 'M199_REBOOT_WORKLOAD_RC=0' in second_markers
    assert 'M199_REBOOT_DURABLE_RC=0' in second_markers
    assert 'cat /var/log/naseed-desktop.log' not in second_commands
    assert '/root/m198-editor.txt' in second_commands
    assert '/root/m197-wan.txt' in second_commands
    assert '/bin/bash /root/m197-software-workload.sh --reopen' in second_commands
    assert not re.search(r'rc-service\s+naseed-desktop\s+(?:start|restart)',
                         second_commands)
    assert not re.search(r'(?:/usr/bin/)?naseed-desktop\s+m187',
                         second_commands)

    # The external hardware witness must be required by each UART run;
    # guest stdout alone cannot fake a graphics/audio/input success.
    uart_source = uart_path.read_text()
    assert 'NAS_M199_QMP_FAIL_FILE' in uart_source
    assert 'NAS_M199_QMP_PASS_FILE' in uart_source
    assert 'before_command_barriers' in uart_source
    assert 'failure_watch' in uart_source
    assert "stage='M199-' + phase.upper()" in uart_source

    # Unlike M198's second CPIO one-shot reopen, M199 uses the signed
    # Alpine /sbin/init on BOTH native-QEMU boots. The runlevel entry is
    # created by rc-update on boot one, then used without boot-two staging.
    manifest = read('scripts/m199-alpine-boot-cases.tsv')
    cases = [line.split('\t') for line in manifest.splitlines()
             if line.strip() and not line.lstrip().startswith('#')]
    assert len(cases) == 1 and len(cases[0]) == 6
    assert cases[0][0] == 'm197-batch'
    assert cases[0][1] == 'cpio' and cases[0][5] == '/sbin/init'
    driver = read('scripts/probe-m197-alpine-software-cohort.sh')
    assert '. scripts/acceptance/profile.sh' in driver
    driver += '\n' + read('scripts/acceptance/profile.sh')
    assert 'NAS_M199_ORIGINAL_OPENRC_DESKTOP:-0' in driver
    assert re.search(r'if \[ "\$m199" = 1 \]; then[\s\S]{0,260}?\n\s*m198=1', driver)
    assert 'scripts/stage-m199-alpine-openrc.sh "$port" "$overlay"' in driver
    assert driver.count('BUSYBOX_PARITY_MANIFEST=scripts/m199-alpine-boot-cases.tsv') >= 2
    assert 'scripts/m199-alpine-uart.py first "$runner" "$serial"' in driver
    assert 'scripts/m199-alpine-uart.py reboot "$runner" "$serial"' in driver
    assert 'scripts/m199-qmp-alpine-openrc.py --mode first' in driver
    assert 'scripts/m199-qmp-alpine-openrc.py --mode reboot' in driver
    assert 'NAS_M199_QMP_FAIL_FILE' in driver
    assert 'NAS_M199_QMP_PASS_FILE' in driver
    assert 'M199_FIRST_DURABLE_RC=0' in driver
    assert 'M199_OPENRC_RESTART_RC=0' in driver
    assert 'M199_REBOOT_AUTO_STATUS_RC=0' in driver
    assert 'M199_REBOOT_DURABLE_RC=0' in driver
    assert 'test -s "$NAS_M199_QMP_PASS_FILE"' in driver
    assert 'scripts/m198-alpine-disk-check.py' in driver
    assert 'scripts/m191-alpine-root-disk.py verify' in driver
    assert 'M199 ALPINE OPENRC TWO BOOT PASS' in driver

    qmp = read('scripts/m199-qmp-alpine-openrc.py')
    assert 'M199_FIRST_PHYSICAL_QMP_PASS' in qmp
    assert 'M199_REBOOT_PHYSICAL_QMP_PASS' in qmp
    assert "first_boot(sock, reader, serial, outdir)" in qmp
    assert "reboot_auto_boot(sock, reader, outdir)" in qmp
    assert 'm188' in qmp and 'changed_until' in qmp
    # The reboot inputs one character at the first terminal row, above the
    # M188 Vim-body ROI (y>=199). Reject a regression to the old invisible
    # keyboard witness even if the synthetic host-only checks otherwise pass.
    assert "before, (340, 180, 500, 200), 65, 25" in qmp
    assert "'M199_REBOOT_LOG_RC=0'" in qmp
    assert "'M187_DESKTOP_INITIAL_VISIBLE', deadline" not in qmp
    assert 'pointer_click' in qmp and 'screenshot' in qmp
    assert "success_file.write_text(marker + '\\n')" in qmp

    print('[M199 OPENRC HOST CONTRACT PASS] original Alpine service profile, '
          'guest rc-update, service lifecycle, two native PID1 boots, real '
          'QMP graphics and persistent signed apps, M198 preserved')


if __name__ == '__main__':
    check()
