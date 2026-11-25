#!/usr/bin/env python3
"""First native Alpine boot serial command profiles; independent wire/disk oracles remain mandatory."""
from importlib.util import module_from_spec, spec_from_file_location
import os
from pathlib import Path
import sys

# timeout, guest command, command description, ledger description, observation message
PROFILES = {'m204': (1800,
          'sh /root/m204-software-workload.sh --first',
          'all manifest entries measured with real public APK and application work',
          'durable first-boot result ledger and installed original ELF files',
          '[M204 ALPINE DISCOVERY RECORDED] official APK cohort '
          '+ individual functional outcomes on ext4; '
          'a package failure is NOT a package PASS'),
 'm205': (4400,
          'sh /root/m205-software-workload.sh --first',
          'original signed Alpine M205 entrypoints and real functional outcomes',
          'durable M205 first-boot ledger, installation and function evidence',
          '[M205 ALPINE DISCOVERY RECORDED] signed original APKs '
          'plus per-ELF real outcomes; individual failures remain failures'),
 'm206': (4400,
          'NAS_M206_COMPAT_COHORT=1 sh /root/m205-software-workload.sh --first',
          '20 new original signed Alpine ELFs with real network/storage/markup probes',
          'durable original M206 installed APK and first-boot function ledger',
          '[M206 ALPINE DISCOVERY RECORDED] 20 original signed '
          'ELF semantics with honest individual failures'),
 'm215': (2400,
          'NAS_M215_PILOT=1 sh /root/m205-software-workload.sh --first',
          'five independently probed original signed Alpine ELFs',
          'durable five-ELF first-boot signed APK and functionality ledger',
          '[M215 ALPINE FIVE-ELF PILOT RECORDED] outcomes counted only by independently validated '
          'signed/first/reopen oracle'),
 'm218': (2400,
          'NAS_M218_PILOT=1 sh /root/m205-software-workload.sh --first',
          'genuine ifstat over changing driver-owned NIC counters',
          'durable original ifstat signed APK and functional ledger',
          '[M218 ALPINE IFSTAT RECORDED] outcomes counted only by independently validated signed/first/reopen '
          'oracle'),
 'm219': (2400,
          'NAS_M219_PILOT=1 sh /root/m205-software-workload.sh --first',
          'genuine signed utmpdump against native deterministic binary records',
          'durable original utmpdump signed APK and functional ledger',
          '[M219 ALPINE UTMPDUMP RECORDED] only native original signed first/reopen and physical ext4 can be '
          'accepted'),
 'm220': (2400,
          'NAS_M220_PILOT=1 sh /root/m205-software-workload.sh --first',
          'signed original Nping CLI-only first boot',
          'durable original Nping candidate ledger',
          '[M220 ALPINE NPING CLI RECORDED] only host UDP+PCAP can establish genuine first+reopen wire delivery'),
 'm221': (2400,
          'NAS_M221_PILOT=1 sh /root/m205-software-workload.sh --first',
          'signed original nsupdate then host authoritative DNS',
          'durable original bind-tools two-ELF ledger',
          '[M221 ALPINE DNS CLI RECORDED] host authoritative UPDATE/ACK/QUERY/ANSWER and first+reopen PCAP still '
          'mandatory'),
 'm222': (2400,
          'NAS_M222_PILOT=1 sh /root/m205-software-workload.sh --first',
          'three genuine original signed SSH clients',
          'durable original OpenSSH three-ELF ledger',
          '[M222 ALPINE SSH CLI RECORDED] independent SSH host authentication, SCP/SFTP host echoes and two QEMU '
          'PCAPs still mandatory'),
 'm223': (2500,
          'NAS_M223_PILOT=1 sh /root/m205-software-workload.sh --first',
          'three original Alpine HTTP semantic probes',
          'durable original HTTP cohort ledger',
          '[M223 ALPINE HTTP CLI RECORDED] host-random HTTP, two independent PCAPs, physical ext4 oracle still '
          'mandatory'),
 'm224': (2500,
          'NAS_M224_PILOT=1 sh /root/m205-software-workload.sh --first',
          'three genuine Alpine TCP semantic probes',
          'durable original TCP signed ledger',
          '[M224 ALPINE TCP CLI RECORDED] true TCP host/PCAP and physical ext4 original SHA oracle still required'),
 'm225': (2700,
          'NAS_M225_BOOT_PHASE=first sh /root/m225-software-workload.sh --first',
          'two original signed FTP clients',
          'durable native guest FTP ledger',
          '[M225 ORIGINAL FTP CLI RECORDED] genuine host FTP PCAP/physical ext4/signed SHA/two native boots still '
          'required'),
 'm226': (3600,
          'NAS_M226_BOOT_PHASE=first sh /root/m226-software-workload.sh --first',
          'signed restic original against true REST repository',
          'durable original native guest remote restic ledger',
          '[M226 ORIGINAL RESTIC CLI RECORDED] host upstream server, two native QEMUs, PCAP/ext4 and exact APK SHA '
          'pending'),
 'm235': (4400,
          'NAS_M235_COMPAT_COHORT=1 sh /root/m205-software-workload.sh --first',
          '12 original signed Alpine ELF functional probes in one QEMU',
          'durable exact 12 original-Alpine first-boot results',
          '[M235 FIRST GUEST RECORDED] 12 original signed Alpine ELF semantics, no inferred PASS'),
 'm236': (4400,
          'NAS_M236_COMPAT_COHORT=1 sh /root/m205-software-workload.sh --first',
          'seven original signed Alpine ELF functional probes in one QEMU',
          'durable M236 first-QEMU functional result ledger',
          '[M236 FIRST GUEST RECORDED] seven original signed APK ELF semantics, not inferred PASS'),
 'm237': (4400,
          'NAS_M237_COMPAT_COHORT=1 sh /root/m205-software-workload.sh --first',
          'fifteen original signed Alpine ELF functional probes in one QEMU',
          'durable M237 first-QEMU functional result ledger',
          '[M237 FIRST GUEST RECORDED] fifteen original signed APK ELF semantics, not inferred PASS'),
 'm238': (4400,
          'NAS_M238_COMPAT_COHORT=1 sh /root/m205-software-workload.sh --first',
          'fifteen original signed Alpine ELF functional probes in one QEMU',
          'durable M238 first-QEMU functional result ledger',
          '[M238 FIRST GUEST RECORDED] fifteen original signed APK ELF semantics, not inferred PASS')}


def main():
    if len(sys.argv) != 6 or sys.argv[1] not in PROFILES:
        raise SystemExit("usage: alpine-first-uart.py PROFILE RUNNER SERIAL RUNNER_LOG FIFO")
    profile, runner, serial, runner_log, fifo = sys.argv[1:]
    label = profile.upper()
    default, guest, description, ledger, message = PROFILES[profile]
    spec = spec_from_file_location("alpine_first_uart_owner", Path(__file__).with_name("m197-alpine-uart.py"))
    if spec is None or spec.loader is None:
        raise RuntimeError("missing native first-QEMU UART owner")
    owner = module_from_spec(spec)
    spec.loader.exec_module(owner)
    commands = owner.build_commands(cache_index=True) + (
        (guest + f"; printf '{label}_COHORT_SCRIPT_RC=%d\\n' \"$?\"\r",
         f"{label}_COHORT_SCRIPT_RC=0", description),
        (f"test -s /root/{profile}-first-results.tsv && sync; "
         f"printf '{label}_DISCOVERY_SYNC_RC=%d\\n' \"$?\"\r",
         f"{label}_DISCOVERY_SYNC_RC=0", ledger),
    )
    seconds = int(os.environ.get(f"NAS_{label}_PUBLIC_SECONDS", str(default)))
    owner.M194.run(runner, Path(serial), Path(runner_log), Path(fifo),
                             seconds + 35, commands=commands, stage=label, success_message=message)


if __name__ == "__main__":
    main()
