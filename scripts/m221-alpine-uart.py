#!/usr/bin/env python3
"""Original signed bind-tools first native QEMU via existing M205 UART owner."""
from importlib.util import module_from_spec, spec_from_file_location
import os
from pathlib import Path
import sys

source = Path(__file__).with_name("m205-alpine-uart.py")
spec = spec_from_file_location("m221_m205_uart", source)
if spec is None or spec.loader is None:
    raise RuntimeError("M221 requires unchanged M205 signed first-QEMU UART")
owner = module_from_spec(spec)
spec.loader.exec_module(owner)

COMMANDS = owner.m204.M197.build_commands(cache_index=True) + (
    ("NAS_M221_PILOT=1 sh /root/m205-software-workload.sh --first; "
     "printf 'M221_COHORT_SCRIPT_RC=%d\\n' \"$?\"\r",
     "M221_COHORT_SCRIPT_RC=0", "signed original nsupdate then host authoritative DNS"),
    ("test -s /root/m221-first-results.tsv && sync; "
     "printf 'M221_DISCOVERY_SYNC_RC=%d\\n' \"$?\"\r",
     "M221_DISCOVERY_SYNC_RC=0", "durable original bind-tools two-ELF ledger"),
)


def main() -> None:
    if len(sys.argv) != 5:
        raise SystemExit("usage: m221-alpine-uart.py RUNNER SERIAL RUNNER_LOG FIFO")
    seconds = int(os.environ.get("NAS_M221_PUBLIC_SECONDS", "2400"))
    owner.m204.M197.M194.run(
        sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]), Path(sys.argv[4]),
        seconds + 35, commands=COMMANDS, stage="M221",
        success_message="[M221 ALPINE DNS CLI RECORDED] host authoritative "
                        "UPDATE/ACK/QUERY/ANSWER and first+reopen PCAP still mandatory")


if __name__ == "__main__":
    main()
