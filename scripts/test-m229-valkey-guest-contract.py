#!/usr/bin/env python3
"""Static fail-closed contract for original Alpine Valkey/OpenRC guest logic.

No faux HTTP/RESP server and no automatic runtime acceptance are provided by
this test; a real dual-PID1 QEMU + physical ext4/PCAP host oracle is mandatory.
"""

from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parent.parent
GUEST = ROOT / 'tests/m229-alpine-valkey-workload.sh'


def require(ok: bool, message: str) -> None:
    if not ok:
        raise AssertionError('[M229 GUEST CONTRACT FAIL] ' + message)


def main() -> None:
    subprocess.run(['sh', '-n', str(GUEST)], check=True)
    source = GUEST.read_text(encoding='utf-8')
    require('set -eu' in source, 'guest workload must fail closed')
    for package in ('valkey', 'valkey-cli', 'valkey-openrc'):
        require(f'apk info -e "$package"' in source or
                'for package in valkey valkey-cli valkey-openrc' in source,
                'missing original signed package ' + package)
    for needle in ('/usr/bin/valkey-server', '/usr/bin/valkey-cli',
                   '7f454c46', '/etc/init.d/valkey', '/etc/conf.d/valkey',
                   'valkey:valkey', 'root:valkey'):
        require(needle in source, 'missing original signed Alpine binary/init ' + needle)

    for needle in ('/root/m229-auth-token', 'stat -c %a',
                   'test "${#token}" -eq 64', 'requirepass $token',
                   'umask 077', 'protected-mode yes', 'port 15445',
                   'cfgfile="/etc/valkey/m229.conf"'):
        require(needle in source, 'missing private authenticated listener ' + needle)
    require(not re.search(r'(?m)^\s*(echo|printf|set -x).*\$token', source),
            'private token must never be printed to serial')
    require('VALKEYCLI_AUTH="$token"' in source,
            'original CLI must authenticate without exposing argv password')
    require(not re.search(r'/usr/bin/valkey-cli[^\n]*(?:\bSET\b|\bSAVE\b)', source),
            'guest must not fabricate the independent host SET/SAVE challenge')

    first = source.split('FIRST)\n', 1)
    require(len(first) == 2, 'first-boot control branch missing')
    first, second = first[1].split('    REOPEN)\n', 1)
    require('/root/m227-alpine-openrc-workload.sh --first' in first,
            'must reuse first genuine Alpine networking/OpenRC setup')
    require('rc-update add valkey default' in first and
            'rc-service valkey start' in first,
            'first boot must register and start original Alpine OpenRC Valkey')
    require('/root/m227-alpine-openrc-workload.sh --reopen' in second,
            'second PID1 must reuse M227 original service-recovery checks')
    require('rc-service valkey start' not in second and
            'rc-service valkey restart' not in second and
            '/usr/bin/valkey-server ' not in second,
            'second original PID1 must not manually start/replace Valkey')
    for needle in ('/etc/runlevels/default/valkey', 'rc-service valkey status',
                   '/var/lib/valkey/m229.rdb', 'dbfilename m229.rdb',
                   'rdbcompression no', 'save ""', 'appendonly no'):
        require(needle in source, 'missing original OpenRC/RDB persistence ' + needle)
    for marker in ('M229_FIRST_HOST_RESP_READY',
                   'M229_REOPEN_HOST_RESP_READY',
                   'M229_FIRST_HOST_RESP_WINDOW_COMPLETE',
                   'M229_REOPEN_HOST_RESP_WINDOW_COMPLETE'):
        require(source.count(marker) == 1, 'UART boundary missing or duplicated: ' + marker)
    require('sleep 55' in source,
            'must keep original guest service alive for independent host RESP')
    require(not re.search(r'(?m)^\s*(?:python3?\b|nc\b|socat\b)', source),
            'guest must not substitute a synthetic network server')
    print('[M229 ORIGINAL VALKEY GUEST STATIC PASS] signed ELF and vendor OpenRC '
          'first boot, private authenticated guest:15445, real RDB from host SAVE, '
          'second true PID1 no manual restart, exact UART host windows; '
          'NOT native QEMU acceptance')


if __name__ == '__main__':
    main()
