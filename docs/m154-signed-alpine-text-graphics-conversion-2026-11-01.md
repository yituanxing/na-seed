# M154: 30 newly imported original Alpine text, graphics and layout programs

M154 extends NaSeed native process execution with **30 previously unlisted,
independent ELF executables** from authentic, signed Alpine Linux v3.23 APKs.
None is rebuilt from source, a BusyBox alias, a script, or a replacement test
binary. The host checks all ELF magic numbers, canonical installed APK paths,
and independent SHA-256 digests before building the initial RAM filesystem.

The official Alpine v3.23 `main` and `community` package indexes were checked
for the complete requested set on `x86_64`, `aarch64`, and `riscv64`; all 21
requested packages are present on all three CPU architectures.

## Authentic Alpine executables

| Task | Original ELF binaries | Real-data criterion |
| --- | --- | --- |
| PostScript/PDF layout | `a2ps`, `barcode`, `enscript`, `epsffit`, `psbook`, `psnup`, `psresize`, `psselect`, `pstops`, `gs` | Valid generated PS/EPS, page selection, resized/imposed page contents, or real PostScript text extraction |
| Text, JSON, CSV and TOML | `csview`, `csvq`, `html2text`, `jql`, `taplo`, `unrtf`, `mandoc`, `demandoc`, `enca`, `xmlcatalog` | CSV SQL row selection, format conversion, encodings, catalog resolution and expected text fields |
| Images and typography | `chafa`, `chkfont`, `figlet`, `gnuplot`, `mkbitmap`, `pango-list`, `pango-segmentation`, `pango-view`, `potrace`, `rsvg-convert` | Actual pixel/text input, raster-vector conversion, plots, font segmentation/listing and PNG/SVG output signatures |

Fixtures are generated deterministically on the **host** in
`scripts/host-configure-m154-batch.sh` and imported as guest `/tests` data:
CSV, JSON, TOML, HTML, RTF, roff, Polish ISO-8859-2/UTF-8 strings, XML
catalog, DSC four-page PS, EPS, SVG, PNG with proper CRC, PBM/PGM and XY
coordinate data. Each test executes its corresponding original ELF, checks
specific transformed output or a semantic property, and emits one exact
`M154_<APP>_PASS` marker only after its checks succeed. The group emits
`M154_BATCH_PASS` only when **all 30/30** independent tests pass; no `--help`
or `--version` counts toward this gate. The QEMU acceptance stage additionally
requires per-ELF `execve` logs and 31 exactly-once markers.

## Reproduction and proof

```sh
sh scripts/probe-m154-batch.sh amd64
sh scripts/probe-m154-batch.sh riscv64
sh scripts/probe-m154-batch.sh arm64
```

The probes run real firmware-booted **system QEMU** with each architecture's
NaSeed native kernel, not qemu-user or host execution. The PR CI workflow
`.github/workflows/m154-batch.yml` runs x86_64 by default; `workflow_dispatch`,
`run-m154-multiarch` and the shared `run-m151-155-multiarch` PR label select
the full three-architecture matrix. Failure preserves serial logs, APK
provenance and original ELF SHA-256 artifacts.

The batch belongs in `scripts/prebuilt-profile-catalog.tsv` as:

```tsv
m154-batch	rootfs/alpine/packages-m154-batch.txt	scripts/m154-original-elfs.txt	scripts/m154-rootfs-copies.tsv	main+community
```

At implementation time the 30 ELF names were checked against the historical
batch manifests, including M151–M155 peers. Only an actual native system-QEMU
CI execution establishes the final runtime pass; host-side APK inspection
and syntax checks are separate evidence and are not presented as QEMU passes.
