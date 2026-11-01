# NaSeed M148: 35 new original Alpine codec executables

**Status: implementation prepared; zero QEMU PASS certificates until the
target workflow has run.**

M148 increases the **distinct original signed ELF pool**, not the number of
architecture repetitions of an old program. This is a coherent exercise of
real image-format conversion through the same VFS, dynamic linker, musl,
OpenFile, memory and process/FD behavior as the rest of NaSeed.

| New original ELF cohort | Distinct binaries | Genuine semantic acceptance |
|---|---:|---|
| 13 encoder+decoder pairs | 26 | Host original PBM/PPM → native guest encoder → actual intermediate format → native guest decoder → Netpbm image-geometry parser |
| Previously accepted encoder → 9 newly added decoders | 9 | Sun Icon, XBM, FITS, SGI, RLE, Palm, XV Mini, TIFF CMYK, JPEG inputs all decoded into actual PNM image data |
| **Total** | **35** | **22 independent codec roundtrip paths, 35 guest execve identities** |

The 13 pairs are ATK, CMUWM, GEM, MDA, MGR, MRF, YBM, G3 fax,
PFM floating-point, AVS, SRF, PDB image and CIS. All 35 entries are
historically unused executable paths according to the checked-in original-ELF
manifests. The official Alpine 3.23 x86-64 `netpbm-11.12.0-r0.apk` was
inspected directly: 35 actual native executable members have unique bytes
and SHA256 identities. No synthetic guest decoder or BusyBox alias is
counted, and package metadata alone never constitutes QEMU acceptance.

## Host-only provenance gates

- `scripts/stage-m148-prebuilt.sh` installs the original signed `netpbm` APK
  using the common `stage-alpine-prebuilt-overlay.sh`, rejects historical
  duplicate paths and any missing/non-ELF/BusyBox-alias payload, validates
  the target ISA, then requires 35 distinct SHA256 digests.
- `tests/m148-batch-run.sh` creates small but nontrivial monochrome and RGB
  images and performs the **real** format conversions. Each pair tests the
  result by parsing the decoded image and checking original width/height;
  no help/version probes are accepted as functionality.
- The host QEMU runner requires every single `M148_*_PASS` exactly once and
  additionally requires 35 NaSeed `[EXEC] replaced process with CPIO ELF`
  trace records proving the actual binary was executed in the guest.

## Real native QEMU gate

```
sh scripts/probe-m148-batch.sh amd64
sh scripts/probe-m148-batch.sh riscv64
sh scripts/probe-m148-batch.sh arm64
```

The PR workflow defaults to the smallest **x86-64 only** job; an explicit
workflow dispatch or `run-m148-multiarch` PR label expands to the other two
architectures after a coherent batch is ready. We must not claim any new
architecture passed until its actual serial and signed hashes are published.

There is no Docker/APK/CPIO/QEMU installation in the current workspace; only
static code/provenance checks can run locally. All future failures are to be
treated as genuine until the individual original program's fixture syntax
is compared with its upstream format specification and guest serial.
