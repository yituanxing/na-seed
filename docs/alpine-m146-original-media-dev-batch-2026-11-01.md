# NaSeed M146: 24 new original Alpine media/development ELFs

**Status: staged acceptance candidate, not yet native-QEMU PASS.**

This M146 cohort intentionally expands the number of **distinct signed
unmodified Alpine 3.23 ELF identities** instead of re-counting prior M143/M144
network service executions across architectures.

| Family | Newly selected ELF identities | Functional guest proof |
|---|---:|---|
| Netpbm media conversions | 20 | PNM/PBM image data processed into FITS, GIF, HTML, SVG, TGA, Palm, PostScript, SGI, TIFF/CMYK, and scaled/stacked PNM |
| ImageMagick | 1 | Original `magick` creates a 3×2 pixel enumeration |
| PHP 8.4 | 1 | Native CLI evaluates an array transform and emits `1,4,9` |
| ELF inspection (`pax-utils`, `scanelf`) | 2 | Original native programs analyze another original signed PHP ELF |
| **Total** | **24** | **24 independent original guest executions** |

## Identity and staging invariants

- The Alpine package identities `netpbm`, `imagemagick`, `php84`, `pax-utils`,
  `scanelf` are listed in the official Alpine v3.23 x86-64, RISC-V 64 and
  AArch64 package indexes on 2026-11-01. Those indexes alone do **not** prove
  guest execution.
- The x86-64 upstream `netpbm-11.12.0-r0.apk` contains all 20 selected native
  binaries as regular executable members (not `pnmtoplainpnm` shell wrappers).
  Their 20 upstream packaged bytes have different SHA256 digests.
- `scripts/stage-m146-prebuilt.sh` rejects any path used in older
  `m*-original-elfs.txt` manifests, any BusyBox alias, non-ELF payload, escaped
  symlink, missing signed APK, or duplicate SHA256 identity.
- The common Alpine overlay uses **host-only** `apk --no-scripts` to install
  official signed binaries. No guest compiler, Make, handcrafted replacement,
  local patched executable, or test-only kernel is allowed.

## Acceptance gate

```
sh scripts/probe-m146-batch.sh amd64
sh scripts/probe-m146-batch.sh riscv64
sh scripts/probe-m146-batch.sh arm64
```

Each run must boot the ordinary native system QEMU, execute the complete guest
`tests/m146-batch-run.sh` workload, and produce 24 unique `M146_*_PASS`
markers and one `M146_BATCH_PASS`. The host probe requires each marker exactly
once. Any missing original ELF or semantic mismatch fails the batch, not an
incomplete skip disguised as a PASS.

Current environment does not have Docker, APK, CPIO, or QEMU, so those native
QEMU tests remain **unrun here**. Host package index/member checks and shell
syntax are preparation only; record a CI run URL and per-architecture pass
counts here after the actual target jobs complete.

## Follow-up after actual QEMU results

Fix failures in small coherent batches (one offending original executable and
its exact syscall/ABI cause), then rerun affected architecture/batch. Avoid
full-matrix runs after every individual change. Continue expanding unique
Alpine executables in parallel instead of redesigning the kernel for a single
synthetic fixture.
