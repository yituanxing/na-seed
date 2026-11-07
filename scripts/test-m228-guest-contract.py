#!/usr/bin/env python3
"""Static M228 guest contract: real signed service, opt-in NIC, no fake reboot."""
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parent.parent
M227 = ROOT / 'tests/m227-alpine-openrc-workload.sh'
M228 = ROOT / 'tests/m228-alpine-host-ingress-workload.sh'


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError('[M228 GUEST CONTRACT FAIL] ' + message)


def main() -> None:
    original = M227.read_text()
    ingress = M228.read_text()
    host_flow = (ROOT / 'scripts/probe-m197-alpine-software-cohort.sh').read_text()
    for source in (M227, M228):
        subprocess.run(['sh', '-n', str(source)], check=True)

    require('server_bind=127.0.0.1' in original and
            'server_bind=0.0.0.0' in original and
            'case "${NAS_M228_EXPOSE_ETH0:-0}"' in original and
            'server.bind = "$server_bind"' in original,
            'M227 default loopback must remain unchanged; M228 wildcard opt-in only')
    require('NAS_M228_EXPOSE_ETH0=1 /bin/sh '
            '/root/m227-alpine-openrc-workload.sh --first' in ingress and
            'NAS_M228_EXPOSE_ETH0=1 /bin/sh '
            '/root/m227-alpine-openrc-workload.sh --reopen' in ingress,
            'must run real M227 signed OpenRC first and automatic reopen work')
    second = ingress.split('    --reopen)', 1)[1].split('    *)', 1)[0]
    for forbidden in ('rc-service lighttpd start', 'apk add',
                      'rc-update add', 'lighttpd -D'):
        require(forbidden not in second,
                'second PID1 secretly installs or manually starts lighttpd')
    require('rc-service lighttpd status' in ingress and
            '/run/lighttpd.pid' in ingress and 'kill -0' in ingress,
            'guest must confirm original OpenRC-managed live lighttpd process')
    require('$busy sleep 45' in ingress and
            ingress.count('M228_FIRST_HOST_INGRESS_READY') == 1 and
            ingress.count('M228_REOPEN_HOST_INGRESS_READY') == 1 and
            ingress.count('M228_FIRST_HOST_INGRESS_WINDOW_COMPLETE') == 1 and
            ingress.count('M228_REOPEN_HOST_INGRESS_WINDOW_COMPLETE') == 1,
            'both boots must expose one bounded, authentic host GET window')
    require('127.0.0.1:15432/index.txt' in original and
            '/usr/bin/wget' in original and '/usr/bin/aria2c' in original,
            'keep genuine original signed guest loopback HTTP consumers')
    require('sudo env NAS_M191_DISK_MIB="$disk_mib"' in host_flow and
            'python3 scripts/m228-inbound-oracle.py' in host_flow,
            'physical M228 disposable ext4 oracle must know 512MiB geometry')
    active = '\n'.join(line for line in ingress.splitlines()
                       if not line.lstrip().startswith('#'))
    require('hostfwd' not in active and
            'curl ' not in active and 'wget ' not in active,
            'host HTTP proof must come from independent host watcher')
    print('[M228 GUEST STATIC PASS] signed M227/OpenRC reuse, wildcard only '
          'when opted in, two genuine PID1 host-GET windows, no manual restart')


if __name__ == '__main__':
    main()
