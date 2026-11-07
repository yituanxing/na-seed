#!/bin/sh
# M114: fifteen distinct UNMODIFIED signed Alpine 3.23 Linux ELF programs.
# A Linux HOST stages every program and dependency; NaSeed only executes them
# on real reproducible QR/GIF/CAB/audio/PNG/ZIP input DATA, never builds apps.
PATH=/usr/bin:/bin
HOME=/tmp
LC_ALL=C
LANG=C
export PATH HOME LC_ALL LANG
busy() { /bin/busybox "$@"; }

original_elf() {
    [ "$#" -gt 0 ] || return 1
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
    echo "[M114 MISSING ORIGINAL ELF] $*" >&2
    return 1
}

make_qr() {
    qr_encoder=$(original_elf /usr/bin/qrencode)
    "$qr_encoder" -s 6 -o "$1" "$2"
    [ -s "$1" ]
}
read_qr() {
    qr_decoder=$(original_elf /usr/bin/zbarimg)
    "$qr_decoder" --raw -q "$1" > "$2"
    [ -s "$2" ]
}
make_gif() {
    # Genuine 2x2 untransparent GIF87a: red, green, blue, yellow pixels.
    # Generated with PIL on the Linux host; only the GIF data enters NaSeed.
    printf '%s' 'R0lGODdhAgACAIEAAP8AAAD/AAAA////ACwAAAAAAgACAAAIBwABBBAwICAAOw==' |
        busy base64 -d > "$1"
    [ -s "$1" ]
}
decode_gif() {
    gif_reader=$(original_elf /usr/bin/gif2rgb)
    # gif2rgb -1 is ONE raw RGB file, not a PPM; 2x2x3 = twelve bytes.
    "$gif_reader" -1 -o "$2" "$1"
    [ -s "$2" ]
    bytes=$(busy wc -c < "$2")
    [ "$bytes" -eq 12 ]
}
check_gif_rgb() {
    printf '\377\000\000\000\377\000\000\000\377\377\377\000' > "$2"
    busy cmp "$1" "$2"
}
make_cab() {
    cab_writer=$(original_elf /usr/bin/gcab)
    busy mkdir -p "$1"
    printf 'NaSeed M114 signed original Linux CAB archive\n' > "$1/input.txt"
    (cd "$1"; "$cab_writer" -c -n -z data.cab input.txt)
    [ -s "$1/data.cab" ]
}
extract_cab() {
    cab_reader=$(original_elf /usr/bin/cabextract)
    busy mkdir -p "$2"
    "$cab_reader" -q -d "$2" "$1"
    busy cmp "$3" "$2/input.txt"
}
make_wav() {
    # PCM 16-bit little-endian mono, 48 kHz, 0.1 s of silence (4800 frames).
    # RIFF size=9636, data size=9600.  No encoder builds guest software.
    printf 'RIFF\244\045\000\000WAVEfmt \020\000\000\000\001\000\001\000\200\273\000\000\000\167\001\000\002\000\020\000data\200\045\000\000' > "$1"
    busy dd if=/dev/zero bs=9600 count=1 2>/dev/null >> "$1"
    bytes=$(busy wc -c < "$1")
    [ "$bytes" -eq 9644 ]
}
probe_audio() {
    audio_probe=$(original_elf /usr/bin/ffprobe)
    "$audio_probe" -v error -select_streams a:0 \
        -show_entries stream=codec_name,sample_rate \
        -of default=noprint_wrappers=1 "$1" > "$2"
    busy grep -Fxq "codec_name=$3" "$2"
    busy grep -Fxq "sample_rate=$4" "$2"
}
make_opus() {
    make_wav "$1/source.wav"
    opus_writer=$(original_elf /usr/bin/opusenc)
    "$opus_writer" --quiet "$1/source.wav" "$1/audio.opus"
    [ -s "$1/audio.opus" ]
}
make_vorbis() {
    make_wav "$1/source.wav"
    vorbis_writer=$(original_elf /usr/bin/oggenc)
    "$vorbis_writer" -Q -o "$1/audio.ogg" "$1/source.wav"
    [ -s "$1/audio.ogg" ]
}
check_decoded_wav() {
    [ -s "$1" ]
    bytes=$(busy wc -c < "$1")
    [ "$bytes" -gt 44 ]
    probe_audio "$1" "$2" pcm_s16le 48000
}
make_gzip() {
    printf 'NaSeed M114 AdvanceCOMP original gzip content\n' > "$1/source"
    busy gzip -c "$1/source" > "$1/archive.gz"
    [ -s "$1/archive.gz" ]
}
make_zip() {
    zip_writer=$(original_elf /usr/bin/advzip)
    printf 'NaSeed M114 original ZIP archive and exact bytes\n' > "$1/input.txt"
    (cd "$1"; "$zip_writer" -a archive.zip input.txt)
    [ -s "$1/archive.zip" ]
}

run_case() {
    app=$1
    work="/tmp/m114-$app"
    busy mkdir -p "$work"
    case "$app" in
      qrencode)
        exe=$(original_elf /usr/bin/qrencode)
        "$exe" -s 6 -o "$work/qr.png" NASEED-M114-QR
        [ -s "$work/qr.png" ]
        read_qr "$work/qr.png" "$work/decoded"
        busy grep -Fxq NASEED-M114-QR "$work/decoded" ;;
      zbarimg)
        exe=$(original_elf /usr/bin/zbarimg)
        make_qr "$work/qr.png" NASEED-M114-ZBAR
        "$exe" --raw -q "$work/qr.png" > "$work/decoded"
        busy grep -Fxq NASEED-M114-ZBAR "$work/decoded" ;;
      gifsicle)
        exe=$(original_elf /usr/bin/gifsicle)
        make_gif "$work/source.gif"
        "$exe" --optimize=2 -o "$work/optimized.gif" "$work/source.gif"
        [ -s "$work/optimized.gif" ]
        decode_gif "$work/source.gif" "$work/source.rgb"
        decode_gif "$work/optimized.gif" "$work/optimized.rgb"
        busy cmp "$work/source.rgb" "$work/optimized.rgb"
        check_gif_rgb "$work/optimized.rgb" "$work/expected.rgb" ;;
      gif2rgb)
        exe=$(original_elf /usr/bin/gif2rgb)
        make_gif "$work/source.gif"
        "$exe" -1 -o "$work/decoded.rgb" "$work/source.gif"
        bytes=$(busy wc -c < "$work/decoded.rgb")
        [ "$bytes" -eq 12 ]
        check_gif_rgb "$work/decoded.rgb" "$work/expected.rgb" ;;
      gcab)
        exe=$(original_elf /usr/bin/gcab)
        printf 'NaSeed M114 gcab original CAB content\n' > "$work/input.txt"
        (cd "$work"; "$exe" -c -n -z data.cab input.txt)
        [ -s "$work/data.cab" ]
        "$exe" -t "$work/data.cab" > "$work/list"
        busy grep -Fq input.txt "$work/list"
        extract_cab "$work/data.cab" "$work/unpacked" "$work/input.txt" ;;
      cabextract)
        exe=$(original_elf /usr/bin/cabextract)
        make_cab "$work"
        busy mkdir -p "$work/extracted"
        "$exe" -q -d "$work/extracted" "$work/data.cab"
        busy cmp "$work/input.txt" "$work/extracted/input.txt" ;;
      opusenc)
        exe=$(original_elf /usr/bin/opusenc)
        make_wav "$work/source.wav"
        "$exe" --quiet "$work/source.wav" "$work/audio.opus"
        [ -s "$work/audio.opus" ]
        probe_audio "$work/audio.opus" "$work/meta" opus 48000
        opus_reader=$(original_elf /usr/bin/opusdec)
        "$opus_reader" --quiet "$work/audio.opus" "$work/decoded.wav"
        check_decoded_wav "$work/decoded.wav" "$work/decoded-meta" ;;
      opusdec)
        exe=$(original_elf /usr/bin/opusdec)
        make_opus "$work"
        "$exe" --quiet "$work/audio.opus" "$work/decoded.wav"
        check_decoded_wav "$work/decoded.wav" "$work/decoded-meta" ;;
      oggenc)
        exe=$(original_elf /usr/bin/oggenc)
        make_wav "$work/source.wav"
        "$exe" -Q -o "$work/audio.ogg" "$work/source.wav"
        [ -s "$work/audio.ogg" ]
        probe_audio "$work/audio.ogg" "$work/meta" vorbis 48000
        vorbis_reader=$(original_elf /usr/bin/oggdec)
        "$vorbis_reader" -Q -o "$work/decoded.wav" "$work/audio.ogg"
        check_decoded_wav "$work/decoded.wav" "$work/decoded-meta" ;;
      oggdec)
        exe=$(original_elf /usr/bin/oggdec)
        make_vorbis "$work"
        "$exe" -Q -o "$work/decoded.wav" "$work/audio.ogg"
        check_decoded_wav "$work/decoded.wav" "$work/decoded-meta" ;;
      advdef)
        exe=$(original_elf /usr/bin/advdef)
        make_gzip "$work"
        "$exe" -z -2 "$work/archive.gz" > "$work/report"
        busy gzip -dc "$work/archive.gz" > "$work/restored"
        busy cmp "$work/source" "$work/restored" ;;
      advpng)
        exe=$(original_elf /usr/bin/advpng)
        make_qr "$work/code.png" NASEED-M114-ADVPNG
        "$exe" -z -2 "$work/code.png" > "$work/report"
        read_qr "$work/code.png" "$work/decoded"
        busy grep -Fxq NASEED-M114-ADVPNG "$work/decoded" ;;
      advzip)
        exe=$(original_elf /usr/bin/advzip)
        make_zip "$work"
        "$exe" -z -2 "$work/archive.zip" > "$work/report"
        "$exe" -t "$work/archive.zip" > "$work/test-report"
        busy mkdir -p "$work/out"
        (cd "$work/out"; "$exe" -x "$work/archive.zip")
        busy cmp "$work/input.txt" "$work/out/input.txt" ;;
      ffmpeg)
        exe=$(original_elf /usr/bin/ffmpeg)
        make_wav "$work/source.wav"
        "$exe" -nostdin -hide_banner -loglevel error -threads 1 -y -i "$work/source.wav" \
            -ar 16000 -ac 1 -c:a pcm_s16le "$work/converted.wav"
        [ -s "$work/converted.wav" ]
        probe_audio "$work/converted.wav" "$work/meta" pcm_s16le 16000 ;;
      ffprobe)
        exe=$(original_elf /usr/bin/ffprobe)
        make_wav "$work/source.wav"
        "$exe" -v error -select_streams a:0 \
            -show_entries stream=codec_name,sample_rate \
            -of default=noprint_wrappers=1 "$work/source.wav" > "$work/meta"
        busy grep -Fxq codec_name=pcm_s16le "$work/meta"
        busy grep -Fxq sample_rate=48000 "$work/meta" ;;
      *)
        echo "[M114 UNKNOWN APPLICATION] $app" >&2
        return 2 ;;
    esac
}

if [ "$#" -ne 0 ] && [ "$1" = --case ]; then
    [ "$#" -eq 2 ] || exit 2
    set -eu
    # Keep a complete command trace in the per-case log. The parent prints
    # that log only when a genuine Linux application/oracle fails, so early
    # stderr cannot disappear behind later kernel QEMU serial diagnostics.
    if [ "${M114_TRACE:-0}" = 1 ]; then
        PS4='[M114 TRACE] '
        set -x
    fi
    run_case "$2"
    exit 0
fi
[ "$#" -eq 0 ] || { echo 'usage: m114-batch-run.sh [--case APP]' >&2; exit 2; }
failed=0
for app in qrencode zbarimg gifsicle gif2rgb gcab cabextract \
           opusenc opusdec oggenc oggdec advdef advpng advzip ffmpeg ffprobe; do
    echo "[M114 CASE START] $app"
    case_log="/tmp/m114-$app-case.log"
    if M114_TRACE=1 /bin/sh /tests/m114-batch-run.sh --case "$app" > "$case_log" 2>&1; then
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
        printf 'M114_%s_PASS\n' "$upper"
    else
        rc=$?
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
        echo "[M114 $upper FAILED] original signed ELF or real functional oracle, rc=$rc" >&2
        busy tail -100 "$case_log" >&2
        printf 'M114_%s_FAIL\n' "$upper"
        failed=$((failed + 1))
    fi
done
if [ "$failed" -eq 0 ]; then
    echo M114_BATCH_PASS
else
    echo "[M114 BATCH FAIL] $failed/15 original signed Linux ELF functional checks" >&2
    echo M114_BATCH_FAIL
    exit 1
fi
