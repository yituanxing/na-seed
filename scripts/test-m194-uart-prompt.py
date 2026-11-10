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

# Actual three-ISA M203 graphical VT boot: the kernel's one-time VirtIO-GPU
# diagnostic interrupts the serial ash prompt just after its CSI 6n query.
# Rejecting this known printk used to leave the serial witness stuck in
# await_prompt although the *physical* tty1 shell had already written ext4.
gpu_m203 = (
    "M203_TTY1_GETTY_RC=0\n"
    "naseed:~# \x1b[6n[M177 VIRTIO GPU 2D STATS] scanout=0 "
    "backing_pages=469 submitted=14 completed=14\n"
    "[M177 VIRTIO GPU 2D PASS]\n"
)
assert m194.shell_prompt_after_step(
    result_boundary(gpu_m203, "M203_TTY1_GETTY_RC=0"))
assert not m194.shell_prompt_after_step(
    "naseed:~# [M177 VIRTIO GPU 2D FAKE] arbitrary user text\n")

# Exact M199 ARM64 QEMU UART suffix: the prompt is real, but a kernel
# unsupported-syscall diagnostic is printed immediately after ESC[6n.
# Previously the driver waited for another prompt forever and never sent
# the next OpenRC GUI log probe, even though the service had already started.
arm64_m199 = (
    "M199_FIRST_SERVICE_START_RC=0\n"
    "localhost:~# \x1b[6n[MISS] syscall nr=153\n"
    "[MISS] syscall nr=153\n"
)
assert m194.shell_prompt_after_step(
    result_boundary(arm64_m199, "M199_FIRST_SERVICE_START_RC=0"))

# Exact M244 RV64/ARM64 first-epoch UART after the independent host SSH PTY
# passed: the kernel's pipe-creation trace raced against ash's prompt. The
# previous parser rejected this legitimate line, so the UART owner never sent
# its next command and never asked the same QEMU process to reboot.
m244_ssh_pipe = (
    "M244_FIRST_HOST_SSH_READY\n"
    "localhost:~# \x1b[6n[PIPE] created read=6 write=7\n"
    "[PROC] fork parent=615 child=645\n"
)
assert m194.shell_prompt_after_step(
    result_boundary(m244_ssh_pipe, "M244_FIRST_HOST_SSH_READY"))
assert not m194.shell_prompt_after_step(
    "localhost:~# [PIPE-FAKE] arbitrary command output\n")

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
    "naseed:~# [MISS] arbitrary user text\n",
    "naseed:~# [MISS] syscall nr=not-a-number\n",
    "naseed:~# [MISS] syscall nr=153 forged suffix\n",
):
    assert not m194.shell_prompt_after_step(fake), repr(fake)

# Never let the command's echoed printf argument stand in for the genuine
# output-line marker, irrespective of whether it contains the expected text.
assert m194.step_match("naseed:~# printf 'M196_TRUST_DNS_RC=0\\n'\n",
                       "M196_TRUST_DNS_RC=0") is None

print("[M194 UART PROMPT CONTRACT PASS] actual ARM64 DSR+printk race, "
      "M244 real SSH pipe/ash collision, x86/RV prompts, "
      "step boundary, echoed commands and spoofed logs")
