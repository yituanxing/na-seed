#!/bin/sh
# M136: twenty historically new, unchanged Alpine ELF tools in real NaSeed.
# Keep independent traces and complete every case even after a failure.
PATH=/usr/sbin:/sbin:/usr/bin:/bin
HOME=/tmp
LANG=C
LC_ALL=C
TERM=xterm
export PATH HOME LANG LC_ALL TERM
busy() { /bin/busybox "$@"; }

original_elf() {
    candidate=$1
    [ -x "$candidate" ] || return 1
    resolved=$(busy readlink -f "$candidate") || return 1
    case "$resolved" in /bin/busybox|/usr/bin/busybox) return 1 ;; esac
    magic=$(busy dd if="$resolved" bs=1 count=4 2>/dev/null |
            busy od -An -tx1 | busy tr -d ' \n')
    [ "$magic" = 7f454c46 ] || return 1
    printf '%s\n' "$candidate"
}

# A real independent terminfo entry is shared by the seven ncurses tools.
seed_terminfo() {
    busy mkdir -p "$work/terminfo"
    busy printf '%s\n' \
        'm136|NaSeed M136 test terminal,' \
        '    am, cols#80, lines#24,' \
        '    clear=\E[H\E[2J, cr=^M, cud1=^J, home=\E[H,' \
        '    ht=^I, hts=\EH, tbc=\E[3g,' > "$work/m136.src"
    /usr/bin/tic -x -o "$work/terminfo" "$work/m136.src"
    [ -s "$work/terminfo/m/m136" ]
    TERMINFO="$work/terminfo"
    TERM=m136
    export TERMINFO TERM
}

seed_pty_recording() {
    SCRIPT_DEBUG=all ULPTY_DEBUG=all /usr/bin/script -q \
        -T "$work/timing.log" -O "$work/typescript" \
        -c '/bin/busybox printf "M136_PTY_RECORDED\n"' \
        > "$work/script.stdout" 2> "$work/stderr"
    [ -s "$work/timing.log" ] && [ -s "$work/typescript" ]
    busy grep -Fq M136_PTY_RECORDED "$work/typescript"
}

run_case() {
    app=$1
    work="/tmp/m136-$app"
    busy mkdir -p "$work"
    case "$app" in
      flex)
        exe=$(original_elf /usr/bin/flex)
        busy printf '%s\n' '%option noyywrap' '%%' \
            '[0-9]+ { return 17; }' '[a-z]+ { return 19; }' \
            '[ \t\n]+ ;' '. ;' '%%' > "$work/lexer.l"
        "$exe" -o "$work/lexer.c" "$work/lexer.l" > "$work/output"
        [ -s "$work/lexer.c" ]
        busy grep -Fq 'yy_accept' "$work/lexer.c"
        busy grep -Fq 'return 17;' "$work/lexer.c" ;;
      byacc)
        exe=$(original_elf /usr/bin/byacc)
        busy printf '%s\n' '%token NUMBER' '%%' \
            'expr: NUMBER { $$ = $1; }' \
            '    | expr "+" NUMBER { $$ = $1 + $3; }' \
            '    ;' '%%' > "$work/grammar.y"
        "$exe" -d -o "$work/parser.c" "$work/grammar.y" > "$work/output"
        [ -s "$work/parser.c" ]
        busy grep -Fq 'yyparse' "$work/parser.c"
        busy grep -Fq 'NUMBER' "$work/parser.c" ;;
      re2c)
        exe=$(original_elf /usr/bin/re2c)
        busy printf '%s\n' 'int scan(void) {' '/*!re2c' \
            '    re2c:define:YYCTYPE = char;' \
            '    re2c:yyfill:enable = 0;' \
            '    "naseed" { return 23; }' \
            '    * { return 0; }' \
            '*/' '}' > "$work/rules.re"
        "$exe" -o "$work/scanner.c" "$work/rules.re" > "$work/output"
        [ -s "$work/scanner.c" ]
        busy grep -Fq 'return 23;' "$work/scanner.c"
        busy grep -Fq 'YYCURSOR' "$work/scanner.c" ;;
      tic)
        exe=$(original_elf /usr/bin/tic)
        busy mkdir -p "$work/terminfo"
        busy printf '%s\n' 'm136|M136 compiled terminal, cols#80, lines#24, clear=\E[H\E[2J,' > "$work/m136.src"
        "$exe" -x -o "$work/terminfo" "$work/m136.src"
        [ -s "$work/terminfo/m/m136" ]
        TERMINFO="$work/terminfo" /usr/bin/infocmp -1 m136 > "$work/decompiled"
        busy grep -Fq 'cols#80' "$work/decompiled" ;;
      infocmp)
        exe=$(original_elf /usr/bin/infocmp)
        seed_terminfo
        "$exe" -1 m136 > "$work/decompiled"
        busy grep -Fq 'cols#80' "$work/decompiled"
        busy grep -Fq 'clear=' "$work/decompiled" ;;
      clear)
        exe=$(original_elf /usr/bin/clear)
        seed_terminfo
        "$exe" > "$work/output"
        [ -s "$work/output" ]
        bytes=$(busy od -An -tx1 "$work/output" | busy tr -d ' \n')
        case "$bytes" in *1b5b*) ;; *) return 1 ;; esac ;;
      tput)
        exe=$(original_elf /usr/bin/tput)
        seed_terminfo
        "$exe" -T m136 cols > "$work/output"
        busy grep -Fxq 80 "$work/output" ;;
      toe)
        exe=$(original_elf /usr/bin/toe)
        seed_terminfo
        "$exe" -a > "$work/output"
        busy grep -Fq 'm136' "$work/output" ;;
      tabs)
        exe=$(original_elf /usr/bin/tabs)
        seed_terminfo
        "$exe" -T m136 4 > "$work/output"
        [ -s "$work/output" ]
        bytes=$(busy od -An -tx1 "$work/output" | busy tr -d ' \n')
        case "$bytes" in *1b*) ;; *) return 1 ;; esac ;;
      tset)
        exe=$(original_elf /usr/bin/tset)
        seed_terminfo
        # tset must query terminal attributes through an actual PTY, not the
        # QEMU batch runner's redirected, non-interactive serial/stdout.
        SCRIPT_DEBUG=all ULPTY_DEBUG=all /usr/bin/script \
            -q -e -c "$exe -I -Q -s m136" "$work/pty.log" \
            > "$work/output" 2> "$work/stderr"
        busy grep -Fq 'm136' "$work/pty.log" ;;
      colcrt)
        exe=$(original_elf /usr/bin/colcrt)
        busy printf 'M136\b_\ncolumn\n' > "$work/input"
        "$exe" < "$work/input" > "$work/output"
        busy grep -Fq 'M136' "$work/output"
        busy grep -Fq 'column' "$work/output" ;;
      whereis)
        exe=$(original_elf /usr/bin/whereis)
        "$exe" -b busybox > "$work/output"
        busy grep -Fq '/bin/busybox' "$work/output" ;;
      fadvise)
        exe=$(original_elf /usr/bin/fadvise)
        busy printf 'M136 FADVISE ORIGINAL ELF DATA\n' > "$work/payload"
        "$exe" -a sequential -o 0 -l 8 "$work/payload" > "$work/output"
        busy grep -Fxq 'M136 FADVISE ORIGINAL ELF DATA' "$work/payload" ;;
      script)
        exe=$(original_elf /usr/bin/script)
        "$exe" --version > "$work/version" 2> "$work/version.err" || {
            echo '[M136 SCRIPT BINARY VERSION FAILED]' >&2
            return 1
        }
        SCRIPT_DEBUG=all ULPTY_DEBUG=all "$exe" -q -T "$work/timing.log" \
            -O "$work/typescript" \
            -c '/bin/busybox printf "M136_SCRIPT_ACTUAL_PTY\n"' \
            > "$work/output" 2> "$work/stderr"
        [ -s "$work/timing.log" ] && [ -s "$work/typescript" ]
        busy grep -Fq 'M136_SCRIPT_ACTUAL_PTY' "$work/typescript" ;;
      scriptreplay)
        exe=$(original_elf /usr/bin/scriptreplay)
        seed_pty_recording
        "$exe" -t "$work/timing.log" -O "$work/typescript" -d 1000 \
            > "$work/replayed"
        busy grep -Fq M136_PTY_RECORDED "$work/replayed" ;;
      uuidgen)
        exe=$(original_elf /usr/bin/uuidgen)
        "$exe" > "$work/uuid1"
        "$exe" > "$work/uuid2"
        busy grep -Eq '^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$' "$work/uuid1"
        ! busy cmp -s "$work/uuid1" "$work/uuid2" ;;
      fallocate)
        exe=$(original_elf /usr/bin/fallocate)
        "$exe" -l 8192 "$work/preallocated" > "$work/output"
        [ -f "$work/preallocated" ]
        size=$(busy stat -c '%s' "$work/preallocated")
        [ "$size" -eq 8192 ] ;;
      findmnt)
        exe=$(original_elf /bin/findmnt)
        busy printf 'tmpfs /m136-fake tmpfs rw,nosuid 0 0\n' > "$work/fstab"
        "$exe" --fstab --tab-file "$work/fstab" -n -o TARGET,FSTYPE \
            > "$work/output"
        busy grep -Fq '/m136-fake' "$work/output"
        busy grep -Fq 'tmpfs' "$work/output" ;;
      flock)
        exe=$(original_elf /usr/bin/flock)
        lock="$work/lock"
        (
            exec 9>"$lock"
            "$exe" -x 9
            busy touch "$work/acquired"
            busy sleep 2
        ) &
        locker=$!
        tries=0
        while [ ! -e "$work/acquired" ] && [ "$tries" -lt 30 ]; do
            busy sleep 0.1
            tries=$((tries + 1))
        done
        [ -e "$work/acquired" ] || { wait "$locker" || :; return 1; }
        if "$exe" -n "$lock" /bin/busybox true; then
            echo 'second holder unexpectedly acquired locked file' >&2
            wait "$locker" || :
            return 1
        fi
        wait "$locker"
        "$exe" -n "$lock" /bin/busybox true ;;
      lscpu)
        exe=$(original_elf /usr/bin/lscpu)
        LSCPU_DEBUG=all "$exe" -J > "$work/output" 2> "$work/stderr"
        busy grep -Fq 'Architecture' "$work/output"
        busy grep -Eq '"data"[[:space:]]*:[[:space:]]*"(x86_64|aarch64|riscv64)"' "$work/output" ;;
      *) echo "[M136 UNKNOWN ORIGINAL ELF CASE] $app" >&2; return 2 ;;
    esac
}

if [ "$#" -eq 2 ] && [ "$1" = --case ]; then
    set -eu
    if [ "${M136_TRACE:-0}" = 1 ]; then
        PS4='[M136 TRACE] '
        set -x
    fi
    run_case "$2"
    exit 0
fi
[ "$#" -eq 0 ] || exit 2
failed=0
for app in flex byacc re2c tic infocmp clear tput toe tabs tset \
           colcrt whereis fadvise script scriptreplay uuidgen fallocate \
           findmnt flock lscpu; do
    echo "[M136 CASE START] $app"
    case_log="/tmp/m136-$app-case.log"
    upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
    if M136_TRACE=1 /bin/sh /tests/m136-batch-run.sh --case "$app" \
          > "$case_log" 2>&1; then
        printf 'M136_%s_PASS\n' "$upper"
    else
        rc=$?
        echo "[M136 $upper FAILED] original signed ELF rc=$rc" >&2
        busy tail -130 "$case_log" >&2 || :
        for detail in output stderr version version.err decompiled lexer.c parser.c scanner.c \
                      replayed script.stdout pty.log typescript timing.log uuid1 \
                      payload fstab input; do
            if [ -s "/tmp/m136-$app/$detail" ]; then
                echo "[M136 FAILED OUTPUT $app/$detail]" >&2
                busy head -c 8000 "/tmp/m136-$app/$detail" >&2 || :
                echo >&2
            fi
        done
        printf 'M136_%s_FAIL\n' "$upper"
        failed=$((failed+1))
    fi
done
if [ "$failed" -ne 0 ]; then
    echo "[M136 BATCH FAIL] $failed/20 original ELFs (full census)" >&2
    echo M136_BATCH_FAIL
    exit 1
fi
echo M136_BATCH_PASS
