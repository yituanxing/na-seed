#!/bin/sh
# Discovery only: run genuine signed Alpine ELFs and record every failure.
# A successful binary launch, usage banner or timeout is never a semantic PASS.
# The M197 owner supplies live DHCP/public APK trust and two independent QEMUs.
set -u
export PATH=/usr/sbin:/sbin:/usr/bin:/bin HOME=/root LC_ALL=C LANG=C

manifest=/root/m205-cohort.tsv
work=/root/m205-work
first=/root/m205-first-results.tsv
reopen=/root/m205-reopen-results.tsv
tab=$(printf '\t')

case "${1:-}" in
    --first) phase=first; ledger=$first ;;
    --reopen) phase=reopen; ledger=$reopen ;;
    --exercise) phase=exercise ;;
    *) echo '[M205 ERROR] usage: --first|--reopen|--exercise CASE PATH' >&2; exit 2 ;;
esac

record() {
    printf 'M205_RESULT\t%s\t%s\t%s\t%s\n' "$1" "$2" "$3" "$4" |
        /bin/busybox tee -a "$ledger"
}

error_tail() {
    # Keep native command stderr accessible without the full ext4 disk image.
    package=$1 elf=$2 stage=$3 log=$4
    # Install also emits a precise exit-code line, so at most three stderr
    # lines keep us below the host report's four-error-entry maximum.
    /bin/busybox tail -n 3 "$log" 2>/dev/null |
        while IFS= read -r line; do
            detail=$(printf '%s' "$line" | /bin/busybox tr '\t\r' '  ' |
                /bin/busybox sed 's/\[FAIL\]/[failed]/g' |
                /bin/busybox cut -c 1-180)
            printf 'M205_ERROR\t%s\t%s\t%s\t%s\n' \
                "$package" "$elf" "$stage" "$detail"
        done
}

# Independently verify the original signed util-linux-misc PTY helper was
# installed; it is not a target-ELF acceptance entry. Interactive probes use
# a separate host-built native fixture to drive the kernel's real ptmx/pts
# interface, since NaSeed does not yet support named FIFO (mknodat).
signed_pty_available() {
    test -x /usr/bin/script || return 1
    apk info -e util-linux-misc >/dev/null 2>&1 || return 1
    apk info -W /usr/bin/script 2>/dev/null |
        /bin/busybox grep -Fq util-linux-misc
}

pty_exchange() {
    pty_dir=$1 pty_prompt1=$2 pty_reply1=$3
    pty_prompt2=$4 pty_reply2=$5
    shift 5
    test -x /root/m205-fixtures/pty-exchange || {
        echo 'M205 missing host-built real PTY exchange fixture' >&2
        return 1
    }
    # The fixture forks the REAL original Alpine command with a controlling
    # slave, waits for prompts from the master, sends replies, and verifies
    # its exit status. No result is synthetic and no shell FIFO is needed.
    /bin/busybox timeout 40 /root/m205-fixtures/pty-exchange \
        "$pty_dir/pty-output" "$pty_prompt1" "$pty_reply1" \
        "$pty_prompt2" "$pty_reply2" "$@"
}

probe() {
    case_id=$1 elf=$2
    # The manifest path is not permitted to point at BusyBox, an absent tool
    # or a non-ELF script. Target ISA/SHA was checked against signed packages
    # on the host, this checks the actual file from the guest's APK database.
    test -x "/$elf" || { echo "M205 missing executable: $elf" >&2; return 1; }
    magic=$(/bin/busybox od -An -tx1 -N4 "/$elf" 2>/dev/null |
            /bin/busybox tr -d ' \t\r\n')
    test "$magic" = 7f454c46 || { echo "M205 not an ELF: $elf" >&2; return 1; }
    # Never accept yesterday's generated output as this boot's semantic
    # result; independent QEMU boots use separate target subdirectories.
    target="$work/$case_id.${NAS_M205_BOOT_PHASE:-manual}"
    mkdir -p "$target" || return 1
    case "$case_id:$elf" in
        zone-roundtrip:usr/bin/named-compilezone)
            printf '%s\n' '$TTL 3600' \
                '@ IN SOA ns.example.test. hostmaster.example.test. 2026110501 3600 900 604800 60' \
                '@ IN NS ns.example.test.' 'ns IN A 192.0.2.53' \
                'www IN A 192.0.2.42' > "$target/zone"
            /usr/bin/named-compilezone -F text -o "$target/compiled" \
                example.test "$target/zone" > "$target/compile.log" 2>&1 &&
                /bin/busybox grep -Fq 192.0.2.42 "$target/compiled" &&
                /usr/bin/named-checkzone example.test "$target/compiled" |
                    /bin/busybox grep -Fq OK
            ;;
        signed-key-generation:usr/sbin/tsig-keygen)
            /usr/sbin/tsig-keygen -a hmac-sha256 m205-tsig > "$target/tsig" &&
                /bin/busybox grep -Fq m205-tsig "$target/tsig" &&
                /bin/busybox grep -Fq hmac-sha256 "$target/tsig" &&
                /bin/busybox grep -Fq secret "$target/tsig"
            ;;
        debug-symbol-resolution:usr/bin/llvm21-symbolizer)
            # No compiler is installed in the runtime APK set. The host must
            # supply a genuine target-ISA DWARF ELF and its symbol address.
            fixture=/root/m205-fixtures/symbolizer.elf
            addr_file=/root/m205-fixtures/symbolizer.addr
            if [ ! -s "$fixture" ] || [ ! -s "$addr_file" ]; then
                echo 'M205 missing genuine target-ISA DWARF fixture' >&2
                return 1
            fi
            addr=$(/bin/busybox head -n 1 "$addr_file")
            test -n "$addr" || return 1
            /usr/bin/llvm21-symbolizer --obj="$fixture" "$addr" > "$target/out" &&
                /bin/busybox grep -Fq m205_symbol "$target/out" &&
                /bin/busybox grep -Fq m205_symbol.c "$target/out"
            ;;
        coff-resource-conversion:usr/bin/llvm21-windres)
            printf '%s\n' '1 RCDATA { "M205-RESOURCE-CONTENTS" }' > "$target/resource.rc"
            /usr/bin/llvm21-windres -i "$target/resource.rc" \
                -o "$target/resource.res" -O res &&
                test -s "$target/resource.res" &&
                /bin/busybox grep -aq M205-RESOURCE-CONTENTS "$target/resource.res"
            ;;
        link-counter-query:sbin/lnstat)
            /sbin/lnstat -c 1 > "$target/out" &&
                /bin/busybox grep -Eq '[0-9]' "$target/out"
            ;;
        live-socket-list:sbin/ss)
            # Query an actual TCP LISTEN socket, not an availability banner.
            # Alpine BusyBox need not contain the optional httpd applet.
            /root/m205-fixtures/loopback-listener \
                > "$target/server.log" 2>&1 &
            server_pid=$!
            n=0; ready=1; found=1
            while [ "$n" -lt 6 ]; do
                if /bin/busybox grep -Fxq M205_LOOPBACK_LISTEN_READY \
                        "$target/server.log"; then
                    ready=0; break
                fi
                kill -0 "$server_pid" 2>/dev/null || break
                n=$((n+1)); sleep 1
            done
            if [ "$ready" -eq 0 ]; then
                n=0
                while [ "$n" -lt 6 ]; do
                    if /sbin/ss -ltn > "$target/sockets" 2> "$target/ss.err" &&
                        /bin/busybox grep -Fq ':18085' "$target/sockets"; then
                        found=0; break
                    fi
                    n=$((n+1)); sleep 1
                done
            fi
            kill -TERM "$server_pid" 2>/dev/null || :
            n=0
            while kill -0 "$server_pid" 2>/dev/null && [ "$n" -lt 3 ]; do
                n=$((n+1)); sleep 1
            done
            kill -KILL "$server_pid" 2>/dev/null || :
            wait "$server_pid" 2>/dev/null || :
            if [ "$found" -ne 0 ]; then
                echo 'M205 live TCP socket probe failed' >&2
                /bin/busybox tail -n 3 "$target/server.log" >&2 || :
                /bin/busybox tail -n 3 "$target/ss.err" >&2 || :
                /bin/busybox tail -n 3 "$target/sockets" >&2 || :
            fi
            return "$found"
            ;;
        route-stat-query:sbin/rtstat)
            /sbin/rtstat -c 1 > "$target/out" &&
                /bin/busybox grep -Eq '[0-9]' "$target/out"
            ;;
        bounded-loopback-traffic:usr/sbin/iftop)
            # The tool must capture actual traffic, not display --help.
            /bin/busybox ping -c 1 -W 2 10.0.2.2 > "$target/ping" 2>&1 || :
            /usr/sbin/iftop -t -s 3 -i eth0 > "$target/out" 2>&1 &&
                /bin/busybox grep -Fq '10.0.2.' "$target/out"
            ;;
        controlled-ssh-authentication:usr/bin/sshpass)
            # A controlled PTY password exchange is genuine sshpass behavior;
            # do not misreport it as an SSH server authentication test.
            /usr/bin/sshpass -P 'Password:' -p m205-secret /bin/sh -c \
                'printf "Password:"; IFS= read -r -t 8 reply; test "$reply" = m205-secret' \
                > "$target/session" 2>&1
            ;;
        awk-field-transformation:usr/bin/goawk)
            printf '12,7\n5,9\n' > "$target/input"
            /usr/bin/goawk -F, '{print $1+$2}' "$target/input" > "$target/out" &&
                printf '19\n14\n' > "$target/expected" &&
                /bin/busybox cmp -s "$target/expected" "$target/out"
            ;;
        c-format-roundtrip:usr/bin/indent)
            # GNU indent may place the function's return type and name on
            # separate lines. Compare the whole token stream instead of a
            # particular whitespace style.
            printf 'int main(void){return(40+2);}\n' > "$target/input.c"
            /usr/bin/indent -st "$target/input.c" > "$target/output.c" &&
                /bin/busybox tr -d '[:space:]' < "$target/input.c" > "$target/expected.tokens" &&
                /bin/busybox tr -d '[:space:]' < "$target/output.c" > "$target/actual.tokens" &&
                /bin/busybox cmp -s "$target/expected.tokens" "$target/actual.tokens"
            ;;
        pty-wrapped-command:usr/bin/rlwrap)
            pty_exchange "$target" \
                M205_RLWRAP_PROMPT m205-line - - \
                /usr/bin/rlwrap /bin/sh -c \
                'printf M205_RLWRAP_PROMPT; IFS= read -r -t 8 answer; test "$answer" = m205-line && printf M205_RLWRAP_OK' &&
                /bin/busybox grep -aFq M205_RLWRAP_OK "$target/pty-output"
            ;;
        path-database-roundtrip:usr/bin/zoxide)
            mkdir -p "$target/visited" "$target/database" || return 1
            export _ZO_DATA_DIR="$target/database"
            /usr/bin/zoxide add "$target/visited" &&
                /usr/bin/zoxide query visited > "$target/query" &&
                /bin/busybox grep -Fxq "$target/visited" "$target/query"
            ;;
        encrypted-zip-roundtrip:usr/bin/zipcloak)
            printf 'M205 ZIP SECRET CONTENTS\n' > "$target/source" || return 1
            (cd "$target" && /usr/bin/zip -q archive.zip source) || return 1
            pty_exchange "$target" 'Enter password:' m205-secret \
                'Verify password:' m205-secret \
                /usr/bin/zipcloak "$target/archive.zip" ||
                return 1
            test -x /usr/bin/unzip || return 1
            /usr/bin/unzip -P m205-secret -p "$target/archive.zip" source \
                > "$target/decrypted" || return 1
            /bin/busybox cmp -s "$target/source" "$target/decrypted" || return 1
            if /usr/bin/unzip -P definitely-wrong -p "$target/archive.zip" source \
                    > "$target/wrong-password" 2>&1; then
                echo 'M205 zipcloak accepted an incorrect password' >&2
                return 1
            fi
            ;;
        zip-stdin-extraction:usr/bin/funzip)
            printf 'M205 ZIP STREAM PAYLOAD\n' > "$target/source"
            (cd "$target" && /usr/bin/zip -q archive.zip source) &&
                /usr/bin/funzip < "$target/archive.zip" > "$target/decoded" &&
                /bin/busybox cmp -s "$target/source" "$target/decoded"
            ;;
        self-extracting-archive:usr/bin/unzipsfx)
            printf 'M205 SELF-EXTRACT PAYLOAD\n' > "$target/source"
            (cd "$target" && /usr/bin/zip -q archive.zip source) || return 1
            /bin/busybox cp /usr/bin/unzipsfx "$target/selfextract" || return 1
            /bin/busybox cat "$target/archive.zip" >> "$target/selfextract" || return 1
            /bin/busybox chmod +x "$target/selfextract" || return 1
            mkdir -p "$target/extract" || return 1
            (cd "$target/extract" && "$target/selfextract" -o \
                > "$target/unpack.log" 2>&1) &&
                /bin/busybox cmp -s "$target/source" "$target/extract/source"
            ;;
        stream-hash-roundtrip:usr/bin/mbuffer)
            printf 'M205 BUFFERED DATA\n' > "$target/source"
            /usr/bin/mbuffer -q -m 1M -i "$target/source" -o "$target/copy" &&
                /bin/busybox cmp -s "$target/source" "$target/copy"
            ;;
        terminal-resource-monitor:usr/bin/btop)
            echo 'M205 btop requires a genuine terminal capture/exit fixture' >&2
            return 1
            ;;
        process-accounting-snapshot:usr/bin/atop-2.12.1)
            /usr/bin/atop-2.12.1 -P CPU -b 1 1 > "$target/out" &&
                /bin/busybox grep -Eq '^CPU ' "$target/out"
            ;;
        bandwidth-monitor-snapshot:usr/bin/bmon)
            # Read actual eth0 packet-rate metrics using bmon's original
            # format backend. A banner/exit code or timeout is not proof.
            /bin/busybox timeout 8 /usr/bin/bmon -p eth0 \
                -o 'format:fmt=$(element:name) $(attr:rxrate:packets)\n' \
                > "$target/counters" 2> "$target/bmon.err" || :
            /bin/busybox grep -Eq '^eth0[[:space:]]+[0-9]' "$target/counters"
            ;;
        *) echo "M205 unknown or mismatched case/ELF: $case_id / $elf" >&2; return 1 ;;
    esac
}

if [ "$phase" = exercise ]; then
    [ "$#" -eq 3 ] || exit 2
    # An exercise receives every input through an explicit file or PTY.
    # Never let an original command accidentally read a parent's UART or
    # the cohort manifest as its standard input.
    exec 0</dev/null
    probe "$2" "$3"
    exit $?
fi

test -r "$manifest" || { echo '[M205 ERROR] missing signed cohort manifest' >&2; exit 1; }
# The original Alpine installer and tools are ordinary programs: some read
# stdin even when called with file arguments. Reserve fd9 for TSV iteration
# and give every child fd0=/dev/null, so a child cannot steal a subsequent
# row as happened in the genuine three-ISA M205 run (19 instead of 20).
exec 0</dev/null
# Refuse a truncated/tampered guest cohort before spending minutes on APK.
cohort_rows=$(/bin/busybox awk -F '\t' '
    $1 != "package" && $1 !~ /^#/ && NF { count++ }
    END { print count+0 }
' "$manifest") || exit 1
test "$cohort_rows" -eq 20 || {
    echo "[M205 ERROR] expected 20 guest cohort rows, got $cohort_rows" >&2
    exit 1
}
if [ "$phase" = first ]; then
    test ! -e "$first" || { echo '[M205 ERROR] preseeded first ledger' >&2; exit 1; }
    test ! -e "$work" || { echo '[M205 ERROR] preseeded work directory' >&2; exit 1; }
    mkdir -p "$work/logs" "$work/packages" || exit 1
    # Snapshot the APK database BEFORE installing anything. An earlier M205
    # package can pull in another cohort package as a signed dependency
    # (iproute2 -> iproute2-ss). That package is newly installed in THIS
    # boot, not PREEXISTING; otherwise 20/20 would become impossible solely
    # because of manifest order. Record the initial state for each package.
    while IFS="$tab" read -r pkg path family check <&9 || [ -n "$pkg" ]; do
        case "$pkg" in ''|'#'*|package) continue ;; esac
        initial="$work/packages/$pkg.initial"
        if [ ! -e "$initial" ]; then
            if apk info -e "$pkg" >/dev/null 2>&1; then
                printf 'PREEXISTING\n' > "$initial"
            else
                printf 'ABSENT\n' > "$initial"
            fi
        fi
    done 9< "$manifest"
else
    test -s "$first" || { echo '[M205 ERROR] missing durable first ledger' >&2; exit 1; }
    test ! -e "$reopen" || { echo '[M205 ERROR] preseeded second ledger' >&2; exit 1; }
    mkdir -p "$work/logs" || exit 1
fi
# ZIP decryption must be available before zipcloak's earlier entry; its
# later funzip/unzipsfx entries share the exact same first-boot install
# record, never count a pre-existing helper as a new signed APK install.
# util-linux-misc is a signed PTY test dependency, NOT a twentieth new ELF.
if [ "$phase" = first ]; then
    # Signed original Alpine packages were predownloaded and SHA-pinned by
    # the host inventory; installation and all scripts still run INSIDE this
    # guest. M197's separate APK transaction remains real public HTTPS.
    test -L /etc/apk/cache &&
        test "$(readlink /etc/apk/cache)" = /var/cache/apk &&
        test -d /var/cache/apk || {
            echo '[M205 CACHE FAIL] guest signed APK cache missing' >&2
            exit 1
        }
    echo M205_APK_SOURCE_OFFICIAL_SIGNED_CACHE_NO_NETWORK
    for helper in unzip util-linux-misc; do
        if apk info -e "$helper" >/dev/null 2>&1; then
            helper_result=PREEXISTING
        elif /bin/busybox timeout 150 apk --cache-dir /var/cache/apk --no-network -v add "$helper" \
                > "$work/logs/$helper.apk.log" 2>&1; then
            helper_result=PASS
        else
            helper_result=FAIL
            # Keep the *first* APK transaction error, not just the final
            # "1 error" summary. The ext4 image is not uploaded to CI.
            printf 'M205_HELPER_DIAG\t%s\t%s\n' "$helper" \
                "$(/bin/busybox grep -iE 'error|failed|not supported|not found' \
                    "$work/logs/$helper.apk.log" 2>/dev/null |
                    /bin/busybox head -n 1 | /bin/busybox cut -c 1-180)"
        fi
        printf 'M205_HELPER\t%s\t%s\n' "$helper" "$helper_result"
        if [ "$helper" = unzip ]; then
            printf '%s\n' "$helper_result" > "$work/packages/unzip.status"
        fi
    done
fi
if signed_pty_available; then
    echo M205_SIGNED_PTY_HELPER_READY
else
    echo M205_SIGNED_PTY_HELPER_UNAVAILABLE
fi
: > "$ledger" || exit 1
export NAS_M205_BOOT_PHASE="$phase"

total=0
while IFS="$tab" read -r package elf family case_id <&9 || [ -n "$package" ]; do
    case "$package" in ''|'#'*|package) continue ;; esac
    total=$((total+1))
    case "$package:$elf:$case_id" in
        *'..'*|*' '*)
            echo '[M205 ERROR] malformed manifest entry' >&2; exit 1 ;;
    esac
    case "$elf" in bin/*|sbin/*|usr/bin/*|usr/sbin/*) ;;
        *) echo '[M205 ERROR] invalid relative ELF path' >&2; exit 1 ;; esac
    if [ "$phase" = first ]; then
        status_file="$work/packages/$package.status"
        if [ -s "$status_file" ]; then
            install_result=$(/bin/busybox head -n 1 "$status_file")
        elif [ "$(/bin/busybox head -n 1 "$work/packages/$package.initial")" = PREEXISTING ]; then
            install_result=PREEXISTING
            printf '%s\n' "$install_result" > "$status_file"
        elif apk info -e "$package" >/dev/null 2>&1; then
            # Already installed by an earlier signed APK dependency in this
            # exact M205 first boot; it was absent from the baseline.
            install_result=PASS
            printf '%s\n' "$install_result" > "$status_file"
        elif /bin/busybox timeout 150 apk --cache-dir /var/cache/apk --no-network -v add "$package" \
                > "$work/logs/$package.apk.log" 2>&1; then
            install_result=PASS
            printf '%s\n' "$install_result" > "$status_file"
        else
            install_rc=$?
            install_result=FAIL
            printf '%s\n' "$install_result" > "$status_file"
            printf 'M205_ERROR\t%s\t%s\tinstall\tapk exit=%s (150s budget)\n' \
                "$package" "$elf" "$install_rc"
            error_tail "$package" "$elf" install "$work/logs/$package.apk.log"
            # Error lines can precede a successful BusyBox trigger, so tail
            # alone hides the original failure. This diagnostic is never
            # parsed as a semantic program result.
            /bin/busybox grep -iE 'error|failed|not supported|not found' \
                "$work/logs/$package.apk.log" 2>/dev/null |
                /bin/busybox head -n 3 |
                while IFS= read -r detail; do
                    detail=$(printf '%s' "$detail" | /bin/busybox tr '\t\r' '  ' |
                        /bin/busybox cut -c 1-180)
                    printf 'M205_APK_DIAG\t%s\t%s\n' "$package" "$detail"
                done
        fi
        record "$package" "$elf" install "$install_result"
        if [ "$install_result" = FAIL ]; then
            record "$package" "$elf" functional BLOCKED
            continue
        fi
        if /bin/busybox timeout 45 sh /root/m205-software-workload.sh \
                --exercise "$case_id" "$elf" \
                > "$work/logs/$case_id.first.log" 2>&1; then
            record "$package" "$elf" functional PASS
        else
            record "$package" "$elf" functional FAIL
            error_tail "$package" "$elf" functional "$work/logs/$case_id.first.log"
        fi
    else
        if /bin/busybox grep -Fxq \
                "M205_RESULT${tab}${package}${tab}${elf}${tab}install${tab}FAIL" "$first"; then
            record "$package" "$elf" reopen BLOCKED
        elif ! apk info -e "$package" >/dev/null 2>&1; then
            record "$package" "$elf" reopen FAIL
            echo "M205_ERROR${tab}${package}${tab}${elf}${tab}reopen${tab}APK database missing package"
        elif /bin/busybox timeout 45 sh /root/m205-software-workload.sh \
                --exercise "$case_id" "$elf" \
                > "$work/logs/$case_id.reopen.log" 2>&1; then
            record "$package" "$elf" reopen PASS
        else
            record "$package" "$elf" reopen FAIL
            error_tail "$package" "$elf" reopen "$work/logs/$case_id.reopen.log"
        fi
    fi
done 9< "$manifest"

test "$total" -eq 20 || { echo "[M205 ERROR] wrong cohort count: $total" >&2; exit 1; }
sync || exit 1
if [ "$phase" = first ]; then
    echo M205_FIRST_DISCOVERY_COMPLETE
else
    echo M205_REOPEN_DISCOVERY_COMPLETE
fi
