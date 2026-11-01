#!/bin/sh
# The host creates deterministic non-ELF PCM and image/video fixtures before
# CPIO assembly; guests execute only untouched signed Alpine APK binaries.
set -eu
[ "$#" -eq 2 ] || exit 2
port=$1 root=$2
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
case "$root" in build/external/m151-batch/*/rootfs) ;; *) exit 2 ;; esac
mkdir -p "$root/tests"
python3 - "$root/tests" <<'PY'
import math
import pathlib
import struct
import sys
import wave
import zlib

p = pathlib.Path(sys.argv[1])
rate = 16000
duration = 32000
mono = []
stereo = []
for i in range(duration):
    t = i / rate
    # Four clearly separated notes, with real silence for quiet/onset tools.
    notes = ((0.12, 0.44, 440), (0.55, 0.86, 659),
             (1.04, 1.35, 330), (1.52, 1.88, 523))
    sample = 0
    for start, end, hz in notes:
        if start <= t < end:
            envelope = min(1, (t - start) * 90, (end - t) * 90)
            sample = int(14000 * envelope * math.sin(2 * math.pi * hz * t))
            break
    mono.append(sample)
    stereo.extend((sample, int(sample * 0.64)))

def wav(path, channels, samples):
    with wave.open(str(p / path), 'wb') as out:
        out.setnchannels(channels)
        out.setsampwidth(2)
        out.setframerate(rate)
        out.writeframes(struct.pack('<' + 'h' * len(samples), *samples))

wav('m151-mono.wav', 1, mono)
wav('m151-stereo.wav', 2, stereo)

def pixel(x, y):
    # Three independent varying channels, structured rectangles, no all-zero
    # fixture that a bogus image decoder could accidentally emulate.
    return ((x * 7 + y * 3) % 256, (x * 2 + y * 13) % 256,
            (x * 11 + y * 5) % 256)

width = height = 64
pixels = bytes(v for y in range(height) for x in range(width)
               for v in pixel(x, y))
(p / 'm151-source.ppm').write_bytes(b'P6\n64 64\n255\n' + pixels)

def chunk(tag, data):
    return (struct.pack('>I', len(data)) + tag + data +
            struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff))

scanlines = b''.join(b'\0' + pixels[y * width * 3:(y + 1) * width * 3]
                     for y in range(height))
png = (b'\x89PNG\r\n\x1a\n' +
       chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) +
       chunk(b'IDAT', zlib.compress(scanlines, 7)) + chunk(b'IEND', b''))
(p / 'm151-source.png').write_bytes(png)

# Y4M 4:2:0 two-frame, visible patterned luma with distinct chroma.
video = bytearray(b'YUV4MPEG2 W16 H16 F24:1 Ip A1:1 C420jpeg\n')
for frame in range(2):
    video += b'FRAME\n'
    video += bytes(32 + ((x * 9 + y * 11 + frame * 41) % 192)
                   for y in range(16) for x in range(16))
    video += bytes(110 + frame * 15 for _ in range(64))
    video += bytes(150 - frame * 12 for _ in range(64))
(p / 'm151-source.y4m').write_bytes(video)

assert (p / 'm151-mono.wav').stat().st_size == 64044
assert (p / 'm151-source.y4m').stat().st_size > 790
assert png[:8] == b'\x89PNG\r\n\x1a\n'
print('[M151 HOST MEDIA FIXTURES PASS] 16kHz PCM, 64x64 RGB PNG/PPM, two Y4M frames')
PY
