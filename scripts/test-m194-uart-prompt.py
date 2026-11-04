#!/usr/bin/env python3
"""Focused host-only checks for M194/M195/M196 real Alpine UART prompt gates.

The AArch64 fixture is copied from M196 run 41867701864: a real ash prompt,
VT100 cursor request and asynchronous lwIP printk shared one UART line.
This tests only the host acceptance parser, not a guest boot or network PASS.
"""

import importlib.util
from pathlib import Path


spec = importlib.util.spec_from_file_location(
    "naseed_m194_prompt", Path(__file__).with_name("m194-alpine-uart.py"))
assert spec is not None and spec.loader is not None
m194 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m194)


def result_boundary(text: str, marker: str) -> str:
    found = m194.step_match(text, marker)
    assert found is not None, (marker, text)
    return text[found.end():]


# Actual M196 ARM64 serial tail at the failure (state=await_prompt step=1):
# the next prompt is already present even though the *last* line is a kernel
# DHCP status message, not a prompt. The old last-line-only check timed out.
arm64 = (
    "naseed:~# \x1b[6ntest -s /etc/ssl/cert.pem && grep -q 'nameserver 10.0.2.3' "
    "/etc/resolv\n.conf; printf 'M196_TRUST_DNS_RC=%d\\n' \"$?\"\n"
    "M196_TRUST_DNS_RC=0\n"
    "naseed:~# \x1b[6n[LWIP NETIF STATUS] up=1 link=1 dhcp=1 ip=10.0.2.15\n"
    "[LWIP DHCP BOUND] addr=10.0.2.15\n"
)
after_arm_marker = result_boundary(arm64, "M196_TRUST_DNS_RC=0")
assert m194.shell_prompt_after_step(after_arm_marker)
assert not m194.m193.shell_prompt(after_arm_marker)  # Reproduces the old race.

# Existing x86/RV64 behavior: the prompt can arrive in a later UART poll,
# or include the upstream ash VT100 query / ordinary color-reset sequences.
assert not m194.shell_prompt_after_step("\n")
assert m194.shell_prompt_after_step("naseed:~# ")
assert m194.shell_prompt_after_step("naseed:~# \x1b[6n")
assert m194.shell_prompt_after_step("\x1b[32mnaseed:/root# \x1b[0m")
assert m194.shell_prompt_after_step("root@host:~# ")
assert m194.shell_prompt_after_step("naseed:~# [LWIP DHCP BOUND] addr=10.0.2.15\n")

# A prompt from BEFORE the command's completed marker cannot advance the
# current step. A command echo and kernel message containing prompt text
# cannot masquerade as fresh proof after step_end.
earlier_prompt = "naseed:~# \x1b[6n\nM196_TRUST_DNS_RC=0\n"
assert not m194.shell_prompt_after_step(
    result_boundary(earlier_prompt, "M196_TRUST_DNS_RC=0"))
for fake in (
    "naseed:~# printf 'M196_TRUST_DNS_RC=0\\n'\n",
    "naseed:~# apk --no-cache add tree jq\n",
    "[LWIP NETIF STATUS] up=1 naseed:~# \n",
    "kernel: guest printed naseed:~# \n",
    "guest says naseed:~# \x1b[6n[LWIP DHCP BOUND]\n",
    "naseed:~# printf 'not a completed prompt'\x1b[6n\n",
    "naseed:~# [shell echo is not a kernel printk]\n",
):
    assert not m194.shell_prompt_after_step(fake), repr(fake)

# Never let the command's echoed printf argument stand in for the genuine
# output-line marker, irrespective of whether it contains the expected text.
assert m194.step_match("naseed:~# printf 'M196_TRUST_DNS_RC=0\\n'\n",
                       "M196_TRUST_DNS_RC=0") is None

print("[M194 UART PROMPT CONTRACT PASS] actual ARM64 DSR+printk race, "
      "x86/RV prompts, step boundary, echoed commands and spoofed logs")
