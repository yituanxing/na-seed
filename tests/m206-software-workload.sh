#!/bin/sh
# M206 semantic probes for ORIGINAL signed Alpine programs, never substitute
# BusyBox applets or count a help/usage screen as successful execution.
# M205's proven installer/ledger/dual-QEMU owner invokes --exercise here.
set -u
export PATH=/usr/sbin:/sbin:/usr/bin:/bin HOME=/root LC_ALL=C LANG=C

test "${1:-}" = --exercise && test "$#" -eq 3 || {
    echo '[M206 ERROR] usage: --exercise CASE ELF_PATH' >&2
    exit 2
}
case_id=$2
elf=$3
target="/root/m206-work/$case_id.${NAS_M206_BOOT_PHASE:-manual}"

failure_excerpt() {
    name=$1 file=$2
    if [ ! -s "$file" ]; then
        echo "M206 $name: original command produced no output" >&2
        return
    fi
    /bin/busybox head -n 2 "$file" 2>/dev/null |
        /bin/busybox tr -cd '\11\12\15\40-\176' |
        /bin/busybox cut -c 1-160 |
        while IFS= read -r line; do
            test -z "$line" || echo "M206 $name: $line" >&2
        done
}

test -x "/$elf" || { echo "M206 missing executable ELF: $elf" >&2; exit 1; }
magic=$(/bin/busybox od -An -tx1 -N4 "/$elf" 2>/dev/null |
    /bin/busybox tr -d ' \t\r\n')
test "$magic" = 7f454c46 || { echo "M206 not original ELF: $elf" >&2; exit 1; }
mkdir -p "$target" || exit 1

# Each invocation rebuilds its own fixtures on ext4, including reboot probes.
# The common XML input is deliberately well-formed for the W3C CSS tools.
printf '%s\n' \
    '<doc><section class="keep"><h2>Same Heading</h2><p id="m206alpha">Alpha &amp; Beta</p><a href="https://example.invalid/m206-first">A Link</a><p class="drop">RemoveMe</p></section><section class="exclude"><h2>Same Heading</h2><p>ExcludeMe</p></section></doc>' \
    > "$target/fixture.xml"

probe_rc=1
case "$case_id:$elf" in
    icmp-loopback-echo:bin/ping)
        # iputils ping, not BusyBox ping; verify actual ICMP replies.
        /bin/busybox timeout 12 /bin/ping -n -c 2 -W 2 127.0.0.1 \
            > "$target/out" 2>&1 &&
            /bin/busybox grep -Eq '2 (packets )?received|2 received' "$target/out" &&
            probe_rc=0
        ;;
    netlink-lo-address-route:sbin/ip)
        /sbin/ip -j address show dev lo > "$target/out" 2>&1 &&
            /bin/busybox grep -Fq '"ifname":"lo"' "$target/out" &&
            /bin/busybox grep -Fq '127.0.0.1' "$target/out" &&
            probe_rc=0
        ;;
    loopback-qdisc-query:sbin/tc)
        /sbin/tc qdisc show dev lo > "$target/out" 2>&1 &&
            # With an explicit `dev lo` filter, genuine Alpine tc can emit
            # `qdisc noqueue 0: root refcnt 0` without repeating `lo`.
            # Require a real root qdisc object (kind + handle), not a banner.
            /bin/busybox grep -Eq '^qdisc [[:alnum:]_+-]+ [[:xdigit:]]+: root([[:space:]]|$)' "$target/out" &&
            probe_rc=0
        ;;
    local-tcp-throughput-roundtrip:usr/bin/iperf)
        # Two genuine iperf endpoints carry nonzero payload on loopback.
        /bin/busybox timeout 15 /usr/bin/iperf -s -B 127.0.0.1 \
            -p 18086 -1 > "$target/server" 2>&1 &
        server_pid=$!
        /bin/busybox sleep 1
        client_rc=0
        /bin/busybox timeout 12 /usr/bin/iperf -c 127.0.0.1 \
            -p 18086 -t 2 > "$target/out" 2>&1 || client_rc=$?
        if test "$client_rc" -eq 0 &&
            /bin/busybox grep -Eq '[1-9][0-9]*([.][0-9]+)?[[:space:]]+[KMG]?bits/sec' "$target/out" &&
            /bin/busybox grep -Fq '127.0.0.1' "$target/out"; then
            probe_rc=0
        else
            printf 'M206 iperf: real client exit=%s; original server diagnostics follow\n' \
                "$client_rc" >&2
            failure_excerpt 'iperf server' "$target/server"
        fi
        kill -TERM "$server_pid" 2>/dev/null || :
        wait "$server_pid" 2>/dev/null || :
        ;;
    nft-ruleset-parse-and-kernel-query:usr/sbin/nft)
        # A parser-only pass cannot prove that netfilter netlink works.
        printf '%s\n' 'table inet m206 { }' > "$target/nft.conf"
        if /usr/sbin/nft -c -f "$target/nft.conf" \
                > "$target/syntax" 2>&1 &&
            /usr/sbin/nft add table inet m206 > "$target/add" 2>&1; then
            /usr/sbin/nft list table inet m206 > "$target/out" 2>&1 &&
                /bin/busybox grep -Fq 'table inet m206' "$target/out" &&
                probe_rc=0
            /usr/sbin/nft delete table inet m206 > "$target/delete" 2>&1 || :
        else
            /bin/busybox cp "$target/syntax" "$target/out" 2>/dev/null || :
            if [ -s "$target/add" ]; then
                /bin/busybox cp "$target/add" "$target/out" || :
            fi
        fi
        ;;
    mbr-image-create-dump-verify:sbin/sfdisk|partition-image-table-read:usr/sbin/partx)
        # Operate only on an isolated sparse regular file, never a host disk.
        /bin/busybox dd if=/dev/zero of="$target/mbr.img" \
            bs=1024 count=1 seek=16383 > "$target/dd.log" 2>&1 || exit 1
        printf 'label: dos\nstart=2048, size=4096, type=83\n' > "$target/layout"
        if /sbin/sfdisk --wipe never "$target/mbr.img" \
                < "$target/layout" > "$target/sfdisk" 2>&1; then
            case "$elf" in
                sbin/sfdisk)
                    /sbin/sfdisk --dump "$target/mbr.img" > "$target/out" 2>&1 &&
                        /bin/busybox grep -Fq 'label: dos' "$target/out" &&
                        /bin/busybox grep -Eq 'start=[[:space:]]*2048' "$target/out" &&
                        /bin/busybox grep -Eq 'size=[[:space:]]*4096' "$target/out" &&
                        probe_rc=0
                    ;;
                usr/sbin/partx)
                    # Upstream util-linux treats --show and --raw as mutually
                    # exclusive output modes. Their conjunction previously
                    # gave a guaranteed oracle FAIL before reading any MBR.
                    /usr/sbin/partx --show -o START,SECTORS \
                        "$target/mbr.img" > "$target/out" 2>&1 &&
                        /bin/busybox grep -Eq '2048[[:space:]]+4096' "$target/out" &&
                        probe_rc=0
                    ;;
            esac
        else
            /bin/busybox cp "$target/sfdisk" "$target/out" || :
        fi
        ;;
    gpt-image-create-verify:usr/bin/sgdisk)
        /bin/busybox dd if=/dev/zero of="$target/gpt.img" \
            bs=1024 count=1 seek=32767 > "$target/dd.log" 2>&1 || exit 1
        /usr/bin/sgdisk -n 1:2048:4095 -t 1:8300 "$target/gpt.img" \
            > "$target/create" 2>&1 &&
            /usr/bin/sgdisk -v "$target/gpt.img" > "$target/verify" 2>&1 &&
            /usr/bin/sgdisk -p "$target/gpt.img" > "$target/out" 2>&1 &&
            /bin/busybox grep -Fq '2048' "$target/out" &&
            /bin/busybox grep -Fq '8300' "$target/out" &&
            probe_rc=0
        ;;
    live-root-block-device-json:bin/lsblk)
        /bin/lsblk -J -b -o NAME,SIZE,TYPE > "$target/out" 2>&1 &&
            /bin/busybox grep -Fq '"blockdevices"' "$target/out" &&
            /bin/busybox grep -Fq '"name"' "$target/out" &&
            /bin/busybox grep -Fq '"type"' "$target/out" &&
            test -d /sys/block && probe_rc=0
        ;;
    css-selector-content-extraction:usr/bin/hxselect)
        /usr/bin/hxselect -c 'p#m206alpha' < "$target/fixture.xml" \
            > "$target/out" 2>&1 &&
            /bin/busybox grep -Fq 'Alpha &amp; Beta' "$target/out" &&
            ! /bin/busybox grep -Fq 'RemoveMe' "$target/out" &&
            probe_rc=0
        ;;
    count-elements-on-fixture:usr/bin/hxcount)
        printf '<root><tag/></root>\n' > "$target/one.xml"
        printf '<root><tag/><tag/></root>\n' > "$target/two.xml"
        /usr/bin/hxcount "$target/one.xml" > "$target/one.count" 2>&1 &&
            /usr/bin/hxcount "$target/two.xml" > "$target/out" 2>&1 &&
            /bin/busybox grep -Fq 'tag' "$target/out" &&
            ! /bin/busybox cmp -s "$target/one.count" "$target/out" &&
            probe_rc=0
        ;;
    html-to-xhtml-reparse:usr/bin/hxnormalize)
        printf '<html><body><p>M206Normalized\n' > "$target/broken.html"
        /usr/bin/hxnormalize -x "$target/broken.html" > "$target/out" 2>&1 &&
            /bin/busybox grep -Fq M206Normalized "$target/out" &&
            /bin/busybox grep -Fq '</p>' "$target/out" &&
            /bin/busybox grep -Fq '</html>' "$target/out" &&
            probe_rc=0
        ;;
    remove-selector-keep-other:usr/bin/hxremove)
        /usr/bin/hxremove '.drop' < "$target/fixture.xml" \
            > "$target/out" 2>&1 &&
            /bin/busybox grep -Fq 'Alpha &amp; Beta' "$target/out" &&
            ! /bin/busybox grep -Fq RemoveMe "$target/out" &&
            probe_rc=0
        ;;
    strip-excluded-node:usr/bin/hxprune)
        /usr/bin/hxprune -x "$target/fixture.xml" > "$target/out" 2>&1 &&
            /bin/busybox grep -Fq 'Alpha &amp; Beta' "$target/out" &&
            ! /bin/busybox grep -Fq ExcludeMe "$target/out" &&
            probe_rc=0
        ;;
    extract-expected-anchor-urls:usr/bin/hxprintlinks)
        printf '%s\n' \
            '<html><head><title>M206</title></head><body><a href="https://example.invalid/m206-first">A Link</a></body></html>' \
            > "$target/links.html"
        /usr/bin/hxprintlinks "$target/links.html" > "$target/out" 2>&1 &&
            [ "$(/bin/busybox grep -oF 'https://example.invalid/m206-first' \
                   "$target/out" | /bin/busybox wc -l)" -ge 2 ] &&
            probe_rc=0
        ;;
    entity-decode-content:usr/bin/hxunent)
        printf '<doc>Fish &amp; Chips</doc>\n' > "$target/entities.xml"
        printf '<doc>Fish & Chips</doc>\n' > "$target/expected"
        /usr/bin/hxunent "$target/entities.xml" > "$target/out" 2>&1 &&
            /bin/busybox cmp -s "$target/expected" "$target/out" &&
            probe_rc=0
        ;;
    cdata-content-transform:usr/bin/hxuncdata)
        printf '<doc><![CDATA[<x&y>]]></doc>\n' > "$target/cdata.xml"
        /usr/bin/hxuncdata "$target/cdata.xml" > "$target/out" 2>&1 &&
            /bin/busybox grep -Fq '&lt;x&amp;y&gt;' "$target/out" &&
            ! /bin/busybox grep -Fq '<![CDATA[' "$target/out" &&
            probe_rc=0
        ;;
    generate-unique-heading-ids:usr/bin/hxaddid)
        /usr/bin/hxaddid -x h2 "$target/fixture.xml" > "$target/out" 2>&1 &&
            [ "$(/bin/busybox grep -oE 'id="[^"]+"' "$target/out" |
                   /bin/busybox grep -vF 'id="m206alpha"' |
                   /bin/busybox sort -u | /bin/busybox wc -l)" -eq 2 ] &&
            probe_rc=0
        ;;
    pipe-escape-transform:usr/bin/hxpipe|pipe-unescape-roundtrip:usr/bin/hxunpipe)
        printf '<doc><p>M206Pipeline</p></doc>\n' > "$target/roundtrip.xml"
        if /usr/bin/hxpipe "$target/roundtrip.xml" > "$target/pipe" 2>&1 &&
            /bin/busybox grep -Fq '(doc' "$target/pipe" &&
            /bin/busybox grep -Fq -- '-M206Pipeline' "$target/pipe"; then
            case "$elf" in
                usr/bin/hxpipe)
                    /bin/busybox cp "$target/pipe" "$target/out" && probe_rc=0
                    ;;
                usr/bin/hxunpipe)
                    /usr/bin/hxunpipe "$target/pipe" > "$target/out" 2>&1 &&
                        /bin/busybox grep -Fq '<doc>' "$target/out" &&
                        /bin/busybox grep -Fq '<p>M206Pipeline</p>' "$target/out" &&
                        probe_rc=0
                    ;;
            esac
        else
            /bin/busybox cp "$target/pipe" "$target/out" 2>/dev/null || :
        fi
        ;;
    *)
        echo "M206 ERROR: unregistered semantic probe: $case_id:$elf" >&2
        exit 1
        ;;
esac

if [ "$probe_rc" -eq 0 ]; then
    echo "M206 original semantic PASS: $case_id:$elf"
    exit 0
fi
echo "M206 original semantic FAIL: $case_id:$elf" >&2
failure_excerpt "$case_id" "$target/out"
exit 1
