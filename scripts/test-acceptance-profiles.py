#!/usr/bin/env python3
"""Fail-closed host contract for every historical M197..M239 profile selector.

Exercises the exact POSIX shell parser without booting QEMU or touching Alpine
media. Historical NAS_Mxxx flags and validation diagnostics are the API.
"""

import itertools
import os
from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parent.parent
SELECTORS = (
    ('m198', 'NAS_M198_INTEGRATED_DESKTOP', 'invalid desktop selector'),
    ('m199', 'NAS_M199_ORIGINAL_OPENRC_DESKTOP', 'invalid original OpenRC selector'),
    ('m204', 'NAS_M204_COMPAT_COHORT', 'invalid compatibility selector'),
    ('m205', 'NAS_M205_COMPAT_COHORT', 'invalid compatibility selector'),
    ('m206', 'NAS_M206_COMPAT_COHORT', 'invalid compatibility selector'),
    ('m235', 'NAS_M235_COMPAT_COHORT', 'invalid batch selector'),
    ('m236', 'NAS_M236_COMPAT_COHORT', 'invalid batch selector'),
    ('m237', 'NAS_M237_COMPAT_COHORT', 'invalid batch selector'),
    ('m238', 'NAS_M238_COMPAT_COHORT', 'invalid batch selector'),
    ('m215', 'NAS_M215_PILOT', 'invalid pilot selector'),
    ('m218', 'NAS_M218_PILOT', 'invalid pilot selector'),
    ('m219', 'NAS_M219_PILOT', 'invalid pilot selector'),
    ('m220', 'NAS_M220_PILOT', 'invalid pilot selector'),
    ('m221', 'NAS_M221_PILOT', 'invalid pilot selector'),
    ('m222', 'NAS_M222_PILOT', 'invalid pilot selector'),
    ('m223', 'NAS_M223_PILOT', 'invalid pilot selector'),
    ('m224', 'NAS_M224_PILOT', 'invalid pilot selector'),
    ('m225', 'NAS_M225_PILOT', 'invalid pilot selector'),
    ('m226', 'NAS_M226_PILOT', 'invalid pilot selector'),
    ('m227', 'NAS_M227_INTEGRATED_OPENRC', 'invalid integrated OpenRC selector'),
    ('m228', 'NAS_M228_HOST_INBOUND', 'invalid host ingress selector'),
    ('m229', 'NAS_M229_VALKEY_HOST', 'invalid host RESP selector'),
    ('m230', 'NAS_M230_DROPBEAR_HOST', 'invalid host SSH selector'),
    ('m231', 'NAS_M231_MOSQUITTO_HOST', 'invalid host MQTT selector'),
    ('m232', 'NAS_M232_MEMCACHED_HOST', 'invalid host cache selector'),
    ('m233', 'NAS_M233_DNSMASQ_HOST', 'invalid host UDP+TCP DNS selector'),
    ('m239', 'NAS_M239_MULTISERVICE', 'invalid multi-service selector'),
)
BY_MODE = {mode: env for mode, env, _ in SELECTORS}
SCRIPT = (
    '. scripts/acceptance/profile.sh; '
    'nas_acceptance_parse_profile; '
    'printf "%s|%s|%s|%s|%s\\n" '
    '"$nas_acceptance_profile" "$m198" "$m199" "$m233" "$m239"'
)


def run(overrides):
    env = os.environ.copy()
    for _, key, _ in SELECTORS:
        env.pop(key, None)
    env.update(overrides)
    return subprocess.run(
        ['sh', '-eu', '-c', SCRIPT], cwd=ROOT, env=env,
        capture_output=True, text=True, check=False,
    )


def success(overrides, profile, desktop='0', openrc_desktop='0', dns='0', multi='0'):
    result = run(overrides)
    expected = '|'.join((profile, desktop, openrc_desktop, dns, multi))
    assert result.returncode == 0, (overrides, result.stderr)
    assert result.stdout.strip() == expected, (overrides, result.stdout, expected)
    assert not result.stderr, (overrides, result.stderr)


def failure(overrides, message):
    result = run(overrides)
    assert result.returncode == 2, (overrides, result.returncode, result.stderr)
    assert result.stdout == '', (overrides, result.stdout)
    assert result.stderr == message + '\n', (overrides, result.stderr, message)


def main():
    assert len(SELECTORS) == 27 and len(BY_MODE) == 27
    main_shell = (ROOT / 'scripts/probe-m197-alpine-software-cohort.sh').read_text()
    profile_shell = (ROOT / 'scripts/acceptance/profile.sh').read_text()
    assert main_shell.count('. scripts/acceptance/profile.sh') == 1
    assert main_shell.count('nas_acceptance_parse_profile') == 1
    assert not re.search(r'^m\d{3}=\$\{NAS_M\d{3}_', main_shell, re.MULTILINE)
    for mode, env, _ in SELECTORS:
        assert f'{mode}=${{{env}:-0}}' in profile_shell
    success({}, 'm197')
    # Original ${VAR:-0} accepts an explicitly empty value as disabled.
    success({key: '' for _, key, _ in SELECTORS}, 'm197')

    for mode, key, error in SELECTORS:
        for bad in ('bad', '00', '-1'):
            failure({key: bad}, f'[{mode.upper()} FAIL] {error}')
        if mode == 'm239':
            failure({key: '1'},
                    '[M239 FAIL] multi-service owner requires the M233 DNS base')
        elif mode == 'm199':
            success({key: '1'}, 'm199', desktop='1', openrc_desktop='1')
        elif mode == 'm198':
            success({key: '1'}, 'm198', desktop='1')
        elif mode == 'm233':
            success({key: '1'}, 'm233', dns='1')
        else:
            success({key: '1'}, mode)

    # Every independent cohort excludes every other cohort. The only valid
    # composition is M239 layered on M233, not two distinct cohort ledgers.
    cohorts = [mode for mode in BY_MODE if mode not in ('m198', 'm199', 'm239')]
    for a, b in itertools.combinations(cohorts, 2):
        failure({BY_MODE[a]: '1', BY_MODE[b]: '1'},
                '[COMPAT FAIL] software cohorts must own separate ledgers')
    for desktop in ('m198', 'm199'):
        for cohort in cohorts:
            failure({BY_MODE[desktop]: '1', BY_MODE[cohort]: '1'},
                    '[COMPAT FAIL] software cohort cannot enable a desktop')

    success({BY_MODE['m198']: '1', BY_MODE['m199']: '1'},
            'm199', desktop='1', openrc_desktop='1')
    valid_multi = {BY_MODE['m233']: '1', BY_MODE['m239']: '1'}
    success(valid_multi, 'm239', dns='1', multi='1')
    for desktop in ('m198', 'm199'):
        failure({**valid_multi, BY_MODE[desktop]: '1'},
                '[COMPAT FAIL] software cohort cannot enable a desktop')
    for cohort in cohorts:
        if cohort != 'm233':
            failure({**valid_multi, BY_MODE[cohort]: '1'},
                    '[COMPAT FAIL] software cohorts must own separate ledgers')
    failure({BY_MODE['m239']: '1', BY_MODE['m231']: '1'},
            '[M239 FAIL] multi-service owner requires the M233 DNS base')
    failure({BY_MODE['m198']: 'bad', **valid_multi},
            '[M198 FAIL] invalid desktop selector')
    print('[ACCEPTANCE PROFILE CONTRACT PASS] 27 selectors; legacy precedence, '
          'all valid profiles, pairwise exclusivity, M199 inheritance, '
          'M239/M233 composition and exact stderr/exit-2')


if __name__ == '__main__':
    main()
