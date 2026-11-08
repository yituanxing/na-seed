#!/usr/bin/env python3
"""M239-only genuine Alpine ttyS0 input proof, after four OpenRC services.

This is an opt-in continuation of the authenticated M194 serial driver, not
a fake shell, a guest script, or another acceptance profile. Every result is
read from the same native QEMU UART *after* the corresponding host input.
"""

import re
from typing import Callable


def output_line(segment: str, marker: str) -> re.Match[str] | None:
    return re.search(r"(?:^|\n)" + re.escape(marker) + r"(?:\n|$)", segment)


class M239TTYInteraction:
    """Bounded by the owning M194 QEMU deadline; no background guest daemon."""

    CANON_READY = "M239_TTY_CANON_READY"
    CANON_OK = "M239_TTY_CANON_BACKSPACE_READ_PASS"
    SIGINT_ARMED = "M239_TTY_SIGINT_ARMED"
    SIGINT_BAD = "M239_TTY_SLEEP_WAS_NOT_INTERRUPTED"
    SERVICES_OK = "M239_TTY_FOUR_OPENRC_AFTER_SIGINT_PASS"
    RELOGIN_OK = "M239_TTY_GETTY_RELOGIN_FOUR_OPENRC_PASS"

    def __init__(self) -> None:
        self.state = "unstarted"
        self.offset = 0
        self.login_start = 0
        self.sigint_at = 0.0
        self.send: Callable[[str], None] | None = None
        self.prompt: Callable[[str], bool] | None = None
        self.getty: Callable[[str], str | None] | None = None
        self.password = ""

    def start(self, offset: int, *, send: Callable[[str], None],
              prompt: Callable[[str], bool],
              getty: Callable[[str], str | None], password: str) -> None:
        if self.state != "unstarted" or offset < 0 or not password:
            raise RuntimeError("M239 tty interaction cannot be reused")
        self.offset = offset
        self.send = send
        self.prompt = prompt
        self.getty = getty
        self.password = password
        self.state = "first_prompt"

    def _type(self, text: str, next_state: str, length: int) -> None:
        assert self.send is not None
        self.offset = length
        self.send(text)
        self.state = next_state

    def feed(self, text: str, now: float) -> bool:
        if self.state == "unstarted":
            raise RuntimeError("M239 tty interaction was not started")
        if self.state == "done":
            return True
        assert self.prompt is not None and self.getty is not None
        segment = text[self.offset:]

        if self.state == "first_prompt":
            if self.prompt(segment):
                self._type("stty -a >/dev/null && printf 'M239_TTY_CANON_READY\\n'; "
                           "read -r answer; [ \"$answer\" = alpine ] && "
                           "printf 'M239_TTY_CANON_BACKSPACE_READ_PASS\\n'\r",
                           "canon_ready", len(text))
        elif self.state == "canon_ready":
            if match := output_line(segment, self.CANON_READY):
                # This sequence types alpiX, erases X using the live N_TTY
                # VERASE byte, then types ne. Merely echoing it cannot pass.
                self._type("alpiX\x7fne\r", "canon_result",
                           self.offset + match.end())
            elif self.prompt(segment):
                raise RuntimeError("M239 real stty/read returned before input")
        elif self.state == "canon_result":
            if match := output_line(segment, self.CANON_OK):
                self.offset += match.end()
                self.state = "canon_prompt"
            elif self.prompt(segment):
                raise RuntimeError("M239 canonical backspace/read did not match")
        elif self.state == "canon_prompt":
            if self.prompt(segment):
                self._type("printf 'M239_TTY_SIGINT_ARMED\\n'; sleep 30; "
                           "printf 'M239_TTY_SLEEP_WAS_NOT_INTERRUPTED\\n'\r",
                           "sigint_arm", len(text))
        elif self.state == "sigint_arm":
            if match := output_line(segment, self.SIGINT_ARMED):
                self.offset += match.end()
                self.sigint_at = now + 1.25  # give foreground sleep time to exec
                self.state = "sigint_delay"
            elif self.prompt(segment):
                raise RuntimeError("M239 foreground sleep never armed")
        elif self.state in ("sigint_delay", "sigint_prompt"):
            if output_line(segment, self.SIGINT_BAD):
                raise RuntimeError("M239 foreground process ignored real Ctrl-C")
            if self.state == "sigint_delay" and now >= self.sigint_at:
                self._type("\x03", "sigint_prompt", self.offset)
            elif self.state == "sigint_prompt" and self.prompt(segment):
                self._type("alive=1; for svc in lighttpd dnsmasq mosquitto memcached; "
                           "do rc-service \"$svc\" status >/dev/null 2>&1 || alive=0; "
                           "done; [ \"$alive\" = 1 ] && "
                           "printf 'M239_TTY_FOUR_OPENRC_AFTER_SIGINT_PASS\\n'\r",
                           "service_result", len(text))
        elif self.state == "service_result":
            if match := output_line(segment, self.SERVICES_OK):
                self.offset += match.end()
                self.state = "service_prompt"
            elif self.prompt(segment):
                raise RuntimeError("M239 shell or four OpenRC services died after SIGINT")
        elif self.state == "service_prompt":
            if self.prompt(segment):
                # Empty-prompt VEOF must end this shell without killing PID1.
                self._type("\x04", "new_getty", len(text))
        elif self.state == "new_getty":
            if self.getty(segment) is not None:
                self.login_start = self.offset
                self._type("root\r", "new_password", len(text))
        elif self.state == "new_password":
            if "Password:" in segment:
                self._type(self.password + "\r", "new_shell", len(text))
        elif self.state == "new_shell":
            if self.prompt(segment):
                # Do not accept a shell prompt without a SECOND genuine
                # upstream /bin/login exec after the Ctrl-D getty respawn.
                if "[EXEC] replaced process with lwext4 ELF: /bin/login" not in \
                        text[self.login_start:]:
                    raise RuntimeError("M239 second getty bypassed original login")
                self._type("alive=1; [ \"$(id -u)\" = 0 ] || alive=0; "
                           "for svc in lighttpd dnsmasq mosquitto memcached; "
                           "do rc-service \"$svc\" status >/dev/null 2>&1 || alive=0; "
                           "done; [ \"$alive\" = 1 ] && sync && "
                           "printf 'M239_TTY_GETTY_RELOGIN_FOUR_OPENRC_PASS\\n'\r",
                           "final_result", len(text))
        elif self.state == "final_result":
            if match := output_line(segment, self.RELOGIN_OK):
                self.offset += match.end()
                self.state = "final_prompt"
            elif self.prompt(segment):
                raise RuntimeError("M239 second login or OpenRC services failed")
        elif self.state == "final_prompt":
            if self.prompt(segment):
                self.state = "done"
                return True
        return False
