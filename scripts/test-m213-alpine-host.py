#!/usr/bin/env python3
"""Fail-closed M213 *planning/census* contract: ZERO native-QEMU claims.

This validates 20 distinct unaccepted original Alpine ELF target *paths* and
their already selected M141 native ELF classes. It does not download APKs,
execute software, attest cross-ISA SHA, or count guest acceptance. Those require
the separate signed census, genuine fixture-dependent semantic oracles, and
independent first/reopen QEMU evidence before M213 can become a CI cohort.
"""

from __future__ import annotations

from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parent.parent
HEADER = "package\telf_path\tfamily\tprobe"
CAN = ROOT / "scripts/m213-candidate-elfs.tsv"
GUEST = ROOT / "scripts/m213-alpine-cohort.tsv"
APK = ROOT / "rootfs/alpine/packages-m213-candidates.txt"
WORKLOAD = ROOT / "tests/m213-software-workload.sh"
DISPATCH = ROOT / "tests/m205-software-workload.sh"
EXECUTABLE_CASES = {
    "child-process-group-transition:usr/bin/setpgid": "/usr/bin/setpgid",
    "child-exit-status-observation:usr/bin/waitpid": "/usr/bin/waitpid",
    "iso9660-size-fixture:usr/bin/isosize": "/usr/bin/isosize",
    "pipe-capacity-set-and-readback:bin/pipesz": "/bin/pipesz",
    "generic-netlink-family-discovery:sbin/genl": "/sbin/genl",
}


def content(path: Path) -> list[str]:
    return [line for raw in path.read_text().splitlines()
            if (line := raw.strip()) and not line.startswith("#")]


def rows(path: Path) -> list[tuple[str, str, str, str]]:
    lines = content(path)
    assert lines and lines[0] == HEADER, f"bad table header: {path}"
    result = [tuple(line.split("\t")) for line in lines[1:]]
    assert all(len(row) == 4 for row in result), "bad TSV field count"
    return result  # type: ignore[return-value]


def old_paths() -> set[str]:
    result: set[str] = set()
    for manifest in (ROOT / "scripts").glob("m[0-9]*-original-elfs.txt"):
        if manifest.name == "m141-original-elfs.txt":
            # M141 selected, but has NOT been native-QEMU accepted.
            continue
        result.update(content(manifest))
    for manifest in ("m205-candidate-elfs.tsv", "m206-candidate-elfs.tsv"):
        result.update(row[1] for row in rows(ROOT / "scripts" / manifest))
    return result


def validate(candidate: list[tuple[str, str, str, str]],
             cohort: list[tuple[str, str, str, str]],
             packages: list[str],
             m141: dict[str, str],
             selected: set[str],
             historical: set[str]) -> None:
    assert len(candidate) == 20, "M213 needs exactly 20 new accountable ELF paths"
    assert candidate == cohort, "candidate signed census differs from guest cohort"
    assert len(packages) == len(set(packages)), "APK owner requested twice"
    assert set(packages) == {row[0] for row in candidate}, "APK owner closure mismatch"
    assert len({row[1] for row in candidate}) == 20, "repeated ELF path"
    assert len({row[3] for row in candidate}) == 20, "ambiguous semantic oracle IDs"
    assert len({row[2] for row in candidate}) >= 6, "insufficient semantic families"
    for pkg, path, family, probe in candidate:
        assert re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9.+_-]*", pkg), pkg
        assert re.fullmatch(r"[a-z][a-z0-9-]*", family), family
        assert re.fullmatch(r"[a-z][a-z0-9-]*", probe), probe
        assert path.startswith(("bin/", "sbin/", "usr/bin/", "usr/sbin/"))
        assert not Path(path).is_absolute() and ".." not in Path(path).parts
        assert path in selected, f"not an M141 cross-ISA canonical ELF class: {path}"
        assert m141.get(path) == pkg, f"Alpine package mismatch: {path}"
        assert path not in historical, f"already in a later accepted batch: {path}"


def strict_negative_contract(candidate: list[tuple[str, str, str, str]],
                             packages: list[str], m141: dict[str, str],
                             selected: set[str], historical: set[str]) -> None:
    def rejected(variant: list[tuple[str, str, str, str]],
                 owner: list[str] = packages,
                 guest: list[tuple[str, str, str, str]] | None = None) -> None:
        try:
            validate(variant, variant if guest is None else guest, owner,
                     m141, selected, historical)
        except AssertionError:
            return
        raise AssertionError("invalid M213 candidate was accepted by host contract")

    for replacement in (
        candidate[0],  # duplicate exact ELF path
        ("iproute2", "sbin/rtstat", "network", "duplicate-m205-elf"),
        ("nftables", "usr/sbin/nft", "network", "duplicate-m206-elf"),
        ("iproute2", "sbin/routel", "network", "script-not-native-elf"),
        ("iproute2", "sbin/../sbin/genl", "network", "path-traversal"),
        ("bind-tools", "sbin/genl", "network", "wrong-apk-owner"),
    ):
        bad = candidate.copy()
        bad[4] = replacement
        rejected(bad)
    rejected(candidate, packages + [packages[0]])
    rejected(candidate, packages[:-1])
    rejected(candidate, packages, list(reversed(candidate)))
    bad = candidate.copy()
    bad[0] = (bad[0][0], bad[0][1], bad[0][2], bad[1][3])
    rejected(bad)
    bad = candidate.copy()
    bad[0] = (bad[0][0], bad[0][1], "", bad[0][3])
    rejected(bad)


def workload_contract(candidate: list[tuple[str, str, str, str]]) -> None:
    guest = WORKLOAD.read_text()
    dispatcher = DISPATCH.read_text()
    assert "NAS_M213_COMPAT_COHORT" in dispatcher
    assert "milestone=M213" in dispatcher
    assert "manifest=/root/m213-cohort.tsv" in dispatcher
    assert "exercise_script=/root/m213-software-workload.sh" in dispatcher
    assert "exec 0</dev/null" in dispatcher
    assert 'done 9< "$manifest"' in dispatcher
    assert 'if [ "$milestone" != M205 ]; then' in dispatcher
    assert "NAS_M213_BOOT_PHASE" in dispatcher and "m213-work" in guest
    assert "7f454c46" in guest and 'test -x "/$elf"' in guest
    assert "M213 genuine guest semantic PASS" in guest
    assert "--help" not in guest and "--version" not in guest
    blocked = 0
    for _, path, _, probe in candidate:
        label = f"{probe}:{path}"
        assert guest.count(label) == 1, f"missing/duplicated original probe {label}"
        branch = guest.split(label, 1)[1].split(";;", 1)[0]
        if label in EXECUTABLE_CASES:
            assert EXECUTABLE_CASES[label] in branch, (
                f"executable case never runs the original ELF: {label}")
            assert "probe_rc=0" in branch and "[M213 BLOCKED]" not in branch
            if label == "pipe-capacity-set-and-readback:bin/pipesz":
                assert "-s8192" in branch and "4096" in branch and "8192" in branch
                assert branch.count("/bin/pipesz") >= 3, (
                    "pipesz must set capacity and independently exec/read it back")
            if label == "generic-netlink-family-discovery:sbin/genl":
                for token in ("ctrl list", "ctrl get name nlctrl",
                              "ctrl get id 0x10", "__naseed_missing_family_213__",
                              "missing_rc", "RTNETLINK answers: No such file or directory",
                              "Name:[[:space:]]*nlctrl", "ID:[[:space:]]*0x10"):
                    assert token in branch, (
                        f"genl is missing real positive/negative oracle: {token}")
                assert "-ne 124" in branch and "-ne 137" in branch, (
                    "timed-out negative family lookup cannot count as success")
        else:
            assert "[M213 BLOCKED]" in branch, (
                f"unsupported native case not explicit fail-closed: {label}")
            assert "probe_rc=0" not in branch, (
                f"blocked case could accidentally count as PASS: {label}")
            blocked += 1
    assert blocked == 15 and len(EXECUTABLE_CASES) == 5
    for script in (WORKLOAD, DISPATCH):
        subprocess.run(["sh", "-n", str(script)], check=True, timeout=10)
    ambiguous = subprocess.run(
        ["sh", str(DISPATCH), "--exercise", "bogus", "usr/bin/setpgid"],
        env={**os.environ, "NAS_M213_COMPAT_COHORT": "1",
             "NAS_M206_COMPAT_COHORT": "1"},
        capture_output=True, text=True, timeout=5)
    assert ambiguous.returncode == 2 and "overlapping" in ambiguous.stderr, (
        "overlapping M206/M213 selectors must refuse execution before IO")


def iso9660_linux_oracle() -> bool:
    """Verify the guest-generated PVD layout against real host util-linux."""
    isosize = shutil.which("isosize")
    if isosize is None:
        print("[M213 ISO HOST ORACLE NOT RUN] util-linux isosize unavailable")
        return False
    with tempfile.TemporaryDirectory(prefix="m213-genuine-iso-pvd-") as tmp:
        pvd = bytearray(20 * 2048)
        pvd[32768:32775] = b"\x01CD001\x01"
        pvd[32848:32856] = bytes((20, 0, 0, 0, 0, 0, 0, 20))
        pvd[32896:32900] = bytes((0, 8, 8, 0))
        image = Path(tmp) / "size-20-2048.iso"
        image.write_bytes(pvd)

        def call(*flags: str) -> subprocess.CompletedProcess[str]:
            return subprocess.run([isosize, *flags, str(image)], check=True,
                                  text=True, capture_output=True, timeout=10)

        assert call().stdout.strip() == "40960", "real isosize PVD byte count"
        assert call("-x").stdout.strip() == (
            "sector count: 20, sector size: 2048"), "real isosize block parse"
        pvd[32848:32856] = bytes(8)
        image.write_bytes(pvd)
        assert call().stdout.strip() == "0", (
            "negative PVD must not preserve the former positive result")
    print("[M213 ISO LINUX ORACLE PASS] genuine util-linux PVD bytes, "
          "block count and zero-volume negative control")
    return True


def main() -> None:
    candidate = rows(CAN)
    cohort = rows(GUEST)
    packages = content(APK)
    m141 = {path: pkg for pkg, path, _, _ in
            rows(ROOT / "scripts/m141-candidate-elfs.tsv")}
    selected = set(content(ROOT / "scripts/m141-selected-elfs.txt"))
    historical = old_paths()
    validate(candidate, cohort, packages, m141, selected, historical)
    strict_negative_contract(candidate, packages, m141, selected, historical)
    workload_contract(candidate)
    iso9660_linux_oracle()
    for profile in ("m141", "m205", "m206", "m213"):
        subprocess.run(["python3", "scripts/check-m141-candidates.py",
                        "--profile", profile], check=True, cwd=ROOT,
                       stdout=subprocess.DEVNULL, timeout=10)
    inventory = (ROOT / "scripts/inventory-m141-signed-apks.sh").read_text()
    assert "m141|m205|m206|m213" in inventory, "M213 not signed-census selectable"
    assert inventory.count('[ "$profile" = m213 ]') == 3, (
        "M213 must reuse signed APK cache, index and independent offline "
        "closure verification for all three target ISAs")
    subprocess.run(["sh", "-n", "scripts/inventory-m141-signed-apks.sh"],
                   check=True, cwd=ROOT, timeout=10)
    print("[M213 HOST CANDIDATE CONTRACT PASS] 20 distinct unaccepted "
          "canonical Alpine ELF paths, 8 exact signed-APK owners, 8 genuine "
          "semantic families; 11 anti-false-positive cases, original historical "
          "and M205/M206 deduplication, all four legacy/new preflight profiles")
    print("[M213 REAL PROBE CONTRACT] 5 guest-local semantics (setpgid, pidfd "
          "waitpid, ISO9660 PVD positive+negative, pipesz set/exec/readback, "
          "genl nlctrl list/name/id/ENOENT); 15 explicit dependency "
          "fail-closed cases, no guessed PASS or parent manifest stdin theft")
    print("[M213 NOT ACCEPTED] original signed APK/native ISA SHA, executable "
          "guest semantic probes and first/reopen QEMU still required")


if __name__ == "__main__":
    main()
