#!/usr/bin/env python3
"""Run M206 through M197's proven real-Alpine UART supervisor."""

from importlib.util import module_from_spec, spec_from_file_location
import os
from pathlib import Path
import sys


source = Path(__file__).with_name("m205-alpine-uart.py")
spec = spec_from_file_location("m206_m205_uart", source)
if spec is None or spec.loader is None:
    raise RuntimeError("M206 requires the existing original-Alpine M205 UART path")
m205 = module_from_spec(spec)
spec.loader.exec_module(m205)

# The original four M197 programs still come from HTTPS and run first; only
# the new offline-cache signed-APK cohort gets an additional durable ledger.
COMMANDS = m205.m204.M197.build_commands(cache_index=True) + (
    ("NAS_M206_COMPAT_COHORT=1 sh /root/m205-software-workload.sh --first; "
     "printf 'M206_COHORT_SCRIPT_RC=%d\\n' \"$?\"\r",
     "M206_COHORT_SCRIPT_RC=0",
     "20 new original signed Alpine ELFs with real network/storage/markup probes"),
    ("test -s /root/m206-first-results.tsv && sync; "
     "printf 'M206_DISCOVERY_SYNC_RC=%d\\n' \"$?\"\r",
     "M206_DISCOVERY_SYNC_RC=0",
     "durable original M206 installed APK and first-boot function ledger"),
)


def main() -> None:
    if len(sys.argv) != 5:
        raise SystemExit("usage: m206-alpine-uart.py RUNNER SERIAL RUNNER_LOG FIFO")
    seconds = int(os.environ.get("NAS_M206_PUBLIC_SECONDS", "4400"))
    m205.m204.M197.M194.run(
        sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]), Path(sys.argv[4]),
        seconds + 35, commands=COMMANDS, stage="M206",
        success_message="[M206 ALPINE DISCOVERY RECORDED] 20 original signed "
                        "ELF semantics with honest individual failures")


if __name__ == "__main__":
    main()
