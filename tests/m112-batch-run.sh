#!/bin/sh
# M112: fifteen distinct UNMODIFIED Alpine 3.23 signed Linux ELF programs.
# Original applications and dependencies are host-staged, never guest-built.
PATH=/usr/bin:/bin
HOME=/tmp
LC_ALL=C
LANG=C
TERM=vt100
export PATH HOME LC_ALL LANG TERM
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
    echo "[M112 MISSING ORIGINAL ELF] $*" >&2
    return 1
}

# Original binaries operate on reproducible input fixtures. A generated input
# image or WAV is DATA, not a guest compiler or a substitute application.
make_png() {
    printf '%s' 'iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAIAAACQkWg2AAABxklEQVR42g2SURHAMAhDsYAFLNQCFrCAhVnAAhawUAtYwAIWuuxjt2sZSR4QETGxkBw6SmpkTv7RFxRJWVSXblMPzdI+eijHQ8LnsCibsTp/H3twJkfxvVzNM9zL7/ESibAcETrCKm7yuegnFlIptySuZMuOvJV+MkRojXq8Dun57Lgf+47GuXmqTt4Tfd6c3TPvNJEqq4n60U+VTNlVPj2hnTqle/W1xmiu1tNLBNumgqbmik8jNwhJ2KR12bu2bTkWa/dZEbmzf+J63NTFHJ7ocw7f9Ffe16e9xu96PE8iIEFOmIcjyOBfHHwU38tv65v7dX93vtovH2hTBEdK1Imr0RbjsV+8CMrgCrlxOnTCNvzFRwTcGQIYWQqr2Y5GuYHjpErAkk4E0k2IOlEV15WKU6m1Vs+rv5ooyQJXusVdPgV/+sqIMErMB9BvKDAgGwzfDkiiDy4v9UUU3wtkStTNPdJ7+mmHdXrX1zdas63abyM1TfO2vD5EWJNpwRBnFegnHGCmAnFGC2LjjZKhHQxciHZ5n2yfHd2yhXZ8m7GeC5Z613plFrtBb5kIK4i9wrJgAzBWzAoDAFWgQn6EglPIoycKH/10EH6QXRZ/nQAAAABJRU5ErkJggg==' |
        busy base64 -d > "$1"
    [ -s "$1" ]
}
make_wav() {
    # RIFF PCM: 8000Hz, mono, unsigned 8-bit, four real samples.
    printf 'RIFF\050\000\000\000WAVEfmt \020\000\000\000\001\000\001\000\100\037\000\000\100\037\000\000\001\000\010\000data\004\000\000\000\200\220\240\200' > "$1"
}
make_jpeg() {
    exe=$(original_elf /usr/bin/cjpeg)
    printf 'P6\n2 2\n255\n\377\000\000\000\377\000\000\000\377\377\377\000' > "$1.ppm"
    "$exe" -quality 82 -outfile "$1" "$1.ppm"
    [ -s "$1" ]
}
png_signature() {
    signature=$(busy dd if="$1" bs=1 count=8 2>/dev/null |
                busy od -An -tx1 | busy tr -d ' \n')
    [ "$signature" = 89504e470d0a1a0a ]
}

run_case() {
    app=$1
    work="/tmp/m112-$app"
    busy mkdir -p "$work"
    case "$app" in
      procs)
        exe=$(original_elf /usr/bin/procs)
        "$exe" > "$work/actual"
        [ -s "$work/actual" ]
        busy grep -Eq 'sh|busybox' "$work/actual" ;;
      gojq)
        exe=$(original_elf /usr/bin/gojq)
        printf '{"items":[2,3,5]}\n' | "$exe" '[.items[]] | add' > "$work/actual"
        printf '10\n' > "$work/expected"
        busy cmp "$work/expected" "$work/actual" ;;
      gron)
        exe=$(original_elf /usr/bin/gron)
        printf '{"record":{"value":42}}\n' | "$exe" > "$work/actual"
        busy grep -Fxq 'json.record.value = 42;' "$work/actual" ;;
      shfmt)
        exe=$(original_elf /usr/bin/shfmt)
        printf 'if true;then echo YES;fi\n' > "$work/script.sh"
        "$exe" "$work/script.sh" > "$work/actual"
        busy grep -Fq 'if true; then' "$work/actual"
        busy grep -Fq 'echo YES' "$work/actual"
        ! busy cmp "$work/script.sh" "$work/actual" >/dev/null 2>&1 ;;
      pngquant)
        exe=$(original_elf /usr/bin/pngquant)
        make_png "$work/source.png"
        "$exe" --force --output "$work/quantized.png" 32 "$work/source.png"
        [ -s "$work/quantized.png" ]
        png_signature "$work/quantized.png" ;;
      sponge)
        exe=$(original_elf /usr/bin/sponge)
        printf 'before\n' > "$work/data.txt"
        printf 'AFTER\n' | "$exe" "$work/data.txt"
        printf 'AFTER\n' > "$work/expected"
        busy cmp "$work/expected" "$work/data.txt" ;;
      flac)
        exe=$(original_elf /usr/bin/flac)
        make_wav "$work/source.wav"
        "$exe" --silent --force -o "$work/encoded.flac" "$work/source.wav"
        "$exe" --test --silent "$work/encoded.flac"
        "$exe" --decode --silent --force -o "$work/decoded.wav" "$work/encoded.flac"
        busy cmp "$work/source.wav" "$work/decoded.wav" ;;
      sox)
        exe=$(original_elf /usr/bin/sox)
        "$exe" -n -r 8000 -b 8 -c 1 "$work/tone.wav" synth 0.05 sine 440
        [ -s "$work/tone.wav" ]
        "$exe" --i -r "$work/tone.wav" > "$work/rate"
        busy grep -Fxq 8000 "$work/rate" ;;
      mediainfo)
        exe=$(original_elf /usr/bin/mediainfo)
        make_wav "$work/source.wav"
        "$exe" '--Inform=Audio;%SamplingRate%' "$work/source.wav" > "$work/actual"
        busy grep -Fq 8000 "$work/actual" ;;
      delta)
        exe=$(original_elf /usr/bin/delta)
        printf '%s\n' '--- a/sample.txt' '+++ b/sample.txt' '@@ -1 +1 @@' '-OLD' '+NEW' > "$work/change.diff"
        "$exe" --paging=never < "$work/change.diff" > "$work/actual"
        busy grep -Fq OLD "$work/actual"
        busy grep -Fq NEW "$work/actual" ;;
      exiv2)
        exe=$(original_elf /usr/bin/exiv2)
        make_jpeg "$work/image.jpg"
        "$exe" -M'set Exif.Image.Artist NaSeed' "$work/image.jpg"
        "$exe" -pa "$work/image.jpg" > "$work/actual"
        busy grep -Fq NaSeed "$work/actual" ;;
      optipng)
        exe=$(original_elf /usr/bin/optipng)
        make_png "$work/source.png"
        "$exe" -o1 -out "$work/optimized.png" "$work/source.png" > "$work/stdout" 2>&1
        [ -s "$work/optimized.png" ]
        png_signature "$work/optimized.png" ;;
      jpegoptim)
        exe=$(original_elf /usr/bin/jpegoptim)
        decoder=$(original_elf /usr/bin/djpeg)
        make_jpeg "$work/image.jpg"
        "$exe" --max=80 "$work/image.jpg" > "$work/stdout"
        "$decoder" "$work/image.jpg" > "$work/decoded.ppm"
        [ -s "$work/decoded.ppm" ]
        busy grep -Fq P6 "$work/decoded.ppm" ;;
      jo)
        exe=$(original_elf /usr/bin/jo)
        "$exe" -p answer=42 name=NaSeed > "$work/actual"
        busy grep -Fq '"answer": 42' "$work/actual"
        busy grep -Fq '"name": "NaSeed"' "$work/actual" ;;
      yq)
        exe=$(original_elf /usr/bin/yq)
        printf 'record:\n  answer: 42\n' > "$work/data.yaml"
        "$exe" '.record.answer' "$work/data.yaml" > "$work/actual"
        busy grep -Fxq 42 "$work/actual" ;;
      *)
        echo "[M112 UNKNOWN APPLICATION] $app" >&2
        return 2 ;;
    esac
}

if [ "$#" -ne 0 ] && [ "$1" = --case ]; then
    [ "$#" -eq 2 ] || exit 2
    set -eu
    run_case "$2"
    exit 0
fi
[ "$#" -eq 0 ] || { echo 'usage: m112-batch-run.sh [--case APP]' >&2; exit 2; }
failed=0
for app in procs gojq gron shfmt pngquant sponge flac sox mediainfo delta \
           exiv2 optipng jpegoptim jo yq; do
    echo "[M112 CASE START] $app"
    if /bin/sh /tests/m112-batch-run.sh --case "$app"; then
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
        printf 'M112_%s_PASS\n' "$upper"
    else
        rc=$?
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
        echo "[M112 $upper FAILED] original Linux ELF or functional oracle, rc=$rc" >&2
        printf 'M112_%s_FAIL\n' "$upper"
        failed=$((failed + 1))
    fi
done
if [ "$failed" -eq 0 ]; then
    echo M112_BATCH_PASS
else
    echo "[M112 BATCH FAIL] $failed/15 signed original Linux program checks" >&2
    echo M112_BATCH_FAIL
    exit 1
fi
