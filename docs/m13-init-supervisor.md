# M13 — Genuine userspace PID1 supervisor and adopted-zombie reaping

NaOS's `naos/src/usr/init/main.cc` creates services (ttyd,
consoled and a shell), supervises them and waitpid-reaps their exits.
The kernel owns process and wait mechanics, not the service policy.
NaSeed implements a **real static Linux-ABI PID1 user program** without
adding mlibc, separate ISA task managers or synthetic syscalls.

## Complete stage (three ISAs, one implementation)
- Build `tests/m13-init.c` for amd64/RV64/ARM64 with identical source and
  each target's existing static libc. Launch it directly as PID1 using
  Limine `naseed.argv=/bin/m13-init --naseed`, no BusyBox PID1 wrapper.
- In three consecutive epochs PID1 fork/execs the original M12
  `m12-process --naseed-limit`, waits for its successful exit, then
  calls `waitpid(-1)` to **reap the adopted grandchild**. The grandchild
  actually checks `getppid()==1`; PID1 verifies distinct child PID,
  success exit status and ECHILD after all children disappear.
- M12 itself executes 14 simultaneous blocked child processes, two
  waves of process-slot reuse, pipe/FD/VM cleanup and EAGAIN capacity.
  No fake reaping, fabricated child or applet-specific kernel code.
- PID1 also starts and waits for the **real source-built BusyBox**:
  `busybox cat /etc/hostname`. It repeats the service lifecycle three
  times. A successful boot emits `m13-init-round-ok` three times and
  `m13-init-ok`, then exits with status zero for reproducible QEMU CI.
- The existing **three QEMU full-kernel runners** gain one optional
  native ELF entry-path column in their shared TSV. Old Debian 17-case
  and Alpine dynamic 7-case Linux compatibility profiles default to
  `/bin/busybox` unchanged; only source-controlled case `init-reaper`
  names `/bin/m13-init`. Source profile increases 8→9.
- The already generic source CPIO extra-binary staging is reused.
  Host amd64 Linux first executes genuine fork->exec->wait in
  `--linux-oracle`. Foreign-ISA Linux QEMU-user lacks a reliable
  binfmt_misc host handler for child execve, so its independent oracle
  checks real target fork->wait (`--linux-fork-oracle`) instead.
  All three full **system-QEMU NaSeed** runs MUST execute real `execve`
  and validate PID1 adoption; no guest-only behavior is attributed to
  a Linux-user oracle.

### Explicit limitations
This is a **finite deterministic PID1 supervision integration test**,
not a fully persistent init daemon or full systemd/NaOS service manager.
A forever-running shell/TTY service needs terminal/session/foreground
process group and shutdown signal semantics that are not yet present.
This stage completes the previously missing fork/orphan/adopt/wait4/
reclaim chain using a standard user process, not hardcoded kernel policy.
