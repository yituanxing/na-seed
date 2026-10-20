# Real-software-first development

The long-term core of NaSeed is **real device driver support**, not just a Bash demo.

- M0: Limine + memory map/HHDM, serial UART, conservative bitmap allocator. QEMU smoke test is mandatory.
- M1: CPU exceptions, separately rooted user mappings inheriting supervisor-only kernel/HHDM mappings, static ELF and userspace ring-3 entry; Linux x86-64 syscall gate with write/exit, trace and -ENOSYS for missing calls.
- M2: Fixed static musl + GNU Bash loaded as a Limine module, actual bash -c echo. (General initramfs is not yet implemented.) Repair related semantic failures in batches.
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

## Official prebuilt Linux userspace — passing regression

NaSeed uses **unmodified Debian 13 amd64 static binaries** as the default new userspace inputs, without rebuilding upstream application sources or installing a Debian rootfs. `make debian-probe` fetches exact `.deb` archives, verifies official SHA256 digests, extracts real `/usr/bin` binaries, runs a Linux-host control, then boots the **same binaries** via Limine in real x86-64 QEMU ring3. CI enforces `DEBIAN_REQUIRE_PASS=1` — a mere attempted boot is not a pass.

| Binary (official `.deb`) | SHA256 | Real QEMU acceptance |
| --- | --- | --- |
| `bash-static_5.2.37-2+b10_amd64.deb` | `a8dfc61a50e70cdf2b790dcd9e1c45e2e42f396988e7fed1c45b1444df932953` | `bash --noprofile --norc -c 'echo hello'`, stdout hello and exit 0 |
| `busybox-static_1.37.0-6+b9_amd64.deb` | `598a3fd92bdafc34cd81196b2952ad36e910e47a4ecf162f8ce5e8e262598e53` | `busybox echo hello`, stdout hello and exit 0 |

Debian's glibc exposed two **real** kernel ABI defects: incomplete page protection (`mprotect` RW/NX) and incorrect initial Linux x86-64 `RDX` (`rtld_fini` must be NULL for static binaries). Both are fixed and checked against M0, M1, original static musl Bash, Debian glibc Bash, and Debian glibc BusyBox. Remaining missing optional/fallback syscalls (`set_robust_list`, `rseq`, `prlimit64`, `readlinkat`, `getrandom`, `clock_gettime`, etc.) are still logged and return `-ENOSYS`; they are not fake-success implementations. `mprotect(PROT_NONE)` and full VMA semantics are **not** supported. No claim of interactive TTY, dynamic ELF, multi-process, complete VFS or real block storage yet; these are separate user-driven goals.

We retain the pinned original musl Bash source-built baseline as a distinct nonregression case while transitioning everyday new user programs to Debian prebuilt packages. `NASEED_SYSCALL_TRACE` can enable verbose syscall RIP/argument tracing when debugging faults.

## M4: real Alpine dynamic BusyBox — QEMU acceptance (2026-10-20)

Upstream, unmodified Alpine v3.23 x86_64 packages are fixed by version and SHA256: busybox-1.37.0-r30.apk (2fea8c73710994099eaec6832b69a0f70163e0d6ee13848af3a5270a16d15c5c), musl-1.2.5-r23.apk (4f3c4a7bf9f51d2c91007e333b17459362ffd881b4a343e8da07b6c50c4f4a0d).

Real Linux host control runs Alpine musl ldso on upstream dynamic BusyBox. QEMU boots NaSeed with distinct BusyBox ET_DYN and musl interpreter ELF modules. The kernel interprets genuine PT_INTERP, maps both ELF objects to nonoverlapping user virtual addresses, passes AT_BASE and AT_ENTRY correctly, then transfers control to the **actual musl runtime linker** in Ring 3. The linker completes startup, BusyBox prints hello, and it exits with status zero. CI requires ALPINE_REQUIRE_PASS=1 make alpine-probe; loading the module alone cannot pass.

A small read-only bootfs maps genuine Limine-provided file bytes for openat/read/lseek/fstat. It is deliberately NOT a general VFS, CPIO initramfs, PT_INTERP loader for arbitrary paths, or complete Alpine rootfs. Other shared-library dependencies, full memory-mapping semantics, interrupts, interactive TTY, processes, scheduling and genuine disk/network drivers remain separate future work. Existing M0/M1/musl Bash/Debian Bash/BusyBox regressions remain mandatory.

## M4.5 engineering checkpoint

The read-only Limine-module BootFS and one-process syscall switch remain temporary and intentionally unchanged in this checkpoint. For the completed pre-M5 audit, review `docs/audit-m45.md` before extending file or process semantics. Run `make preflight` before opening a PR; GitHub Actions repeats preflight before real QEMU acceptance. Cached musl Bash no longer recompiles solely due to checkout mtimes; the CI cache key includes the Bash builder script's content hash. To deliberately rebuild after changing the script locally: `rm -f build/userspace/bash && make bash-probe`.

## Linux syscall number policy (x86-64 native)

NaSeed's `kernel/nr.h` is generated from the **pinned Linux v6.18**
`arch/x86/entry/syscalls/syscall_64.tbl`, including only `common` and `64`
entries (383 definitions). Kernel dispatch and standalone Ring-3 tests use
Linux's official `__NR_*` spellings, not handwritten syscall integers.
`make preflight` verifies every overlapping number against the host Linux
x86-64 UAPI header, with checks for duplicates and native ABI scope. Pin
upgrades must regenerate the header and update its validation together.
**Number defined does not mean function implemented:** unknown syscall IDs
continue to return `-ENOSYS` and are traced. x32/i386 syscall ABIs are
explicitly out of scope. Linux `errno` constants are a distinct ABI contract.

## Linux errno ABI and unsupported operations (x86-64)

`kernel/errno.h` pins all 133 definitions and aliases from Linux v6.18
`include/uapi/asm-generic/errno-base.h` and `errno.h`; as with
`kernel/nr.h`, a preflight validator compares each constant with the
Linux host UAPI. The kernel's existing negative error returns are now
spelled symbolically (`-EFAULT`, `-ENOENT`, `-ENOSYS`, etc.), without
changing their numeric values or dispatch branches. The independently
assembled Ring-3 probe verifies raw `-EFAULT` and `-ENOSYS` syscall
results in real QEMU. The libc wrappers (musl/glibc), not the kernel,
convert these into `-1` and `errno`.

Linux's UAPI notes that `-ENOSYS` is normally reserved for unknown
syscall numbers; known syscalls with unsupported arguments/features should
return the **interface-prescribed** error, not a fabricated success. Legacy
partial implementations (`mmap/mprotect`, signal installation without real
delivery) still require per-behavior audits, and are **not** claimed complete.

## M5: CPIO-backed read-only files and directories (QEMU accepted, 2026-10-20)

The new fixture keeps the two original unmodified Alpine ELF modules used for
PT_INTERP execution, and supplies an additional, uncompressed CPIO newc
archive via a **third Limine module**. A bounded in-kernel parser indexes
real `/bin/busybox`, `/lib/ld-musl-x86_64.so.1`, and a deterministic
`/etc/hostname` test file without copying file contents or inventing
syscall-specific bytes. The existing M4 hardcoded module lookup is retained
as a compatibility fallback when the archive is absent.

The archive provides actual read-only directory entries, per-entry stable
inode/mode/size metadata, and read-only regular file data to open/openat,
getdents64, fstat/newfstatat, read, lseek and close. No write support,
symlinks, chdir, mount namespaces, writable tmpfs, block storage or general
VFS is claimed. `make alpine-fs-probe` checks genuine unmodified Alpine
BusyBox `ls -la /`, `ls /bin` and `cat /etc/hostname` through QEMU.
The existing M0-M4 CI probes remain mandatory. The first full PR #10
CI run failed because the authentic BusyBox `ls` used legacy Linux
`lstat` (syscall 6); adding that genuine directory/file metadata
path, alongside `stat`, allowed all three real BusyBox applets to
pass CI #39415504635. All older QEMU regressions remained green.

## M5.1 — archive-only ELF boot and real BusyBox text/path operations

The archive-only ISO contains **one userspace module**: `rootfs.cpio`.
The kernel indexes that archive and obtains the real, unmodified
`/bin/busybox` and `/lib/ld-musl-x86_64.so.1` ELF image bytes directly
from the indexed entries, then uses the same existing ELF mapper and
real musl PT_INTERP startup. A small `struct elf_image` abstracts the
byte source; it does not introduce speculative VFS interfaces or `execve`.

The earlier M4 two-module layout and M5 three-module layout remain explicit
regressions. A matching black-box QEMU matrix exercises `ls -la /`,
`ls /bin`, `cat /etc/hostname`, `find /etc -type f`,
`wc -c /etc/hostname`, `grep -n naseed /etc/hostname`,
and `head -n 1 /etc/hostname`. The expected text and byte count are
checked independently on the host; a successful boot alone cannot pass.

Limitations: currently the archive-only single-program boot selects the
known `/bin/busybox` pathname. It does not claim a general `execve`
implementation, dynamic dependencies, symlinks, signal delivery, or a
writable root filesystem.

## M5.2 — CPIO executable selection from existing cmdline argv[0]

In the archive-only mode, the first argument already supplied by Limine,
`naseed.argv=/absolute/path [arguments...]`, selects the real executable
file entry before Linux userspace begins. The same parsed argv, including
the exact argv[0], reaches the ELF Linux entry stack; the kernel does not
silently replace the program name or fall back to another ELF. The default
without a cmdline remains `/bin/busybox echo hello` for regression.

The acceptance fixture uses **two separate official, previously pinned
ELFs**, not renamed copies: Alpine dynamic-musl `/bin/busybox` and Debian
static-glibc `/usr/bin/busybox`. A real QEMU test selects each by its
path and checks independent stdout + exit status. Additional negative
tests demand explicit boot failures for a missing executable and for the
nonexecuting real `/etc/hostname` file. The M0-M5.1 regression remains
mandatory; no general execve, process creation, shell/PATH search, or
writable filesystem is claimed.

## M5.3 — actual BusyBox ash drives cwd, FD lifetime and in-memory poll

The first archive-only invocation of upstream Alpine BusyBox `sh -c ...`
was used to diagnose syscall classes **before** writing implementations.
The shell's `echo` and `pwd` builtins already worked; `cd /etc`
required Linux `chdir(80)` and per-process cwd-aware path resolution.
Real shell input redirection required `fcntl(72)` and Linux's shared
open-file-description offset semantics for `dup/dup2/dup3`, not just
copying fd table values. A builtin `read` also required `poll(7)`
readiness on a genuine memory-backed regular file. Added Linux raw
`fchdir`, `getcwd` and descriptor CLOEXEC/status flags as part of
the same single-process family. `/dev/null` now accepts sensible
character-device open flags while archive files remain read-only.

The strict QEMU black-box suite boots the **unmodified** BusyBox ash
from the one-module CPIO and verifies actual stdout and zero exit for:
`sh -c 'echo shell-ready'`, `sh -c 'pwd'`,
`sh -c 'cd /etc; pwd'`, `sh -c 'read word < /etc/hostname; echo $word'`,
`sh -c 'cd /etc; read word < hostname; echo $word'`,
stdout redirection to real `/dev/null`, and an open fd duplicated to
two sequential shell `read` calls (shared EOF/offset). Older M0–M5.2
real software regressions remain mandatory.

**Still unsupported and explicitly diagnosed, not marked as accepted:**
`sh -c '/bin/busybox cat /etc/hostname'` reaches `execve(59)`;
`sh -c 'echo abc | wc -c'` reaches `pipe(22)`. Bare shell `cat`
cannot be located because `/bin/cat` is absent from the archive;
this is a userspace rootfs/path issue, **not** a syscall success.
There is no `fork`, `execve`, pipes, scheduler, interactive TTY,
or writable root filesystem yet. `poll` only reports readiness for
in-memory files and available output devices; it returns
`-EOPNOTSUPP` for requests requiring an unsupported blocking wait.
No kernel syscall silently claims to have waited.
