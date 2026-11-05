#!/usr/bin/env python3
"""M197's unchanged live original-Alpine path plus M204 package discovery."""

import importlib.util
import os
from pathlib import Path
import sys


def load_m197():
    path = Path(__file__).with_name("m197-alpine-uart.py")
    spec = importlib.util.spec_from_file_location("m204_m197_uart", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("M204 requires the proven M197 UART driver")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


M197 = load_m197()
COMMANDS = M197.build_commands(cache_index=True) + (
    ("sh /root/m204-software-workload.sh --first; "
     "printf 'M204_COHORT_SCRIPT_RC=%d\\n' \"$?\"\r",
     "M204_COHORT_SCRIPT_RC=0",
     "all manifest entries measured with real public APK and application work"),
    ("test -s /root/m204-first-results.tsv && sync; "
     "printf 'M204_DISCOVERY_SYNC_RC=%d\\n' \"$?\"\r",
     "M204_DISCOVERY_SYNC_RC=0",
     "durable first-boot result ledger and installed original ELF files"),
)


def main():
    if len(sys.argv) != 5:
        raise SystemExit("usage: m204-alpine-uart.py RUNNER SERIAL RUNNER_LOG FIFO")
    seconds = int(os.environ.get("NAS_M204_PUBLIC_SECONDS", "1800"))
    M197.M194.run(
        sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]), Path(sys.argv[4]),
        seconds + 35, commands=COMMANDS, stage="M204",
        success_message="[M204 ALPINE DISCOVERY RECORDED] official APK cohort "
                        "+ individual functional outcomes on ext4; "
                        "a package failure is NOT a package PASS")


if __name__ == "__main__":
    main()
