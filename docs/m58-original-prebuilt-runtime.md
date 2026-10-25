# M58 — original prebuilt GNU Bash, Tcl and OpenSSL (x86-64 / RV64 / AArch64)

## Why these three together

* **GNU Bash** exercises the real command parser, redirection, pipes,
  asynchronous subprocesses, `wait` and shell traps. BusyBox `ash` is only
  the test launcher; Bash itself is the ELF under test.
* **Tcl** exercises another real language runtime, dynamic ELF loading,
  file rename/read/write, child process pipelines and timer notifier.
* **OpenSSL** adds random entropy, digest, symmetric encryption, an Ed25519
  keypair and local X.509 signature verification. This tests crypto and
  common Linux ABI without prematurely requiring TCP/IP networking.

## Provenance and build boundaries

`scripts/stage-m58-prebuilt.sh` reuses the **already established** signed
Alpine Linux v3.23 host rootfs builder's M39 runtime output, including the
target ISA's verified signing keys and APK database. A second **host-side**
`apk --arch ... --root ... --no-scripts add` installs the official unmodified
Alpine Bash, Tcl and OpenSSL package dependency closure. The original target
ELFs are checked with `readelf -h`, not recompiled or replaced.

**No GCC, GNU Make, CMake, Ninja, Kconfig, autotools or source compilation
runs under NaSeed in this milestone.** Only execution and data operations
run in the QEMU guest. Previous M38–M57 suites remain unchanged.

Run `sh scripts/probe-m58-runtime.sh amd64` (or `riscv64`, `arm64`). Each
architecture executes `tests/m58-runtime-run.sh` in a single native system
QEMU boot, collects serial markers and refuses to pass if ANY assertion is
missing. Common test logic, package list and expectations apply to all three
architectures. M58's path-filtered workflow has no guest-compiler stage.

## Scope and acceptance

1. Real `/bin/bash` runs another Bash script through pipes, file redirection,
   arrays, a real asynchronous child and `wait`, and an `EXIT` trap.
2. Real `/usr/bin/tclsh` uses Tcl's file channels, `exec` pipelines and
   `after` / `vwait` event loop.
3. Real `/usr/bin/openssl` obtains entropy, verifies the SHA-256 of `abc`,
   encrypts/decrypts a file and signs/verifies a self-issued Ed25519 X.509
   certificate. No network service is needed.
4. The shared three-ISA CI must see all 16 explicit `M58_*_PASS` markers and
   preserve every older accepted kernel/userland capability.

Passing these cases is **not** a claim about an interactive Bash TTY
session, arbitrarily large scripts, live network TLS, or full upstream
test suites. Those are future user-driven compatibility milestones.

## Why M58 does not edit the old Alpine host builder

M55–M57 already exercise expensive guest builds. Editing the common
`rootfs/alpine/build.sh` would make their auto-triggered workflows rebuild
the guest toolchain projects even though M58 changes only installed
applications. A small host-only APK overlay keeps the old trusted package
path and avoids redundant nested Guest compilation. Actual M58 failures
will be grouped by common Task, VFS, MM, TTY or syscall ownership before
kernel modifications; the exact-head original suites remain merge gates.
