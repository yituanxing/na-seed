#!/bin/sh
# Original signed Alpine 3.23 multimedia ELFs, never version/help scoring.
# Every admitted marker follows real media encoding, decoding, DSP, analysis,
# file metadata, or audio frame arithmetic on nontrivial deterministic content.
PATH=/usr/sbin:/sbin:/usr/bin:/bin
HOME=/tmp
LC_ALL=C
export PATH HOME LC_ALL
busy() { /bin/busybox "$@"; }

original() {
    elf="/usr/bin/$1"
    [ -x "$elf" ] || return 1
    resolved=$(busy readlink -f "$elf") || return 1
    case "$resolved" in /bin/busybox|/usr/bin/busybox) return 1 ;; esac
    magic=$(busy dd if="$resolved" bs=1 count=4 2>/dev/null |
            busy od -An -tx1 | busy tr -d ' \n')
    [ "$magic" = 7f454c46 ] || return 1
    printf '%s\n' "$elf"
}
size_gt() { [ -s "$1" ] && [ "$(busy wc -c < "$1")" -gt "$2" ]; }

check_audio() {
    input=$1
    [ -s "$input" ] || return 1
    /usr/bin/sndfile-info "$input" > "$work/audio-info.txt" 2>&1
    # A successfully parsed sound file must contain the original samplerate,
    # nonzero decoded PCM frames and actual channel metadata.
    busy grep -Eq 'Sample[[:space:]]+Rate[[:space:]]*:[[:space:]]*16000' "$work/audio-info.txt" || return 1
    busy grep -Eq 'Frames[[:space:]]*:[[:space:]]*[1-9][0-9]+' "$work/audio-info.txt" || return 1
    busy grep -Eq 'Channels[[:space:]]*:[[:space:]]*[12]' "$work/audio-info.txt"
}
check_wav() {
    f=$1
    magic=$(busy dd if="$f" bs=1 count=4 2>/dev/null)
    [ "$magic" = RIFF ] || return 1
    check_audio "$f"
}
check_pnm() {
    f=$1
    [ -s "$f" ] || return 1
    /usr/bin/pamfile "$f" > "$work/pnm-info.txt" 2>&1
    busy grep -Fq '64 by 64' "$work/pnm-info.txt"
}
check_ivf() {
    f=$1
    size_gt "$f" 40 || return 1
    [ "$(busy dd if="$f" bs=1 count=4 2>/dev/null)" = DKIF ]
}
check_yuv() {
    # Two decoded 16x16 planar YUV420 frames, 384 bytes each, not a header.
    [ "$(busy wc -c < "$1")" -eq 768 ] || return 1
    # Both encoded source frames differ, so a frozen/single-frame video
    # decoder must not be accepted merely for writing 768 zero bytes.
    first=$(busy dd if="$1" bs=384 count=1 2>/dev/null | busy sha256sum)
    second=$(busy dd if="$1" bs=384 skip=1 count=1 2>/dev/null | busy sha256sum)
    [ "$first" != "$second" ]
}

pcm_formats() {
    cv=$(original sndfile-convert)
    info=$(original sndfile-info)
    # Seven independently written and decoded real libsndfile container files.
    # The .wav is restored by the SAME unchanged ELF, then parsed afresh.
    for ext in aiff au caf w64 rf64 flac nist; do
        "$cv" /tests/m151-mono.wav "$work/media.$ext" > "$work/$ext-encode.log" 2>&1
        size_gt "$work/media.$ext" 400 || return 1
        "$cv" "$work/media.$ext" "$work/back-$ext.wav" > "$work/$ext-decode.log" 2>&1
        check_wav "$work/back-$ext.wav"
        echo "[M151 ROUNDTRIP] sndfile-$ext WAV=>$ext=>WAV"
    done
}
sndfile_cmp() {
    cmp=$(original sndfile-cmp)
    busy cp /tests/m151-mono.wav "$work/same.wav"
    "$cmp" /tests/m151-mono.wav "$work/same.wav" > "$work/cmp-same.log" 2>&1
    if "$cmp" /tests/m151-mono.wav /tests/m151-stereo.wav > "$work/cmp-diff.log" 2>&1; then
        echo '[M151 BAD CMP] identical verdict for unequal channel content' >&2
        return 1
    fi
}
sndfile_concat() {
    concat=$(original sndfile-concat)
    "$concat" /tests/m151-mono.wav /tests/m151-mono.wav "$work/join.wav" > "$work/concat.log" 2>&1
    check_wav "$work/join.wav"
    busy grep -Eq 'Frames[[:space:]]*:[[:space:]]*64000' "$work/audio-info.txt"
}
sndfile_split() {
    dec=$(original sndfile-deinterleave)
    enc=$(original sndfile-interleave)
    busy cp /tests/m151-stereo.wav "$work/original.wav"
    "$dec" "$work/original.wav" > "$work/split.log" 2>&1
    [ -s "$work/original_00.wav" ] && [ -s "$work/original_01.wav" ]
    "$enc" "$work/original_00.wav" "$work/original_01.wav" -o "$work/stereo-back.wav" \
        > "$work/join.log" 2>&1
    check_wav "$work/stereo-back.wav"
    busy grep -Eq 'Channels[[:space:]]*:[[:space:]]*2' "$work/audio-info.txt"
    echo '[M151 ROUNDTRIP] sndfile stereo=>two independent mono=>stereo'
}
sndfile_metadata() {
    setmeta=$(original sndfile-metadata-set)
    getmeta=$(original sndfile-metadata-get)
    "$setmeta" --str-title NASEED_M151_REAL_PCM /tests/m151-mono.wav "$work/tagged.wav" \
        > "$work/meta-set.log" 2>&1
    check_wav "$work/tagged.wav"
    "$getmeta" --str-title "$work/tagged.wav" > "$work/meta-get.log" 2>&1
    busy grep -Fq NASEED_M151_REAL_PCM "$work/meta-get.log"
}
sndfile_salvage() {
    salvage=$(original sndfile-salvage)
    "$salvage" /tests/m151-mono.wav "$work/salvaged.w64" > "$work/salvage.log" 2>&1
    size_gt "$work/salvaged.w64" 1000
    check_audio "$work/salvaged.w64"
    echo '[M151 TRANSCODE] sndfile WAV=>salvaged W64'
}
aubio_case() {
    binary=$1
    analyzer=$(original "$binary")
    input=/tests/m151-mono.wav
    # A two-second melody is suitable for notes/onsets but too short for
    # aubio's stateful tempo tracker. Use twelve evenly spaced real beats.
    [ "$binary" != aubiotrack ] || input=/tests/m151-beats.wav
    "$analyzer" -i "$input" > "$work/analysis.txt" 2> "$work/analysis.err"
    size_gt "$work/analysis.txt" 2 || return 1
    # All six tools emit actual timestamps, frequency/cepstra/beat events.
    busy grep -Eq '[0-9]+([.][0-9]+)?' "$work/analysis.txt"
}
fdkaac_case() {
    encoder=$(original fdkaac)
    decoder=$(original faad)
    "$encoder" -m 2 -o "$work/audio.m4a" /tests/m151-mono.wav > "$work/encode.log" 2>&1
    size_gt "$work/audio.m4a" 120 || return 1
    # Original FAAD's -w selects stdout WAV data, overriding -o entirely.
    # Use the genuine named WAV output and verify its decoded PCM metadata.
    "$decoder" -o "$work/decoded.wav" "$work/audio.m4a" > "$work/decode.log" 2>&1
    # The pinned original fdkaac mode emits HE-AAC with SBR. FAAD correctly
    # reconstructs 32 kHz stereo PCM from the 16 kHz mono encoder input:
    # demanding the source sample rate here rejects a real codec conversion.
    size_gt "$work/decoded.wav" 1000 || return 1
    [ "$(busy dd if="$work/decoded.wav" bs=1 count=4 2>/dev/null)" = RIFF ] || return 1
    /usr/bin/sndfile-info "$work/decoded.wav" > "$work/audio-info.txt" 2>&1
    busy grep -Eq 'Sample[[:space:]]+Rate[[:space:]]*:[[:space:]]*32000' "$work/audio-info.txt" || return 1
    busy grep -Eq 'Frames[[:space:]]*:[[:space:]]*[1-9][0-9]+' "$work/audio-info.txt" || return 1
    busy grep -Eq 'Channels[[:space:]]*:[[:space:]]*2' "$work/audio-info.txt" || return 1
    echo '[M151 ROUNDTRIP] FDK AAC MP4=>FAAD WAV decode'
}
audiofile_case() {
    converter=$(original sfconvert)
    inspector=$(original sfinfo)
    for spec in 'aiff:aif' 'next:snd' 'aifc:aifc' 'nist:nist'; do
        fmt=${spec%%:*}
        ext=${spec#*:}
        "$converter" /tests/m151-mono.wav "$work/media.$ext" format "$fmt" \
            > "$work/encode-$fmt.log" 2>&1
        size_gt "$work/media.$ext" 400 || return 1
        "$inspector" "$work/media.$ext" > "$work/info-$fmt.log" 2>&1
        size_gt "$work/info-$fmt.log" 30 || return 1
        "$converter" "$work/media.$ext" "$work/back-$fmt.wav" format wave \
            > "$work/decode-$fmt.log" 2>&1
        check_wav "$work/back-$fmt.wav"
        echo "[M151 ROUNDTRIP] audiofile WAV=>$fmt=>WAV"
    done
}
rubberband_case() {
    dsp=$(original rubberband)
    cv=$(original sndfile-convert)
    for ratio in 1.20 0.80; do
        "$dsp" -t "$ratio" /tests/m151-mono.wav "$work/dsp-$ratio.wav" > "$work/dsp-$ratio.log" 2>&1
        check_wav "$work/dsp-$ratio.wav"
        if busy grep -Eq 'Frames[[:space:]]*:[[:space:]]*32000' "$work/audio-info.txt"; then return 1; fi
        "$cv" "$work/dsp-$ratio.wav" "$work/transform-$ratio.aiff" > "$work/ai-$ratio.log" 2>&1
        "$cv" "$work/transform-$ratio.aiff" "$work/restored-$ratio.wav" > "$work/back-$ratio.log" 2>&1
        check_wav "$work/restored-$ratio.wav"
        echo "[M151 ROUNDTRIP] rubberband $ratio WAV=>AIFF=>WAV"
    done
}
soundstretch_case() {
    dsp=$(original soundstretch)
    cv=$(original sndfile-convert)
    for tempo in 15 -12; do
        "$dsp" /tests/m151-mono.wav "$work/dsp-$tempo.wav" "-tempo=$tempo" > "$work/dsp-$tempo.log" 2>&1
        check_wav "$work/dsp-$tempo.wav"
        if busy grep -Eq 'Frames[[:space:]]*:[[:space:]]*32000' "$work/audio-info.txt"; then return 1; fi
        "$cv" "$work/dsp-$tempo.wav" "$work/transform-$tempo.aiff" > "$work/ai-$tempo.log" 2>&1
        "$cv" "$work/transform-$tempo.aiff" "$work/restored-$tempo.wav" > "$work/back-$tempo.log" 2>&1
        check_wav "$work/restored-$tempo.wav"
        echo "[M151 ROUNDTRIP] soundstretch $tempo WAV=>AIFF=>WAV"
    done
}
aom_case() {
    enc=$(original aomenc)
    dec=$(original aomdec)
    "$enc" --passes=1 --threads=1 --cpu-used=8 --ivf \
        --output="$work/av1.ivf" /tests/m151-source.y4m > "$work/encode.log" 2>&1
    check_ivf "$work/av1.ivf"
    "$dec" --rawvideo -o "$work/av1.i420" "$work/av1.ivf" > "$work/decode.log" 2>&1
    check_yuv "$work/av1.i420"
    echo '[M151 ROUNDTRIP] Y4M 2 frames=>AV1 IVF=>2 YUV420 frames'
}
vpx_case() {
    enc=$(original vpxenc)
    dec=$(original vpxdec)
    "$enc" --codec=vp8 --good --cpu-used=5 --passes=1 --ivf --output="$work/vp8.ivf" \
        /tests/m151-source.y4m > "$work/encode.log" 2>&1
    check_ivf "$work/vp8.ivf"
    "$dec" --rawvideo -o "$work/vp8.i420" "$work/vp8.ivf" > "$work/decode.log" 2>&1
    check_yuv "$work/vp8.i420"
    echo '[M151 ROUNDTRIP] Y4M 2 frames=>VP8 IVF=>2 YUV420 frames'
}
jxl_case() {
    enc=$(original cjxl)
    dec=$(original djxl)
    info=$(original jxlinfo)
    "$enc" /tests/m151-source.ppm "$work/image.jxl" -d 0 -e 1 > "$work/encode.log" 2>&1
    size_gt "$work/image.jxl" 100 || return 1
    "$info" "$work/image.jxl" > "$work/jxl-info.log" 2>&1
    busy grep -Eq '64[^0-9]+64|64x64' "$work/jxl-info.log" || return 1
    "$dec" "$work/image.jxl" "$work/decoded.ppm" > "$work/decode.log" 2>&1
    check_pnm "$work/decoded.ppm"
    # -d 0 explicitly requests lossless coding. Check the actual 64*64*3
    # RGB payload bytes, not just a parsable PPM header/dimensions.
    input_rgb=$(busy tail -c 12288 /tests/m151-source.ppm | busy sha256sum)
    output_rgb=$(busy tail -c 12288 "$work/decoded.ppm" | busy sha256sum)
    [ "$input_rgb" = "$output_rgb" ]
    echo '[M151 ROUNDTRIP] JPEG XL 64x64 PPM=>JXL=>PPM'
}
heif_case() {
    enc=$(original heif-enc)
    dec=$(original heif-dec)
    info=$(original heif-info)
    "$enc" -q 80 -o "$work/image.heic" /tests/m151-source.png > "$work/encode.log" 2>&1
    size_gt "$work/image.heic" 200 || return 1
    "$info" "$work/image.heic" > "$work/heif-info.log" 2>&1
    busy grep -Eq '64x64|64 *x *64' "$work/heif-info.log" || return 1
    "$dec" "$work/image.heic" "$work/decoded.png" > "$work/decode.log" 2>&1
    size_gt "$work/decoded.png" 100 || return 1
    /usr/bin/pngtopnm "$work/decoded.png" > "$work/decoded.ppm" 2> "$work/png.log"
    check_pnm "$work/decoded.ppm"
    echo '[M151 ROUNDTRIP] PNG 64x64=>HEIC=>PNG=>PNM'
}
heif_thumbnailer_case() {
    enc=$(original heif-enc)
    thumbnailer=$(original heif-thumbnailer)
    # Convert a real 64x64 RGB PNG through the genuine HEIC encoder, then
    # invoke the independent original thumbnailer to decode and downsample.
    "$enc" -q 80 -o "$work/thumbnail-source.heic" /tests/m151-source.png \
        > "$work/encode.log" 2>&1
    size_gt "$work/thumbnail-source.heic" 200 || return 1
    "$thumbnailer" -s 32 -p "$work/thumbnail-source.heic" "$work/thumb.png" \
        > "$work/thumbnail.log" 2>&1
    size_gt "$work/thumb.png" 90 || return 1
    pngmagic=$(busy dd if="$work/thumb.png" bs=1 count=8 2>/dev/null | busy od -An -tx1 | busy tr -d ' \n')
    [ "$pngmagic" = 89504e470d0a1a0a ] || return 1
    /usr/bin/pngtopnm "$work/thumb.png" > "$work/thumb.ppm" 2> "$work/png.log"
    /usr/bin/pamfile "$work/thumb.ppm" > "$work/pnm-info.txt" 2>&1
    busy grep -Fq '32 by 32' "$work/pnm-info.txt" || return 1
    size_gt "$work/thumb.ppm" 3000 || return 1
    echo '[M151 ROUNDTRIP] PNG 64x64=>HEIC=>32x32 thumbnail PNG=>PNM'
}
jasper_case() {
    codec=$(original jasper)
    info=$(original imginfo)
    cmp=$(original imgcmp)
    # Alpine JasPer 4.2.7 defaults to lossless JPEG2000. Explicit `rate=1`
    # is a lossy target bitrate: original imgcmp returns 1 instead of 0.
    "$codec" --input /tests/m151-source.ppm --output "$work/image.jp2" \
        --output-format jp2 > "$work/encode.log" 2>&1
    size_gt "$work/image.jp2" 100 || return 1
    "$info" -f "$work/image.jp2" > "$work/jp2-info.log" 2>&1
    size_gt "$work/jp2-info.log" 15 || return 1
    "$codec" --input "$work/image.jp2" --output "$work/decoded.ppm" \
        --output-format pnm > "$work/decode.log" 2>&1
    check_pnm "$work/decoded.ppm"
    "$cmp" -m equal -f /tests/m151-source.ppm -F "$work/decoded.ppm" > "$work/compare.log" 2>&1
    # JasPer imgcmp's upstream source defines the equality distortion
    # metric as ZERO for exact pixel equality; 3 channels must all be zero.
    [ "$(busy grep -c '^0$' "$work/compare.log")" -eq 3 ]
    echo '[M151 ROUNDTRIP] PPM 64x64=>JPEG2000 JP2=>PPM, decoded pixels compared'
}
run_case() {
    case_id=$1
    work="/tmp/m151-$case_id"
    busy mkdir -p "$work"
    case "$case_id" in
        pcm) pcm_formats ;; cmp) sndfile_cmp ;; concat) sndfile_concat ;;
        split) sndfile_split ;; metadata) sndfile_metadata ;;
        salvage) sndfile_salvage ;;
        aubiomfcc|aubionotes|aubioonset|aubiopitch|aubioquiet|aubiotrack)
            aubio_case "$case_id" ;;
        thumbnail) heif_thumbnailer_case ;; fdkaac) fdkaac_case ;; audiofile) audiofile_case ;;
        rubberband) rubberband_case ;; soundstretch) soundstretch_case ;;
        aom) aom_case ;; vpx) vpx_case ;; jxl) jxl_case ;;
        heif) heif_case ;; jasper) jasper_case ;;
        *) echo "[M151 UNKNOWN CASE] $case_id" >&2; return 2 ;;
    esac
}

if [ "$#" -eq 2 ] && [ "$1" = --case ]; then
    set -eu
    run_case "$2"
    exit 0
fi
[ "$#" -eq 0 ] || exit 2
failed=0
# Each ELF identity is emitted exactly once after its own actual media case.
for entry in \
    'pcm:sndfile-convert,sndfile-info' 'cmp:sndfile-cmp' \
    'concat:sndfile-concat' 'split:sndfile-deinterleave,sndfile-interleave' \
    'metadata:sndfile-metadata-set,sndfile-metadata-get' 'salvage:sndfile-salvage' \
    'aubiomfcc:aubiomfcc' 'aubionotes:aubionotes' \
    'aubioonset:aubioonset' 'aubiopitch:aubiopitch' \
    'aubioquiet:aubioquiet' 'aubiotrack:aubiotrack' \
    'thumbnail:heif-thumbnailer' 'fdkaac:fdkaac,faad' 'audiofile:sfconvert,sfinfo' \
    'rubberband:rubberband' 'soundstretch:soundstretch' \
    'aom:aomenc,aomdec' 'vpx:vpxenc,vpxdec' \
    'jxl:cjxl,djxl,jxlinfo' 'heif:heif-enc,heif-dec,heif-info' \
    'jasper:jasper,imginfo,imgcmp'; do
    case_id=${entry%%:*}
    binaries=${entry#*:}
    echo "[M151 CASE START] $case_id"
    log="/tmp/m151-$case_id-case.log"
    if /bin/sh /tests/m151-batch-run.sh --case "$case_id" > "$log" 2>&1; then
        busy grep -F '[M151 ROUNDTRIP]' "$log" || :
        saved_ifs=$IFS
        IFS=,
        for binary in $binaries; do
            upper=$(printf '%s' "$binary" | busy tr '[:lower:]-' '[:upper:]_')
            printf 'M151_%s_PASS\n' "$upper"
        done
        IFS=$saved_ifs
    else
        rc=$?
        echo "[M151 CASE FAILED] $case_id rc=$rc" >&2
        busy tail -110 "$log" >&2 || :
        for f in /tmp/m151-"$case_id"/*.log /tmp/m151-"$case_id"/*.txt; do
            if [ -s "$f" ]; then
                echo "[M151 DETAIL] $f" >&2
                busy head -c 700 "$f" >&2 || :
                echo >&2
            fi
        done
        upper=$(printf '%s' "$case_id" | busy tr '[:lower:]-' '[:upper:]_')
        printf 'M151_%s_FAIL\n' "$upper"
        failed=$((failed+1))
    fi
done
if [ "$failed" -ne 0 ]; then
    echo "[M151 BATCH FAIL] $failed/22 original functional case groups failed" >&2
    echo M151_BATCH_FAIL
    exit 1
fi
echo M151_BATCH_PASS
