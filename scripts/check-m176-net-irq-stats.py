#!/usr/bin/env python3
"""Strict M176 *guest-produced* RX IRQ stats parser, with negative selftests."""
import re
import sys
from pathlib import Path

PREFIX = "[M176 NET RX IRQ STATS] "
VERDICT = "[M176 NET RX IRQ ONLY PASS]"
KEYS = ("nic_irq", "nic_proven", "irq_ack", "irq_wakes", "rx_packets",
        "deferred_drains", "fallback_poll")


def parse_guest_log(log: str) -> dict[str, int]:
    lines = log.splitlines()
    stats = [line for line in lines if line.startswith(PREFIX)]
    if len(stats) != 1 or lines.count(VERDICT) != 1:
        raise ValueError("exactly one complete native IRQ stats and PASS required")
    raw = stats[0][len(PREFIX):].split()
    if len(raw) != len(KEYS):
        raise ValueError("missing, repeated or extra NIC counters")
    numbers: dict[str, int] = {}
    for item in raw:
        match = re.fullmatch(r"([a-z_]+)=([0-9]+)", item)
        if not match or match.group(1) not in KEYS:
            raise ValueError("malformed device counter")
        key = match.group(1)
        if key in numbers:
            raise ValueError("repeated device counter")
        numbers[key] = int(match.group(2))
    if set(numbers) != set(KEYS) or numbers["nic_irq"] != 2 or \
       numbers["nic_proven"] != 2 or \
       numbers["irq_ack"] < 1 or numbers["irq_wakes"] < 1 or \
       numbers["rx_packets"] < 1 or numbers["deferred_drains"] < 1 or \
       numbers["fallback_poll"] != 0:
        raise ValueError("native hardware/deferred/fallback counters invalid")
    return numbers


def selftest() -> None:
    good = (PREFIX + "nic_irq=2 nic_proven=2 irq_ack=3 irq_wakes=2 rx_packets=5 "
            "deferred_drains=2 fallback_poll=0\n" + VERDICT + "\n")
    assert parse_guest_log(good)["rx_packets"] == 5
    corrupt = (
        good.replace("nic_irq=2", "nic_irq=1"),
        good.replace("nic_proven=2", "nic_proven=1"),
        good.replace("nic_proven=2", "nic_proven=0"),
        good.replace("irq_ack=3", "irq_ack=0"),
        good.replace("irq_wakes=2", "irq_wakes=0"),
        good.replace("rx_packets=5", "rx_packets=0"),
        good.replace("deferred_drains=2", "deferred_drains=0"),
        good.replace("fallback_poll=0", "fallback_poll=1"),
        good.replace("fallback_poll=0", "fallback_poll=foo"),
        good.replace("nic_irq=2 ", ""),
        good.replace("fallback_poll=0", "irq_ack=1 fallback_poll=0"),
        good + good,
        good.replace(VERDICT, "[M176 NET RX IRQ ONLY FAKE]"),
        "",
    )
    for index, case in enumerate(corrupt):
        try:
            parse_guest_log(case)
        except ValueError:
            continue
        raise AssertionError(f"M176 parser accepted fraudulent case {index}")
    print("[M176 HOST STATS PARSER PASS] IRQ and RX numbers fail closed")


if __name__ == "__main__":
    if sys.argv[1:] == ["--selftest"]:
        selftest()
    elif len(sys.argv) == 2:
        try:
            values = parse_guest_log(Path(sys.argv[1]).read_text(errors="replace"))
        except (ValueError, OSError) as exc:
            raise SystemExit(f"[M176 NET RX IRQ FAIL] {exc}") from exc
        print("[M176 GUEST IRQ STATS VERIFIED] " +
              " ".join(f"{key}={values[key]}" for key in KEYS))
    else:
        raise SystemExit("usage: check-m176-net-irq-stats.py [--selftest | guest.log]")
