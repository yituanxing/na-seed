# M151: 35 new signed Alpine 3.23 multimedia ELF programs

Scope: add 35 **new binary paths with distinct actual SHA256**, on unchanged
NaSeed native Linux system-QEMU (`amd64`, `riscv64`, `arm64`).  All candidates
were read directly from official Alpine `v3.23` main/community `APKINDEX` and
the ELF-containing `.apk` payloads; every package exists for all three ISAs.
This is an implementation and static audit record, **not a claim that native
QEMU was run successfully**. The CI guest must independently establish PASS.

## Original programs, not BusyBox aliases or guest-built code

| Package | New ELF executable names | Count |
| --- | --- | ---: |
| `libsndfile` | `sndfile-cmp`, `sndfile-concat`, `sndfile-convert`, `sndfile-deinterleave`, `sndfile-info`, `sndfile-interleave`, `sndfile-metadata-get`, `sndfile-metadata-set`, `sndfile-salvage` | 9 |
| `aubio` | `aubiomfcc`, `aubionotes`, `aubioonset`, `aubiopitch`, `aubioquiet`, `aubiotrack` | 6 |
| `faac`, `faad2`, `fdkaac` | `faac`, `faad`, `fdkaac` | 3 |
| `audiofile` | `sfconvert`, `sfinfo` | 2 |
| `rubberband`, `soundtouch` | `rubberband`, `soundstretch` | 2 |
| `aom` | `aomdec`, `aomenc` | 2 |
| `libvpx-utils` | `vpxdec`, `vpxenc` | 2 |
| `libjxl-tools` | `cjxl`, `djxl`, `jxlinfo` | 3 |
| `libheif-tools` | `heif-dec`, `heif-enc`, `heif-info` | 3 |
| `jasper` | `imgcmp`, `imginfo`, `jasper` | 3 |
| **Total** | **22 original audio/DSP plus 13 image/video codec ELFs** | **35** |

Do **not** count `/usr/bin/heif-convert` because it is an APK symlink to
`heif-dec`, nor `rubberband-r3` because it has the *same SHA256 bytes* as
`rubberband` in the Alpine x86_64 package. The staging gate compares each
M151 path with all historical `scripts/m[0-9]*-original-elfs.txt`, runs
Alpine's official signature-verifying `apk --no-scripts` on the **host**, uses
`readelf` for the target ISA, validates ELF magic/no BusyBox alias, and checks
exactly 35 distinct SHA256 before any native guest execution. No guest ELF is
recompiled, relinked, modified, or replaced by a shim.

## Real, nontrivial audio and multimedia evidence

`scripts/host-configure-m151-batch.sh` creates **format-conforming** files
on the host, then CPIO packs them unchanged into the guest:

- A 16-bit/16-kHz PCM WAV with four separated sinusoidal notes and actual
  quiet regions, plus a two-channel PCM WAV with independent channel content.
- A nonconstant 64×64 RGB PPM, a structurally valid RGB PNG with IHDR/IDAT
  CRC32, and a two-frame 16×16 YUV4MPEG2 (YUV 4:2:0) video with changing
  luma/chroma fields.

`tests/m151-batch-run.sh` schedules 22 strict guest semantic case groups,
with **23 actual format roundtrips plus one independent WAV→W64 salvage**:

| Groups | Repeated authentic transformations | Roundtrips |
| --- | --- | ---: |
| Libsndfile | WAV→AIFF/AU/CAF/W64/RF64/FLAC/NIST→WAV | 7 |
| Interleave | Stereo→2 mono→stereo | 1 |
| Audiofile | WAV→AIFF/NeXT/AIFC/NIST→WAV | 4 |
| AAC encoders | Original FAAC ADTS→FAAD WAV, FDK MP4→FAAD WAV | 2 |
| DSP | Two Rubber Band and two SoundStretch time transforms; WAV→AIFF→WAV | 4 |
| AV1/VP8 | Two real Y4M frames→IVF→raw 768-byte YUV420 | 2 |
| Still-image codecs | RGB→JPEG XL→RGB, PNG→HEIC→PNG, PPM→JPEG2000→PPM | 3 |
| **Total** | **23 roundtrips; WAV→W64 salvage tested separately** | **23** |

Each encoding has an actual output size/header verification and genuine decoder
parsing; decoded WAV frames/rate/channel metadata are independently checked
using the original `sndfile-info`; raw video checks exact frame bytes; image
outputs are reparsed through original Alpine `pamfile`/`pngtopnm` tools.
Additionally, genuine `sndfile-cmp` positively compares equal PCM and
negatively rejects different channel content; `sndfile-concat` checks a
precisely doubled PCM frame count; title metadata is actually written/read;
the six aubio analyzers must compute nonempty numeric note, onset, pitch,
tempo, MFCC, and quiet/noisy outputs from the four-note WAV.

No `--help`, `--version`, fake pre-recorded stdout, guest-built binary, or
host replay counts toward semantic admission.

## Acceptance and CI orchestration

1. Add the authoritative catalog row (owned by the integration agent):

   ```tsv
   m151-batch	rootfs/alpine/packages-m151-batch.txt	scripts/m151-original-elfs.txt	scripts/m151-rootfs-copies.tsv	main+community
   ```

2. On pull requests, `.github/workflows/m151-batch.yml` runs genuine native
   x86_64 QEMU by default. `workflow_dispatch`, label `run-m151-multiarch`,
   or the coordinated label `run-m151-155-multiarch` additionally enable
   the real RV64 and AArch64 guest jobs.
3. `scripts/probe-m151-batch.sh` checks 35 separate exact `M151_*_PASS`
   markers, a unique `M151_BATCH_PASS`, and the original kernel's real
   `[EXEC] replaced process with CPIO ELF: /usr/bin/...` line for *each* of
   35 binaries. CI also publishes complete native serial logs, APK log,
   package list and all 35 signed original SHA256 per ISA.

Static fixture and shell syntax tests do not establish successful QEMU
semantics; only those per-ISA serial artifacts can do so. The first CI
iteration may reveal real encoder parameter variations or kernel ABI gaps;
those should be diagnosed from actual serial logs without skipping tests.
