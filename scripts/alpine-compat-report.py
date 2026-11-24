#!/usr/bin/env python3
"""One strict first/reopen guest-ledger frontend; wire, disk and APK proof remain independent."""
import argparse
from importlib.util import module_from_spec, spec_from_file_location
import json
from pathlib import Path

# Exact historical counts, output labels and independent evidence boundaries.
COUNTS = {"m206": 20, "m215": 5, "m218": 1, "m219": 1, "m220": 1,
          "m221": 2, "m222": 3, "m223": 3, "m224": 3, "m225": 2,
          "m226": 1, "m235": 12, "m236": 7, "m237": 15, "m238": 15}
PILOTS = {"m215": "ELFs", "m218": "ifstat", "m219": "utmpdump"}
BATCHES = {"m235", "m236", "m237", "m238"}
WIRE_ACCEPTANCE = {
    "m220": "cli-ledger-only-host-wire-gate-required",
    "m221": "cli-ledger-only-authoritative-host-and-pcap-required",
    "m222": "cli-ledger-only-real-host-sshd-and-two-qemu-pcaps-required",
    "m223": "cli-ledger-only-independent-real-host-http-pcap-and-physical-disk-sha-required",
    "m224": "cli-ledger-only-independent-real-host-tcp-pcap-and-physical-ext4-signed-sha-required",
    "m225": "guest-ftp-ledger-only-real-host-pcap-ext4-signed-sha-required",
    "m226": "guest-restic-ledger-only-external-real-REST-PCAP-ext4-required",
}
WIRE_SUMMARY = {
    "m220": "first+reopen; actual UDP requires both independent wire oracles",
    "m221": "signed original nsupdate+host first+reopen; independent DNS wire mandatory",
    "m222": "original signed SSH/SCP/SFTP first+reopen; independent authenticated OpenSSH host/wire still mandatory",
    "m223": "original signed wget/aria2c/lighttpd first+reopen; independent host real HTTP/PCAP/disk still mandatory",
    "m224": "original signed nmap/iperf3/ncat first+reopen; real host/PCAP/ext4 still mandatory",
    "m225": "signed lftp/rclone, first+reopen; host TCP/PCAP/ext4 not inferred",
    "m226": "M137 historical signed restic, first/reopen remote REST semantics (0 new ELF identities)",
}
FAILURE = {
    "m206": "discovery cannot count as full compatibility",
    "m215": "any missing original ELF or preexisting APK is failure",
    "m218": "missing, preinstalled or failed original ifstat",
    "m219": "missing, preinstalled or failed original utmpdump",
    "m220": "missing original signed ELF or incomplete guest ledger",
    "m221": "original signed ELF or guest ledger incomplete",
    "m235": "real guest functional evidence incomplete",
    **dict.fromkeys(("m236", "m237", "m238"), "at least one true native functional entry missing"),
}


def load_owner(profile):
    """Each profile gets an isolated parser module, not mutable shared globals."""
    count = COUNTS[profile]
    label = profile.upper()
    spec = spec_from_file_location(f"{profile}_m205_report", Path(__file__).with_name("m205-compat-report.py"))
    if spec is None or spec.loader is None:
        raise RuntimeError(f"{label} requires the existing M205 evidence parser")
    owner = module_from_spec(spec)
    spec.loader.exec_module(owner)
    owner.EXPECTED_COUNT = count
    owner.RESULT, owner.ERROR = f"{label}_RESULT\t", f"{label}_ERROR\t"
    owner.MARKERS = {"first": f"{label}_FIRST_DISCOVERY_COMPLETE",
                     "reopen": f"{label}_REOPEN_DISCOVERY_COMPLETE"}
    return owner


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile", choices=COUNTS)
    for name in ("manifest", "first_uart", "reopen_uart", "json_output"):
        parser.add_argument(name, type=Path)
    parser.add_argument("--require-all", action="store_true")
    args = parser.parse_args()
    profile, label = args.profile, args.profile.upper()
    count = COUNTS[profile]
    owner = load_owner(profile)
    try:
        report = owner.build_report(owner.manifest_cases(args.manifest),
                                    args.first_uart.read_text(errors="replace"),
                                    args.reopen_uart.read_text(errors="replace"))
        report["milestone"] = label
        if profile == "m226":
            report["new_original_elf_count"] = 0  # Already proved under M137.
        passed, preexisting = report["newly_installed_full_pass_entrypoints"], report["preexisting"]
        complete = passed == count and preexisting == 0
        total = report["total_entrypoints"]
        acceptance = "discovery-not-full-compatibility"
        if complete and (args.require_all or profile in ("m225", "m226")):
            if profile in WIRE_ACCEPTANCE:
                acceptance = WIRE_ACCEPTANCE[profile]
            elif profile in BATCHES:
                acceptance = f"strict-guest-first-and-reopen-{count}-of-{count}"
            elif profile != "m206" or total == count:
                acceptance = f"strict-native-first-and-reopen-{count}-of-{count}"
        report["acceptance"] = acceptance
        args.json_output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    except (OSError, ValueError) as exc:
        error = {"m225": "GUEST LEDGER FAIL", "m226": "SIGNED REST GUEST LEDGER FAIL"}.get(
            profile, "INVALID GUEST LEDGER" if profile in WIRE_ACCEPTANCE else "INVALID EVIDENCE")
        parser.error(f"[{label} {error}] {exc}")
    if profile == "m206":
        print(f"[M206 DISCOVERY REPORT] {passed}/{total} original signed Alpine programs passed fresh install, real function and independent second QEMU; preexisting={preexisting}")
    elif profile in PILOTS:
        print(f"[{label} PILOT REPORT] {passed}/{count} original signed Alpine {PILOTS[profile]} first+independent reopen; preexisting={preexisting}")
    elif profile in BATCHES:
        description = "original signed Alpine ELF semantics first+reopen" if profile == "m235" else "original signed ELF first/reopen"
        print(f"[{label} REPORT] {passed}/{count} {description}; preexisting={preexisting}")
    else:
        kind = {"m225": "FTP GUEST LEDGER", "m226": "REST GUEST LEDGER"}.get(profile, "GUEST CLI LEDGER")
        print(f"[{label} {kind}] {passed}/{count} {WIRE_SUMMARY[profile]}")
    if profile not in WIRE_ACCEPTANCE:
        for item in report["software"]:
            print(f"[{label} ENTRY] {item['elf_path']} install={item['install']} functional={item['functional']} reopen={item['reopen']}")
    if profile == "m206":
        print(f"[M206 REPORT JSON] {args.json_output}")
    if args.require_all and (not complete or (profile in BATCHES and total != count)):
        if profile in FAILURE:
            print(f"[{label} STRICT FAIL] {FAILURE[profile]}")
        return 1
    if args.require_all:
        if profile == "m206":
            print("[M206 STRICT GUEST PASS] 20/20 original signed Alpine software functional in first and independent second QEMU boot")
        elif profile == "m235":
            print("[M235 STRICT GUEST PASS] 12/12 original Alpine ELF, actual install/function/reopen")
        elif profile in BATCHES:
            print(f"[{label} STRICT GUEST PASS] {count}/{count} original APK ELF, native first/reopen semantics")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
