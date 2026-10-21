# M7.4 — Initramfs boot-image handoff is not a new filesystem

## Facts established by previous real-program milestones

The deterministic `scripts/make-initramfs.sh` produces an uncompressed
CPIO newc **archive** containing pinned, unmodified Alpine BusyBox, musl,
and a read-only /etc/hostname. Limine delivers the archive as an opaque
module of bytes, or (M1–M4 compatibility) delivers direct ELF modules.

`kernel/bootfs.c` already owns the **read-only, zero-copy index** of
entries in these CPIO bytes, file types, symlinks and archive path lookup.
No CPIO extraction into tmpfs takes place; writable `/tmp` lives in a
different bounded RAM backend. The archive is not another new VFS or a
mountable on-disk filesystem.

Previously `kernel/m1.c` directly:
- distinguished a Limine ELF module from a CPIO newc archive;
- selected a root archive from module 0 or the legacy module 2;
- called `bootfs_mount` and selected real ELF bytes by argv0;
- selected the actual musl interpreter when available; and
- mixed that boot-specific logic with ring3 ELF mapping, page tables,
  syscall dispatch, signal delivery and process scheduling.

**M7.4 extracts only those first four responsibilities.**

```text
scripts/make-initramfs.sh  ─── generates immutable rootfs.cpio
Limine boot modules
    │
    ▼
kernel/boot_image.c       ─── module recognition, CPIO mount,
    │                          ELF/interpreter byte selection and
    │                          executable identity (Bash/BusyBox)
    ├── kernel/bootfs.c   ─── CPIO archive index and read-only file ops
    │
    ▼
kernel/m1.c               ─── actual ELF mapping and ring3 execution,
                               argv construction and syscall handlers
kernel/tmpfs.c            ─── separate writable /tmp inode backend
```

`nas_boot_prepare` returns a small immutable byte-range descriptor
and an explicit fatal error reason. The existing ELF loader is
unchanged; no extra filesystems, fake userspace, loader emulator, or
intermediate CPIO copy are introduced.

## Intentional acceptance evidence

- Unchanged previous QEMU suite including direct static ELF M1,
  real GNU Bash, Debian BusyBox, Alpine dynamic BusyBox+musl,
  single-archive CPIO ELF, the **two distinct** archive-backed authentic
  BusyBox executables, invalid argv0 and no-exec selectors, real shell
  fork/pipe/tmpfs/pathwalk.
- **New** QEMU acceptance tests for the archive-only Limine image
  **without** cmdline (`/bin/busybox echo hello` default) and with an
  explicit `naseed.argv=/bin/busybox cat /etc/hostname`, requiring
  true userspace stdout, loaded musl interpreter, and correct exit.
- The fast preflight prevents the extracted module from gaining
  syscall user pointers or ELF address-space mapping responsibilities.

## Debt deliberately not hidden

`m1.c` remains a concentrated syscall/ELF/VM implementation. Archive
mounting and executable selection are now isolated, but no general
VFS, Linux mount namespace, writeable root or deep filesystem
integration exists. Real program requirements must justify that later.
NaOS differs: its boot process unpacks initramfs into a writable tmpfs;
that is not required by the workloads NaSeed currently validates.
