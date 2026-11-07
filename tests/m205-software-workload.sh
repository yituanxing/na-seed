#!/bin/sh
# Discovery only: run genuine signed Alpine ELFs and record every failure.
# A successful binary launch, usage banner or timeout is never a semantic PASS.
# The M197 owner supplies live DHCP/public APK trust and two independent QEMUs.
set -u
export PATH=/usr/sbin:/sbin:/usr/bin:/bin HOME=/root LC_ALL=C LANG=C

case "${NAS_M213_COMPAT_COHORT:-0}:${NAS_M206_COMPAT_COHORT:-0}:${NAS_M215_PILOT:-0}:${NAS_M218_PILOT:-0}:${NAS_M219_PILOT:-0}:${NAS_M220_PILOT:-0}:${NAS_M221_PILOT:-0}:${NAS_M222_PILOT:-0}:${NAS_M223_PILOT:-0}" in
    0:0:0:0:0:0:0:0:0)
        milestone=M205
        manifest=/root/m205-cohort.tsv
        work=/root/m205-work
        first=/root/m205-first-results.tsv
        reopen=/root/m205-reopen-results.tsv
        exercise_script=/root/m205-software-workload.sh
        ;;
    0:1:0:0:0:0:0:0:0)
        milestone=M206
        manifest=/root/m206-cohort.tsv
        work=/root/m206-work
        first=/root/m206-first-results.tsv
        reopen=/root/m206-reopen-results.tsv
        exercise_script=/root/m206-software-workload.sh
        ;;
    1:0:0:0:0:0:0:0:0)
        milestone=M213
        manifest=/root/m213-cohort.tsv
        work=/root/m213-work
        first=/root/m213-first-results.tsv
        reopen=/root/m213-reopen-results.tsv
        exercise_script=/root/m213-software-workload.sh
        ;;
    0:0:1:0:0:0:0:0:0)
        # The M215 pilot contains ONLY the five genuinely executable M213
        # cases; the other 15 remain blocked and cannot inflate acceptance.
        milestone=M215
        manifest=/root/m215-cohort.tsv
        work=/root/m215-work
        first=/root/m215-first-results.tsv
        reopen=/root/m215-reopen-results.tsv
        exercise_script=/root/m213-software-workload.sh
        ;;
    0:0:0:1:0:0:0:0:0)
        milestone=M218
        manifest=/root/m218-cohort.tsv
        work=/root/m218-work
        first=/root/m218-first-results.tsv
        reopen=/root/m218-reopen-results.tsv
        exercise_script=/root/m218-ifstat-workload.sh
        ;;
    0:0:0:0:1:0:0:0:0)
        milestone=M219
        manifest=/root/m219-cohort.tsv
        work=/root/m219-work
        first=/root/m219-first-results.tsv
        reopen=/root/m219-reopen-results.tsv
        exercise_script=/root/m219-utmpdump-workload.sh
        ;;
    0:0:0:0:0:1:0:0:0)
        milestone=M220
        manifest=/root/m220-cohort.tsv
        work=/root/m220-work
        first=/root/m220-first-results.tsv
        reopen=/root/m220-reopen-results.tsv
        exercise_script=/root/m220-nping-workload.sh
        ;;
    0:0:0:0:0:0:1:0:0)
        milestone=M221
        manifest=/root/m221-cohort.tsv
        work=/root/m221-work
        first=/root/m221-first-results.tsv
        reopen=/root/m221-reopen-results.tsv
        exercise_script=/root/m221-dns-update-workload.sh
        ;;
    0:0:0:0:0:0:0:1:0)
        milestone=M222
        manifest=/root/m222-cohort.tsv
        work=/root/m222-work
        first=/root/m222-first-results.tsv
        reopen=/root/m222-reopen-results.tsv
        exercise_script=/root/m222-ssh-workload.sh
        ;;
    0:0:0:0:0:0:0:0:1)
        milestone=M223
        manifest=/root/m223-cohort.tsv
        work=/root/m223-work
        first=/root/m223-first-results.tsv
        reopen=/root/m223-reopen-results.tsv
        exercise_script=/root/m223-http-workload.sh
        ;;
    *) echo '[COMPAT ERROR] invalid/overlapping signed Alpine cohort selector' >&2; exit 2 ;;
esac
tab=$(printf '\t')

case "${1:-}" in
    --first) phase=first; ledger=$first ;;
    --reopen) phase=reopen; ledger=$reopen ;;
    --exercise) phase=exercise ;;
    *) echo "[$milestone ERROR] usage: --first|--reopen|--exercise CASE PATH" >&2; exit 2 ;;
esac

record() {
    printf '%s_RESULT\t%s\t%s\t%s\t%s\n' "$milestone" "$1" "$2" "$3" "$4" |
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
            printf '%s_ERROR\t%s\t%s\t%s\t%s\n' \
                "$milestone" "$package" "$elf" "$stage" "$detail"
        done
}

# The guest writes probe output to persistent ext4, but the CI artifact does
# not contain that scratch directory. On a genuine functional failure,
# preserve at most two bounded, printable ORIGINAL command-output lines in
# the existing --exercise log; its owner will attach the proper package and
# ELF identity via M205_ERROR. This never changes PASS classification.
probe_failure_excerpt() {
    pname=$1 plog=$2
    if [ ! -s "$plog" ]; then
        printf 'M205 %s: original command produced no output\n' "$pname"
        return
    fi
    /bin/busybox tail -n 2 "$plog" 2>/dev/null |
        /bin/busybox tr -cd '\11\12\15\40-\176' |
        /bin/busybox cut -c 1-160 |
        while IFS= read -r line; do
            [ -n "$line" ] && printf 'M205 %s: %s\n' "$pname" "$line"
        done
}

# iproute2's original lnstat main() returns 1 even after successfully
# printing a sample; rtstat is the same executable selected by argv[0].
# Verify real parsed Linux counters, not an exit code, banner, or a random
# digit in a loader error. The first row names the source; the second names
# fields; the fourth contains formatted decimal counter values.
lnstat_parsed_rt_cache_ok() {
    table=$1
    /bin/busybox head -n 1 "$table" | /bin/busybox grep -Fq 'rt_cache|' &&
        /bin/busybox sed -n '2p' "$table" |
            /bin/busybox grep -Eq 'entries[|]' &&
        /bin/busybox sed -n '2p' "$table" |
            /bin/busybox grep -Eq 'in_hit[|]' &&
        /bin/busybox sed -n '4p' "$table" |
            /bin/busybox grep -Eq '^[[:space:]]*[0-9]+[|][[:space:]]*[0-9]+[|]'
}

# The original reader consumes seventeen hexadecimal rt_cache fields from
# /proc/net/stat/rt_cache.  The content must be supplied by NaSeed's procfs,
# not by a copied userspace fixture or invented route-cache activity.
lnstat_kernel_rt_cache_ok() {
    test -r /proc/net/stat/rt_cache &&
        /bin/busybox awk '
            NR == 1 {if (NF != 17 || $1 != "entries" || $2 != "in_hit") exit 1}
            NR == 2 {
                if (NF != 17) exit 1
                for (i = 1; i <= NF; i++)
                    if ($i !~ /^[0-9a-fA-F]+$/) exit 1
            }
            END {if (NR != 2) exit 1}
        ' /proc/net/stat/rt_cache
}

# When invoked as `rtstat`, the original iproute2 binary must select the
# rt_cache family alone; never accept an all-family lnstat header instead.
rtstat_only_rt_cache_ok() {
    /bin/busybox awk -F '[|]' '
        NR == 1 {
            if (NF != 18) exit 1
            for (i = 1; i <= 17; i++)
                if ($i != "rt_cache") exit 1
        }
        END {if (NR < 4) exit 1}
    ' "$1"
}

# A missing large DSO may first emit "Error loading shared library" and
# then hundreds of follow-up "Error relocating" lines.  The shared M205
# report keeps only the final three stderr lines, so expose the ORIGINAL
# earliest loader error alongside the last relocation when an LLVM probe
# actually fails. No replacement ELF, inferred success or fake output.
llvm_loader_failure_excerpt() {
    loader_log=$1
    if [ ! -s "$loader_log" ]; then
        echo 'M205 LLVM: original command failed with empty stderr' >&2
        return
    fi
    first_loader_error=$(/bin/busybox grep -iE \
        'error loading shared library|error relocating' "$loader_log" \
        2>/dev/null | /bin/busybox head -n 1)
    [ -n "$first_loader_error" ] ||
        first_loader_error=$(/bin/busybox head -n 1 "$loader_log")
    last_loader_error=$(/bin/busybox tail -n 1 "$loader_log")
    first_loader_error=$(printf '%s' "$first_loader_error" |
        /bin/busybox tr '\t\r' '  ' | /bin/busybox cut -c 1-160)
    last_loader_error=$(printf '%s' "$last_loader_error" |
        /bin/busybox tr '\t\r' '  ' | /bin/busybox cut -c 1-160)
    printf 'M205 LLVM first original loader error: %s\n' "$first_loader_error" >&2
    printf 'M205 LLVM last original loader error: %s\n' "$last_loader_error" >&2
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
            if ! /usr/bin/llvm21-symbolizer --obj="$fixture" "$addr" \
                    > "$target/out" 2> "$target/llvm.stderr"; then
                llvm_loader_failure_excerpt "$target/llvm.stderr"
                return 1
            fi
            /bin/busybox grep -Fq m205_symbol "$target/out" &&
                /bin/busybox grep -Fq m205_symbol.c "$target/out"
            ;;
        coff-resource-conversion:usr/bin/llvm21-windres)
            # The checked RCDATA fixture has no macros/includes.  LLVM's
            # original windres --no-preprocess option runs the real resource
            # parser/compiler without requiring a separately installed
            # clang driver.  The emitted .res bytes are still verified below.
            printf '%s\n' '1 RCDATA { "M205-RESOURCE-CONTENTS" }' > "$target/resource.rc"
            if ! /usr/bin/llvm21-windres --no-preprocess -i "$target/resource.rc" \
                    -o "$target/resource.res" -O res \
                    2> "$target/llvm.stderr"; then
                llvm_loader_failure_excerpt "$target/llvm.stderr"
                return 1
            fi
            test -s "$target/resource.res" &&
                /bin/busybox grep -aq M205-RESOURCE-CONTENTS "$target/resource.res"
            ;;
        link-counter-query:sbin/lnstat)
            lnstat_rc=0
            /sbin/lnstat -c 1 > "$target/out" 2> "$target/err" || lnstat_rc=$?
            if [ "$lnstat_rc" -eq 1 ] &&
               lnstat_parsed_rt_cache_ok "$target/out" &&
               lnstat_kernel_rt_cache_ok; then
                return 0
            fi
            printf 'M205 lnstat: original exit=%s or invalid parsed rt_cache table\n' "$lnstat_rc" >&2
            probe_failure_excerpt 'lnstat stderr' "$target/err" >&2
            probe_failure_excerpt 'lnstat stdout' "$target/out" >&2
            /bin/busybox ls -l /proc/net/stat > "$target/dir" 2>&1 || :
            probe_failure_excerpt 'lnstat stat directory' "$target/dir" >&2
            return 1
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
                    # This exact marker survives an outer 45s timeout if
                    # original ss parks in recvmsg before any error text.
                    echo 'M205 ss: real listener ready; querying original ss -ltn' >&2
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
                probe_failure_excerpt 'ss listener' "$target/server.log" >&2
                probe_failure_excerpt 'ss stderr' "$target/ss.err" >&2
                probe_failure_excerpt 'ss output' "$target/sockets" >&2
            fi
            return "$found"
            ;;
        route-stat-query:sbin/rtstat)
            rtstat_rc=0
            /sbin/rtstat -c 1 > "$target/out" 2> "$target/err" || rtstat_rc=$?
            if [ "$rtstat_rc" -eq 1 ] &&
               lnstat_parsed_rt_cache_ok "$target/out" &&
               lnstat_kernel_rt_cache_ok &&
               rtstat_only_rt_cache_ok "$target/out"; then
                return 0
            fi
            printf 'M205 rtstat: original exit=%s or invalid rt_cache-only table\n' "$rtstat_rc" >&2
            probe_failure_excerpt 'rtstat stderr' "$target/err" >&2
            probe_failure_excerpt 'rtstat stdout' "$target/out" >&2
            /bin/busybox head -n 2 /proc/net/stat/rt_cache \
                > "$target/rt_cache" 2>&1 || :
            probe_failure_excerpt 'rtstat rt_cache' "$target/rt_cache" >&2
            return 1
            ;;
        bounded-loopback-traffic:usr/sbin/iftop)
            # Upstream iftop's -t -s 3 timer advances from the packet handler,
            # not an independent timer: it can capture real traffic and still
            # wait forever if the last packet arrives before the 3s deadline.
            # The previous ICMP stimulus never sent packets: NaSeed correctly
            # rejects unsupported AF_INET/SOCK_RAW, so BusyBox ping aborted.
            # Exercise the REAL eth0 path with UDP/DNS through supported lwIP
            # SOCK_DGRAM for >3s, and require all 40 actual sends as well as
            # the unmodified signed Alpine iftop's final capture output.
            /root/m205-fixtures/udp-traffic > "$target/traffic" 2>&1 &
            traffic_pid=$!
            /bin/busybox timeout 18 /usr/sbin/iftop -t -s 3 -i eth0 \
                > "$target/out" 2>&1
            capture_rc=$?
            traffic_rc=0
            wait "$traffic_pid" || traffic_rc=$?
            if test "$capture_rc" -eq 0 &&
               test "$traffic_rc" -eq 0 &&
               /bin/busybox grep -Fxq \
                    'M205_REAL_UDP_SENT=40 dst=10.0.2.3:53' "$target/traffic" &&
               /bin/busybox grep -Fq '10.0.2.' "$target/out"; then
                return 0
            fi
            printf 'M205 iftop: original exit=%s, UDP stimulus exit=%s, no verified live wire capture\n' \
                "$capture_rc" "$traffic_rc" >&2
            probe_failure_excerpt iftop "$target/out" >&2
            probe_failure_excerpt 'iftop real UDP traffic' "$target/traffic" >&2
            return 1
            ;;
        controlled-ssh-authentication:usr/bin/sshpass)
            # A controlled PTY password exchange is genuine sshpass behavior;
            # do not misreport it as an SSH server authentication test.
            # Real sshpass only makes the slave a *controlling tty*: it does
            # not dup2 it onto stdin/stdout.  The child must use /dev/tty
            # explicitly just like OpenSSH.  stdin here is /dev/null and
            # stdout is a persistent diagnostic file, not the PTY master.
            /usr/bin/sshpass -P 'Password:' -p m205-secret /bin/sh -c \
                'exec 3<>/dev/tty || exit 21; printf "Password:" >&3; IFS= read -r -t 8 reply <&3; test "$reply" = m205-secret' \
                > "$target/session" 2>&1 && return 0
            echo 'M205 sshpass: original PTY password exchange failed' >&2
            probe_failure_excerpt sshpass "$target/session" >&2
            return 1
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
            # btop is interactive; neither --help nor a timeout is an
            # acceptance oracle. The real PTY fixture verifies kernel
            # TIOCSWINSZ (120x40), captures CPU/MEM ANSI display bytes,
            # sends the raw q key only after it paints, and waits for the
            # original Alpine btop to exit successfully. Our test environment
            # fixes LC_ALL=C for parseable diagnostics; original btop 1.4.5
            # explicitly requires a UTF-8 locale unless --force-utf is set.
            # The PTY itself is a genuine xterm-256color terminal; use that
            # documented startup option rather than skipping real UI checks.
            test -x /root/m205-fixtures/pty-exchange || return 1
            # Alpine btop 1.4.5 records its REAL startup failure in
            # $XDG_STATE_HOME/btop.log. Give each boot a clean state path so
            # a failed run cannot surface stale diagnostics from another boot.
            mkdir -p "$target/btop-state" || return 1
            if XDG_STATE_HOME="$target/btop-state" \
                /bin/busybox timeout 40 /root/m205-fixtures/pty-exchange \
                --screen "$target/btop-terminal.raw" /usr/bin/btop --force-utf; then
                return 0
            fi
            if test -s "$target/btop-state/btop.log"; then
                /bin/busybox tail -n 2 "$target/btop-state/btop.log" |
                    /bin/busybox tr -cd '\11\12\15\40-\176' |
                    /bin/busybox cut -c 1-160 |
                    while IFS= read -r line; do
                        test -n "$line" && printf 'M205 btop original log: %s\n' "$line" >&2
                    done
            else
                echo 'M205 btop: original program did not create its log file' >&2
            fi
            return 1
            ;;
        process-accounting-snapshot:usr/bin/atop-2.12.1)
            # Original atop's -b is a *begin time* for raw-log replay, not
            # "batch" mode. Live parseable CPU records use -P CPU 1 1.
            /usr/bin/atop-2.12.1 -P CPU 1 1 > "$target/out" &&
                /bin/busybox grep -Eq '^CPU ' "$target/out"
            ;;
        bandwidth-monitor-snapshot:usr/bin/bmon)
            # Read actual eth0 packet-rate metrics using bmon's original
            # format backend. A banner/exit code or timeout is not proof.
            monitor_rc=0
            /bin/busybox timeout 8 /usr/bin/bmon -p eth0 \
                -o 'format:fmt=$(element:name) $(attr:rxrate:packets)\n' \
                > "$target/counters" 2> "$target/bmon.err" || monitor_rc=$?
            if /bin/busybox grep -Eq '^eth0[[:space:]]+[0-9]' "$target/counters"; then
                return 0
            fi
            printf 'M205 bmon: original exit=%s, no verified eth0 counters\n' "$monitor_rc" >&2
            probe_failure_excerpt 'bmon stderr' "$target/bmon.err" >&2
            probe_failure_excerpt 'bmon output' "$target/counters" >&2
            return 1
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
    if [ "$milestone" != M205 ]; then
        sh "$exercise_script" --exercise "$2" "$3"
    else
        probe "$2" "$3"
    fi
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
expected_rows=20
[ "$milestone" = M215 ] && expected_rows=5
[ "$milestone" = M218 ] && expected_rows=1
[ "$milestone" = M219 ] && expected_rows=1
[ "$milestone" = M220 ] && expected_rows=1
[ "$milestone" = M221 ] && expected_rows=2
[ "$milestone" = M222 ] && expected_rows=3
[ "$milestone" = M223 ] && expected_rows=3
test "$cohort_rows" -eq "$expected_rows" || {
    echo "[$milestone ERROR] expected $expected_rows guest cohort rows, got $cohort_rows" >&2
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
if [ "$phase" = first ] && [ "$milestone" = M205 ]; then
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
if [ "$milestone" = M205 ]; then
    if signed_pty_available; then
        echo M205_SIGNED_PTY_HELPER_READY
    else
        echo M205_SIGNED_PTY_HELPER_UNAVAILABLE
    fi
fi
: > "$ledger" || exit 1
export NAS_M205_BOOT_PHASE="$phase"
export NAS_M206_BOOT_PHASE="$phase"
export NAS_M213_BOOT_PHASE="$phase"
export NAS_M218_BOOT_PHASE="$phase"
export NAS_M219_BOOT_PHASE="$phase"
export NAS_M220_BOOT_PHASE="$phase"
export NAS_M221_BOOT_PHASE="$phase"
export NAS_M222_BOOT_PHASE="$phase"
export NAS_M223_BOOT_PHASE="$phase"
# M218/M220/M221 network probes must wait for DHCP on the independent second
# CPIO boot. The other cohort budgets remain unchanged; this is not a
# blanket timeout increase or a bypass of original ELF semantics.
exercise_budget=45
[ "$milestone" = M218 ] && exercise_budget=165
[ "$milestone" = M220 ] && exercise_budget=140
[ "$milestone" = M221 ] && exercise_budget=140
[ "$milestone" = M222 ] && exercise_budget=240
[ "$milestone" = M223 ] && exercise_budget=180

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
        elif [ "$milestone" != M222 ] && [ "$milestone" != M223 ] && apk info -e "$package" >/dev/null 2>&1; then
            # Already installed by an earlier signed APK dependency in this
            # exact M205 first boot; it was absent from the baseline. M222/M223
            # must use the real offline apk add below to explicitly register
            # even transitive dependencies in /etc/apk/world.
            install_result=PASS
            printf '%s\n' "$install_result" > "$status_file"
        else
            # llvm21 unpacks large signed C++ DSOs on emulated ext4. The
            # 150-second budget used for small utilities timed out midway
            # through its genuine x86 install (exit 143), leaving both
            # LLVM entrypoints blocked. Keep a finite package-specific
            # limit; a timed-out transaction must stay FAIL, never PASS.
            install_budget=150
            case "$package" in llvm21) install_budget=600 ;; esac
            if /bin/busybox timeout "$install_budget" apk --cache-dir /var/cache/apk --no-network -v add "$package" \
                > "$work/logs/$package.apk.log" 2>&1; then
                install_result=PASS
                printf '%s\n' "$install_result" > "$status_file"
            else
                install_rc=$?
                install_result=FAIL
                printf '%s\n' "$install_result" > "$status_file"
                printf '%s_ERROR\t%s\t%s\tinstall\tapk exit=%s (%ss budget)\n' \
                    "$milestone" "$package" "$elf" "$install_rc" "$install_budget"
                error_tail "$package" "$elf" install "$work/logs/$package.apk.log"
                # Error lines can precede a successful BusyBox trigger, so
                # tail alone hides the original failure. This diagnostic is
                # never parsed as a semantic program result.
                /bin/busybox grep -iE 'error|failed|not supported|not found' \
                    "$work/logs/$package.apk.log" 2>/dev/null |
                    /bin/busybox head -n 3 |
                    while IFS= read -r detail; do
                        detail=$(printf '%s' "$detail" | /bin/busybox tr '\t\r' '  ' |
                            /bin/busybox cut -c 1-180)
                        printf '%s_APK_DIAG\t%s\t%s\n' "$milestone" "$package" "$detail"
                    done
            fi
        fi
        record "$package" "$elf" install "$install_result"
        if [ "$install_result" = FAIL ]; then
            record "$package" "$elf" functional BLOCKED
            continue
        fi
        if /bin/busybox timeout "$exercise_budget" sh "$exercise_script" \
                --exercise "$case_id" "$elf" \
                > "$work/logs/$case_id.first.log" 2>&1; then
            record "$package" "$elf" functional PASS
        else
            record "$package" "$elf" functional FAIL
            error_tail "$package" "$elf" functional "$work/logs/$case_id.first.log"
        fi
    else
        if /bin/busybox grep -Fxq \
                "${milestone}_RESULT${tab}${package}${tab}${elf}${tab}install${tab}FAIL" "$first"; then
            record "$package" "$elf" reopen BLOCKED
        elif ! apk info -e "$package" >/dev/null 2>&1; then
            record "$package" "$elf" reopen FAIL
            echo "${milestone}_ERROR${tab}${package}${tab}${elf}${tab}reopen${tab}APK database missing package"
        elif /bin/busybox timeout "$exercise_budget" sh "$exercise_script" \
                --exercise "$case_id" "$elf" \
                > "$work/logs/$case_id.reopen.log" 2>&1; then
            record "$package" "$elf" reopen PASS
        else
            record "$package" "$elf" reopen FAIL
            error_tail "$package" "$elf" reopen "$work/logs/$case_id.reopen.log"
        fi
    fi
done 9< "$manifest"

test "$total" -eq "$expected_rows" || {
    echo "[$milestone ERROR] wrong cohort count: $total" >&2; exit 1;
}
sync || exit 1
if [ "$phase" = first ]; then
    echo "${milestone}_FIRST_DISCOVERY_COMPLETE"
else
    echo "${milestone}_REOPEN_DISCOVERY_COMPLETE"
fi
