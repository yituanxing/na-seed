# GNU Make in NaSeed — the beginning of building on our own OS

We want to be able to **develop software inside NaSeed**. Real GNU Make
is useful in its own right: read a Makefile, resolve a target, and start
ordinary Linux shell processes from recipes.

The code is **unmodified** GNU Make 4.4.1 from the published release
tarball (SHA256 dd16fb1d67bfab79a72f5e8390735c49e3e8e70b4945a15ab1f81ddb78658fb3)
compiled as a static Linux x86-64 ELF. It and the exact Makefile first
run on native Linux with two shell recipes. Next, the same ELF reads a
CPIO-backed /tests/Makefile inside the real NaSeed native QEMU guest.

GNU Make chooses and invokes the shell as a separate process. The guest
uses original Debian static BusyBox ash as /bin/sh; it has no fake or
NaSeed-specific callbacks. The real kernel must implement the genuine
fork/exec/wait/pipe/read/stat syscalls demanded by GNU Make and the shell.
The test requires two distinct output lines from the recipes and status
zero from GNU Make. If only GNU Make --version works, the milestone fails.

This is a single limited stage, **not** full GNU Make compliance. Next,
we may run Make recipes that invoke the existing original TinyCC to
compile C and eventually create and run a new binary within NaSeed.
That next stage is driven by wanting to write C on our own machine,
not by a synthetic syscall coverage target.

## Real GNU Make optimization discovered

GNU Make optimized the original simple `echo` recipe into a direct
`execvp("echo")`. The tiny guest image had `/bin/sh` but not a separate
`/bin/echo`; Linux distributions have both. This is real behavior, not
an OS bug. The test Makefile now explicitly calls `/bin/sh -c`, exercising
GNU Make's child lifecycle and the genuine original BusyBox POSIX shell.
The test never patches GNU Make or returns simulated echo strings.

### Why the guest rootfs includes /bin/echo

GNU Make's first recipe is the ordinary shell command `echo ...`.
GNU Make legitimately optimizes a simple command by calling execve
on `echo` directly; therefore a minimal Linux-like rootfs needs an
actual command on PATH. A symlink to the **unmodified Debian BusyBox
echo applet** supplies the real program, just as BusyBox distributions
do. The second recipe explicitly executes the genuine `/bin/sh` to
cover the shell path as well. No GNU Make patch, fake syscall or
synthetic status output is used.

## GNU Make + TinyCC as a genuine native build toolchain

The unchanged GNU Make program processes a real Makefile, executes
BusyBox echo/shell, and calls the original TinyCC Linux ELF to compile
`/tests/first.c` into a nonempty `/tmp/first.o` via RAM-backed tmpfs.
An independent original BusyBox shell runs `test -s` before emitting
`naseed-compiled-object`. An identical Linux-run Makefile, Make binary
and TinyCC binary produce a real ELF relocatable object whose
`naseed_answer` symbol is checked with `readelf`. Linking and executing
that generated object inside NaSeed are a further software-driven stage.

## CI performance and syscall evidence (2026-10-23)

One GitHub Actions run took over 14 minutes because apt downloaded 48.3 MB
at only 59.6 kB/s and installed ~95 GUI/Spice and related packages as
QEMU *recommendations*. The compiler and kernel portion took seconds.
The focused acceptance now requests only missing **headless** QEMU/UEFI
packages via `--no-install-recommends`, avoids reinstalling runner tools,
and caps the dependency-install stage at six minutes. This bounds CI
latency during mirror stalls; failure is reported, not silently ignored.

The original GNU Make fails its first real guest recipe with
`echo: Function not implemented` (ENOSYS). The new Linux oracle records
actual clone/vfork/execve behavior with strace, before altering NaSeed's
fork/clone semantics. The fix must belong in the Linux syscall adapter
and shared Task/process core as appropriate, not in a Make-specific shim.

## Program configuration versus kernel VM architecture

An actual Linux process trace found the unmodified default glibc build
of GNU Make uses `posix_spawn`, internally `clone3(CLONE_VM | CLONE_VFORK
| CLONE_CLEAR_SIGHAND)`. NaSeed has a tested **private address-space
fork/exec/wait** path, but deliberately does not yet promise shared-VM
vfork continuation or Linux thread stack semantics. Mapping these flags
to ordinary fork would be an incorrect ABI shortcut.

GNU Make 4.4.1 officially supports `./configure --disable-posix-spawn`,
which compiles the upstream's existing **real fork+exec path** without
source edits. Use that upstream option for our first useful native
build workflow. Linux `strace -f` must verify genuine fork-equivalent
`clone(SIGCHLD)` and reject `clone3(CLONE_VM)` or vfork before entering
the NaSeed guest. Real shared-VM `clone3` remains a future **general
Task/VM feature**, not a fake fork alias and not a prerequisite for Make.

### Disable `vfork` as well as `posix_spawn`

Linux tracing of Make 4.4.1 showed that disabling posix_spawn alone
fell back to glibc `vfork()`, **not** the independent-VM fork semantics
provided by NaSeed. We now use the standard Autoconf portability overrides
`ac_cv_func_vfork=no ac_cv_func_vfork_works=no` with GNU Make's
`--disable-posix-spawn`; Autoconf remaps `vfork` to real `fork` in
the generated upstream config header. Linux's actual trace must show
fork-equivalent SIGCHLD children without CLONE_VM/CLONE_VFORK.
This is standard compile-time portability configuration, not a kernel
special case, source edit, or claim that NaSeed supports true vfork.
