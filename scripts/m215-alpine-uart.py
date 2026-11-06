#!/usr/bin/env python3
"""Five original Alpine probe UART commands via unchanged M197/M194 runner."""
from importlib.util import module_from_spec, spec_from_file_location
import os
from pathlib import Path
import sys

source = Path(__file__).with_name("m205-alpine-uart.py")
spec = spec_from_file_location("m215_m205_uart", source)
if spec is None or spec.loader is None:
    raise RuntimeError("M215 requires the proven original-Alpine M205 UART")
owner = module_from_spec(spec)
spec.loader.exec_module(owner)

COMMANDS = owner.m204.M197.build_commands(cache_index=True) + (
    ("NAS_M215_PILOT=1 sh /root/m205-software-workload.sh --first; "
     "printf 'M215_COHORT_SCRIPT_RC=%d\\n' \"$?\"\r",
     "M215_COHORT_SCRIPT_RC=0",
     "five independently probed original signed Alpine ELFs"),
    ("test -s /root/m215-first-results.tsv && sync; "
     "printf 'M215_DISCOVERY_SYNC_RC=%d\\n' \"$?\"\r",
     "M215_DISCOVERY_SYNC_RC=0",
     "durable five-ELF first-boot signed APK and functionality ledger"),
)


def main() -> None:
    if len(sys.argv) != 5:
        raise SystemExit("usage: m215-alpine-uart.py RUNNER SERIAL RUNNER_LOG FIFO")
    seconds = int(os.environ.get("NAS_M215_PUBLIC_SECONDS", "2400"))
    owner.m204.M197.M194.run(
        sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]), Path(sys.argv[4]),
        seconds + 35, commands=COMMANDS, stage="M215",
        success_message="[M215 ALPINE FIVE-ELF PILOT RECORDED] outcomes counted "
                        "only by independently validated signed/first/reopen oracle")


if __name__ == "__main__":
    main()
