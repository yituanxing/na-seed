#!/bin/sh
# Run genuine official APK programs inside the M197 Alpine PID1/ext4 guest.
# This is DISCOVERY: failed/blocked software remains failed/blocked in the ledger.
set -u
export PATH=/usr/sbin:/sbin:/usr/bin:/bin HOME=/root LC_ALL=C LANG=C

manifest=/root/m204-cohort.tsv
work=/root/m204-work
first=/root/m204-first-results.tsv
reopen=/root/m204-reopen-results.tsv
tab=$(printf '\t')

case "${1:-}" in
    --first) phase=first; ledger=$first ;;
    --reopen) phase=reopen; ledger=$reopen ;;
    --exercise) phase=exercise ;;
    *) echo '[M204 FAIL] usage: m204-software-workload.sh --first|--reopen|--exercise WORKLOAD' >&2; exit 2 ;;
esac

record() {
    # Exact records are parsed again from each independent native-QEMU UART.
    printf 'M204_RESULT\t%s\t%s\t%s\t%s\n' "$1" "$2" "$3" "$4" |
        /bin/busybox tee -a "$ledger"
}

error_tail() {
    # Preserve a bounded per-package symptom on UART; full log stays on ext4.
    package=$1 operation=$2 logfile=$3
    /bin/busybox tail -n 4 "$logfile" 2>/dev/null |
        while IFS= read -r line; do
            # M194's UART supervisor recognizes literal [FAIL] globally;
            # software stderr must never impersonate its boot-failure sentinel.
            detail=$(printf '%s' "$line" | /bin/busybox tr '\t\r' '  ' |
                /bin/busybox sed 's/\[FAIL\]/[failed]/g' |
                /bin/busybox cut -c 1-180)
            printf 'M204_ERROR\t%s\t%s\t%s\n' "$package" "$operation" "$detail"
        done
}

functional() {
    case "$1" in
        tmux)
            # An ext4 file from the FIRST boot must not satisfy the REOPEN run.
            rm -f "$work/tmux.txt" || return 1
            tmux -L m204 -f /dev/null new-session -d -s m204 \
                "printf 'm204-tmux-ok\\n' > $work/tmux.txt" || return 1
            n=0
            while [ ! -s "$work/tmux.txt" ] && [ "$n" -lt 8 ]; do
                sleep 1; n=$((n + 1))
            done
            tmux -L m204 kill-server >/dev/null 2>&1 || :
            test "$(cat "$work/tmux.txt" 2>/dev/null)" = m204-tmux-ok
            ;;
        procps)
            # Do not accidentally invoke the preinstalled BusyBox ps applet.
            owner=$(apk info -W /usr/bin/pgrep 2>/dev/null) || return 1
            case "$owner" in *procps-ng-*) ;; *) return 1 ;; esac
            /usr/bin/pgrep -f init >/dev/null
            ;;
        iproute2)
            /sbin/ip -o addr show | /bin/busybox grep -Fq '10.0.2.'
            ;;
        nginx)
            printf '%s\n' 'worker_processes 1;' \
                "pid $work/nginx.pid;" 'events { worker_connections 32; }' \
                "error_log $work/nginx-error.log;" \
                'http { access_log off; server { listen 127.0.0.1:18081;' \
                'location / { return 200 "m204-nginx-ok"; } } }' \
                > "$work/nginx.conf" || return 1
            nginx -t -c "$work/nginx.conf" >/dev/null 2>&1 || return 1
            nginx -c "$work/nginx.conf" >/dev/null 2>&1 || return 1
            sleep 1
            body=$(/bin/busybox timeout 12 /bin/busybox wget -q -O - \
                http://127.0.0.1:18081/ 2>/dev/null)
            result=$?
            nginx -s quit -c "$work/nginx.conf" >/dev/null 2>&1 || :
            test "$result" -eq 0 && test "$body" = m204-nginx-ok
            ;;
        openssh-keygen)
            rm -f "$work/ed25519" "$work/ed25519.pub"
            /usr/bin/ssh-keygen -q -t ed25519 -N '' -f "$work/ed25519" &&
                test -s "$work/ed25519.pub" &&
                /usr/bin/ssh-keygen -l -f "$work/ed25519.pub" >/dev/null
            ;;
        redis)
            redis-server --port 16379 --bind 127.0.0.1 --daemonize yes \
                --save '' --appendonly no --pidfile "$work/redis.pid" \
                --dir "$work" >/dev/null 2>&1 || return 1
            sleep 1
            answer=$(redis-cli -p 16379 set m204 correct 2>/dev/null)
            result=$?
            value=$(redis-cli -p 16379 get m204 2>/dev/null)
            redis-cli -p 16379 shutdown nosave >/dev/null 2>&1 || :
            test "$result" -eq 0 && test "$answer" = OK && test "$value" = correct
            ;;
        make)
            printf 'all:\n\tprintf "m204-make-ok\\n" > %s/make.out\n' "$work" \
                > "$work/GNUmakefile" || return 1
            make -s -C "$work" && test "$(cat "$work/make.out")" = m204-make-ok
            ;;
        coreutils)
            printf 'm204-coreutils\n' > "$work/hash-input" || return 1
            actual=$(/usr/bin/sha256sum "$work/hash-input") || return 1
            expected=$(/bin/busybox sha256sum "$work/hash-input") || return 1
            test "$actual" = "$expected"
            ;;
        findutils)
            mkdir -p "$work/find/sub" && printf 'found\n' > "$work/find/sub/needle" || return 1
            /usr/bin/find "$work/find" -type f -name needle |
                /bin/busybox grep -Fxq "$work/find/sub/needle"
            ;;
        rsync)
            mkdir -p "$work/rsync-src" "$work/rsync-dst" || return 1
            printf 'm204-rsync-ok\n' > "$work/rsync-src/input" || return 1
            rsync -a "$work/rsync-src/" "$work/rsync-dst/" &&
                /bin/busybox cmp -s "$work/rsync-src/input" "$work/rsync-dst/input"
            ;;
        patch)
            printf 'before\n' > "$work/patch.txt" || return 1
            printf '%s\n' '--- patch.txt' '+++ patch.txt' '@@ -1 +1 @@' \
                '-before' '+after' > "$work/change.diff" || return 1
            (cd "$work" && patch -s -p0 < change.diff) &&
                /bin/busybox grep -Fxq after "$work/patch.txt"
            ;;
        openssl)
            printf 'm204-openssl\n' > "$work/openssl-input" || return 1
            actual=$(openssl dgst -sha256 "$work/openssl-input") || return 1
            digest=$(printf '%s\n' "$actual" | /bin/busybox sed 's/^.*= //')
            expected=$(/bin/busybox sha256sum "$work/openssl-input") || return 1
            case "$expected" in "$digest"' '*) return 0 ;; *) return 1 ;; esac
            ;;
        *) echo "[M204 FAIL] unknown workload $1" >&2; return 1 ;;
    esac
}

if [ "$phase" = exercise ]; then
    [ "$#" -eq 2 ] || exit 2
    functional "$2"
    exit $?
fi
test -r "$manifest" || { echo '[M204 FAIL] missing signed-root overlay manifest' >&2; exit 1; }
if [ "$phase" = first ]; then
    test ! -e "$first" || { echo '[M204 FAIL] preseeded first ledger' >&2; exit 1; }
    mkdir -p "$work/logs" || exit 1
    # One official signed index refresh, not twelve --no-cache downloads.
    # If this fails, the cause (WAN, DNS, TLS, disk, or ABI) is NOT inferred.
    if ! /bin/busybox timeout 120 apk update > "$work/logs/apk-update.log" 2>&1; then
        echo '[M204 APK INDEX FAIL] repository or guest cause needs diagnosis' >&2
        /bin/busybox tail -n 15 "$work/logs/apk-update.log" >&2 || :
        exit 1
    fi
else
    test -s "$first" || { echo '[M204 FAIL] first ledger not durable' >&2; exit 1; }
    test ! -e "$reopen" || { echo '[M204 FAIL] preseeded reopen ledger' >&2; exit 1; }
fi
: > "$ledger" || exit 1

total=0
while IFS="$tab" read -r category package test_id || [ -n "$category" ]; do
    case "$category" in ''|'#'*) continue ;; esac
    total=$((total + 1))
    if [ "$package" != "$test_id" ]; then
        test "$package:$test_id" = procps-ng:procps || {
            echo '[M204 FAIL] manifest workload mismatch' >&2; exit 1;
        }
    fi
    if [ "$phase" = first ]; then
        if apk info -e "$package" >/dev/null 2>&1; then
            record "$category" "$package" install PREEXISTING
        elif /bin/busybox timeout 150 apk add "$package" \
                > "$work/logs/$package.apk.log" 2>&1; then
            record "$category" "$package" install PASS
        else
            record "$category" "$package" install FAIL
            error_tail "$package" install "$work/logs/$package.apk.log"
            record "$category" "$package" functional BLOCKED
            continue
        fi
        if /bin/busybox timeout 45 sh /root/m204-software-workload.sh \
                --exercise "$test_id" > "$work/logs/$package.work.log" 2>&1; then
            record "$category" "$package" functional PASS
        else
            record "$category" "$package" functional FAIL
            error_tail "$package" functional "$work/logs/$package.work.log"
        fi
    else
        if /bin/busybox grep -Fq "M204_RESULT${tab}${category}${tab}${package}${tab}install${tab}FAIL" "$first"; then
            record "$category" "$package" reopen BLOCKED
        elif ! apk info -e "$package" >/dev/null 2>&1; then
            record "$category" "$package" reopen FAIL
        elif /bin/busybox timeout 45 sh /root/m204-software-workload.sh \
                --exercise "$test_id" > "$work/logs/$package.reopen.log" 2>&1; then
            record "$category" "$package" reopen PASS
        else
            record "$category" "$package" reopen FAIL
            error_tail "$package" reopen "$work/logs/$package.reopen.log"
        fi
    fi
done < "$manifest"

test "$total" -eq 12 || { echo "[M204 FAIL] wrong cohort count: $total" >&2; exit 1; }
test "$(/bin/busybox wc -l < "$ledger")" -ge 12 || exit 1
sync || exit 1
if [ "$phase" = first ]; then
    echo M204_FIRST_DISCOVERY_COMPLETE
else
    echo M204_REOPEN_DISCOVERY_COMPLETE
fi
