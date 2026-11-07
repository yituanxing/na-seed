#!/usr/bin/env python3
"""M227 only: both authentic getty hostnames, no command-echo false proof."""
import importlib.util
from pathlib import Path


source = Path(__file__).with_name('m194-alpine-uart.py')
spec = importlib.util.spec_from_file_location('m227_m194_login_unit', source)
assert spec and spec.loader
owner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(owner)


def check() -> None:
    both = ('naseed', 'localhost')
    assert owner.getty_login_host('\nlocalhost login: ', both) == 'localhost'
    assert owner.getty_login_host('\nnaseed login: ', both) == 'naseed'
    assert owner.getty_login_host('\nlocalhost login: \r\n', both) is None
    assert owner.getty_login_host('\nlocalhost login: \n', both) == 'localhost'
    assert owner.getty_login_host('\nlocalhost login: ', ('naseed',)) is None
    assert owner.getty_login_host('\nevilhost login: ', both) is None
    assert owner.getty_login_host('naseed:~# echo localhost login: ', both) is None
    assert owner.getty_login_host('[EXEC] localhost login: ', both) is None
    assert owner.getty_login_host(' M227 localhost login: ', both) is None
    print('[M227 UART LOGIN UNIT PASS] original naseed and restored localhost '
          'whole-line getty accepted only when opted in; command echo rejected')


if __name__ == '__main__':
    check()
