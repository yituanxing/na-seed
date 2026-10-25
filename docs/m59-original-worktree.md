# M59 — original prebuilt rsync + GNU diffutils + patch + file

This milestone uses **four original signed Alpine 3.23 precompiled ELF
programs**, not locally invented replacements or a guest compiler. All
three NaSeed architecture ports execute identical program logic under
full-system QEMU: x86-64, RV64 and AArch64.

## Coverage driven by actual userland requirements

- `/usr/bin/rsync`: preserve a directory and symbolic link, prove an
  unchanged incremental second run, then update a file and delete a stale
  destination with `--checksum --delete`. This tests genuine Linux VFS
  metadata, read/write/rename, symlink and directory traversal contracts.
- `/usr/bin/diff` + `/usr/bin/patch`: produce a genuine unified patch,
  require diff's exit status 1 for different content, apply it using
  upstream patch, then compare the final file bytes. This tests real
  temporary files and atomic replacement behavior.
- `/usr/bin/file`: use unmodified libmagic's installed database to
  distinguish the shipped BusyBox ELF from a newly written ASCII text file.

The `run-m59.sh` coordinator runs **three independently reported stages**
in a single QEMU boot, collecting their failures before returning failure.
Host acceptance refuses to pass unless all ten exact markers are present.
No application-name syscall stubs or architecture-specific kernel bypasses.

## Real syscall contract surfaced by the first three-ISA run

The original rsync executable started successfully but BusyBox `ln -s`
returned `ENOSYS` because NaSeed had not implemented creation of volatile
symlinks. The change is deliberately at the common VFS/tmpfs and Linux syscall
boundary, not in rsync's test or a CPU-specific dispatcher: add Linux native
`symlink`/`symlinkat`, store symbolic link targets in actual tmpfs inodes,
resolve tmpfs and immutable CPIO links in the same bounded pathwalk, and
preserve `lstat`/`readlink`/directory-entry `DT_LNK` behavior. All three
architectures share those semantics; the syscall number is decoded through
each architecture's existing ABI boundary.

## Common host packaging, no guest compilation

M58's proved signed Alpine APK overlay was moved into the single shared
`stage-alpine-prebuilt-overlay.sh` host-only helper, so adding M59 does
not duplicate the package solver, ELF ISA validation or CPIO construction.
Both milestones start from the same signed target Alpine M39 rootfs profile,
install target packages with host Docker APK `--no-scripts`, copy unchanged
test fixtures and verify the original target ELF architecture with readelf.
**NaSeed itself executes only the original binaries and their runtime
tests; no GCC/Make/CMake/Ninja/source build runs in either guest.**

M58+M59 share one three-ISA CI workflow and install QEMU once per job,
but retain **two complete independent native QEMU boots** and separate
M58/M59 marker manifests; failure of either fails the CI job. The old
M38–M57 acceptance probes and exact error markers are preserved.

Unsupported networking is not smuggled into this milestone: rsync uses
*local* source and destination directories. Network rsync/TLS and external
devices need their own real kernel network/device milestone.
