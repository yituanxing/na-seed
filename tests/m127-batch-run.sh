#!/bin/sh
# Fifteen new signed Alpine audio ELFs. Host creates fixtures; one NaSeed QEMU
# executes each actual program, with independent observable audio-data oracles.
PATH=/usr/bin:/bin
HOME=/tmp
LC_ALL=C
LANG=C
export PATH HOME LC_ALL LANG
busy() { /bin/busybox "$@"; }

original_elf() {
    for candidate do
        [ -x "$candidate" ] || continue
        resolved=$(busy readlink -f "$candidate") || continue
        [ -x "$resolved" ] || continue
        case "$resolved" in /bin/busybox|/usr/bin/busybox) continue ;; esac
        magic=$(busy dd if="$resolved" bs=1 count=4 2>/dev/null |
                busy od -An -tx1 | busy tr -d ' \n')
        if [ "$magic" = 7f454c46 ]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    echo "[M127 MISSING ORIGINAL ELF] $*" >&2
    return 1
}

check_inputs() {
    for item in tone.wav tone.raw tone.flac tone.ogg tone.mp3 tone.spx \
                tone.wv vorbis-comments.txt; do
        [ -s "/tests/m127-$item" ] || return 1
    done
}

has() { busy grep -Fq "$2" "$1"; }
nonempty() { [ -s "$1" ]; }
bytes() { busy wc -c < "$1"; }
has_ogg() { busy grep -iq 'vorbis' "$1"; }

check_magic() {
    actual=$(busy dd if="$1" bs=1 count=4 2>/dev/null |
             busy od -An -tx1 | busy tr -d ' \n')
    [ "$actual" = "$2" ]
}

valid_wav() {
    nonempty "$1"
    check_magic "$1" 52494646
    form=$(busy dd if="$1" bs=1 skip=8 count=4 2>/dev/null |
           busy od -An -tx1 | busy tr -d ' \n')
    [ "$form" = 57415645 ]
    [ "$(bytes "$1")" -gt 65000 ]
}

run_case() {
    app=$1
    work="/tmp/m127-$app"
    busy mkdir -p "$work"
    wave=/tests/m127-tone.wav
    ogg=/tests/m127-tone.ogg
    mp3=/tests/m127-tone.mp3
    wv=/tests/m127-tone.wv
    case "$app" in
      wavpack)
        exe=$(original_elf /usr/bin/wavpack)
        "$exe" -y -o "$work/packed.wv" "$wave"
        check_magic "$work/packed.wv" 7776706b
        unpack=$(original_elf /usr/bin/wvunpack)
        "$unpack" -y -r -o "$work/decoded.raw" "$work/packed.wv"
        busy cmp -s /tests/m127-tone.raw "$work/decoded.raw" ;;
      wvunpack)
        exe=$(original_elf /usr/bin/wvunpack)
        "$exe" -y -r -o "$work/decoded.raw" "$wv"
        nonempty "$work/decoded.raw"
        busy cmp -s /tests/m127-tone.raw "$work/decoded.raw" ;;
      wvgain)
        exe=$(original_elf /usr/bin/wvgain)
        busy cp "$wv" "$work/gain.wv"
        "$exe" "$work/gain.wv"
        tag=$(original_elf /usr/bin/wvtag)
        "$tag" -l "$work/gain.wv" > "$work/tags.txt"
        busy grep -iq 'replaygain_track_gain' "$work/tags.txt" ;;
      wvtag)
        exe=$(original_elf /usr/bin/wvtag)
        busy cp "$wv" "$work/tagged.wv"
        "$exe" -w 'Artist=M127WAVPACKPOLAR' "$work/tagged.wv"
        "$exe" -l "$work/tagged.wv" > "$work/tags.txt"
        has "$work/tags.txt" M127WAVPACKPOLAR ;;
      ogginfo)
        exe=$(original_elf /usr/bin/ogginfo)
        "$exe" "$ogg" > "$work/info.txt"
        has_ogg "$work/info.txt"
        has "$work/info.txt" M127OGGSIGNAL
        busy grep -Eq '16000|16,000' "$work/info.txt" ;;
      vcut)
        exe=$(original_elf /usr/bin/vcut)
        "$exe" "$ogg" "$work/first.ogg" "$work/second.ogg" 16000
        nonempty "$work/first.ogg"
        nonempty "$work/second.ogg"
        ! busy cmp -s "$work/first.ogg" "$work/second.ogg"
        decoder=$(original_elf /usr/bin/oggdec)
        "$decoder" -R -o "$work/first.raw" "$work/first.ogg"
        "$decoder" -R -o "$work/second.raw" "$work/second.ogg"
        [ "$(bytes "$work/first.raw")" -gt 25000 ]
        [ "$(bytes "$work/first.raw")" -lt 42000 ]
        [ "$(bytes "$work/second.raw")" -gt 55000 ]
        [ "$(bytes "$work/second.raw")" -lt 75000 ] ;;
      vorbiscomment)
        exe=$(original_elf /usr/bin/vorbiscomment)
        "$exe" -a -t 'M127ADDED=M127VORBISMOON' "$ogg" "$work/tagged.ogg"
        "$exe" -l "$work/tagged.ogg" > "$work/comments.txt"
        has "$work/comments.txt" M127VORBISMOON
        has "$work/comments.txt" M127OGGSIGNAL ;;
      speexenc)
        exe=$(original_elf /usr/bin/speexenc)
        "$exe" "$wave" "$work/encoded.spx"
        check_magic "$work/encoded.spx" 4f676753
        decoder=$(original_elf /usr/bin/speexdec)
        "$decoder" "$work/encoded.spx" "$work/decoded.wav"
        valid_wav "$work/decoded.wav" ;;
      speexdec)
        exe=$(original_elf /usr/bin/speexdec)
        "$exe" /tests/m127-tone.spx "$work/decoded.wav"
        valid_wav "$work/decoded.wav" ;;
      metaflac)
        exe=$(original_elf /usr/bin/metaflac)
        "$exe" --show-sample-rate /tests/m127-tone.flac > "$work/rate.txt"
        "$exe" --show-total-samples /tests/m127-tone.flac > "$work/samples.txt"
        "$exe" --show-tag=TITLE /tests/m127-tone.flac > "$work/title.txt"
        has "$work/rate.txt" 16000
        has "$work/samples.txt" 48000
        has "$work/title.txt" M127FLACMEMORY ;;
      lame)
        exe=$(original_elf /usr/bin/lame)
        "$exe" --quiet -V4 "$wave" "$work/encoded.mp3"
        nonempty "$work/encoded.mp3"
        decoder=$(original_elf /usr/bin/mpg123)
        "$decoder" -q -w "$work/decoded.wav" "$work/encoded.mp3"
        valid_wav "$work/decoded.wav" ;;
      mpg123)
        exe=$(original_elf /usr/bin/mpg123)
        "$exe" -q -w "$work/decoded.wav" "$mp3"
        valid_wav "$work/decoded.wav" ;;
      mpg123-id3dump)
        exe=$(original_elf /usr/bin/mpg123-id3dump)
        "$exe" "$mp3" > "$work/id3.txt"
        has "$work/id3.txt" M127ID3GALAXY
        has "$work/id3.txt" M127AUDIOENGINEER ;;
      mpg123-strip)
        exe=$(original_elf /usr/bin/mpg123-strip)
        check_magic "$mp3" 49443303
        "$exe" < "$mp3" > "$work/stripped.mp3"
        nonempty "$work/stripped.mp3"
        ! check_magic "$work/stripped.mp3" 49443303
        decoder=$(original_elf /usr/bin/mpg123)
        "$decoder" -q -w "$work/before.wav" "$mp3"
        "$decoder" -q -w "$work/after.wav" "$work/stripped.mp3"
        valid_wav "$work/before.wav"
        valid_wav "$work/after.wav"
        busy cmp -s "$work/before.wav" "$work/after.wav" ;;
      out123)
        exe=$(original_elf /usr/bin/out123)
        "$exe" -w "$work/wrapped.wav" -r 16000 -c 1 -e s16 < /tests/m127-tone.raw
        valid_wav "$work/wrapped.wav"
        [ "$(bytes "$work/wrapped.wav")" -gt 96000 ]
        [ "$(bytes "$work/wrapped.wav")" -lt 100000 ] ;;
      *) echo "[M127 UNKNOWN CASE] $app" >&2; return 2 ;;
    esac
}

if [ "$#" -gt 0 ] && [ "$1" = --case ]; then
    [ "$#" -eq 2 ] || exit 2
    set -eu
    check_inputs
    if [ "${M127_TRACE:-0}" = 1 ]; then
        PS4='[M127 TRACE] '
        set -x
    fi
    run_case "$2"
    exit 0
fi
[ "$#" -eq 0 ] || { echo 'usage: m127-batch-run.sh [--case APP]' >&2; exit 2; }
failed=0
for app in wavpack wvunpack wvgain wvtag ogginfo vcut vorbiscomment \
           speexenc speexdec metaflac lame mpg123 mpg123-id3dump mpg123-strip \
           out123; do
    echo "[M127 CASE START] $app"
    case_log="/tmp/m127-$app-case.log"
    if M127_TRACE=1 /bin/sh /tests/m127-batch-run.sh --case "$app" > "$case_log" 2>&1; then
        upper=$(printf '%s' "$app" | busy tr '[:lower:]-' '[:upper:]_')
        printf 'M127_%s_PASS\n' "$upper"
    else
        rc=$?
        upper=$(printf '%s' "$app" | busy tr '[:lower:]-' '[:upper:]_')
        echo "[M127 $upper FAILED] original ELF rc=$rc" >&2
        busy tail -120 "$case_log" >&2
        printf 'M127_%s_FAIL\n' "$upper"
        failed=$((failed + 1))
    fi
done
if [ "$failed" -eq 0 ]; then
    echo M127_BATCH_PASS
else
    echo "[M127 BATCH FAIL] $failed/15 original signed audio programs" >&2
    echo M127_BATCH_FAIL
    exit 1
fi
