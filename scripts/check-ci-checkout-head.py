#!/usr/bin/env python3
"""Fail closed when a PR acceptance job silently tests GitHub's merge ref.

For pull_request runs, checkout@v4 defaults to refs/pull/N/merge rather than
the submitted commit. The native QEMU checks must use the exact PR head;
workflow_dispatch and main pushes continue to use github.sha. This checker
uses only the Python standard library, so it is runnable on a stock runner.
"""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
WORKFLOWS = ROOT / ".github" / "workflows"
CHECKOUT = re.compile(r"^(?P<space> *)- uses: actions/checkout@v[0-9]+\s*$")
REQUIRED_REF = "ref: ${{ github.event.pull_request.head.sha || github.sha }}"
PR_EVENT = re.compile(r"^  pull_request:\s*$", re.MULTILINE)


def audit(path):
    source = path.read_text(encoding="utf-8")
    # The opt-in wrapper delegates the actual checkout to this reusable job.
    reusable_pr_job = path.name == "m55-cmake.yml"
    if not PR_EVENT.search(source) and not reusable_pr_job:
        return 0, []

    lines = source.splitlines()
    errors = []
    checked = 0
    for index, line in enumerate(lines):
        match = CHECKOUT.match(line)
        if not match:
            continue
        checked += 1
        width = len(match.group("space"))
        siblings = []
        for following in lines[index + 1 :]:
            indentation = len(following) - len(following.lstrip(" "))
            if following.strip() and indentation <= width:
                break
            siblings.append(following)

        with_line = " " * (width + 2) + "with:"
        ref_line = " " * (width + 4) + REQUIRED_REF
        if with_line not in siblings or ref_line not in siblings:
            errors.append(f"{path.relative_to(ROOT)}:{index + 1}: PR checkout is not pinned to submitted head")

    # M55 is a wrapper for a reusable workflow whose checkout is pinned;
    # every other current PR workflow must contain a directly audited step.
    if checked == 0 and path.name != "m55-optin.yml":
        errors.append(f"{path.relative_to(ROOT)}: no directly checked-out PR head")
    if checked == 0 and path.name == "m55-optin.yml" and "uses: ./.github/workflows/m55-cmake.yml" not in source:
        errors.append(f"{path.relative_to(ROOT)}: reusable M55 checkout contract missing")
    return checked, errors


def main():
    total = 0
    workflows = 0
    failures = []
    for path in sorted(WORKFLOWS.glob("*.yml")):
        if PR_EVENT.search(path.read_text(encoding="utf-8")):
            workflows += 1
        count, errors = audit(path)
        total += count
        failures.extend(errors)

    for failure in failures:
        print(f"[CI HEAD FAIL] {failure}", file=sys.stderr)
    if failures:
        return 1
    print(f"[CI HEAD PASS] {workflows} PR workflows plus M55 reuse, {total} checkout steps pinned to submitted commit")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
