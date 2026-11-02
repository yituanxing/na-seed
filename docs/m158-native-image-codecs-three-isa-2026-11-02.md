# M158: thirty new signed Alpine image codecs, with all three native ISAs

**Development branch:** `naseed-app-batch2`, Draft PR #148. The native
guest execution evidence for this M158 batch is a CI gate, not something
inferred from x86 host Linux-user testing or from successfully staging APKs.

## Actual software, not extra aliases or special kernel code

`m158-batch` imports one **original Alpine 3.23 community `netpbm` APK**
through the existing signature-checking host overlay. It contains 30
historically new, native executable paths (15 encoder/decoder pairs):

| Source or target format | Actual original Alpine ELF pair |
| --- | --- |
| PCX | `ppmtopcx` / `pcxtoppm` |
| ILBM | `ppmtoilbm` / `ilbmtoppm` |
| Leaf | `ppmtoleaf` / `leaftoppm` |
| Atari NeoChrome | `ppmtoneo` / `neotoppm` |
| HP PaintJet | `ppmtopj` / `pjtoppm` |
| Atari Degas PI1 | `ppmtopi1` / `pi1toppm` |
| FS image | `pgmtofs` / `fstopgm` |
| Lisp Machine | `pgmtolispm` / `lispmtopgm` |
| Sun icon | `pbmtoicon` / `icontopbm` |
| MacPaint | `pbmtomacp` / `macptopbm` |
| X Window Dump | `pnmtoxwd` / `xwdtopnm` |
| Windows icon | `pamtowinicon` / `winicontopam` |
| HDIFF | `pamtohdiff` / `hdifftopam` |
| SIR | `pnmtosir` / `sirtopnm` |
| YUV | `ppmtoyuv` / `yuvtoppm` |

`stage-m158-prebuilt.sh` rejects every ELF path that was admitted by an
earlier M-series manifest, checks all targets are real signed-package ELF
files of the correct architecture, disallows BusyBox aliases and requires
30 different SHA-256 image hashes. No guest compile, relink, generated
binary, install script, secondary CPIO packer, or silent duplicate is used.

## Strong guest checks

The host hook builds deterministic 8×8 image content with nonconstant
RGB samples, an Atari-compatible two-color palette, grayscale samples
and PBM bitmaps. In the guest, **each of the 30 cases** calls its own
original Alpine ELF and the genuine matching encoder/decoder to prove
format-specific output bytes or lengths, decode the result, inspect
dimensions/type with `pamfile`, crop real pixel data with `pamcut` and
inspect actual values with `pamtable`. No `--version` or `--help` passes.

The Atari decoders' fixed 320×200 canvas and MacPaint's 576×720 canvas
are checked explicitly. Where a historic format loses or expands image
information, the gate checks parseable dimensions/type and actual pixel
content instead of falsely promising byte-exact losslessness.

`check-m158-inventory.py` establishes exactly 30 manifest identities,
30 executable guest case labels, 31 independent expected markers and
15 complete two-way codec pairs. The actual NaSeed system-QEMU runner
must also find a real kernel `[EXEC]` record for every one of the
30 signed ELF paths and one, and only one, guest PASS marker per case.

## Per-round three-ISA architectural acceptance

M156 certificate-verified HTTPS, M157's preceding 30 native image programs
and M158's 30 new codec programs now all require native QEMU on **amd64,
riscv64 and arm64** whenever their affected PR checks run. The two
non-x86 ports execute the unchanged common Linux ABI, VM, VFS and
NetDevice code, using their own ISA and VirtIO-MMIO hardware providers.

The existing host static gates are:

```sh
python3 scripts/check-prebuilt-profile-catalog.py
python3 scripts/check-m157-inventory.py
python3 scripts/check-m158-inventory.py
python3 scripts/check-three-isa-core.py
python3 scripts/check-architecture-boundaries.py
```

Those checks are not guest proof. The exact-head three-ISA CI logs and
kernel `[EXEC]` records decide acceptance. If one ISA fails, classify the
actual guest/program output before modifying shared kernel contracts.

## The next missing image piece is *display*, not encoding

Earlier original Alpine applications already handle PNG, GIF, TIFF,
JPEG XL, HEIC, SVG, PNM and many older formats. There is still no
guest-visible pixel display contract: `kernel/kernel.c` declares but does
not consume Limine's framebuffer request; no `/dev/fb0`, DRM or
VirtIO-GPU interface is exposed, and the present system-QEMU acceptance
launches use headless `-display none`. `chafa` can render pixels as serial
characters, but that is not a graphical display.

The first real graphical viewer should drive a small, optional framebuffer
device API with explicit pixel format, pitch, bounds and mapping ownership;
only add VirtIO-GPU/DRM or windowing when original applications demand it.
Never move PNG/JPEG decoders into the kernel or invent a screen PASS marker.
