# M191 — original Alpine root filesystem on an independent ext4 disk

## Purpose and limits

NaSeed has already demonstrated original Alpine executable, GPU/input,
network and `/async` ext4 persistence through M190. M191 prepares a **real
Alpine `/`** without replacing the NaSeed kernel or manufacturing an Alpine
binary. This document covers the **host build**, not guest PID1 acceptance.
The guest root-mount/init change and genuine three-ISA QEMU evidence remain
separate gates; a successful host build must never be reported as a boot.

## Inputs and existing owners

* `rootfs/alpine/build.sh` remains the only Alpine release/key/download/APK
  owner. With `NAS_ALPINE_PROFILE=m191-rootfs`, it checks the official 3.23.7
  minirootfs SHA256, fully extracts it with numeric UID/GID ownership, and
  invokes signature-verifying `apk --arch --root --initdb --no-scripts`.
  Historical profiles still extract only release public keys, install their
  original APK sets and build CPIO exactly as before.
* `rootfs/alpine/packages-m191-rootfs.txt` declares original `alpine-base`
  and `openrc`; the official signed dependency graph supplies BusyBox, musl,
  APK and other required base packages. Package scripts for foreign ISAs do
  not run on the host; no target source compilation or ELF modification.
* `rootfs/alpine/m191-repositories` pins the actual guest's APK repositories
  to the same v3.23 main/community branch as prior M-series acceptance.
* `scripts/m191-stage-alpine-rootfs.sh` adds only mountpoint directories,
  package applet links missing due `--no-scripts`, and basic configuration
  through a guest-aware symlink resolver (a guest `/sbin -> /usr/sbin` must
  never redirect host-side writes to the host's actual `/usr/sbin`).
  It does not invent replacements for Alpine init/OpenRC. The normal
  `/etc/inittab` comes from the signed Alpine base.

Official Alpine 3.23.7 release paths:

* `https://dl-cdn.alpinelinux.org/alpine/v3.23/releases/x86_64/`
* `https://dl-cdn.alpinelinux.org/alpine/v3.23/releases/riscv64/`
* `https://dl-cdn.alpinelinux.org/alpine/v3.23/releases/aarch64/`

The minirootfs tarball is checksum-verified against the official release
checksum downloaded by the existing build owner; APK **signatures** are
validated by APK with official target-ISA release keys (no
`--allow-untrusted`). These are distinct checks; the former is not a claim
that the rootfs tarball's detached OpenPGP signature was validated.

## Host operation

Requires Docker, `sudo`, GNU tar, `readelf`, `mke2fs`, `debugfs`, `e2fsck`,
`cpio`, `curl`, SHA256 utilities and Python 3 on the host.

```sh
sh scripts/m191-stage-alpine-rootfs.sh amd64
sh scripts/m191-stage-alpine-rootfs.sh riscv64
sh scripts/m191-stage-alpine-rootfs.sh arm64
```

Each invocation produces:

* `build/external/m191-alpine-rootfs/<port>/rootfs/` — verified original
  user-space files and base host configuration, with packaged UID/GID;
* `.../requested-packages.txt` and `.../apk-install.log` — independent
  package-install audit;
* `.../alpine-root.img` — **256 MiB physical raw disk** with DOS MBR,
  partition 1 at LBA 4096 (2 MiB), 254 MiB ext4. The root partition is
  deliberately distinct from M178–M190's fixed 16 MiB `/async` disk.

The `mke2fs -d` import preserves native files, directories, symlinks and
the original package ownership. Ext4 uses the already-accepted lwext4
feature set `^metadata_csum,^64bit` with 4096-byte blocks. The disk is
published atomically only after a clean read-only `e2fsck`, exact physical
MBR inspection and independent raw disk `debugfs` content readback.
The host rejects wrong-architecture ELF files **throughout the staged tree**,
missing Alpine init/OpenRC, absent APK metadata, an incorrect repository
branch or broken critical guest symlinks.

The rootfs APK repository is pinned to v3.23 but package revisions may
change within that stable branch. The resulting tree's critical-content
digest is printed; a later fully immutable release bill of materials can
pin every APK revision as a separate improvement. Avoid interpreting repeat
builds as guaranteed byte-identical ext4 images: fresh filesystem UUID,
journal state and timestamps can differ while actual user-space contents
and physical persistence oracles remain identical.

## Tests and acceptance boundary

```sh
sh -n rootfs/alpine/build.sh scripts/m191-stage-alpine-rootfs.sh
python3 -m py_compile scripts/m191-alpine-root-disk.py scripts/m191-host-smoke.py
python3 scripts/m191-host-smoke.py
```

The dependency-free smoke exercises x86-64/RV64/AArch64 ELF identity,
original-tree metadata, guest absolute symlink containment and exact MBR
geometry. The full host stage additionally requires available Alpine
downloads and e2fsprogs; it validates the resulting disk with `debugfs` and
`e2fsck`. Genuine NaSeed PID1 boot must separately prove `/` mount,
unchanged Alpine `/sbin/init` execution, process/service startup and correct
guest serial/exit status in **each** ISA. Existing M174–M190 evidence stays
unchanged; no synthesized guest success markers count.

## Kernel handoff and staged runtime acceptance

M191 is explicitly selected by `NAS_ENABLE_M191_ALPINE_ROOT=1` together with
`NAS_ENABLE_LWEXT4=1 NAS_ENABLE_VIRTIO_BLK=1` on all three native kernels.
The existing Limine CPIO remains a *rescue bootstrap archive*, but its
`/bin/busybox` is not executed as PID 1. The selected ELF is opened through
NaSeed's normal VFS from the physical ext4 partition mounted at `/`.
The initial pathname comes from `naseed.argv=...` and must be absolute.
After the root is attached, the existing `/dev`, `/tmp`, `/proc` and `/sys`
pseudo-filesystems attach to that same root; they are detached before root
unmount when the process finishes.

The first native-runtime checkpoint selects the **original Alpine**
`/bin/busybox` from that ext4 `/` to execute one short-lived command.
The three existing system-QEMU runners can then verify true exit status
and output as well as the two distinct `[M191 ROOT]` serial markers.
This is a filesystem/PID1 ELF-path smoke test, **not** proof that the Alpine
distribution's init and services have booted. The next checkpoint must run
the original `/sbin/init` as long-lived PID 1, inspect genuine OpenRC and
console readiness and diagnose any missing syscalls/device integration.

The current physical root uses the established synchronous U-Boot VirtIO
block adapter. The previously proved M179/M180 interrupt-backed worker still
owns its separate `/async` filesystem; M191 does **not** claim that the
external `/` is already worker/IRQ-backed. Extending that ownership without
breaking `execve` and mmap file reads is a later architectural checkpoint.
