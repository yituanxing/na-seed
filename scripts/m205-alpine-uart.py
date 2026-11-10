#!/usr/bin/env python3
"""M197's original public Alpine UART path plus an honest M205 ELF cohort."""

import os
from pathlib import Path
import sys

# M204 already imports and uses the proven M197/M194 UART supervisor. Keep
# that single owner of serial prompts, login and live guest command injection.
from importlib.util import module_from_spec, spec_from_file_location

source = Path(__file__).with_name("m204-alpine-uart.py")
spec = spec_from_file_location("m205_m204_uart", source)
if spec is None or spec.loader is None:
    raise RuntimeError("M205 requires the proven M204 UART driver")
m204 = module_from_spec(spec)
spec.loader.exec_module(m204)

COMMANDS = m204.M197.build_commands(cache_index=True) + (
    ("sh /root/m205-software-workload.sh --first; "
     "printf 'M205_COHORT_SCRIPT_RC=%d\\n' \"$?\"\r",
     "M205_COHORT_SCRIPT_RC=0",
     "original signed Alpine M205 entrypoints and real functional outcomes"),
    ("test -s /root/m205-first-results.tsv && sync; "
     "printf 'M205_DISCOVERY_SYNC_RC=%d\\n' \"$?\"\r",
     "M205_DISCOVERY_SYNC_RC=0",
     "durable M205 first-boot ledger, installation and function evidence"),
)


def main() -> None:
    if len(sys.argv) != 5:
        raise SystemExit("usage: m205-alpine-uart.py RUNNER SERIAL RUNNER_LOG FIFO")
    seconds = int(os.environ.get("NAS_M205_PUBLIC_SECONDS", "4400"))
    m204.M197.M194.run(
        sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]), Path(sys.argv[4]),
        seconds + 35, commands=COMMANDS, stage="M205",
        success_message="[M205 ALPINE DISCOVERY RECORDED] signed original APKs "
                        "plus per-ELF real outcomes; individual failures "
                        "remain failures")


if __name__ == "__main__":
    main()
