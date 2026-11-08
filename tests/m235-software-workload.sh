#!/bin/sh
# M235: one original Alpine signed-APK batch, twelve real semantics, two boots.
# No version/help probes; the new ELF must consume actual inputs and the
# independent second QEMU must re-read the first boot's durable ext4 files.
set -eu
[ "$#" -eq 3 ] && [ "$1" = --exercise ] || exit 2
case "${NAS_M235_BOOT_PHASE:-}" in first|reopen) ;; *) exit 2 ;; esac
phase=$NAS_M235_BOOT_PHASE case_id=$2 elf=$3
busy=/bin/busybox
test -x "/$elf"
magic=$($busy od -An -tx1 -N4 "/$elf" | $busy tr -d ' \t\r\n')
test "$magic" = 7f454c46 || { echo 'M235: candidate is not a native ELF' >&2; exit 1; }
state=/root/m235-state
target="$state/$case_id.$phase"
test ! -e "$target" || { echo 'M235: stale probe output' >&2; exit 1; }
mkdir -p "$target"

bdb_check() {
    test -s "$state/original.db" && test -s "$state/pairs.txt" || return 1
    /usr/bin/db_verify "$state/original.db" > "$target/integrity.log" 2>&1 || return 1
    /usr/bin/db_dump -p "$state/original.db" > "$target/read.dump" 2>&1 || return 1
    $busy grep -Fq 'M235_DB_KEY_ONE' "$target/read.dump" &&
        $busy grep -Fq 'M235_DB_VALUE_ONE' "$target/read.dump" &&
        $busy grep -Fq 'M235_DB_KEY_TWO' "$target/read.dump" &&
        $busy grep -Fq 'M235_DB_VALUE_TWO' "$target/read.dump"
}

case "$case_id:$elf" in
    bdb-import-records:usr/bin/db_load)
        if [ "$phase" = first ]; then
            test ! -e "$state/original.db"
            printf 'M235_DB_KEY_ONE\nM235_DB_VALUE_ONE\nM235_DB_KEY_TWO\nM235_DB_VALUE_TWO\n' > "$state/pairs.txt"
            /usr/bin/db_load -T -t btree -f "$state/pairs.txt" "$state/original.db" > "$target/import.log" 2>&1
            test -s "$state/original.db"
        else
            /usr/bin/db_load -T -t btree -f "$state/pairs.txt" "$target/reimport.db" > "$target/import.log" 2>&1
            test -s "$target/reimport.db"
            /usr/bin/db_dump -p "$target/reimport.db" > "$target/reimport.dump" 2>&1
            $busy grep -Fq 'M235_DB_VALUE_TWO' "$target/reimport.dump"
        fi
        bdb_check
        ;;
    bdb-dump-records:usr/bin/db_dump)
        bdb_check
        ;;
    bdb-verify-persisted:usr/bin/db_verify)
        test -s "$state/original.db"
        /usr/bin/db_verify "$state/original.db" > "$target/verify.log" 2>&1
        bdb_check
        ;;
    bdb-btree-record-stats:usr/bin/db_stat)
        bdb_check
        /usr/bin/db_stat -d "$state/original.db" > "$target/stat.txt" 2>&1
        $busy grep -Ei '(number of (unique )?keys|btree|page size)' "$target/stat.txt" >/dev/null
        ;;
    bdb-btree-tune-analysis:usr/bin/db_tuner)
        bdb_check
        /usr/bin/db_tuner -d "$state/original.db" > "$target/tuner.txt" 2>&1
        $busy grep -Ei '(page size|page-size|pagesize)' "$target/tuner.txt" >/dev/null
        ;;
    bdb-upgrade-data-preserve:usr/bin/db_upgrade)
        bdb_check
        $busy cp "$state/original.db" "$target/upgrade.db"
        /usr/bin/db_upgrade "$target/upgrade.db" > "$target/upgrade.log" 2>&1
        /usr/bin/db_dump -p "$target/upgrade.db" > "$target/upgraded.dump" 2>&1
        $busy grep -Fq 'M235_DB_VALUE_ONE' "$target/upgraded.dump"
        $busy grep -Fq 'M235_DB_VALUE_TWO' "$target/upgraded.dump"
        ;;
    jsonnet-evaluate-object:usr/bin/jsonnet)
        if [ "$phase" = first ]; then
            test ! -e "$state/config.jsonnet"
            printf '%s\n' '{ name: "NaSeed", slots: [x * 3 for x in [2, 4]], enabled: true }' > "$state/config.jsonnet"
        fi
        test -s "$state/config.jsonnet"
        /usr/bin/jsonnet "$state/config.jsonnet" > "$target/actual.json" 2> "$target/jsonnet.err"
        $busy grep -Fq '"NaSeed"' "$target/actual.json" &&
            $busy grep -Fq '6' "$target/actual.json" &&
            $busy grep -Fq '12' "$target/actual.json" &&
            $busy grep -Fq 'true' "$target/actual.json"
        ;;
    jsonnet-format-roundtrip:usr/bin/jsonnetfmt)
        test -s "$state/config.jsonnet"
        /usr/bin/jsonnetfmt "$state/config.jsonnet" > "$target/formatted.jsonnet" 2> "$target/format.err"
        test -s "$target/formatted.jsonnet"
        /usr/bin/jsonnet "$state/config.jsonnet" > "$target/expected.json"
        /usr/bin/jsonnet "$target/formatted.jsonnet" > "$target/formatted.json"
        $busy cmp -s "$target/expected.json" "$target/formatted.json"
        ;;
    cidr-network-broadcast:bin/ipcalc)
        /bin/ipcalc -n -b 198.51.100.77/27 > "$target/cidr.txt" 2>&1
        $busy grep -Fq '198.51.100.64' "$target/cidr.txt" &&
            $busy grep -Fq '198.51.100.95' "$target/cidr.txt"
        ;;
    wireguard-key-derivation:usr/bin/wg)
        if [ "$phase" = first ]; then
            test ! -e "$state/wg.private" && test ! -e "$state/wg.public"
            /usr/bin/wg genkey > "$state/wg.private"
            test -s "$state/wg.private"
            /usr/bin/wg pubkey < "$state/wg.private" > "$state/wg.public"
        fi
        test -s "$state/wg.private" && test -s "$state/wg.public"
        /usr/bin/wg pubkey < "$state/wg.private" > "$target/derived.public"
        $busy cmp -s "$state/wg.public" "$target/derived.public"
        ;;
    git-lfs-pointer-roundtrip:usr/bin/git-lfs)
        repo="$state/lfs-repo"
        if [ "$phase" = first ]; then
            test ! -e "$repo"
            mkdir -p "$repo"
            git -C "$repo" init -q
            ( cd "$repo" && git lfs install --local > "$target/init.log" 2>&1 )
            printf 'M235_SIGNED_GIT_LFS_DURABLE_PAYLOAD_2026\n' > "$repo/payload.bin"
            ( cd "$repo" && git lfs clean payload.bin < payload.bin > payload.pointer )
        fi
        test -s "$repo/.git/config" && test -s "$repo/payload.pointer"
        $busy grep -Fxq 'version https://git-lfs.github.com/spec/v1' "$repo/payload.pointer"
        ( cd "$repo" && git lfs smudge payload.bin < payload.pointer > "$target/restored.bin" )
        $busy cmp -s "$repo/payload.bin" "$target/restored.bin"
        ;;
    http-response-body:usr/bin/xh)
        # Reuse M223's host-only per-boot unpredictable challenge route.
        # The independent HOST oracle compares exact QEMU-ext4 bytes against
        # the random source and verifies the genuine host GET transaction.
        attempt=0
        while [ "$attempt" -lt 8 ]; do
            attempt=$((attempt+1))
            if /usr/bin/xh --body GET http://10.0.2.2:15431/wget \
                    > "$target/download.body" 2> "$target/xh.err" &&
                    test -s "$target/download.body"; then
                break
            fi
            $busy sleep 2
        done
        test -s "$target/download.body"
        phase_upper=$(printf '%s' "$phase" | $busy tr '[:lower:]' '[:upper:]')
        $busy grep -Eq "^M223_${phase_upper}_WGET_SOURCE_[[:xdigit:]]{192}$" "$target/download.body"
        $busy cp "$target/download.body" "$state/xh.$phase.body"
        ;;
    *) echo "M235 unknown original ELF $case_id:$elf" >&2; exit 2 ;;
esac
echo "M235_REAL_FUNCTION_OK case=$case_id phase=$phase original=/$elf"
