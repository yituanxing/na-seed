#!/bin/sh
# Genuine original Alpine M213 probes. NOT wired to native QEMU yet.
# Only proven guest-local cases are executable here; every external-service,
# unimplemented-ABI or unbuilt-fixture case fails closed with its dependency.
# A banner, binary launch, sample-free snapshot or timeout is never PASS.
set -u
export PATH=/usr/sbin:/sbin:/usr/bin:/bin HOME=/root LC_ALL=C LANG=C

test "${1:-}" = --exercise && test "$#" -eq 3 || {
    echo '[M213 ERROR] usage: --exercise CASE ELF_PATH' >&2
    exit 2
}
case_id=$2
elf=$3
target="/root/m213-work/$case_id.${NAS_M213_BOOT_PHASE:-manual}"
test -x "/$elf" || { echo "[M213 FAIL] absent signed ELF: $elf" >&2; exit 1; }
magic=$(/bin/busybox od -An -tx1 -N4 "/$elf" 2>/dev/null |
    /bin/busybox tr -d ' \t\r\n')
test "$magic" = 7f454c46 || {
    echo "[M213 FAIL] original program is not an ELF: $elf" >&2
    exit 1
}
mkdir -p "$target" || exit 1
probe_rc=1

case "$case_id:$elf" in
    child-process-group-transition:usr/bin/setpgid)
        # util-linux setpgid(1) calls setpgid(0,0) before exec. The ORIGINAL
        # shell's pid must equal its pgrp, while the parent is in another
        # pgrp. Read real /proc/self/stat fields 1 and 5, not ps/help text.
        read -r parent_pid parent_comm parent_state parent_ppid parent_pgrp parent_rest \
            < /proc/self/stat || exit 1
        test -n "$parent_pgrp" || exit 1
        if M213_PARENT_PGRP="$parent_pgrp" /usr/bin/setpgid /bin/sh -c '
            read -r pid comm state ppid pgrp rest < /proc/self/stat || exit 31
            test "$pid" = "$pgrp" &&
                test "$pgrp" != "$M213_PARENT_PGRP" || exit 32
            printf "M213_NEW_PGRP:%s:%s\n" "$pid" "$pgrp"
        ' > "$target/out" 2> "$target/err" &&
            /bin/busybox grep -Eq '^M213_NEW_PGRP:[1-9][0-9]*:[1-9][0-9]*$' \
                "$target/out"; then
            probe_rc=0
        fi
        ;;
    child-exit-status-observation:usr/bin/waitpid)
        # Linux waitpid(1) uses pidfd_open + poll. Start a REAL child, require
        # the original program to remain until it is a zombie or reaped,
        # and then wait for its actual zero exit. A no-op immediate success
        # while the child is still sleeping is never accepted.
        /bin/busybox sleep 5 &
        child=$!
        if kill -0 "$child" 2>/dev/null; then
            if /bin/busybox timeout 16 /usr/bin/waitpid --timeout 12 "$child" \
                    > "$target/out" 2> "$target/err"; then
                finished=1
                if [ -r "/proc/$child/stat" ]; then
                    read -r live_pid live_comm live_state live_rest \
                        < "/proc/$child/stat" || finished=0
                    test "${live_state:-?}" = Z || finished=0
                fi
                if [ "$finished" -eq 1 ]; then
                    wait "$child" && probe_rc=0
                else
                    wait "$child" || :
                fi
            else
                wait "$child" || :
            fi
        else
            echo '[M213 waitpid FAIL] child disappeared before a real wait' >&2
            wait "$child" || :
        fi
        ;;
    iso9660-size-fixture:usr/bin/isosize)
        # Generate a minimal genuine ISO9660 Primary Volume Descriptor:
        # 20 x 2048-byte logical blocks, both-endian volume size 20 and
        # logical block size 2048, plus mandatory CD001 PVD identification.
        # This is enough for a real filesystem LENGTH probe; additionally
        # erase the volume count and require the original parser to change.
        /bin/busybox dd if=/dev/zero of="$target/pvd.iso" \
            bs=2048 count=20 > "$target/dd.log" 2>&1 || exit 1
        printf '\001CD001\001' | /bin/busybox dd of="$target/pvd.iso" \
            bs=1 seek=32768 conv=notrunc > "$target/pvd.log" 2>&1 || exit 1
        printf '\024\000\000\000\000\000\000\024' |
            /bin/busybox dd of="$target/pvd.iso" \
                bs=1 seek=32848 conv=notrunc >> "$target/pvd.log" 2>&1 || exit 1
        printf '\000\010\010\000' | /bin/busybox dd of="$target/pvd.iso" \
            bs=1 seek=32896 conv=notrunc >> "$target/pvd.log" 2>&1 || exit 1
        if /usr/bin/isosize "$target/pvd.iso" > "$target/out" 2> "$target/err" &&
            /bin/busybox grep -Fxq '40960' "$target/out" &&
            /usr/bin/isosize -x "$target/pvd.iso" > "$target/sectors" 2>&1 &&
            /bin/busybox grep -Fxq 'sector count: 20, sector size: 2048' \
                "$target/sectors"; then
            /bin/busybox cp "$target/pvd.iso" "$target/zero.iso" || exit 1
            printf '\000\000\000\000\000\000\000\000' |
                /bin/busybox dd of="$target/zero.iso" bs=1 seek=32848 \
                    conv=notrunc > "$target/zero.dd" 2>&1 || exit 1
            if /usr/bin/isosize "$target/zero.iso" \
                    > "$target/zero.bytes" 2>&1 &&
                /bin/busybox grep -Fxq '0' "$target/zero.bytes"; then
                probe_rc=0
            fi
        fi
        ;;

    bridge-netlink-query:sbin/bridge)
        echo '[M213 BLOCKED] requires a real create/query/delete bridge device; RTM_GETLINK alone is not evidence' >&2 ;;
    generic-netlink-family-discovery:sbin/genl)
        echo '[M213 BLOCKED] AF_NETLINK NETLINK_GENERIC(16) family support missing' >&2 ;;
    live-interface-counter-delta:sbin/ifstat)
        echo '[M213 BLOCKED] requires staged real UDP stimulus and before/after /proc/net/dev counters' >&2 ;;
    live-protocol-counter-delta:sbin/nstat)
        echo '[M213 BLOCKED] requires authentic /proc/net/snmp and /proc/net/netstat producers' >&2 ;;
    route-multicast-event:sbin/rtmon)
        echo '[M213 BLOCKED] requires real RTMGRP_LINK subscribe+unicast event trigger' >&2 ;;
    native-loopback-packet-capture:usr/bin/nping)
        echo '[M213 BLOCKED] requires real original nping packet generation and independent host PCAP oracle' >&2 ;;
    authenticated-host-command:usr/bin/ssh)
        echo '[M213 BLOCKED] requires isolated real host sshd, Ed25519 fixture and authenticated remote output' >&2 ;;
    authenticated-scp-file-roundtrip:usr/bin/scp)
        echo '[M213 BLOCKED] requires isolated real host sshd with bidirectional SHA-pinned transfer' >&2 ;;
    authenticated-sftp-file-roundtrip:usr/bin/sftp)
        echo '[M213 BLOCKED] requires isolated real host sftp subsystem and byte-for-byte roundtrip' >&2 ;;
    validated-dnssec-chain:usr/bin/delv)
        echo '[M213 BLOCKED] requires signed authoritative DNSSEC chain and trusted-root validation oracle' >&2 ;;
    genuine-dnstap-fixture-decode:usr/bin/dnstap-read)
        echo '[M213 BLOCKED] requires signed genuine fstrm/dnstap binary fixture with known packet content' >&2 ;;
    genuine-dns-journal-decode:usr/bin/named-journalprint)
        echo '[M213 BLOCKED] requires genuine BIND journal fixture with independently verified serial changes' >&2 ;;
    authoritative-dns-update-roundtrip:usr/bin/nsupdate)
        echo '[M213 BLOCKED] requires isolated authoritative DNS UPDATE server and readback oracle' >&2 ;;
    pipe-capacity-set-and-readback:bin/pipesz)
        # The ORIGINAL util-linux pipesz must see a fresh 4 KiB shell pipe,
        # enlarge a second one to 8 KiB, then exec itself to read the same
        # inherited pipe object back. Neither banner nor launch is evidence.
        if /bin/busybox printf 'pipe-base\n' | \
                /bin/pipesz -cgi > "$target/base" 2> "$target/base.err" &&
           /bin/busybox printf 'pipe-grown\n' | \
                /bin/pipesz -ci -s8192 -- /bin/pipesz -cgi \
                > "$target/grown" 2> "$target/grown.err" &&
           /bin/busybox awk '
                $1=="fd" && $2=="0" && $3=="4096" && NF==4 {ok=1}
                END {exit !ok}
           ' "$target/base" &&
           /bin/busybox awk '
                $1=="fd" && $2=="0" && $3=="8192" && NF==4 {ok=1}
                END {exit !ok}
           ' "$target/grown"; then
            probe_rc=0
        fi
        ;;
    utmp-record-roundtrip:usr/bin/utmpdump)
        echo '[M213 BLOCKED] requires authentic utmp binary record fixture and reverse byte comparison' >&2 ;;
    nonempty-gmon-callgraph-parse:usr/bin/gprof)
        echo '[M213 BLOCKED] requires target-ISA host-generated -pg ELF and matching nonempty gmon.out' >&2 ;;
    llvm-c-api-module-validation:usr/bin/llvm21-c-test)
        echo '[M213 BLOCKED] requires verified original llvm-c-test C API invocation and native fixture' >&2 ;;
    *) echo "[M213 ERROR] unknown original probe or wrong ELF: $case_id:$elf" >&2; exit 1 ;;
esac

if [ "$probe_rc" -eq 0 ]; then
    echo "M213 genuine guest semantic PASS: $case_id:$elf"
    exit 0
fi
echo "[M213 genuine semantic FAIL] $case_id:$elf" >&2
exit 1
