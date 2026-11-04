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
