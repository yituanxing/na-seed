# Real-software-first development

The long-term core of NaSeed is **real device driver support**, not just a Bash demo.

- M0: Limine + memory map/HHDM, serial UART, conservative bitmap allocator. QEMU smoke test is mandatory.
- M1: CPU exceptions, separately rooted user mappings inheriting supervisor-only kernel/HHDM mappings, static ELF and userspace ring-3 entry; Linux x86-64 syscall gate with write/exit, trace and -ENOSYS for missing calls.
- M2: Fixed static musl + GNU Bash loaded from initramfs, actual bash -c echo. Repair related semantic failures in batches.
- M3: Serial/keyboard-backed TTY, interactive Bash; then fork, exec, wait, pipes, signals and real external commands.
- Drivers: grow the driver model from real requirements: interrupt-driven serial and keyboard, PCI enumeration, block I/O, AHCI/NVMe, etc. Test both QEMU and available physical hardware.

Loop: **run real binary -> collect syscall/exception logs -> group failures -> fix semantics -> rerun -> keep regression test**.

Do not fake successes for absent syscalls. Do not reclaim Limine-owned bootloader memory while using its page tables or responses. Do not claim a stage until CI/runtime confirms it.

NaOS, Kerla and Maestro are **reference implementations**, not sources to copy wholesale.

## Verified baseline (2026-10-20)

- M0: Limine + COM1 UART + physical allocator, QEMU serial PASS.
- M1: isolated user PML4, static standalone ELF with CPL3 assertion,
  real SYSCALL/SYSRET write/-ENOSYS/exit, QEMU PASS.
- **M2: original GNU Bash 5.2.37 from a SHA256-pinned tarball, statically
  compiled with musl; host test and native `bash --noprofile --norc -c
  'echo hello'` under QEMU; observed stdout and exit(0), QEMU PASS.**
- Keep `BASH_REQUIRE_PASS=1 make bash-probe` mandatory in CI; a mere
  successful boot or syscall-probe trace is not acceptance.
- Known *nonblocking* missing Linux interfaces observed with Bash:
  `clock_gettime(228)`, `gettimeofday(96)`, `prlimit64(302)`,
  and `getrlimit(97)`. Unsupported calls return real `-ENOSYS`
  and are not faked as successes.
- **Limitations:** only single-process user execution, no scheduling or
  signals delivery (only registration/masking); memory permissions, address
  space lifecycle and TLS/FS state need hardening before multitasking;
  no actual console RX/termios, VFS, block storage, networking, or PCI yet.
  `AT_RANDOM` has an insecure early boot seed, not production entropy.
  No claim of full Linux/POSIX compatibility.
