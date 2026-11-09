# M242 — original Alpine reboot in the SAME QEMU process (three-ISA PASS)

## Native result, 2026-11-09

**All three ISAs passed** the automatically push-triggered native GitHub
Actions run [42569731989](https://github.com/yituanxing/na-seed/actions/runs/42569731989),
commit `088b76815c1c5fcdb1780f243bdab6f78591d644`.
The run completed successfully in approximately 2 minutes 17 seconds,
including the parallel CI jobs' dependency installation, signed Alpine
staging, two actual guest boots per architecture, physical disk verification
and artifact upload.

| Original Alpine ISA | **Same QEMU PID** (boot 1 and boot 2) | Raw artifact ID | Result |
| --- | --- | --- | --- |
| x86-64 | `4297` | `13652464197` | **PASS** |
| RISC-V RV64 | `4611` | `13651789891` | **PASS** |
| AArch64 | `4403` | `13651854581` | **PASS** |

These process IDs are specific to their ephemeral GitHub Actions runner;
the crucial fact is that each process ID was **identical across both
authentic boots** of its own run. Each job independently reported:

```text
[M242 UART] original authenticated shell boot=1; QEMU PID=...
[M242 UART] verified first-boot physical ext4 write
[M242 UART] verified first-boot guest sync
[M242 UART] issuing original /sbin/reboot in first QEMU guest
[M242 UART] original reboot completed clean ext4 flush; waiting for physical second boot
[M242 UART] SAME QEMU PID ... booted second original Alpine
[M242 UART] original authenticated shell boot=2; QEMU PID=...
[M242 UART] verified second-boot readback of physical ext4 nonce
[M242 UART] issuing original /sbin/poweroff after second-boot persisted nonce
[M242 ORIGINAL ALPINE TWO BOOTS PASS] ...
[M242 REBOOT DISK PASS] ... clean physical ext4 ...
[M242 ORIGINAL ALPINE REBOOT NATIVE PASS] ...
```

The raw artifacts also preserve the original UART log for **both** boots;
runner summaries alone are not treated as proof. Artifacts are retained
for seven days (until 2026-11-16); this document pins the immutable run,
commit, artifact IDs and observed PID evidence after the artifact expires.

## What actually rebooted

This is **not** two separate QEMU processes, a host-side QMP reset, an
initramfs test PID1, or a custom shutdown program:

1. Verify the upstream original Alpine root and original musl/BusyBox ELF
   via the established M191 signed staging path. Use a disposable **real
   physical ext4** disk image, leaving the signed source immutable.
2. Boot the original `/sbin/init` and original OpenRC, use the serial getty
   to authenticate `root`, write `/root/m242-reboot-nonce.txt`, then `sync`.
3. Invoke original `/sbin/reboot`. Its `SIGTERM` is consumed by original
   BusyBox PID1, and only its genuine `vfork` child may request Linux
   `RB_AUTOBOOT`. The scheduler retires the stuck parent PID1 after the
   vfork child has exited, quiesces userspace, then unmounts runtime mounts
   and the upstream ext4 filesystem with a final physical device flush.
4. Only after durable unmount, issue a **real platform reset**: Q35 CF9 on
   x86, SBI SRST type-1 cold reboot on RV64, or PSCI SYSTEM_RESET via HVC
   on ARM64. x86's historical `-no-reboot` is disabled **only** in this
   opt-in profile; previous M241/other regression commands are unchanged.
5. In the same live QEMU process, observe an entirely new firmware/kernel,
   original Alpine PID1, OpenRC/getty/password login. Read the first
   boot's nonce from the same ext4 image and demand exact equality.
6. Invoke original `/sbin/poweroff`, then require final PID1 quiescence,
   durable ext4 teardown, the native QEMU process's **natural** exit, and
   released physical image lock. Independently extract the partition and
   run read-only `e2fsck` plus `debugfs` nonce readback from the host.

The strict two-epoch UART parser has 11 fast deterministic tests covering
out-of-order markers, forged second boots, stale QEMU PID and missing
nonce. Original M241 shutdown preflight continues to pass. **Real native
QEMU acceptance** was separately required and is recorded above.

## Reproduce and boundary

On a CI/native host with the M241 signed-staging, QEMU, Docker and ext4
dependencies:

```sh
sh scripts/probe-m242-alpine-reboot.sh amd64
sh scripts/probe-m242-alpine-reboot.sh riscv64
sh scripts/probe-m242-alpine-reboot.sh arm64
```

The workflow `.github/workflows/m242-original-alpine-physical-reboot.yml`
automatically triggers on the feature branch's relevant push events. No
manual workflow dispatch or desktop environment is part of this gate.

**Next, M243:** original Alpine command-line package/service lifecycle:
real upstream `apk` operations and OpenRC service configuration persisted
across a physical restart, with one bounded native boot pair per ISA.
Prioritize practical Linux userspace compatibility; a graphical desktop,
display manager and self-built GUI are explicitly out of scope.

This QEMU evidence does **not** claim a physical FPGA board has already
reset or booted Alpine. Real FPGA clock/DDR/peripheral bring-up will be a
separate board acceptance gate.
