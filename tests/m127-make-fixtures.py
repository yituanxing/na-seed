#!/usr/bin/env python3
"""Generate signed-software audio inputs on the Linux host, never in NaSeed."""

import math
import pathlib
import shutil
import struct
import subprocess
import sys
import wave


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: m127-make-fixtures.py ROOTFS_TESTS")
    if not shutil.which("ffmpeg"):
        raise SystemExit("M127 host needs ffmpeg; install it on the CI Linux host")
    root = pathlib.Path(sys.argv[1])
    root.mkdir(parents=True, exist_ok=True)
    samples = 16000 * 3
    # Dual-frequency, low-amplitude 16-bit PCM exercises lossy/lossless paths
    # without clipping and gives ReplayGain a non-silent signal to measure.
    pcm = bytearray()
    for sample in range(samples):
        second = sample / 16000
        value = round(5000 * math.sin(2 * math.pi * 440 * second)
                      + 1700 * math.sin(2 * math.pi * 659 * second))
        pcm.extend(struct.pack("<h", value))
    (root / "m127-tone.raw").write_bytes(pcm)
    with wave.open(str(root / "m127-tone.wav"), "wb") as stream:
        stream.setnchannels(1)
        stream.setsampwidth(2)
        stream.setframerate(16000)
        stream.writeframes(pcm)

    def encode(codec: str, suffix: str, *options: str) -> None:
        output = root / f"m127-tone.{suffix}"
        cmd = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
               "-i", str(root / "m127-tone.wav"), "-map", "0:a:0",
               "-c:a", codec, *options, str(output)]
        subprocess.run(cmd, check=True)
        if output.stat().st_size < 64:
            raise RuntimeError(f"M127 fixture too short: {output}")

    encode("flac", "flac", "-metadata", "TITLE=M127FLACMEMORY")
    encode("libvorbis", "ogg", "-metadata", "TITLE=M127OGGSIGNAL")
    encode("libmp3lame", "mp3", "-id3v2_version", "3", "-metadata",
           "title=M127ID3GALAXY", "-metadata", "artist=M127AUDIOENGINEER")
    encode("libspeex", "spx")
    encode("wavpack", "wv")
    (root / "m127-vorbis-comments.txt").write_text("M127ADDED=M127VORBISMOON\n", encoding="ascii")
    names = ["tone.wav", "tone.raw", "tone.flac", "tone.ogg", "tone.mp3",
             "tone.spx", "tone.wv", "vorbis-comments.txt"]
    assert all((root / f"m127-{name}").stat().st_size > 0 for name in names)
    print(f"[M127 HOST FIXTURE PASS] {len(names)} audio inputs, {samples} frames at 16000 Hz")


if __name__ == "__main__":
    main()
