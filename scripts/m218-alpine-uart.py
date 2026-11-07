#!/usr/bin/env python3
"""Original Alpine ifstat first-QEMU commands via existing M205 UART owner."""
from importlib.util import module_from_spec, spec_from_file_location
import os
from pathlib import Path
import sys

source = Path(__file__).with_name("m205-alpine-uart.py")
spec = spec_from_file_location("m218_m205_uart", source)
if spec is None or spec.loader is None:
    raise RuntimeError("M218 requires the existing M205 UART owner")
owner = module_from_spec(spec)
spec.loader.exec_module(owner)

COMMANDS = owner.m204.M197.build_commands(cache_index=True) + (
    ("NAS_M218_PILOT=1 sh /root/m205-software-workload.sh --first; "
     "printf 'M218_COHORT_SCRIPT_RC=%d\\n' \"$?\"\r",
     "M218_COHORT_SCRIPT_RC=0",
     "genuine ifstat over changing driver-owned NIC counters"),
    ("test -s /root/m218-first-results.tsv && sync; "
     "printf 'M218_DISCOVERY_SYNC_RC=%d\\n' \"$?\"\r",
     "M218_DISCOVERY_SYNC_RC=0",
     "durable original ifstat signed APK and functional ledger"),
)


def main() -> None:
    if len(sys.argv) != 5:
        raise SystemExit("usage: m218-alpine-uart.py RUNNER SERIAL RUNNER_LOG FIFO")
    seconds = int(os.environ.get("NAS_M218_PUBLIC_SECONDS", "2400"))
    owner.m204.M197.M194.run(
        sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]), Path(sys.argv[4]),
        seconds + 35, commands=COMMANDS, stage="M218",
        success_message="[M218 ALPINE IFSTAT RECORDED] outcomes counted only "
                        "by independently validated signed/first/reopen oracle")


if __name__ == "__main__":
    main()
