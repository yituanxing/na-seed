#!/usr/bin/env python3
"""Offline fail-closed contract for the original four-service Alpine cohort.

This is a wiring preflight, NOT a claim that native QEMU or real protocols
passed. The actual M239 PASS is emitted only after the three physical disk
and PCAP oracles on two independent native boots.
"""
from pathlib import Path
import importlib.util
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent


def load_uart():
    spec = importlib.util.spec_from_file_location('m239_m233_uart',
                   ROOT / 'scripts/m233-alpine-uart.py')
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    for path in ('scripts/probe-m197-alpine-software-cohort.sh',
                 'scripts/m239-multi-host.sh',
                 'tests/m239-alpine-multiservice-workload.sh'):
        subprocess.run(['sh', '-n', str(ROOT / path)], check=True)
    module = load_uart()
    old = module.FIRST_COMMANDS + module.REOPEN_COMMANDS
    assert all('/root/m239-' not in c for c, _, _ in old)
    new = module.multiservice_commands(module.FIRST_COMMANDS)
    assert len(new) == len(module.FIRST_COMMANDS)
    assert any('apk add dnsmasq dnsmasq-openrc mosquitto mosquitto-clients '
               'mosquitto-openrc memcached memcached-openrc' in command
               for command, _, _ in new)
    assert any('/root/m239-alpine-multiservice-workload.sh --first' in c
               for c, _, _ in new)
    assert any('/root/m239-alpine-multiservice-workload.sh --reopen' in c
               for c, _, _ in
               module.multiservice_commands(module.REOPEN_COMMANDS))

    guest = (ROOT / 'tests/m239-alpine-multiservice-workload.sh').read_text()
    host = (ROOT / 'scripts/m239-multi-host.sh').read_text()
    probe = (ROOT / 'scripts/probe-m197-alpine-software-cohort.sh').read_text()
    for service in ('lighttpd', 'dnsmasq', 'mosquitto', 'memcached'):
        assert 'rc-service "$service" status' in guest, service
        assert '"/etc/runlevels/default/$service"' in guest, service
    for marker in ('M231_${phase}_HOST_MQTT_READY',
                   'M232_${phase}_HOST_CACHE_READY',
                   'M233_${phase}_HOST_DNS_READY'):
        assert marker in guest, marker
    for marker in ('M239_${phase}_FOUR_OPENRC_SERVICES_READY',
                   'M239_${phase}_FOUR_OPENRC_SERVICES_COMPLETE'):
        assert marker in guest, marker
    for program in ('m231-host-mqtt-check.py', 'm232-host-cache-check.py'):
        assert program in host
    for checker in ('m231-mosquitto-disk-check.py',
                    'm232-memcached-disk-check.py',
                    'm233-dnsmasq-disk-check.py'):
        assert checker in probe
    assert "NAS_VIRTIO_NET_EXTRA_HOSTFWD='tcp:15447:15447,tcp:15448:15448'" in probe
    assert '[M239 ORIGINAL ALPINE FOUR SERVICE HOST PROTOCOL TWO BOOT PASS]' in probe
    assert 'm239_finish_host "$phase"' in probe
    assert "trap 'm239_stop_host; m233_stop_host' EXIT" in probe
    assert 'if [ "$m239" = 1 ]; then' in probe
    assert 'NAS_M239_MULTISERVICE' in probe
    # The x86-64 pilot must hand the exact pinned Limine protocol header and
    # UEFI archive to both child jobs. A live limine.h download once yielded
    # RV64 HTTP 504/503/500 before QEMU (run #42477071454).
    workflow = (ROOT / '.github/workflows/m239-four-openrc-services.yml').read_text()
    assert workflow.count('m239-limine-verified-${{ github.sha }}') == 2
    assert 'actions/upload-artifact@v4' in workflow
    assert 'actions/download-artifact@v4' in workflow
    assert 'sha256sum -c build/deps/limine-binary.tar.gz.sha256' in workflow
    assert 'sha256sum -c build/deps/limine.h.sha256' in workflow
    assert workflow.count('build/deps/limine.h.sha256') >= 2
    for target in ('BOOTX64.EFI', 'BOOTRISCV64.EFI', 'BOOTAA64.EFI'):
        assert target in workflow
    print('[M239 HOST CONTRACT PASS] multi-service original APK/OpenRC, '
          'three independent host protocols and three strict physical '
          'NIC/ext4 oracles wired; actual native QEMU remains required')


if __name__ == '__main__':
    main()
