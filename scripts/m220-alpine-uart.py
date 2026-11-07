#!/usr/bin/env python3
"""New original Nping first-QEMU commands through existing M197/M205 UART."""
from importlib.util import module_from_spec, spec_from_file_location
import os
from pathlib import Path
import sys

source = Path(__file__).with_name("m205-alpine-uart.py")
spec = spec_from_file_location("m220_m205_uart", source)
if spec is None or spec.loader is None:
    raise RuntimeError("M220 requires unchanged M205 UART owner")
owner = module_from_spec(spec)
spec.loader.exec_module(owner)

COMMANDS = owner.m204.M197.build_commands(cache_index=True) + (
    ("NAS_M220_PILOT=1 sh /root/m205-software-workload.sh --first; "
     "printf 'M220_COHORT_SCRIPT_RC=%d\\n' \"$?\"\r",
     "M220_COHORT_SCRIPT_RC=0", "signed original Nping CLI-only first boot"),
    ("test -s /root/m220-first-results.tsv && sync; "
     "printf 'M220_DISCOVERY_SYNC_RC=%d\\n' \"$?\"\r",
     "M220_DISCOVERY_SYNC_RC=0", "durable original Nping candidate ledger"),
)


def main() -> None:
    if len(sys.argv) != 5:
        raise SystemExit("usage: m220-alpine-uart.py RUNNER SERIAL RUNNER_LOG FIFO")
    seconds = int(os.environ.get("NAS_M220_PUBLIC_SECONDS", "2400"))
    owner.m204.M197.M194.run(
        sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]), Path(sys.argv[4]),
        seconds + 35, commands=COMMANDS, stage="M220",
        success_message="[M220 ALPINE NPING CLI RECORDED] only host UDP+PCAP "
                        "can establish genuine first+reopen wire delivery")


if __name__ == "__main__":
    main()
