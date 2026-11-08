#!/bin/sh
# Seven ORIGINAL APK ELF identities, seven authentic functional witnesses.
# libdeflate-gunzip is an alias of the single counted libdeflate-gzip ELF.
# Every reopen runs in another native QEMU and reads the FIRST QEMU's ext4.
set -eu
[ "$#" -eq 3 ] && [ "$1" = --exercise ] || exit 2
case "${NAS_M236_BOOT_PHASE:-}" in first|reopen) ;; *) exit 2 ;; esac
phase=$NAS_M236_BOOT_PHASE case_id=$2 elf=$3
busy=/bin/busybox
test -x "/$elf"
magic=$($busy od -An -tx1 -N4 "/$elf" | $busy tr -d ' \t\r\n')
test "$magic" = 7f454c46 || { echo 'M236: not an original native ELF' >&2; exit 1; }
state=/root/m236-state
target="$state/$case_id.$phase"
test ! -e "$target" || { echo 'M236: preseeded functional result' >&2; exit 1; }
mkdir -p "$target"

case "$case_id:$elf" in
    fossil-persisted-commit:usr/bin/fossil)
        payload="$state/fossil-work/payload.txt"
        repo="$state/history.fossil"
        if [ "$phase" = first ]; then
            test ! -e "$repo"
            mkdir -p "$state/fossil-work"
            /usr/bin/fossil init -A m236 "$repo" > "$target/init.log" 2>&1
            if ! ( cd "$state/fossil-work" &&
                   /usr/bin/fossil open --nosync "$repo" &&
                   printf 'M236_REAL_FOSSIL_COMMITTED_DATA_2026\n' > payload.txt &&
                   /usr/bin/fossil add payload.txt &&
                   USER=m236 /usr/bin/fossil commit --nosync --no-warnings \
                       --user-override m236 -m 'M236 durable original fossil check-in' ) \
                    > "$target/commit.log" 2>&1; then
                # The original Fossil stderr lives on physical ext4. Propagate
                # its bounded actual last lines to UART before strict FAIL,
                # never infer a syscall problem from an exit status alone.
                echo 'M236 FOSSIL original checkout/commit exit nonzero' >&2
                $busy tail -n 3 "$target/commit.log" >&2
                exit 1
            fi
        fi
        test -s "$repo" && test -s "$payload"
        /usr/bin/fossil cat -R "$repo" payload.txt > "$target/checked-out.txt" 2> "$target/cat.err"
        $busy cmp -s "$payload" "$target/checked-out.txt"
        /usr/bin/fossil timeline -R "$repo" -n 5 > "$target/timeline.txt" 2>&1
        $busy grep -Fq 'M236 durable original fossil check-in' "$target/timeline.txt"
        ;;
    libdeflate-gzip-roundtrip:usr/bin/libdeflate-gzip)
        if [ "$phase" = first ]; then
            printf 'M236_LIBDEFLATE_FIRST_EXT4_2026\nPAYLOAD=alpha,beta,gamma\n' > "$state/codec.txt"
            if ! /usr/bin/libdeflate-gzip -c "$state/codec.txt" > "$state/codec.gz" 2> "$target/compress.err"; then
                echo 'M236 LIBDEFLATE original gzip compression exit nonzero' >&2
                if [ -s "$target/compress.err" ]; then
                    $busy tail -n 3 "$target/compress.err" >&2
                else
                    echo 'M236 LIBDEFLATE original stderr empty; inspect native syscall trace' >&2
                fi
                exit 1
            fi
        fi
        test -s "$state/codec.txt" && test -s "$state/codec.gz"
        # The historical upstream installs gunzip as a binary alias. It is
        # a genuine decompression exercise, NOT a second unique ELF count.
        /usr/bin/libdeflate-gunzip -c "$state/codec.gz" > "$target/restored.txt" 2> "$target/decompress.err"
        $busy cmp -s "$state/codec.txt" "$target/restored.txt"
        ;;
    dfu-stellaris-prefix-roundtrip:usr/bin/dfu-prefix)
        original="$state/firmware-prefix.raw"
        added="$state/firmware-prefix.added"
        if [ "$phase" = first ]; then
            printf 'M236_REAL_DFU_PREFIX_FIRMWARE_2026\n128-byte-USB-free-payload\n' > "$original"
            $busy cp "$original" "$added"
            /usr/bin/dfu-prefix --stellaris-address 0x1000 --add "$added" > "$target/add.log" 2>&1
        fi
        test -s "$original" && test -s "$added"
        size_before=$($busy wc -c < "$original")
        size_after=$($busy wc -c < "$added")
        test "$size_after" -gt "$size_before" || exit 1
        $busy cmp -s "$original" "$added" && exit 1
        $busy cp "$added" "$target/after-add.dfu"
        /usr/bin/dfu-prefix --stellaris --check "$target/after-add.dfu" > "$target/check.log" 2>&1
        /usr/bin/dfu-prefix --stellaris --delete "$target/after-add.dfu" > "$target/delete.log" 2>&1
        $busy cmp -s "$original" "$target/after-add.dfu"
        ;;
    dfu-suffix-crc-roundtrip:usr/bin/dfu-suffix)
        original="$state/firmware-suffix.raw"
        added="$state/firmware-suffix.added"
        if [ "$phase" = first ]; then
            printf 'M236_REAL_DFU_SUFFIX_FIRMWARE_2026\nUSB-VID-PID-CRC\n' > "$original"
            $busy cp "$original" "$added"
            /usr/bin/dfu-suffix --vid 0123 --pid 4567 --add "$added" > "$target/add.log" 2>&1
        fi
        test -s "$original" && test -s "$added"
        size_before=$($busy wc -c < "$original")
        size_after=$($busy wc -c < "$added")
        test "$size_after" -eq "$((size_before+16))" || exit 1
        $busy cmp -s "$original" "$added" && exit 1
        $busy cp "$added" "$target/after-add.dfu"
        /usr/bin/dfu-suffix --check "$target/after-add.dfu" > "$target/check.log" 2>&1
        /usr/bin/dfu-suffix --delete "$target/after-add.dfu" > "$target/delete.log" 2>&1
        $busy cmp -s "$original" "$target/after-add.dfu"
        ;;
    ofarc-zip-recovery:usr/bin/ofarc)
        archive="$state/real-ofarc.zip"
        if [ "$phase" = first ]; then
            test ! -e "$archive"
            mkdir -p "$state/archive-src"
            printf 'M236_REAL_OFARC_ALPHA\n' > "$state/archive-src/alpha.txt"
            printf 'M236_REAL_OFARC_BETA\n' > "$state/archive-src/beta.txt"
            ( cd "$state/archive-src" &&
              /usr/bin/ofarc -c "$archive" alpha.txt beta.txt ) > "$target/create.log" 2>&1
        fi
        test -s "$archive"
        /usr/bin/ofarc -l "$archive" > "$target/list.log" 2>&1
        $busy grep -Fq 'alpha.txt' "$target/list.log"
        $busy grep -Fq 'beta.txt' "$target/list.log"
        ( cd "$target" && /usr/bin/ofarc -x "$archive" ) > "$target/extract.log" 2>&1
        $busy cmp -s "$state/archive-src/alpha.txt" "$target/alpha.txt"
        $busy cmp -s "$state/archive-src/beta.txt" "$target/beta.txt"
        ;;
    ofhash-independent-digest:usr/bin/ofhash)
        file="$state/archive-src/alpha.txt"
        test -s "$file"
        /usr/bin/ofhash --sha256 "$file" > "$target/ofhash.txt" 2> "$target/ofhash.err"
        expected=$($busy sha256sum "$file" | $busy awk '{print $1}')
        $busy tr '[:upper:]' '[:lower:]' < "$target/ofhash.txt" | $busy grep -Fq "$expected"
        printf '%s\n' "$expected" > "$target/expected.sha256"
        ;;
    xmlsec-hmac-signature:usr/bin/xmlsec1)
        if [ "$phase" = first ]; then
            test ! -e "$state/signed.xml"
            printf 'M236_ORIGINAL_XMLSEC_HMAC_32_BYTE_KEY\n' > "$state/hmac.key"
            # XMLDSig enveloped HMAC-SHA256; no external certificate or
            # generated-key package is needed for this authentic MAC test.
            printf '%s\n' \
                '<?xml version="1.0"?>' \
                '<Envelope xmlns="urn:m236:real-xml">' \
                ' <Message>M236_REAL_SIGNED_XML_DOCUMENT_2026</Message>' \
                ' <Signature xmlns="http://www.w3.org/2000/09/xmldsig#">' \
                '  <SignedInfo>' \
                '   <CanonicalizationMethod Algorithm="http://www.w3.org/TR/2001/REC-xml-c14n-20010315"/>' \
                '   <SignatureMethod Algorithm="http://www.w3.org/2001/04/xmldsig-more#hmac-sha256"/>' \
                '   <Reference URI="">' \
                '    <Transforms><Transform Algorithm="http://www.w3.org/2000/09/xmldsig#enveloped-signature"/></Transforms>' \
                '    <DigestMethod Algorithm="http://www.w3.org/2001/04/xmlenc#sha256"/>' \
                '    <DigestValue/>' \
                '   </Reference>' \
                '  </SignedInfo>' \
                '  <SignatureValue/>' \
                '  <KeyInfo><KeyName>m236</KeyName></KeyInfo>' \
                ' </Signature>' \
                '</Envelope>' > "$state/template.xml"
            /usr/bin/xmlsec1 --sign --hmac-key:m236 "$state/hmac.key" \
                --output "$state/signed.xml" "$state/template.xml" > "$target/sign.log" 2>&1
        fi
        test -s "$state/hmac.key" && test -s "$state/signed.xml"
        /usr/bin/xmlsec1 --verify --hmac-key:m236 "$state/hmac.key" \
            "$state/signed.xml" > "$target/verified.log" 2>&1
        $busy grep -Fq 'OK' "$target/verified.log"
        $busy sed 's/M236_REAL_SIGNED_XML_DOCUMENT_2026/M236_TAMPERED_SIGNED_XML_DOCUMENT_2026/' \
            "$state/signed.xml" > "$target/tampered.xml"
        $busy cmp -s "$state/signed.xml" "$target/tampered.xml" && exit 1
        if /usr/bin/xmlsec1 --verify --hmac-key:m236 "$state/hmac.key" \
                "$target/tampered.xml" > "$target/tampered.log" 2>&1; then
            echo 'M236: XML tampering incorrectly verified' >&2
            exit 1
        fi
        ;;
    *) echo "M236 unknown ELF semantic probe $case_id:$elf" >&2; exit 2 ;;
esac
echo "M236_REAL_FUNCTION_OK case=$case_id phase=$phase original=/$elf"
