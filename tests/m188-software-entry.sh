#!/bin/sh
# Real NaSeed ring3 guest loader + CLI checks for exactly two independent
# original Alpine v3.23 main ELFs. Not a claim of editor/duplicate-file
# FUNCTIONAL semantics; those require independent stateful guest validation.
set -eu
export PATH=/usr/sbin:/sbin:/usr/bin:/bin
work=/tmp/m188-software-entry
/bin/busybox mkdir -p "$work"

# Upstream mg's option grammar is -nRb:f:u:. There is NO supported --help
# or --version switch; either risks launching the full-screen editor and
# hanging the preflight. An invalid -Z goes directly to usage() -> exit(1).
# Verify native loader+argument parsing+real program diagnostic, nothing more.
mg_rc=0
/usr/bin/mg -Z > "$work/mg-usage" 2>&1 || mg_rc=$?
[ "$mg_rc" -eq 1 ] && [ -s "$work/mg-usage" ] || {
    echo "[M188 MG ENTRY FAIL] no original mg usage/exit(1): $mg_rc" >&2
    exit 1
}
/bin/busybox grep -Eiq 'usage:.*mg' "$work/mg-usage" || {
    echo '[M188 MG ENTRY FAIL] no original mg command-line diagnostic' >&2
    exit 1
}
echo M188_MG_NATIVE_ELF_ENTRY_ONLY_PASS

# Upstream rdfind(1) documents --help as an immediate, non-mutating CLI
# action. No file search, filesystem modification, or duplicate claim occurs.
rdfind_rc=0
/usr/bin/rdfind --help > "$work/rdfind-help" 2>&1 || rdfind_rc=$?
[ "$rdfind_rc" -eq 0 ] && [ -s "$work/rdfind-help" ] || {
    echo "[M188 RDFIND ENTRY FAIL] original rdfind --help failed: $rdfind_rc" >&2
    exit 1
}
/bin/busybox grep -iq rdfind "$work/rdfind-help" || {
    echo '[M188 RDFIND ENTRY FAIL] no original rdfind CLI diagnostic' >&2
    exit 1
}
echo M188_RDFIND_NATIVE_ELF_ENTRY_ONLY_PASS
echo M188_SOFTWARE_2_SIGNED_ELF_ENTRY_ONLY_PASS
