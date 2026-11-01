# M155 — original Alpine filesystem/partition software

M155 stages nine authentic signed Alpine Linux v3.23 packages from the official
`main` and `community` repositories, on `x86_64`, `riscv64`, and `aarch64`.
Its manifest lists **33 independently hashed ELF contents**; no M123
e2fsprogs/dosfstools, M128 squashfs-tools, M150 forensic utilities or other
previously admitted program paths are counted twice. The canonical APK
overlay verifies signatures, architecture, readelf, exact resolved paths and
the 33 unique SHA256 hashes. The original binaries are not rebuilt, patched,
wrapped into BusyBox, or replaced with host utilities.

## Proof boundaries

There are two deliberately distinct guest checks:

1. **30 substantive offline image operations**, including Btrfs checks,
   root-tree discovery, logical/physical block mapping and metadata imaging;
   XFS superblock/log/recovery analysis and real file reads/writes; F2FS
   filesystem verification, creation and byte-checked I/O; EROFS creation,
   validation and byte-exact content extraction; NTFS label, MFT, file
   content, resize feasibility, clone-save/restore and fresh volume creation;
   GPT partition-table inspection and actual second-partition roundtrip.
   Each case has its own marker, and failure propagates to the QEMU gate.
2. **Eight clearly labeled ELF startup probes** for tools that need a mounted
   kernel filesystem or potentially destructive superblock operations:
   `btrfs-convert`, `btrfs-select-super`, `btrfstune`, `xfs_copy`,
   `xfs_mdrestore`, `xfs_spaceman`, `f2fscrypt`, and `parse.f2fs`.
   These bounded original-ELF argument-parser starts **are not counted as
   filesystem functions**. They merely exercise the real userspace loader.
   The host probe nevertheless requires the exact kernel `[EXEC] replaced
   process with CPIO ELF: /path` line for **all 33** manifest paths.

The images are generated on the host from actual filesystem-format tools:

| Format | Uncompressed size | What is checked |
| --- | ---: | --- |
| Btrfs | 64 MiB | Mixed block groups, superblock checksum, root tree and metadata |
| XFS | 320 MiB | Original log, allocation groups, superblock and readonly repair |
| F2FS | 64 MiB | Superblock/checkpoint, integrity and fresh format |
| NTFS | 32 MiB | MFT, Unicode label, real `evidence.txt`, clone/restore |
| GPT disk | 16 MiB | GPT protective MBR, first partition sectors, second partition write |
| EROFS | ~4 KiB | Original file bytes embedded and extracted from EROFS image |

The five large images are compressed on the host (`gzip -1 -n`, typically
approximately 2.4 MiB *total* instead of 496 MiB in CPIO) and inflated **one
at a time** in writable guest storage. Each format's uncompressed image is
removed before the next group. This avoids doubling the 320 MiB XFS image
in initramfs and keeps the peak below the 2 GiB guest RAM budget. The host
hook guards against unexpectedly large compressed fixtures.

`scripts/probe-m155-batch.sh` validates exact serial markers, 33 kernel
exec identities and eight explicitly non-semantic ELF starts. On CI, x86_64
native QEMU runs by default; `run-m155-multiarch` or the common
`run-m151-155-multiarch` pull-request label (or workflow dispatch) expands
the real test to RV64 and ARM64.

These checks do **not** claim mounted Btrfs/XFS/F2FS/NTFS/EROFS support in
NaSeed's kernel; they exercise authentic offline filesystem software against
real images through its existing POSIX file/data plane. Nor does a successful
host-only execution substitute for the required native guest QEMU evidence.
