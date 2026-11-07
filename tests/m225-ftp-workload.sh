#!/bin/sh
# Original APK-signed Alpine lftp/rclone against a genuinely independent host
# FTP server.  This emits *guest CLI evidence only*: neither a success marker
# nor a copied guest file substitutes for host receipts, real PCAP or ext4.
set -eu
test "$#" -eq 3 && test "$1" = --exercise || exit 2
case "${NAS_M225_BOOT_PHASE:-}" in
    first|reopen) phase=$NAS_M225_BOOT_PHASE ;;
    *) echo '[M225 TCP FAIL] native QEMU phase missing' >&2; exit 2 ;;
esac
case_id=$2 elf=$3
busy=/bin/busybox
case "$case_id:$elf" in
    original-host-bidirectional-transfer:usr/bin/lftp) package=lftp ;;
    original-host-bidirectional-transfer:usr/bin/rclone) package=rclone ;;
    *) echo '[M225 TCP FAIL] unexpected signed ELF case' >&2; exit 2 ;;
esac
test -x "/$elf" && apk info -e "$package" >/dev/null 2>&1 || {
    echo '[M225 TCP FAIL] original signed Alpine package not installed' >&2; exit 1;
}
magic=$($busy od -An -tx1 -N4 "/$elf" | $busy tr -d ' \t\r\n')
test "$magic" = 7f454c46 || {
    echo '[M225 TCP FAIL] original executable is not a native ELF' >&2; exit 1;
}
work="/root/m225-work/$package.$phase"
test ! -e "$work" || { echo '[M225 TCP FAIL] stale first/reopen evidence' >&2; exit 1; }
mkdir -p "$work"

# Match M222's successful DHCP readiness owner. This is not a substitute for
# the later independent PCAP or host evidence.
elapsed=0
until $busy grep -q '^nameserver 10\.0\.2\.3' /proc/net/naseed-resolv.conf 2>/dev/null; do
    [ "$elapsed" -lt 90 ] || {
        echo '[M225 TCP FAIL] genuine DHCP-backed route not ready' >&2; exit 1;
    }
    elapsed=$((elapsed+1))
    $busy sleep 1
done

if [ "$package" = lftp ]; then
    # Each command performs an actual FTP control+passive data exchange.
    # Host-generated .source is unknown to the guest until RETR succeeds.
    $busy timeout 90 /usr/bin/lftp -p 15451 -u anonymous,m225-disposable \
        10.0.2.2 -e \
        "set ftp:ssl-allow no; set ftp:prefer-epsv yes; \
         set net:max-retries 1; set net:timeout 12; \
         get /lftp.source -o $work/download.bin; \
         put $work/download.bin -o /lftp.upload; bye" \
        > "$work/client.log" 2> "$work/client.err" || {
            $busy tail -n 10 "$work/client.err" >&2 || :; exit 1;
        }
else
    # Tested against a *real host rclone* FTP backend before guest wiring.
    # The Alpine guest must run its original signed /usr/bin/rclone, never
    # the host's binary or an alternative generated FTP client.
    password=$(/usr/bin/rclone obscure m225-disposable)
    remote=":ftp,host=10.0.2.2,port=15451,user=anonymous,pass=$password:"
    $busy timeout 90 /usr/bin/rclone copyto \
        "${remote}rclone.source" "$work/download.bin" \
        --ftp-disable-mlsd --transfers 1 --checkers 1 --retries 1 \
        --low-level-retries 1 --contimeout 12s \
        > "$work/download.log" 2>&1 || {
            $busy tail -n 10 "$work/download.log" >&2 || :; exit 1;
        }
    # Alpine rclone 1.72 defaults to a temporary .partial upload plus rename.
    # Its documented --inplace option forces exact final-path FTP STOR;
    # the original signed client still sends all bytes over real TCP.
    $busy timeout 90 /usr/bin/rclone copyto \
        "$work/download.bin" "${remote}rclone.upload" \
        --inplace --ftp-disable-mlsd --transfers 1 --checkers 1 --retries 1 \
        --low-level-retries 1 --contimeout 12s \
        > "$work/upload.log" 2>&1 || {
            $busy tail -n 10 "$work/upload.log" >&2 || :; exit 1;
        }
fi
test -s "$work/download.bin" || {
    echo '[M225 TCP FAIL] original FTP client produced no real data' >&2; exit 1;
}
$busy cp "$work/download.bin" "$work/uploaded.bin"
$busy cmp -s "$work/download.bin" "$work/uploaded.bin"
sync
echo "[M225 GUEST FTP RECORDED] phase=$phase original=$elf real_host_oracle_still_required"
