# NaSeed M150 — signed archive/compression and filesystem forensic cohort

**Status: prepared; NO native NaSeed QEMU PASS is claimed yet.**

This cohort adds **26 new original ELF identities** from the official
Alpine v3.23 signed APK repositories, without compiling or replacing any
guest executable. The prior M146 media cohort and M147 network
cohort remain separate; this batch focuses on compressed stream roundtrips,
parity verification and authentic filesystem/disk-image interpretation.

| Signed package | New original binaries | Guest semantic proof |
|---|---:|---|
| lrzip | 1 | Long-range archive decompress and content identity |
| pixz | 1 | Indexed xz stream compress/decompress and content identity |
| plzip | 1 | Lzip stream compress/decompress and content identity |
| par2cmdline | 1 | PAR2 recovery-file creation and original-file verification |
| sleuthkit | 22 | Real 8MiB ext2 filesystem and DOS/MBR partition, directory/inode/content, block/image, block address conversion, timeline, recovery |
| **Total** | **26** | **26 independent semantic cases** |

The 22 Sleuth Kit images are `blkcalc`, `blkcat`, `blkls`, `blkstat`, `fcat`, `ffind`,
`fiwalk`, `fls`, `fsstat`, `icat`, `ifind`, `ils`, `img_cat`, `img_stat`,
`istat`, `mmcat`, `mmls`, `mmstat`, `srch_strings`, `tsk_gettimes`,
`tsk_imageinfo`, and `tsk_recover`. The other four images are `lrzip`,
`pixz`, `plzip` and `par2`.

## Provenance and acceptance

1. `scripts/stage-m150-prebuilt.sh` reads 26 absolute-inside-rootfs paths,
   searches all historical `scripts/m*-original-elfs.txt` inventories,
   calls the existing canonical signature-verifying APK overlay, and
   independently requires 26 different SHA-256 digests from real ELF files.
   It rejects any BusyBox alias, symlink escape or wrong architecture.
2. The host-only `host-configure-m150-batch.sh` creates a real ext2 filesystem
   containing `evidence.txt`, then embeds it into a DOS/MBR Linux partition.
   This exercises actual Sleuth Kit parsers; no guest mkfs or compiler runs.
3. The original 26 binaries are scheduled to run inside a native NaSeed QEMU guest under
   `tests/m150-batch-run.sh`, each reporting a distinct success marker only
   after checking real bytes, file names, inodes, partitions or recovery.
4. `scripts/probe-m150-batch.sh` requires every marker **exactly once** in
   the real guest serial and preserves APK hashes and QEMU logs as CI artifacts.
   x86-64 is the normal narrow PR gate; explicit dispatch or
   `run-m150-multiarch` broadens to RV64/AArch64.

## Current blockers and truthful scope

- The shared prebuilt profile catalog contains the `m150-batch` entry,
  registered separately by the integration owner; this component has not
  changed that shared catalog itself.
- Local static checks and source/provenance inspection are not equivalent
  to a passed NaSeed QEMU execution. Any failed syscall, CLI option or
  filesystem behavior must be diagnosed from a real guest serial, repaired
  narrowly, and then rerun in the smallest affected cohort.
- The official x86-64 Alpine APK files were inspected directly to verify
  each selected program is a unique ELF rather than a shell script. Other
  ISAs still require the signed-profile staging + native guest proof.
