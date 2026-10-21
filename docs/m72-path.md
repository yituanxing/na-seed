# M7.2 — pathname processing boundary and symlink+dotdot audit

## Reason and NaOS comparison

The current NaOS `vfs_lookup.c` resolves a path *component by component*:
`..` is evaluated **after** following an intermediate symlink, while
`vfs_path.c` tracks dentry and mount lifetimes. NaSeed cannot sensibly
copy its mount namespace/dcache yet; we have two small known backends,
CPIO (read-only) and RAM /tmp (finite/writable), with no generic mounts.

The previous `resolve_path` in `m1.c` combined syscall user-pointer
copying, fd/cwd lookup and a **lexical** reduction of `..`, before
`bootfs_resolve` had a chance to traverse actual CPIO symlinks.

Example: /bin/cat is a genuine symlink to the BusyBox ELF, a *regular
file*. Linux must reject `/bin/cat/../busybox` with `ENOTDIR`;
lexical collapse could incorrectly return /bin/busybox and open it.

## Minimal change, no premature full VFS

- `kernel/path.[ch]`: process kernel pathname bytes, use the real CPIO
  resolver before collapsing `..` when possible, and retain the bounded
  lexical fallback for the volatile /tmp backend.
- `kernel/m1.c`: remains the owner of user-address validation and
  relative cwd/dirfd lookup. It passes only kernel-owned string inputs to
  `nas_path_resolve`. `path.c` never dereferences userspace bytes.
- One confirmed Linux ABI fix: `O_RDONLY|O_CREAT` on an **existing**
  immutable CPIO regular file does not require writing the archive.
  `O_CREAT|O_EXCL` with an existing name returns `EEXIST`;
  truncating an existing CPIO file still returns `EROFS`.
- Five untouched BusyBox ash QEMU cases check intermediate symlink+dotdot,
  ordinary file + dotdot, link-final readlink, ../ CPIO navigation, and
  relative /tmp writes; all prior tests remain mandatory.
- A **supplementary**, independent freestanding x86-64 ring3 ELF checks
  exact `openat` return codes: existing CPIO with `O_RDONLY|O_CREAT`
  succeeds and returns actual hostname bytes, `O_CREAT|O_EXCL` gets
  `EEXIST`, `O_WRONLY|O_TRUNC` and new CPIO creation get `EROFS`,
  and applet symlink+`..` gets `ENOTDIR`. It is staged only in a
  dedicated ISO and does **not** replace the unmodified BusyBox binary.

## Remaining explicit limitations

This is NOT full POSIX pathwalk or NaOS's VFS. In particular, a missing
intermediate path in the volatile /tmp tree followed by `..` is still
lexically collapsed; *the backend does not yet validate every component*.
Likewise missing intermediate CPIO entries may still be accepted after
lexical fallback. Full mount-aware pathwalk, trailing-slash handling,
link-loop error ordering, and concurrent rename are future work.
Do not announce global Linux path lookup correctness on the strength of
these five cases.
