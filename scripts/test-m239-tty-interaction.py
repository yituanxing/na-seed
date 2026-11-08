#!/usr/bin/env python3
"""Host-only falsification for M239's real Alpine UART terminal extension.

The fake transcript here may prove parser fail-closed behavior, NEVER native
QEMU, official Alpine login, or real Linux N_TTY/foreground SIGINT success.
"""

import importlib.util
from pathlib import Path


ROOT = Path(__file__).resolve().parent


def load(name: str):
    source = ROOT / name
    spec = importlib.util.spec_from_file_location(name.replace('-', '_'), source)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


m194 = load('m194-alpine-uart.py')
tty = load('m239-tty-interaction.py')


class Transcript:
    def __init__(self):
        self.sent = []
        self.text = ''
        self.probe = tty.M239TTYInteraction()
        self.probe.start(
            0, send=self.sent.append, prompt=m194.shell_prompt_after_step,
            getty=lambda text: m194.getty_login_host(text, ('naseed', 'localhost')),
            password=m194.m193.PASSWORD)

    def append(self, fragment: str, now: float = 0.0) -> bool:
        self.text += fragment
        return self.probe.feed(self.text, now)

    def tick(self, now: float) -> bool:
        return self.probe.feed(self.text, now)


def reject(callback, label: str):
    try:
        callback()
    except RuntimeError:
        return
    raise AssertionError('M239 accepted forged terminal evidence: ' + label)


def reach_sigint_arm():
    t = Transcript()
    assert not t.append('naseed:~# ')
    assert t.probe.state == 'rearm_result'
    assert 'rc-service mosquitto start' in t.sent[-1]
    assert not t.append('\nM239_TTY_FOUR_OPENRC_REARM_PASS\n')
    assert not t.append('naseed:~# ')
    assert t.probe.state == 'canon_ready'
    assert 'read -r answer' in t.sent[-1]
    assert not t.append('\nM239_TTY_CANON_READY\n')
    assert t.sent[-1] == 'alpiX\x7fne\r'
    assert not t.append('alpiX\b \bne\nM239_TTY_CANON_BACKSPACE_READ_PASS\n')
    assert not t.append('naseed:~# ')
    assert t.probe.state == 'sigint_arm'
    assert 'sleep 30' in t.sent[-1]
    return t


def valid_cycle():
    t = reach_sigint_arm()
    assert not t.append('\nM239_TTY_SIGINT_ARMED\n', now=1.0)
    assert t.probe.state == 'sigint_delay'
    assert not t.tick(2.0)
    assert not t.tick(2.26)
    assert t.sent[-1] == '\x03', 'Ctrl-C must arrive from host UART'
    assert not t.append('^C\nnaseed:~# ', now=2.4)
    assert t.probe.state == 'service_result'
    assert 'rc-service "$svc" status' in t.sent[-1]
    assert not t.append('\nM239_TTY_FOUR_OPENRC_AFTER_SIGINT_PASS\n')
    assert not t.append('naseed:~# ')
    assert t.sent[-1] == '\x04', 'VEOF must arrive at genuine empty prompt'
    assert not t.append('logout\nlocalhost login: ')
    assert t.sent[-1] == 'root\r'
    assert not t.append('root\nPassword:')
    assert t.sent[-1] == m194.m193.PASSWORD + '\r'
    assert not t.append('\n[EXEC] replaced process with lwext4 ELF: /bin/login\n'
                        'localhost:~# ')
    assert t.probe.state == 'final_result'
    assert 'id -u' in t.sent[-1]
    assert 'rc-service mosquitto stop' in t.sent[-1]
    assert '[ -s /var/lib/mosquitto/m231.db ]' in t.sent[-1]
    assert '&& sync &&' in t.sent[-1], 'final physical ext4 writes must flush'
    assert not t.append('\nM239_TTY_GETTY_RELOGIN_FOUR_OPENRC_PASS\n')
    assert t.append('localhost:~# ')
    assert t.probe.state == 'done'


def negative_cases():
    # An echoed command containing an output-marker literal is not evidence.
    fake = Transcript()
    fake.append('naseed:~# ')
    fake.append("naseed:~# printf 'M239_TTY_FOUR_OPENRC_REARM_PASS\\n'")
    assert fake.probe.state == 'rearm_result'

    reject(lambda: (lambda t: (
        t.append('naseed:~# '), t.append('naseed:~# ')))(Transcript()),
        'returned before four OpenRC re-arm')

    t = Transcript()
    t.append('naseed:~# ')
    t.append('\nM239_TTY_FOUR_OPENRC_REARM_PASS\n')
    t.append('naseed:~# ')
    reject(lambda: t.append('naseed:~# '),
           'returned before canonical input')

    t = reach_sigint_arm()
    t.append('\nM239_TTY_SIGINT_ARMED\n', now=1.0)
    t.tick(3.0)
    reject(lambda: t.append('\nM239_TTY_SLEEP_WAS_NOT_INTERRUPTED\n'),
           'foreground process not interrupted')

    t = reach_sigint_arm()
    t.append('\nM239_TTY_SIGINT_ARMED\n', now=1.0)
    t.tick(3.0)
    t.append('^C\nnaseed:~# ', now=3.1)
    reject(lambda: t.append('\nnaseed:~# '), 'service liveness skipped')

    t = reach_sigint_arm()
    t.append('\nM239_TTY_SIGINT_ARMED\n', now=1.0)
    t.tick(3.0)
    t.append('^C\nnaseed:~# ', now=3.1)
    t.append('\nM239_TTY_FOUR_OPENRC_AFTER_SIGINT_PASS\n')
    t.append('naseed:~# ')
    t.append('logout\nlocalhost login: root')
    assert t.probe.state == 'new_getty', 'echoed getty username accepted'
    t.append('\nlocalhost login: ')
    t.append('root\nPassword:')
    reject(lambda: t.append('\nlocalhost:~# '),
           'second genuine login exec required')


def source_wiring():
    m233 = (ROOT / 'm233-alpine-uart.py').read_text()
    m194_source = (ROOT / 'm194-alpine-uart.py').read_text()
    workflow = (ROOT.parent / '.github/workflows/m239-four-openrc-services.yml').read_text()
    assert 'terminal_probe=(m239_terminal_probe() if multiservice == \'1\'' in m233
    assert "and phase == 'reopen' else None)" in m233
    assert 'terminal_probe=None' in m194_source
    assert 'terminal_probe.feed(text, time.monotonic())' in m194_source
    assert 'scripts/test-m239-tty-interaction.py' in workflow


if __name__ == '__main__':
    valid_cycle()
    negative_cases()
    source_wiring()
    print('[M239 TTY HOST CONTRACT PASS] canonical erase/read, delayed real UART Ctrl-C, '
          'four OpenRC services, Ctrl-D getty respawn, password and second login; '
          'native QEMU still REQUIRED')
