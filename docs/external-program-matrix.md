# From BusyBox milestones to independent real-program profiles

## Verified existing baseline
x86-64, RV64 and AArch64 boot an original Linux ELF, support POSIX
pipes, process lifecycle, TTY/termios, basic VFS, timer preemption, and
signals. The growing BusyBox 17/18-case matrix remains a compatibility
**regression** rather than the sole software chosen to demand new features.

## First independent program: Lua 5.4.8
- Download Lua's official release tarball and check its published SHA256.
- Compile unmodified POSIX Lua for each Linux ISA with its native toolchain.
- Link statically against the **target's real Linux libc**; no in-tree
  NaSeed libc, guest-specific code or modified compiler backend.
- Run exact Lua source workloads under Linux/QEMU-user as the oracle, then
  the same Lua ELF and source in a BusyBox-free CPIO on each system QEMU.
- Workloads: interpreter/GC/tables/coroutines and standard Lua libc
  fopen/fclose/read/write/remove against writable /tmp.
- A real error, missing syscall or timeout is a **failure**, never an
  invitation to invent kernel-success return values or suppress a test.
- Keep M31 time-system work isolated from this application-profile change.

The existing x86/RV/ARM boot launchers only gain an optional exact archive
input `NAS_ACCEPTANCE_CPIO`; without it, all former BusyBox fixtures and
17/18-case acceptance are unchanged. Reusing the launcher is distinct from
using BusyBox **as the program under test**.

## Delivery and CI
The dedicated `.github/workflows/lua.yml` runs a small 3-ISA matrix.
It does not rebuild the old 18-case BusyBox suite **inside** each Lua
iteration. Final mainline merges still require the established kernel
regression checks.

## Next software selection (after measuring Lua failures)
Choose a genuinely different subsystem, not another shell flavor:
native GNU coreutils file/time utilities (VFS+RTC), then nano (TTY),
then SQLite (locking/persistence), conditioned on concrete Linux syscall
traces and runtime failures. Do not enable a large VFS, RTC or scheduler
subsystem merely because it appears in another OS.
