#!/usr/bin/env python3
"""Original Alpine utmpdump first-QEMU via existing native M205 UART owner."""
from importlib.util import module_from_spec, spec_from_file_location
import os
from pathlib import Path
import sys

source = Path(__file__).with_name("m205-alpine-uart.py")
spec = spec_from_file_location("m219_m205_uart", source)
if spec is None or spec.loader is None:
    raise RuntimeError("M219 requires the existing M205 UART owner")
owner = module_from_spec(spec)
spec.loader.exec_module(owner)

COMMANDS = owner.m204.M197.build_commands(cache_index=True) + (
    ("NAS_M219_PILOT=1 sh /root/m205-software-workload.sh --first; "
     "printf 'M219_COHORT_SCRIPT_RC=%d\\n' \"$?\"\r",
     "M219_COHORT_SCRIPT_RC=0",
     "genuine signed utmpdump against native deterministic binary records"),
    ("test -s /root/m219-first-results.tsv && sync; "
     "printf 'M219_DISCOVERY_SYNC_RC=%d\\n' \"$?\"\r",
     "M219_DISCOVERY_SYNC_RC=0",
     "durable original utmpdump signed APK and functional ledger"),
)


def main() -> None:
    if len(sys.argv) != 5:
        raise SystemExit("usage: m219-alpine-uart.py RUNNER SERIAL RUNNER_LOG FIFO")
    seconds = int(os.environ.get("NAS_M219_PUBLIC_SECONDS", "2400"))
    owner.m204.M197.M194.run(
        sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]), Path(sys.argv[4]),
        seconds + 35, commands=COMMANDS, stage="M219",
        success_message="[M219 ALPINE UTMPDUMP RECORDED] only native original "
                        "signed first/reopen and physical ext4 can be accepted")


if __name__ == "__main__":
    main()
