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
    # M197 amd64 run 42374117820 reached the original /sbin/getty, but the
    # DHCP callback printk interrupted its non-newline-terminated prompt.
    # This is real whole-line UART evidence, not a failed Alpine boot.
    status = '[LWIP NETIF STATUS] up=1 link=1 dhcp=1 ip=10.0.2.15'
    assert owner.getty_login_host('\nnaseed login: ' + status + '\n', both) == 'naseed'
    assert owner.getty_login_host('\nlocalhost login: ' + status + '\n', both) == 'localhost'
    # M229 run 42379360720: second real PID1/getty wrote the complete
    # prompt, then the kernel appended this optional-syscall diagnostic on
    # the *same* UART line. It must not leave the second boot waiting forever.
    miss = '[MISS] syscall nr=324'
    assert owner.getty_login_host('\nlocalhost login: ' + miss + '\n', both) == 'localhost'
    assert owner.getty_login_host('\nnaseed login: ' + miss + '\r\n', both) == 'naseed'
    assert owner.getty_login_host('\nlocalhost login: ' + miss + '\n', ('naseed',)) is None
    assert owner.getty_login_host('\nlocalhost login: ' + status + '\n', ('naseed',)) is None
    assert owner.getty_login_host('\nlocalhost login: \r\n', both) is None
    assert owner.getty_login_host('\nlocalhost login: \n', both) == 'localhost'
    assert owner.getty_login_host('\nlocalhost login: ', ('naseed',)) is None
    assert owner.getty_login_host('\nevilhost login: ', both) is None
    assert owner.getty_login_host('naseed:~# echo localhost login: ', both) is None
    assert owner.getty_login_host('[EXEC] localhost login: ', both) is None
    assert owner.getty_login_host(' M227 localhost login: ', both) is None
    for fake in (
        '\nnaseed login: root\n',
        '\nnaseed login: some ordinary text\n',
        '\nnaseed login: ' + status + ' forged trailing bytes\n',
        '\nnaseed login: [LWIP NETIF STATUS] bogus data\n',
        '\nnaseed login: [LWIP NETIF STATUS] up=bad link=1 dhcp=1 ip=10.0.2.15\n',
        '\nnaseed login: [LWIP DHCP BOUND] addr=10.0.2.15\n',
        '\nlocalhost login: [MISS] syscall nr=nope\n',
        '\nlocalhost login: [MISS] syscall nr=324 forged trailer\n',
        '\nlocalhost login: [MISS] syscall nr=324 \n',
        '\nlocalhost login: [MISS] syscall nr=-1\n',
        '[EXEC] localhost login: [MISS] syscall nr=324\n',
        'sh: echo localhost login: [MISS] syscall nr=324\n',
        '[EXEC] naseed login: ' + status + '\n',
        'sh: echo naseed login: ' + status + '\n',
    ):
        assert owner.getty_login_host(fake, both) is None, repr(fake)
    # Authentic getty prints the login prompt without a newline, then echoes
    # the typed username on that SAME line.  Its final transcript is no
    # longer a prompt-only line; authentication must preserve the previously
    # validated host rather than attempt to rediscover the initial prompt.
    assert owner.getty_login_host('\nnaseed login: root\nPassword:\n', both) is None
    source_text = source.read_text(encoding='utf-8')
    assert 'authenticated_login_host = detected_host' in source_text
    assert 'if authenticated_login_host is None:' in source_text
    assert 'getty_login_host(text, (authenticated_login_host,))' not in source_text
    # Independent SHA256 of original four profiles, both genuine UART boots.
    import hashlib, runpy
    shared = runpy.run_path(str(Path(__file__).with_name('m227-alpine-uart.py')))
    fingerprints = {"m228":{"first":"c10698e268d412aabed324c4b5dd34191e485151b13609014e987b1bc346bd8a","reopen":"737f0317a2f32e32431536bc2032192f90892b67ddc19f2e0ba8bb4c1e665705"},"m229":{"first":"6a919d41b98e6845c0a6023a52f91e99a53942924bfa01d8eb3a80b35cc05475","reopen":"725bec0c33b26c631818b0169ef47a0159b7556f6de424af800cb9ad47f8ce65"},"m231":{"first":"ec4a9ef49e3f9968feb0eaf260b7787bd0d862cdb9976fa3f2025230185db544","reopen":"8902bdfc99995414d7199fbcc047c308deea2ce7928c487574de798266039d0c"},"m232":{"first":"3046b5aa678eb9773e80a68b5442b44c54867e3dd5d3f0c4e526f53f70f592ed","reopen":"81afe7605713f455aa656d1bd6c99a3e9c79339f6e443c95164f39cb8a44c800"}}
    for name, phases in fingerprints.items():
        for phase, digest in phases.items():
            original = shared['service_commands'](name, phase)
            assert hashlib.sha256(repr(original).encode()).hexdigest() == digest, (name, phase)
    print('[M227 UART LOGIN UNIT PASS] original naseed and restored localhost '
          'whole-line getty accepted only when opted in; command echo rejected')


if __name__ == '__main__':
    check()
