#!/bin/sh
# Host-only production VT100 + genuine interactive GNU nano gate.
# The real original signed Alpine GUI/QMP proof is a separate guest gate.
set -eu
sh -n scripts/probe-m187-vt100.sh
python3 -B scripts/m187-qmp-verify.py --help > /dev/null
awk -F '\t' '
    /^#/ {next}
    NF!=6 || $1!="m187-vt100" || $2!="cpio" || $3!="m187" ||
    $4!="M187_DESKTOP_PASS" || $5!="-" ||
    $6!="/usr/bin/naseed-m174-devfs-launch" {exit 1}
    {valid++}
    END {if(valid!=1)exit 1}
' scripts/m187-vt100-batch-cases.tsv
grep -Fq '"m187"' user/m174-devfs-launch.c
grep -Fq 'if(argc==2 && strcmp(argv[1],"m187")==0)' \
    scripts/test-m176-ncat-launch.c
grep -Fq 'return m174_original_main(argc,argv);' \
    scripts/test-m176-ncat-launch.c
sh scripts/check-m170-desktop.sh
work=$(mktemp -d /tmp/naseed-m187.XXXXXX)
trap 'rm -rf "$work"' EXIT HUP INT TERM

gcc -std=c11 -O2 -Wall -Wextra -Werror \
    tests/m187-vt100.c -o "$work/vt100-fixture"
gcc -std=c11 -O2 -Wall -Wextra -Werror \
    tests/m187-fullscreen-pty.c -o "$work/fullscreen-fixture"
gcc -std=c11 -O2 -Wall -Wextra -Werror \
    tests/m187-pty-blocking-policy.c -o "$work/pty-blocking-policy"
gcc -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
    user/desktop/naseed-terminal.c user/desktop/protocol.c \
    user/desktop/text.c -o "$work/naseed-terminal"
gcc -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
    user/desktop/naseed-desktop.c user/desktop/protocol.c \
    user/desktop/text.c -o "$work/naseed-desktop"

"$work/vt100-fixture" > "$work/parser.log"
"$work/naseed-terminal" --selftest170 > "$work/legacy.log"
"$work/naseed-terminal" --selftest187 > "$work/terminal.log"
"$work/naseed-desktop" --selftest187 > "$work/compositor.log"
"$work/fullscreen-fixture" > "$work/fullscreen.log"
"$work/pty-blocking-policy" > "$work/pty-blocking-policy.log"
grep -Fq '[M187 HOST VT100 PASS]' "$work/parser.log"
grep -Fq '[M187 HOST TERMINAL PASS]' "$work/terminal.log"
grep -Fq '[M187 HOST COMPOSITOR PASS]' "$work/compositor.log"
grep -Fq '[M187 HOST FULLSCREEN PASS]' "$work/fullscreen.log"
grep -Fq '[M187 PTY BLOCKING POLICY PASS]' "$work/pty-blocking-policy.log"
grep -Fq '[M170 HOST TERMINAL PASS]' "$work/legacy.log"
cat "$work/parser.log" "$work/terminal.log" \
    "$work/compositor.log" "$work/fullscreen.log" "$work/pty-blocking-policy.log"
echo '[M187 HOST PASS] legacy compatible, VT ANSI parser, independent full-screen GNU nano PTY'
