#!/usr/bin/env python3
"""Fail-closed audit of M88-M103 smoke-check names versus actual evidence.

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

    # Every M88..M103 entry must be explicit in the mapping. A separate M99
    # "green" row would be a duplicate run of M98, not an independent proof.
    for number in range(88, 104):
        if number != 99 and number not in expected:
            errors.append(f"M{number}: milestone omitted from audited CI map")

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

    # No other M88-M103 step is allowed to masquerade as independent coverage.
    for step in steps:
        script = step.group("script")
        number_match = re.match(r"check-m(\d+)", script)
        if number_match and 88 <= int(number_match.group(1)) <= 103:
            if script not in expected.values():
                errors.append(f"unexpected/duplicate M88-M103 CI check: {script}")
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
    print("[CI PROOF MAP PASS] M88-M103: 3 host-only, 12 host + target-compile; "
          "M98/M99 share one host fixture; no claim of native three-ISA IRQ proof")
    if args.self_test:
        print("[CI PROOF MAP SELF-TEST PASS] duplicate/mislabel/missing gates rejected")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
