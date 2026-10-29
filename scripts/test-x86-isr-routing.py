#!/usr/bin/env python3
"""Inspect assembled production entry.S, including hardware error-code frames.

This verifies real ISR targets and push counts, not guest signal delivery.
"""
import re
import subprocess
import sys

assert len(sys.argv) == 2, "usage: test-x86-isr-routing.py entry.o"
disassembly = subprocess.check_output(["objdump", "-dr", sys.argv[1]], text=True)


def instructions(vector):
    match = re.search(
        rf"^[0-9a-f]+ <isr{vector}>:\n(.*?)(?=^[0-9a-f]+ <|\Z)",
        disassembly,
        re.MULTILINE | re.DOTALL,
    )
    assert match, f"missing assembled isr{vector}"
    return match.group(1)


# Same trap entry used for ptrace #DB/#BP and #GP/#PF; additions must keep
# the ABI's two metadata words: vector plus error code (CPU or dummy zero).
with_cpu_error = {10, 11, 12, 13, 14, 17}
user_sync = {0, 4, 5, 6, 10, 11, 12, 16, 17, 19}
debug_or_memory = {1, 3, 13, 14}
for vector in sorted(user_sync | debug_or_memory):
    code = instructions(vector)
    pushes = re.findall(r"\bpush\b", code)
    expected = 1 if vector in with_cpu_error else 2
    assert len(pushes) == expected, (
        f"isr{vector} expected {expected} software pushes, got {len(pushes)}"
    )
    assert re.search(r"\bjmp\s+.*<nas_x86_traced_exception_entry>", code), (
        f"isr{vector} bypasses live user/kernel trap policy"
    )


# Privileged or hardware-fatal exceptions must NOT be rerouted to user
# signals. In particular: NMI, double fault and machine check.
for vector in (2, 7, 8, 9, 15, 18, 20, 21, 28, 29, 30, 31):
    code = instructions(vector)
    assert re.search(r"\bjmp\s+.*<exception_common>", code), (
        f"isr{vector} unexpectedly bypasses hardware-fatal reporter"
    )

print("[X86 ISR PASS] assembled 0..31 user sync routing, CPU error frames, fatal exceptions")
