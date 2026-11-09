# M249: original Debian/glibc processes from a real external ext4 `/`

M248 proved original signed Debian 13 (`trixie`) glibc `dash` as direct PID1
on RV64 and ARM64, but loaded its entire minimal userland from a Limine CPIO.
M249 deliberately moves the actual root and executable to a **second physical
VirtIO block drive**, using the existing tested M191/lwext4 external `/` mount.
No change to the general Linux syscall ABI or to the ELF data format is
introduced. This is a native NaSeed guest test, not `qemu-*-static`, chroot,
or host Linux execution.

## Reproducible commands

```
PYTHONDONTWRITEBYTECODE=1 python3 tests/m249-debian-ext4-host.py
sh scripts/m249-probe-debian-ext4.sh riscv64
sh scripts/m249-probe-debian-ext4.sh arm64
sh scripts/m249-probe-debian-ext4.sh amd64
```

Each probe reuses M248's signed and exact-version-pinned Debian source
acquisition (`dash`, `coreutils`, `libc6`).  The source APT InRelease is
signature verified; each original Debian package is bound to its Packages
SHA256 and declared Size, while every staged ELF must match the signed source
manifest byte-for-byte. A new 256MiB MBR disk contains an ext4 root with
the original five executables and glibc ELF objects, plus explicit CI-only
`/etc/m249-init.sh` and input data. This is a **minimal original Debian
package subset**, not a full Debian distribution or Debian's original init.

The guest's actual `naseed.argv` selects `/usr/bin/dash /etc/m249-init.sh`
from ext4; original Debian `cat`, `wc`, `true`, and `printf` are exec'd from
the same mounted disk, including asynchronous pipe/wait. The script creates
`/var/tmp/m249-persisted`, reads it back inside the guest, and exits 0.
The kernel closes guest Files, quiesces tasks, cleanly unmounts the real ext4
root, and flushes the physical virtual drive. The host extracts the final
partition, runs real `e2fsck`, compares all signed source and scripted input
bytes against their source, and checks the *guest-created durable file*.

The separate CPIO contains an already-verified static native BusyBox rescue
fixture, required by the generic Limine matrix runner. It contains none of
the five Debian ELF test subjects and **is not PID1 or the tested root**.

## Strict acceptance

The probe's final PASS requires the **actual native system-QEMU serial** to
contain every one of these observable steps:

1. Rescue CPIO indexed, followed by external physical ext4 `/` registration.
2. `boot_image.c` selected `/usr/bin/dash` from the mounted VFS, not CPIO.
3. Limine PID1 argv preserved with `/etc/m249-init.sh`.
4. Original glibc interpreter mapped dynamically for `dash` and children.
5. Four `execve` replacements sourced from `lwext4`, expected stdout, and
   unique architecture marker.
6. Kernel's *clean upstream* ext4 unmount and durable flush after exit 0.
7. Host `e2fsck` clean, original ELF bytes unchanged and exact guest-created
   `/var/tmp/m249-persisted` read back from the real partition.

Neither a prepared image, green host unit tests, successful signed APT fetch,
nor a Linux-user QEMU execution is a substitute for all seven conditions.

The existing M191 compile switch is called `NAS_ENABLE_M191_ALPINE_ROOT` for
historical reasons. Its underlying mounted-root, boot inode, and final
shutdown mechanics are generic and are used unchanged here; historical M191
serial messages still contain `Alpine`, while the distinct `[ROOTFS]` PID1
marker identifies the actual filesystem executable chosen in M249.

M249 does **not** claim a Debian service manager, login/getty, arbitrary
Debian packages, complete package-manager dependency closure, or a graphical
desktop. Those are independent subsequent acceptance levels.
