# M192 — initial genuine Alpine init boot observation

M191 is a strict, green and unchanged three-ISA gate: a signed original Alpine
BusyBox exits with status 0 after reading the physical ext4 `/`, then the
kernel unmounts and flushes the volume, and the host verifies fsck/readback.

M192's first probe is deliberately **not** a claim that OpenRC or a login
shell works. It answers the next question: does the same NaSeed kernel map
the original Alpine `/sbin/init` from ext4 `/` into ring 3 on each ISA?

Run `sh scripts/probe-m192-alpine-init.sh amd64|riscv64|arm64` on a host with
M191's Docker/QEMU/ext4/cross-build dependencies. The script:

1. Runs the original M191 guest and on-disk integrity gates unchanged.
2. Copies the **verified** official Alpine disk to a disposable writable
   physical QEMU volume; an intentionally stopped live init is not a clean
   filesystem shutdown, so the original verified disk is never corrupted.
3. Uses the established system-QEMU ISA runner and unchanged rescue CPIO,
   changing only the Limine program to `naseed.argv=/sbin/init`.
4. Preserves real serial output and rejects missing physical root, missing
   original PID1 inode/ELF-entry markers, kernel `[FAIL]`, or a PID1 exit.
5. Prints `[M192 INIT ENTRY OBSERVED]`, explicitly **not** an OpenRC or login
   PASS. It does not count a watchdog timeout as service readiness.

Serial records: `build/busybox-source-parity-x86/m192-batch.clean.log` and
`build/port-{riscv64,aarch64}/full/matrix-source/m192-batch.clean.log`.

The next M192 gate needs positive *userspace* evidence of OpenRC's default
runlevel, a real tty login prompt, and PID1 still running; the host should
stop QEMU **after** that evidence, not merely because a timer expires.
Alpine `/etc/inittab`, its signed BusyBox/musl ELF, and OpenRC remain distro
owned. Minimal NaSeed-specific configuration/`/etc/init.d` integration may
be added as needed, but no replacement PID1 or fake success marker belongs
in the kernel.

Historical NaOS used a custom initramfs Stage 1 to mount and switch root,
then preferred its own Aether Stage 2. NaSeed already mounts the real ext4
root in kernel before loading PID1; copying NaOS's `switch_root` or custom
PID1 would bypass the purpose of the original Alpine init acceptance.

## Next coherent Alpine boot compatibility batch

The initial M192 three-ISA QEMU logs showed original `/sbin/init` really
executing original `/sbin/openrc` from the physical Alpine ext4 volume.
They also showed OpenRC failing on **real missing mount(2)** for `proc` at
`/proc` and `tmpfs` at `/run`, followed by six repeated nonexistent tty1–6
getty attempts. Initial PID1 entry was observed on all three ports, but
OpenRC and login were **not** ready.

The follow-up host and kernel changes keep the original signed executables:

- Rootfs assembly only adapts original Alpine `/etc/inittab` to one real
  `/dev/ttyS0` getty and preserves its BusyBox PID1/OpenRC lines.
- Generic FS syscall dispatch maps x86-64 `mount=165, umount2=166` and
  RV64/AArch64 `mount=40, umount2=39` onto two shared semantic operations.
- Already-mounted genuine `procfs` accepts only a matching idempotent
  `proc` → `/proc` request; no arbitrary mount request receives fake success.
- Runtime `tmpfs` uses an independent mounted inode/namespace (not the
  historical `/tmp` bootfs overlay). Detach uses the VFS mount leases and
  walks its owned inode directory tree before final root reclamation.
- Unknown mount filesystems, remount/bind/propagation flags, and unsupported
  tmpfs options return errors instead of silently changing boot semantics.

The host smoke verifies independent mount roots, nested temporary files,
iterative reclaim, and idempotent serialized Alpine `inittab` adaptation.
The first M192 diagnostic was **entry-only**. The follow-up three-ISA QEMU
log on commit `2c6d98b` has now observed a real original serial login prompt
following OpenRC startup on amd64, riscv64 and arm64. The subsequent M192
probe requires those exact positive guest markers and refuses the previous
mount/TIOCSCTTY failure signatures; it must never count a timeout alone as
success. Interactive authentication and a post-login Shell are the next gate.

The next real three-ISA QEMU run then reached original `/sbin/getty`, after
OpenRC successfully cached its dependencies and returned from sysinit/boot/
default stages. Its new common failure was **`TIOCSCTTY: EPERM`**. BusyBox
getty uses the Linux-defined force argument `1` after `setsid`; the shared
TTY ioctl now recognizes that privileged session-leader ownership transfer,
resets the old session input state and releases the controlling TTY on the
session leader's final exit. Host regression exercises forced ownership,
rejected ordinary stealing, and reacquisition after a session leader exits.
This is not yet proof of a login shell or a runnable Alpine distribution.

### M192 strict-gate correction: real OpenRC sysinit failures

The positive `naseed login:` marker on run `41819669551` was genuine but
**insufficient** to establish a successful OpenRC sysinit. On run
`41820208870`, the strict negative gate caught errors *earlier* in the same
serial log even though BusyBox init later launched getty:

```
mount: mounting proc on /proc failed: Not supported
mount: mounting tmpfs on /run failed: Not supported
 * Unable to mount tmpfs on /run.
 * Can't continue.
```

Original OpenRC 0.63 `sh/init.sh.Linux.in` mounts `/proc` with
`noexec,nosuid,nodev` and `/run` with
`mode=0755,nosuid,nodev,nr_inodes=800k,size=20%,strictatime`:
https://github.com/OpenRC/openrc/blob/0.63/sh/init.sh.Linux.in

The shared NaSeed mount implementation now recognizes those specific Linux
flags, treats the already-mounted genuine procfs as idempotent with its
inherent noexec/nosuid/nodev policy, and parses actual tmpfs memory and inode
limits. Each runtime tmpfs superblock owns resource counters for allocated
data pages and live inodes, freed on truncate/unlink/umount, and strictatime
updates successful reads. Unknown options still fail; neither Alpine's
original executable nor OpenRC script is replaced. Until the revised
three-ISA QEMU run succeeds, **full sysinit success remains unverified**.
