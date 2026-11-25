#!/usr/bin/env python3
"""M229 source-only contract; deliberately NOT native QEMU acceptance."""
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
COHORT = ((ROOT / 'scripts/probe-m197-alpine-software-cohort.sh').read_text()
          + '\n' + (ROOT / 'scripts/acceptance/profile.sh').read_text())
UART = (ROOT / 'scripts/m227-alpine-uart.py').read_text()
WORKFLOW = (ROOT / '.github/workflows/alpine-host-services-native.yml').read_text()


def require(condition: bool, context: str) -> None:
    if not condition:
        raise AssertionError('M229 static contract: ' + context)


def check() -> None:
    require('m229=${NAS_M229_VALKEY_HOST:-0}' in COHORT,
            'M229 opt-in must remain disabled by default')
    require('m227+m228+m229' in COHORT,
            'M229 must not contaminate other software cohorts')
    require('disk_mib=512' in COHORT and '|| [ "$m229" = 1 ]' in COHORT,
            'one disposable 512MiB signed ext4 owner, not baseline mutation')
    require('tests/m227-alpine-openrc-workload.sh' in COHORT and
            'tests/m229-alpine-valkey-workload.sh' in COHORT,
            'M227 original OpenRC and independent Valkey work required')
    require('openssl rand -hex 32' in COHORT and
            '"$overlay/root/m229-auth-token"' in COHORT and
            'sudo install -m 0600 -o 0 -g 0' in COHORT,
            'random per-run auth identity must not be staged world-readable')
    require('NAS_M229_RESP_PASSWORD="$m229_password"' in COHORT,
            'real host watcher uses same random AUTH credential')
    require('NAS_VIRTIO_NET_GUESTFWD_PORT=15445' in COHORT and
            'NAS_M229_HOST_PORT:-15445' in COHORT,
            'one original NIC guest/host port must be explicit')
    require('m229_start_host first "$serial"' in COHORT and
            'm229_start_host reopen "$serial"' in COHORT and
            'm229_finish_host first' in COHORT and
            'm229_finish_host reopen' in COHORT,
            'two live independent host challenges mandatory')
    require('m229-first-host-resp.json' in COHORT and
            'm229-reopen-host-resp.json' in COHORT and
            'm229-reopen-wire.pcap' in COHORT and
            'm229-valkey-final-oracle.json' in COHORT,
            'four independent host/PCAP receipts must feed one final oracle')
    require('sudo env NAS_M191_DISK_MIB="$disk_mib"' in COHORT and
            'scripts/m229-valkey-disk-check.py' in COHORT,
            'explicit disposable disk geometry and exact physical verifier')
    require('apk add valkey valkey-cli valkey-openrc' in UART and
            'FIRST_COMMANDS[:-2]' in UART and
            'REBOOT_COMMANDS[:1]' in UART,
            'signed public packages and original PID1/OpenRC lifecycle reused')
    require('alpine-host-services-native.yml' in WORKFLOW and
            'm229)' in WORKFLOW and 'export NAS_M229_VALKEY_HOST=1' in WORKFLOW,
            'M229 real QEMU must be isolated opt-in on first amd64 pilot')
    for path in ('tests/m229-alpine-valkey-workload.sh',
                 'scripts/m229-*.py', 'scripts/test-m229-*.py'):
        require("- '" + path + "'" in WORKFLOW,
                'M229-only source repairs must automatically retrigger amd64 pilot')
    m229_artifact = WORKFLOW.split('      - name: Preserve exact M229', 1)[1].split(
        '      - name: Preserve exact M230', 1)[0]
    require('retention-days: 1' in m229_artifact and
            'm229-auth-token' not in m229_artifact and
            'id_ed25519' not in m229_artifact,
            'single-use AUTH token may enter PCAP; 1-day retention and no raw secrets')
    print('[M229 COHORT STATIC PASS] signed Valkey/OpenRC, two real PID1, '
          'strong one-run AUTH, isolated single-NIC hostfwd, exact ext4/PCAP; '
          'NOT native QEMU acceptance')


if __name__ == '__main__':
    check()
