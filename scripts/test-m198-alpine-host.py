#!/usr/bin/env python3
"""Host-only M198 contract checks; never substitutes for QEMU input/video."""
from pathlib import Path
import importlib.util


def load(name):
    path = Path(__file__).with_name(name)
    spec = importlib.util.spec_from_file_location('m198_' + name.replace('-', '_'), path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def check():
    old = load('m197-alpine-uart.py')
    new = load('m198-alpine-uart.py')
    assert new.COMMANDS[:len(old.COMMANDS)] == old.COMMANDS
    assert len(new.COMMANDS) == len(old.COMMANDS) + 5
    all_packages = ('imagemagick', 'lynx', 'mc', 'dialog', 'vim', 'nano',
                    'ncurses-terminfo-base')
    for pkg in all_packages:
        assert 'apk info -e ' + pkg in new.COMMANDS[len(old.COMMANDS)][0]
    assert '/usr/bin/naseed-desktop m187' in new.COMMANDS[-3][0]
    assert 'M198_DESKTOP_EXIT_RC=0' == new.COMMANDS[-3][1]
    assert '/root/m198-editor.txt' in new.COMMANDS[-2][0]
    assert new.COMMANDS[-2][1] == 'M198_GUI_DURABLE_RC=0'
    manifest = Path('scripts/m198-alpine-reopen-cases.tsv').read_text()
    cases = [line.split('\t') for line in manifest.splitlines()
             if line and not line.startswith('#')]
    assert len(cases) == 1 and len(cases[0]) == 6
    assert cases[0][0] == 'm197-batch' and cases[0][5] == '/bin/busybox'
    assert 'M198_GUI_REOPEN_PASS' in cases[0][2]
    assert '/root/m198-editor.txt' in cases[0][2]
    assert 'M197_PUBLIC_ALPINE_SOFTWARE_2026' in cases[0][3]
    disk = load('m198-alpine-disk-check.py')
    assert disk.WITNESS == b'm198vimrealinput\n'
    stage = Path('scripts/stage-m198-alpine-desktop.sh').read_text()
    for app in ('naseed-desktop', 'naseed-image', 'naseed-terminal',
                'naseed-browser', 'naseed-lvgl'):
        assert app in stage
    assert 'host-configure-m171-lvgl.sh' in stage
    assert 'add ' + ' '.join(all_packages) in stage
    # Every *actual* original ELF launched by the four desktop clients must
    # be present in the signed host-stage and the guest executable checks.
    original_clients = {'naseed-image.c': {'/usr/bin/magick'},
                        'naseed-terminal.c': {'/usr/bin/dialog'},
                        'naseed-browser.c': {'/usr/bin/lynx', '/usr/bin/mc'}}
    for source, launched in original_clients.items():
        client = Path('user/desktop', source).read_text()
        for elf in launched:
            assert elf in client
            assert 'test -x ' + elf in new.COMMANDS[len(old.COMMANDS) + 1][0]
            assert elf.rsplit('/', 1)[-1] in stage
    assert '--keys-dir /etc/apk/keys' in stage
    assert '--no-scripts --quiet' in stage
    cross_apk_command = stage.split('apk --arch "$APK_ARCH"', 1)[1].split(
        'add ' + ' '.join(all_packages), 1)[0]
    assert '--initdb' not in cross_apk_command
    assert 'm167-desktop-fixture.ppm' in stage
    assert 'm168-home.html' in stage
    watcher = Path('scripts/m198-qmp-alpine-desktop.py').read_text()
    for token in ('M187_DESKTOP_INITIAL_VISIBLE', 'lwext4 ELF: ',
                  'screenshot', 'vim_ready', 'original_reaped', 'f12',
                  'M198_DESKTOP_INPUTS_RC=0', 'NAS_M198_PRE_GUI_SECONDS'):
        assert token in watcher
    driver = Path('scripts/probe-m197-alpine-software-cohort.sh').read_text()
    assert 'NAS_M198_FIRST_BOOT_SECONDS' in driver
    assert 'NAS_M198_DESKTOP_SECONDS:-300' in driver
    print('[M198 HOST CONTRACT PASS] original Alpine init, M197 public apps, '
          'existing static GUI, independent QMP editor+ext4 oracle')


if __name__ == '__main__':
    check()
