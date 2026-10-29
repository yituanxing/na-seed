#!/usr/bin/env python3
"""Fail-closed audit of M88-M103 plus M105 host proof versus actual evidence.

This is intentionally a narrow, dependency-free contract checker, not a YAML
parser or a replacement for native-QEMU acceptance. Unknown/moved/duplicated
milestone steps fail until the evidence classification is reviewed.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HOST_ONLY = {
    88: "check-m88-exceptions.sh",
    89: "check-m89-poll.sh",
    90: "check-m90-io-wait.sh",
    105: "check-m105-signal-group.sh",
}
# M105 has two independent host-only subsystems; the additional ELF/MM check
# must never be presented as another system-QEMU or target-ISA execution.
EXTRA_HOST_CHECKS = {
    "M105 ELF/MM": "check-m105-elf-permissions.sh",
    "M105 PTY/ISIG": "test-m105-pty-isig.sh",
}
HOST_TARGET = {
    91: "check-m91-kcontext.sh",
    92: "check-m92-wait-queue.sh",
    93: "check-m93-futex-queues.sh",
    94: "check-m94-run-state.sh",
    95: "check-m95-kthread.sh",
    96: "check-m96-k-sync.sh",
    97: "check-m97-k-timeouts.sh",
    98: "check-m98-sched-core.sh",  # M99 is tested inside this host fixture.
    100: "check-m100-signal-failure.sh",
    101: "check-m101-cow.sh",
    102: "check-m102-tty-cow.sh",
    103: "check-m103-thread-tid-lifecycle.sh",
}
MILESTONE_STEP = re.compile(
    r"^      - name: (?P<name>[^\n]+)\n"
    r"        run: sh scripts/(?P<script>check-m\d+[^\s]+\.sh)\s*$",
    re.MULTILINE,
)


def check(root: Path, ci_text: str | None = None) -> list[str]:
    errors: list[str] = []
    workflow = ci_text if ci_text is not None else (
        root / ".github/workflows/ci.yml"
    ).read_text(encoding="utf-8")
    steps = list(MILESTONE_STEP.finditer(workflow))
    expected = HOST_ONLY | HOST_TARGET

    if workflow.count("run: python3 scripts/check-ci-proof-map.py") != 1:
        errors.append("CI proof-type audit must execute exactly once in smoke")

    # Every M88..M103 entry must be explicit in the mapping. M104 is a
    # *native guest* probe, not a smoke host checker. M105 has its own host
    # fixture; a separate M99 "green" row would duplicate the M98 host probe.
    for number in range(88, 104):
        if number != 99 and number not in expected:
            errors.append(f"M{number}: milestone omitted from audited CI map")
    if 105 not in expected:
        errors.append("M105: host signal-group model omitted from audited CI map")

    for number, script in expected.items():
        if workflow.count("scripts/" + script) != 1:
            errors.append(f"M{number}: {script} must be referenced exactly once")
        matches = [step for step in steps if step.group("script") == script]
        if len(matches) != 1:
            errors.append(
                f"M{number}: expected exactly one CI step for {script}, found {len(matches)}"
            )
            continue
        name = matches[0].group("name").strip("'\"")
        proof = "[HOST]" if number in HOST_ONLY else "[HOST + TARGET-COMPILE]"
        if not name.startswith(proof + " "):
            errors.append(f"M{number}: expected {proof} label, got {name!r}")
        if number == 98 and "M98/M99" not in name:
            errors.append("M99: shared host ring-scan fixture must be labelled M98/M99")
        if re.search(r"(?i)\b(?:native|real|qemu|irq parity|saved.frame commit)\b", name):
            errors.append(f"M{number}: title overclaims host/target-compile evidence")

    for label, script in EXTRA_HOST_CHECKS.items():
        # The PTY checker has a historical test-* prefix, unlike check-mXX.
        matches = [step for step in steps if step.group("script") == script]
        if script == "test-m105-pty-isig.sh":
            matches = list(re.finditer(
                r"(?m)^      - name: (?P<name>[^\n]+)\n"
                r"        run: sh scripts/test-m105-pty-isig\.sh\s*$", workflow
            ))
        if workflow.count("scripts/" + script) != 1 or len(matches) != 1:
            errors.append(f"{label}: expected exactly one CI invocation of {script}")
            continue
        name = matches[0].group("name").strip("'\"")
        if not name.startswith("[HOST] M105 "):
            errors.append(f"{label}: host fixture must be labelled [HOST], got {name!r}")
        if re.search(r"(?i)\b(?:native|real|qemu)\b", name):
            errors.append(f"{label}: title overclaims ELF/MM host fixture evidence")
        body = (root / "scripts" / script).read_text(encoding="utf-8")
        markers = (
            ("kernel/task/elf_segments.c", "kernel/mm/mm.c",
             '"$m105_tmp_dir/elf-permissions"', '"$m105_tmp_dir/elf-cow"')
            if script == "check-m105-elf-permissions.sh" else
            ("tests/m105-pty-isig.c", "kernel/drivers/pty.c",
             "kernel/drivers/tty_ldisc.c", "--linux-pty-oracle",
             '"$work/m105-pty-isig-kernel"')
        )
        for marker in markers:
            if marker not in body:
                errors.append(f"{label}: missing linked source or executed host binary {marker!r}")
        if "qemu-system-" in body or "scripts/probe-" in body:
            errors.append(f"{label}: host script changed modes; audit proof type")

    # No other M88-M103/M105 host step may masquerade as independent coverage.
    for step in steps:
        script = step.group("script")
        number_match = re.match(r"check-m(\d+)", script)
        if number_match and (88 <= int(number_match.group(1)) <= 103 or
                             int(number_match.group(1)) == 105):
            if script not in expected.values() and script not in EXTRA_HOST_CHECKS.values():
                errors.append(f"unexpected/duplicate M88-M103/M105 CI check: {script}")
    for name in re.findall(r"(?m)^      - name: (.+)$", workflow):
        if re.search(r"\bM99\b", name) and "M98/M99" not in name:
            errors.append("M99: no independent native IRQ saved-frame test exists")

    for number, script in expected.items():
        path = root / "scripts" / script
        if not path.is_file():
            errors.append(f"M{number}: missing checker {path}")
            continue
        body = path.read_text(encoding="utf-8")
        commands = "\n".join(line for line in body.splitlines()
                             if line.strip() and not line.lstrip().startswith("#"))
        if number == 105:
            # M105 intentionally allows the configured host C compiler and a
            # timeout-guarded invocation. This is *not* a target-QEMU proof.
            if "${CC:-cc}" not in commands or "kernel/task/signal.c" not in commands:
                errors.append("M105: production signal.c host compilation missing")
            if "timeout 30s build/m105-signal-group/host" not in commands:
                errors.append("M105: bounded host fixture execution missing")
        else:
            if not re.search(r"(?m)^cc\s", commands):
                errors.append(f"M{number}: host fixture compile disappeared")
            if not re.search(r"(?m)^build/[^\s]+/(?:host|check|test)(?:\s|$)", commands):
                errors.append(f"M{number}: host fixture execution disappeared")
        if re.search(r"qemu-system-|qemu-efi|scripts/(?:probe-|run\.sh)", commands):
            errors.append(f"M{number}: run mode changed; reclassify its proof type")
        if number in HOST_ONLY:
            if re.search(r"(?m)^\s*clang\s|for arch in", commands):
                errors.append(f"M{number}: target compile added; reclassify proof type")
        else:
            markers = ["clang $flags", "--target=riscv64", "--target=aarch64"]
            if number == 103:
                markers += ["for arch in x86_64 riscv64 aarch64; do",
                            "--target=x86_64", "NAS_M103_HOST_ONLY"]
            else:
                markers.append("for arch in riscv64 aarch64; do")
            for marker in markers:
                if marker not in commands:
                    errors.append(f"M{number}: target compilation missing {marker!r}")

    # The host-only escape hatch is legitimate for local diagnosis, but CI
    # cannot opt into it while claiming three compiled targets.
    if "NAS_M103_HOST_ONLY" in workflow:
        errors.append("M103: CI may not opt out of default fail-closed target compilation")

    fixture = (root / "scripts/test-m98-sched-core.c").read_text(encoding="utf-8")
    for marker in ("/* M99:", "nas_sched_scan_init(&scan,2)",
                   "nas_sched_scan_next(&scan,&slot)==blocked && slot==2",
                   "[M99 HOST MODEL PASS]"):
        if marker not in fixture:
            errors.append(f"M99: shared host ring-scan assertion missing {marker!r}")

    # Guard the expensive source-build gates being opt-in. Auto PR M50 is
    # fast-only and is not equivalent to exact-head M50 full/M56/M57.
    m50 = (root / ".github/workflows/m50-alpine-matrix.yml").read_text(encoding="utf-8")
    for marker in ("workflow_dispatch:", "      mode:",
                   "NAS_NATIVE_MODE:",
                   "github.event_name == 'workflow_dispatch' && inputs.mode || 'fast'",
                   "inputs.mode == 'full'", "fromJSON("):
        if marker not in m50:
            errors.append(f"M50: manual full/fast contract missing {marker!r}")
    for milestone, filename in ((56, "m56-native-busybox.yml"),
                                (57, "m57-native-sqlite.yml")):
        body = (root / ".github/workflows" / filename).read_text(encoding="utf-8")
        if "  workflow_dispatch:" not in body or re.search(
            r"(?m)^  (?:pull_request|push):", body
        ):
            errors.append(f"M{milestone}: manual-only full gate contract changed")

    # M105 has three separately evidenced HOST smoke checks AND a distinct
    # physical-serial, original-BusyBox system-QEMU gate on each ISA. The
    # latter must never be replaced by HOST or by a target-object compile.
    native_run = "run: sh scripts/probe-m105-signal-group.sh ${{ matrix.port }}"
    native_name = "[NATIVE QEMU] M105 physical TTY signal group"
    if workflow.count(native_run) != 1 or workflow.count(native_name) != 1:
        errors.append("M105: amd64/RV64 source BusyBox real-QEMU step missing/mislabelled")
    for prerequisite in ("run: sh scripts/probe-source-busybox.sh ${{ matrix.port }}",
                         "run: sh scripts/test-m104-probe.sh ${{ matrix.port }}"):
        if prerequisite not in workflow or workflow.find(prerequisite) > workflow.find(native_run):
            errors.append(f"M105: native step lost existing source/exception prerequisite {prerequisite!r}")

    arm64_source = (root / ".github/workflows/source-busybox-arm64.yml").read_text(
        encoding="utf-8"
    )
    arm_native_run = "run: sh scripts/probe-m105-signal-group.sh arm64"
    if arm64_source.count(arm_native_run) != 1 or arm64_source.count(
        "[NATIVE QEMU] M105 ARM64 physical TTY"
    ) != 1:
        errors.append("M105: genuine ARM64 original-source QEMU step missing/mislabelled")
    for required_path in ("scripts/probe-m105-signal-group.sh",
                          "scripts/build-m105-signal-group.sh",
                          "scripts/m105-signal-group-serial-check.py",
                          "tests/m105-signal-group.c",
                          "kernel/arch/aarch64/**", "kernel/mm/**",
                          "kernel/drivers/pty*", "kernel/drivers/tty*"):
        if f"- '{required_path}'" not in arm64_source:
            errors.append(f"M105: ARM64 PR path filter misses {required_path}")

    probe = (root / "scripts/probe-m105-signal-group.sh").read_text(encoding="utf-8")
    serial = (root / "scripts/m105-signal-group-serial-check.py").read_text(
        encoding="utf-8"
    )
    for marker in ("source_binary=", "sh scripts/build-m105-signal-group.sh",
                   "sh scripts/stage-busybox-rootfs.sh",
                   "NASEED_INTERACTIVE_DRIVER=scripts/m105-signal-group-serial-check.py",
                   "sh scripts/probe-m18-ash-interactive.sh",
                   "sh scripts/probe-m18-port-interactive.sh",
                   "[M105 GROUP NATIVE PASS]"):
        if marker not in probe:
            errors.append(f"M105: native-probe system-QEMU/BusyBox action missing {marker!r}")
    for marker in ('p.stdin.write(b"\\003")', 'p.stdin.write(b"\\034")',
                   "M105_SIGNAL_GROUP_NATIVE_PASS", "[PASS] cmdline userspace exit 0",
                   "[M105 NATIVE QEMU FAIL]", "[M105 NATIVE QEMU PASS]",
                   "[RV64] userspace debug-exit status=0",
                   "[ARM64] userspace debug-exit status=0"):
        if marker not in serial:
            errors.append(f"M105: physical serial guest fail-closed evidence missing {marker!r}")

    # A second independent guest proves *executed* RW->RX instructions and
    # post-fork private COW under the native CPU's TLB/I-cache. Host mocks do
    # not count, and this source-BusyBox gate must stay after source staging.
    jit_run = "run: sh scripts/test-m105-native-jit-probe.sh ${{ matrix.port }}"
    if workflow.count(jit_run) != 1 or workflow.count(
        "[NATIVE QEMU] M105 real CPU RW/RX JIT"
    ) != 1:
        errors.append("M105 JIT: amd64/RV64 real-QEMU step missing/mislabelled")
    if workflow.find(native_run) > workflow.find(jit_run):
        errors.append("M105 JIT: source QEMU JIT gate must follow group-signal gate")
    arm_jit_run = "run: sh scripts/test-m105-native-jit-probe.sh arm64"
    if arm64_source.count(arm_jit_run) != 1 or arm64_source.count(
        "[NATIVE QEMU] M105 ARM64 real CPU RW/RX JIT"
    ) != 1:
        errors.append("M105 JIT: ARM64 real-QEMU step missing/mislabelled")
    for required_path in ("scripts/test-m105-native-jit-probe.sh",
                          "scripts/test-m105-native-jit.c",
                          "scripts/test-m105-native-jit-cases.tsv"):
        if f"- '{required_path}'" not in arm64_source:
            errors.append(f"M105 JIT: ARM64 PR path filter misses {required_path}")
    jit_probe = (root / "scripts/test-m105-native-jit-probe.sh").read_text(
        encoding="utf-8"
    )
    for marker in ("scripts/test-m105-native-jit.c", "--target=\"$target\"",
                   "BUSYBOX_PARITY_BINARY", "sh \"$harness\"",
                   "BUSYBOX_PARITY_EXPECTED_COUNT=1",
                   "M105_JIT_MPROTECT_EXEC_PASS", "M105_NATIVE_JIT_COW_PASS",
                   "[PASS] cmdline userspace exit 0", "[M105 NATIVE JIT QEMU PASS]"):
        if marker not in jit_probe:
            errors.append(f"M105 JIT: actual native guest acceptance missing {marker!r}")
    manifest = (root / "scripts/test-m105-native-jit-cases.tsv").read_text(
        encoding="utf-8"
    )
    cases = [line.split() for line in manifest.splitlines()
             if line.strip() and not line.lstrip().startswith("#")]
    if cases != [["m105-native-jit", "cpio", "-", "M105_NATIVE_JIT_COW_PASS",
                  "-", "/bin/m105-native-jit"]]:
        errors.append("M105 JIT: six-field parity manifest malformed or fake method")
    return errors


def self_test() -> list[str]:
    workflow = (ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")
    cases = {
        "duplicate M99": workflow.replace(
            "        run: sh scripts/check-m98-sched-core.sh",
            "        run: sh scripts/check-m98-sched-core.sh\n"
            "      - name: '[NATIVE QEMU] M99 false separate IRQ parity'\n"
            "        run: sh scripts/check-m98-sched-core.sh",
            1,
        ),
        "fake native QEMU": workflow.replace(
            "'[HOST + TARGET-COMPILE] M101", "'[NATIVE QEMU] M101", 1
        ),
        "M103 host-only mislabel": workflow.replace(
            "'[HOST + TARGET-COMPILE] M103", "'[HOST] M103", 1
        ),
        "M103 target opt-out": workflow.replace(
            "run: sh scripts/check-m103-thread-tid-lifecycle.sh",
            "run: NAS_M103_HOST_ONLY=1 sh scripts/check-m103-thread-tid-lifecycle.sh",
            1,
        ),
        "M105 false native claim": workflow.replace(
            "'[HOST] M105", "'[NATIVE QEMU] M105", 1
        ),
        "M105 missing host fixture": workflow.replace(
            "run: sh scripts/check-m105-signal-group.sh",
            "run: sh scripts/not-an-m105-test.sh", 1
        ),
        "M105 ELF/MM false native claim": workflow.replace(
            "'[HOST] M105 ELF", "'[NATIVE QEMU] M105 ELF", 1
        ),
        "M105 ELF/MM missing host fixture": workflow.replace(
            "run: sh scripts/check-m105-elf-permissions.sh",
            "run: sh scripts/not-an-m105-elf-test.sh", 1
        ),
        "M105 PTY/ISIG false native claim": workflow.replace(
            "'[HOST] M105 PTY", "'[NATIVE QEMU] M105 PTY", 1
        ),
        "M105 PTY/ISIG missing host fixture": workflow.replace(
            "run: sh scripts/test-m105-pty-isig.sh",
            "run: sh scripts/not-an-m105-pty-test.sh", 1
        ),
        "M105 fake host guest step": workflow.replace(
            "'[NATIVE QEMU] M105 physical TTY", "'[HOST] M105 physical TTY", 1
        ),
        "M105 guest dispatch missing": workflow.replace(
            "run: sh scripts/probe-m105-signal-group.sh ${{ matrix.port }}",
            "run: sh scripts/not-a-native-m105-probe.sh ${{ matrix.port }}", 1
        ),
        "M105 JIT fake host guest step": workflow.replace(
            "'[NATIVE QEMU] M105 real CPU RW/RX JIT", "'[HOST] M105 real CPU RW/RX JIT", 1
        ),
        "M105 JIT guest dispatch missing": workflow.replace(
            "run: sh scripts/test-m105-native-jit-probe.sh ${{ matrix.port }}",
            "run: sh scripts/not-a-native-m105-jit.sh ${{ matrix.port }}", 1
        ),
        "missing host execution": workflow.replace(
            "        run: sh scripts/check-m88-exceptions.sh",
            "        run: sh scripts/never-run-m88.sh", 1
        ),
    }
    return [f"self-test did not reject {label}" for label, mutated in cases.items()
            if not check(ROOT, mutated)]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true",
                        help="verify detection of duplicate, mislabelled and missing gates")
    args = parser.parse_args()
    errors = check(ROOT)
    if args.self_test:
        errors.extend(self_test())
    if errors:
        for error in errors:
            print(f"[CI PROOF MAP FAIL] {error}", file=sys.stderr)
        return 1
    print("[CI PROOF MAP PASS] M88-M103/M105: 6 host-only, 12 host + target-compile; "
          "M98/M99 share one host fixture; no claim of native three-ISA IRQ proof")
    if args.self_test:
        print("[CI PROOF MAP SELF-TEST PASS] duplicate/mislabel/missing gates rejected")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
