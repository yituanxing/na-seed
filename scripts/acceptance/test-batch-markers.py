#!/usr/bin/env python3
"""Exercise the actual shared marker gate; no QEMU success is inferred."""
from pathlib import Path
from profile_markers import DERIVED, expected_markers
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "scripts/acceptance/check-batch-markers.sh"
subprocess.run(["sh", "-n", CHECKER], check=True)
with tempfile.TemporaryDirectory() as tmp:
    manifest, log = Path(tmp) / "manifest", Path(tmp) / "serial"

    def check(entries, records, expected=2, status=0):
        manifest.write_text("\n".join(entries) + "\n")
        log.write_text("\n".join(records) + "\n")
        p = subprocess.run(["sh", CHECKER, "M999", "amd64", manifest, log,
                            str(expected), "0"], capture_output=True, text=True)
        assert p.returncode == status, (entries, records, p.returncode, p.stderr)

    good = ["M999_ONE_PASS", "M999_TWO_PASS", "M999_BATCH_PASS"]
    check(["# comment", "", *good], good)
    check(good, good[1:], status=1)
    check(good, [*good, good[0]], status=1)
    check(good, [good[-1]], status=1)
    check([*good, good[0]], good, status=2)
    check(["M998_ONE_PASS", *good[1:]], good, status=2)
    check(good[:-1], good, status=1)
    check(good, good, expected=3, status=1)
    check(good, ["prefix " + good[0], *good[1:]], status=1)
    # Every migrated caller retains its own manifest and exact expected count.
    migrated = 0
    for script in sorted((ROOT / "scripts").glob("probe-m*-batch.sh")):
        body = script.read_text()
        if "sh scripts/acceptance/check-batch-markers.sh" not in body:
            continue
        subprocess.run(["sh", "-n", script], check=True)
        m = re.search(r'^sh scripts/acceptance/check-batch-markers\.sh (M\d+) "\$port" (\S+) "\$log" (\d+) (\d+)$', body, re.M)
        assert m, script
        prefix, source, expected, tail = m.groups()
        expected = int(expected)
        path = ROOT / "scripts" / (prefix.lower() + "-expected-markers.txt")
        if source != '"$manifest"':
            assert ROOT / source == path, script
        else:
            assert "manifest=scripts/" + path.name in body, script
        entries = [s for s in path.read_text().splitlines() if s and not s.startswith("#")]
        assert len(entries) == len(set(entries)) == expected + 1, (script, entries)
        assert entries.count(prefix + "_BATCH_PASS") == 1, script
        assert all(s.startswith(prefix + "_") and s.endswith("_PASS") for s in entries), script
        manifest.write_text(path.read_text())
        log.write_text("\n".join(entries) + "\n")
        subprocess.run(["sh", CHECKER, prefix, "amd64", manifest, log,
                        str(expected), "0"], check=True)
        migrated += 1
    # Run the actual shared probe with an isolated fake transport to test
    # wiring and rejection, NOT as evidence of a native guest execution.
    # The same marker gate is exercised against every original manifest.
    probe = ROOT / "scripts/probe-alpine-signed-batch.sh"
    subprocess.run(["sh", "-n", probe], check=True)
    with tempfile.TemporaryDirectory() as work:
        import os
        import shutil
        cwd = Path(work)
        scripts = cwd / "scripts"
        scripts.mkdir()
        (scripts / "acceptance").mkdir()
        shutil.copyfile(CHECKER, scripts / "acceptance/check-batch-markers.sh")
        shutil.copyfile(ROOT / "scripts/acceptance/profile_markers.py", scripts / "acceptance/profile_markers.py")
        shutil.copyfile(ROOT / "scripts/acceptance/batch-manifest.sh", scripts / "acceptance/batch-manifest.sh")
        shutil.copyfile(ROOT / "scripts/native-qemu-target.sh", scripts / "native-qemu-target.sh")
        (scripts / "stage-alpine-signed-batch.sh").write_text(
            '#!/bin/sh\n[ "$#" -eq 2 ] && [ "$1" = "$TEST_PORT" ] && [ "$2" = "$TEST_PROFILE" ]\n')
        fake = r'''#!/bin/sh
set -eu
[ "$BUSYBOX_PARITY_BINARY" = "build/external/$TEST_PROFILE-batch/$TEST_PORT/rootfs/bin/busybox" ]
[ "$NAS_ACCEPTANCE_CPIO" = "build/external/$TEST_PROFILE-batch/$TEST_PORT/rootfs.cpio" ]
[ "$BUSYBOX_PARITY_EXPECTED_COUNT" = 1 ]
[ "$NAS_ACCEPTANCE_QEMU_RAM" = "$TEST_RAM" ]
[ "$NAS_ACCEPTANCE_DISK_MB" = "$TEST_DISK" ]
if [ -n "$TEST_TIMEOUT" ]; then [ "$NAS_APP_BATCH_QEMU_TIMEOUT" = "$TEST_TIMEOUT" ]; fi
[ "$BUSYBOX_PARITY_MANIFEST" = "build/acceptance-manifests/$TEST_PROFILE.tsv" ]
[ -s "$BUSYBOX_PARITY_MANIFEST" ]
if [ "$TEST_PROFILE" = m110 ]; then [ "$NAS_SYSCALL_TRACE" = 1 ]; fi
case "$TEST_PORT" in
    amd64) logs=build/busybox-source-parity-x86 ;;
    riscv64) logs=build/port-riscv64/full/matrix-source ;;
    arm64) logs=build/port-aarch64/full/matrix-source ;;
    *) exit 2 ;;
esac
mkdir -p "$logs"
log="$logs/$TEST_PROFILE-batch.clean.log"
cp fixture.log "$log"
[ "${TEST_FAIL_RUNNER:-0}" = 0 ]
'''
        for name in ("probe-x86-busybox-parity.sh", "probe-riscv64-busybox-matrix.sh",
                     "probe-aarch64-busybox-matrix.sh"):
            (scripts / name).write_text(fake)
        extended = {
            "m146": (24, 600, 1024, False),
            "m148": (35, 660, 1024, True),
            "m150": (26, 650, 1024, False),
            "m151": (35, 900, 1536, True),
            "m153": (36, 700, 1024, False),
            "m154": (30, 700, 1024, True),
            "m155": (30, 800, 1024, True),
            "m157": (30, 700, 1024, True),
            "m158": (30, 700, 1024, True),
            "m159": (30, 900, 1024, True),
            "m163": (33, 900, 1024, True),
            "m165": (30, 900, 1024, True),
        }
        def run_profile(profile, port, records, status=0, fail_runner=False):
            (cwd / "fixture.log").write_text("\n".join(records) + "\n")
            config = extended.get(profile)
            env = dict(os.environ, TEST_PROFILE=profile, TEST_PORT=port,
                       TEST_FAIL_RUNNER=str(int(fail_runner)),
                       TEST_RAM="2048M" if config else "1536M",
                       TEST_DISK=str(config[2]) if config else "768",
                       TEST_TIMEOUT=str(config[1]) if config else "")
            p = subprocess.run(["sh", probe, port, profile], cwd=cwd, env=env,
                               text=True, capture_output=True)
            assert p.returncode == status, (profile, port, p.stdout, p.stderr)
        for n in (*range(109, 141), *[int(p[1:]) for p in extended]):
            profile = f"m{n}"
            prefix = profile.upper()
            path = ROOT / "scripts" / (profile + "-expected-markers.txt")
            if profile in DERIVED:
                elf_path = ROOT / "scripts" / (profile + "-original-elfs.txt")
                shutil.copyfile(elf_path, scripts / elf_path.name)
            else:
                shutil.copyfile(path, scripts / path.name)
            entries = expected_markers(profile)
            config = extended.get(profile)
            count = config[0] if config else (39 if n == 140 else 20 if n == 110 or n >= 136 else 15)
            assert len(entries) == len(set(entries)) == count + 1
            assert entries.count(prefix + "_BATCH_PASS") == 1
            assert all(s.startswith(prefix + "_") and s.endswith("_PASS") for s in entries)
            execs = []
            special = []
            if config and config[3]:
                elf_file = ROOT / "scripts" / (profile + "-original-elfs.txt")
                shutil.copyfile(elf_file, scripts / elf_file.name)
                execs = ["[EXEC] replaced process with CPIO ELF: /" + e
                         for e in elf_file.read_text().splitlines() if e and not e.startswith("#")]
            if profile == "m151":
                special = ["[M151 ROUNDTRIP] fixture"] * 23
            elif profile == "m155":
                special = ["[M155 ELF STARTUP ONLY] fixture"] * 8
                assert len(execs) == 33
            evidence = entries + execs + special
            for port in ("amd64", "riscv64", "arm64"):
                run_profile(profile, port, evidence)
            run_profile(profile, "amd64", evidence[1:], status=1)
            run_profile(profile, "amd64", [*evidence, entries[0]], status=1)
            run_profile(profile, "amd64", evidence, status=1, fail_runner=True)
            if execs:
                run_profile(profile, "amd64", entries + execs[1:] + special, status=1)
                run_profile(profile, "amd64",
                            entries + [execs[0] + "-different"] + execs[1:] + special,
                            status=1)
            if special:
                run_profile(profile, "amd64", evidence[:-1], status=1)
            if profile == "m112":
                # A malformed or forged canonical ELF manifest must be rejected
                # before consulting the synthetic guest log, even if that log
                # claims every expected success marker.
                canonical = scripts / "m112-original-elfs.txt"
                original = canonical.read_text()
                names = [line for line in original.splitlines()
                         if line and not line.startswith("#")]
                assert len(names) == 15
                try:
                    canonical.write_text(original.replace(names[1], names[0]))
                    run_profile(profile, "amd64", evidence, status=2)
                    canonical.write_text(original.replace(names[0], "../../forged/path"))
                    run_profile(profile, "amd64", evidence, status=2)
                    canonical.write_text(original + "usr/bin/forged-extra\n")
                    run_profile(profile, "amd64", evidence, status=2)
                finally:
                    canonical.write_text(original)
            migrated += 1
        run_profile("m999", "amd64", [], status=2)
        run_profile("m109", "invalid", [], status=2)
    assert migrated >= 37, migrated
print(f"[BATCH MARKER HOST PASS] {migrated} real manifests; duplicate, missing, malformed and summary-only rejection")
