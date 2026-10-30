#!/usr/bin/env python3
"""Guard the single native-QEMU runner shared by historical program probes.

Each original workload keeps its own guest assertions and previous entrypoint.
The M50 source-workload roster remains owned by native-alpine-matrix.tsv;
this checker must not establish a competing list of those programs.
This host-only check is intentionally separate from real QEMU acceptance.
"""

from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parent.parent
HELPER = "scripts/native-qemu-target.sh"
DEEP_ONLY = ("m56-native-busybox", "m57-native-sqlite")
EXPECTED = {
    "amd64": ("scripts/probe-x86-busybox-parity.sh", "build/busybox-source-parity-x86"),
    "riscv64": ("scripts/probe-riscv64-busybox-matrix.sh", "build/port-riscv64/full/matrix-source"),
    "arm64": ("scripts/probe-aarch64-busybox-matrix.sh", "build/port-aarch64/full/matrix-source"),
}
EARLY_WORKFLOWS = (
    "m33-native-tcc.yml",
    "arm64-full.yml",
    "lua.yml",
)


def resolve(port: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["sh", "-c", f'port="{port}"; . {HELPER}; printf "%s\n%s\n" "$runner" "$logs"'],
        cwd=ROOT, text=True, capture_output=True, check=False,
    )


def catalog_probes() -> list[Path]:
    """Derive each M58+ consumer from the *existing* APK profile owner.

    Profile suffixes and probe suffixes may differ (m60-terminal versus
    probe-m60-terminals); the unique milestone and actual staging call are
    the identity, not a second hard-coded filename list.
    """
    catalog = ROOT / "scripts/prebuilt-profile-catalog.tsv"
    found: dict[str, Path] = {}
    for line in catalog.read_text().splitlines():
        if not line or line.startswith("#") or line.startswith("profile\t"):
            continue
        fields = line.split("\t")
        assert len(fields) == 5, f"{catalog}: malformed profile row {line!r}"
        profile = fields[0]
        assert re.fullmatch(r"m[0-9]+-[a-z0-9-]+", profile), (
            f"{catalog}: invalid profile {profile!r}"
        )
        milestone = profile.split("-", 1)[0]
        assert milestone not in found, f"{catalog}: duplicate milestone {milestone}"
        matches = sorted((ROOT / "scripts").glob(f"probe-{milestone}-*.sh"))
        assert len(matches) == 1, (
            f"{catalog}: {milestone} requires exactly one original probe, got {matches}"
        )
        path = matches[0]
        stage = f'sh scripts/stage-{milestone}-prebuilt.sh "$port"'
        assert path.read_text().count(stage) == 1, (
            f"{path}: missing or duplicate canonical staging call {stage}"
        )
        found[milestone] = path
    assert len(found) >= 14, "historical M58+ signed APK profiles were lost"
    # No orphan consumer may call a cataloged stage wrapper from another
    # probe. This also rejects a second probe added under a different name.
    for path in (ROOT / "scripts").glob("probe-m*.sh"):
        stages = re.findall(r"sh scripts/stage-(m[0-9]+)-prebuilt\.sh", path.read_text())
        for milestone in stages:
            assert found.get(milestone) == path, (
                f"{path}: orphaned or duplicate {milestone} staging consumer"
            )
    return list(found.values())


def probes() -> list[Path]:
    canonical = ROOT / "scripts/native-alpine-matrix.tsv"
    original = []
    for line in canonical.read_text().splitlines():
        if not line or line.startswith("#"):
            continue
        fields = line.split("\t")
        assert len(fields) == 3, f"{canonical}: malformed roster row {line!r}"
        original.append(ROOT / fields[2])
    return (original +
            [ROOT / f"scripts/probe-{name}.sh" for name in DEEP_ONLY] +
            catalog_probes())


def early_probes() -> list[Path]:
    """Read the historical QEMU entrypoints from their *existing* CI owners.

    M31, M33-M37 and external Lua have distinct staging and Linux-host oracle
    rules; no second script/config roster is introduced here.
    """
    entries: list[Path] = []
    pattern = re.compile(
        r"^\s+run:\s+sh\s+(scripts/probe-(?:m31-[a-z0-9-]+|"
        r"m3[3-7]-[a-z0-9-]+|external-lua)\.sh)\s+\S+",
        re.MULTILINE,
    )
    for name in EARLY_WORKFLOWS:
        source = (ROOT / ".github/workflows" / name).read_text()
        entries.extend(ROOT / match for match in pattern.findall(source))
    assert len(entries) == 7 and len(set(entries)) == 7, (
        "original M31/M33-M37/Lua CI QEMU probes lost or duplicated", entries
    )
    for path in entries:
        assert path.is_file(), f"CI references missing original probe: {path}"
    return entries


def validate_runner(path: Path, source: str) -> None:
    assert source.count(f". {HELPER}") == 1, (
        f"{path}: target helper must be sourced exactly once"
    )
    assert "runner=" not in source, f"{path}: stale private runner mapping"
    assert 'sh "$runner"' in source, f"{path}: genuine native QEMU execution removed"


def validate_probe(path: Path, source: str) -> None:
    validate_runner(path, source)
    assert "logs=" not in source, f"{path}: stale private serial-path mapping"
    assert '"$logs/' in source, f"{path}: raw guest serial verification removed"


def validate_early_probe(path: Path, source: str) -> None:
    validate_runner(path, source)
    if path.name.startswith("probe-m37-"):
        assert 'log="$logs/m37-incremental.clean.log"' in source, (
            f"{path}: source-runner serial evidence path changed"
        )
        assert 'test -s "$log"' in source, f"{path}: serial evidence guard removed"
    elif path.name.startswith("probe-m31-"):
        # M31 unsets custom binaries: its real raw log lives in the *other*
        # historical (non-source) directory, intentionally not helper.logs.
        assert "unset BUSYBOX_PARITY_CASE BUSYBOX_PARITY_BINARY NAS_ACCEPTANCE_CPIO" in source, (
            f"{path}: M31 must run default BusyBox, not inherited source mode"
        )
        for port in ("build/busybox-parity-x86/m31-date.clean.log",
                     "build/port-riscv64/full/matrix/m31-date.clean.log",
                     "build/port-aarch64/full/matrix/m31-date.clean.log"):
            assert port in source, f"{path}: original non-source log {port} lost"
        assert 'test -s "$log"' in source, f"{path}: serial evidence guard removed"
    elif path.name.startswith(("probe-m34-", "probe-m35-", "probe-m36-")):
        # The Linux QEMU-user emulator is an independent oracle, not the
        # system-QEMU kernel runner; consolidating it would erase evidence.
        for emulator in ("qemu-riscv64", "qemu-aarch64"):
            assert f"emulator={emulator}" in source, (
                f"{path}: native Linux emulator oracle {emulator} lost"
            )
        assert '"$emulator"' in source, f"{path}: Linux emulator no longer used"


def mutation_early_guard(path: Path, source: str) -> None:
    validate_early_probe(path, source)
    variants = (
        source.replace(f". {HELPER}", "# helper omitted", 1),
        source.replace(f". {HELPER}", f". {HELPER}\nrunner=/bin/true", 1),
        source.replace('sh "$runner"', 'true', 1),
    )
    for broken in variants:
        assert broken != source, f"{path}: historical mutation fixture was unchanged"
        try:
            validate_early_probe(path, broken)
        except AssertionError:
            continue
        raise AssertionError(f"{path}: malformed historical QEMU probe was accepted")


def mutation_guard(path: Path, source: str) -> None:
    """Fail if an old duplicate mapping or a lost evidence edge slips by."""
    validate_probe(path, source)
    variants = (
        source.replace(f". {HELPER}", "# old helper removed", 1),
        source.replace(f". {HELPER}", f". {HELPER}\nrunner=/bin/true", 1),
        source.replace(f". {HELPER}", f". {HELPER}\nlogs=/tmp/fake", 1),
        source.replace('sh "$runner"', 'true', 1),
        source.replace('"$logs/', '"/tmp/', 1),
    )
    for broken in variants:
        assert broken != source, f"{path}: mutation fixture did not change source"
        try:
            validate_probe(path, broken)
        except AssertionError:
            continue
        raise AssertionError(f"{path}: malformed native-QEMU probe was accepted")


def main() -> None:
    for port, expected in EXPECTED.items():
        result = resolve(port)
        assert result.returncode == 0 and result.stdout.splitlines() == list(expected), (
            port, result.returncode, result.stdout, result.stderr
        )
        assert (ROOT / expected[0]).is_file(), f"missing genuine QEMU runner: {expected[0]}"
    for invalid in ("", "x86_64", "invalid"):
        result = resolve(invalid)
        assert result.returncode == 2 and not result.stdout, (
            invalid, result.returncode, result.stdout
        )
    paths = probes()
    assert len(paths) >= 33 and len(set(paths)) == len(paths), (
        "lost or duplicated M38-M79 canonical probe"
    )
    for path in paths:
        source = path.read_text()
        validate_probe(path, source)
    early = early_probes()
    for path in early:
        validate_early_probe(path, path.read_text())
    mutation_guard(paths[0], paths[0].read_text())
    mutation_early_guard(early[0], early[0].read_text())
    print(f"[NATIVE QEMU TARGET PASS] 3 target mappings, "
          f"{len(paths) + len(early)} original workloads "
          "(including 7 historical CI probes), stale-implementation mutation guards")


if __name__ == "__main__":
    main()
