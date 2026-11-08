#!/usr/bin/env python3
"""M230's two real native-QEMU, host-owned SSH, OpenRC integration gates.

Pure host static/negative test. A green result here is NOT an SSH PASS;
only the original Alpine guest, host OpenSSH transaction, PCAP and physical
ext4 oracle after two independent QEMUs can claim functional acceptance.
"""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def gate(source: str, workflow: str) -> None:
    required = (
        'm230=${NAS_M230_DROPBEAR_HOST:-0}',
        'm204+m205+m206+m215+m218+m219+m220+m221+m222+m223+m224+m225+m226+m227+m228+m229+m230',
        'tests/m230-alpine-dropbear-workload.sh',
        '"$overlay/root/m230-alpine-dropbear-workload.sh"',
        '"$overlay/root/m230-authorized-pubkey"',
        'ssh-keygen -q -t ed25519',
        '"$m230_client_dir/id_ed25519.pub"',
        'NAS_VIRTIO_NET_HOSTFWD_PORT=${NAS_M230_HOST_PORT:-12222}',
        'NAS_VIRTIO_NET_GUESTFWD_PORT=15446',
        'BUSYBOX_PARITY_MANIFEST=scripts/m228-alpine-boot-cases.tsv',
        'm230_start_host first "$serial"',
        'm230_start_host reopen "$serial"',
        'scripts/m230-alpine-uart.py first',
        'scripts/m230-alpine-uart.py reopen',
        'M230_FIRST_HOST_SSH_WINDOW_COMPLETE',
        'M230_REOPEN_HOST_SSH_WINDOW_COMPLETE',
        '"$PWD/$work/m230-reopen-wire.pcap"',
        'scripts/m230-dropbear-disk-check.py',
        'NAS_M191_DISK_MIB="$disk_mib"',
        'm230_cleanup',
    )
    for token in required:
        assert token in source, f'M230 integration missing: {token}'

    first = source.index('m230_start_host first "$serial"')
    reopen = source.index('m230_start_host reopen "$serial"')
    bootstrap = source.index('sh scripts/probe-m191-alpine-root.sh "$port"')
    assert bootstrap < first < reopen, 'SSH host watcher before real Alpine PID1'
    assert source.count('m230_start_host reopen "$serial"') == 1
    assert source.count('m230_start_host first "$serial"') == 1
    assert 'rm -rf "$m230_client_dir"' not in source
    assert 'sudo install -m 0600 "$m230_client_dir/id_ed25519"' not in source
    assert '"$overlay/root/id_ed25519"' not in source
    assert '"$overlay/root/.ssh/id_ed25519"' not in source

    workflow_tokens = (
        "NAS_M230_DROPBEAR_HOST: '1'",
        "NAS_M230_HOST_PORT: '12222'",
        'timeout-minutes: 100',
        "options: [amd64, riscv64, arm64, all]",
        "github.event_name == 'push' && '[\"amd64\",\"riscv64\",\"arm64\"]'",
        'scripts/test-m230-dropbear-guest-contract.py',
        'scripts/test-m230-host-ssh.py',
        'scripts/test-m230-dropbear-disk-check.py',
        'm230-first-host-ssh.json',
        'm230-reopen-host-ssh.json',
        'm197-public-wire.pcap',
        'm230-reopen-wire.pcap',
        'm230-dropbear-final-oracle.json',
        'ssh-keygen:openssh-client',
    )
    for token in workflow_tokens:
        assert token in workflow, f'M230 workflow missing: {token}'
    # Treat a production security claim as a test failure while DAC/entropy
    # and arbitrary account switching remain incomplete.
    assert 'production-safe' not in workflow.lower()
    for forbidden in ('id_ed25519\n', 'known_hosts\n', 'm230-host-key.'):
        assert forbidden not in workflow.split('path: |', 1)[-1], (
            f'host-only SSH identity in artifact upload: {forbidden}'
        )


def negative() -> None:
    script = ((ROOT / 'scripts/probe-m197-alpine-software-cohort.sh').read_text()
              + '\n' + (ROOT / 'scripts/acceptance/profile.sh').read_text())
    workflow = (ROOT / '.github/workflows/m230-dropbear-host-ssh.yml').read_text()
    gate(script, workflow)
    broken = [
        (script.replace('NAS_VIRTIO_NET_GUESTFWD_PORT=15446',
                        'NAS_VIRTIO_NET_GUESTFWD_PORT=22'), workflow),
        (script.replace('m230_start_host reopen "$serial"',
                        'echo fake-reboot-receipt'), workflow),
        (script.replace('ssh-keygen -q -t ed25519', 'true -q -t ed25519'), workflow),
        (script, workflow.replace('m230-reopen-wire.pcap', 'missing-second-pcap')),
        (script, workflow.replace(
            "github.event_name == 'push' && '[\"amd64\",\"riscv64\",\"arm64\"]'",
            "github.event_name == 'push' && '[\"amd64\"]'")),
    ]
    for mutation, (src, wf) in enumerate(broken, 1):
        try:
            gate(src, wf)
        except AssertionError:
            continue
        raise AssertionError(f'M230 negative corruption #{mutation} accepted')


if __name__ == '__main__':
    negative()
    print('[M230 STATIC+NEGATIVE COHORT CONTRACT PASS] real two-QEMU SSH only')
