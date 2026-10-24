# M51 — original signed Alpine Git in three native NaSeed Linux kernels

## Real program, not a fake compatibility probe

Stage the unmodified `git` ELF and its signed APK-provided musl, curl,
Expat, PCRE2 and zlib dependencies, once per ISA, from Alpine v3.23.
Run on x86-64/RV64/AArch64 full NaSeed QEMU, not host Git, not a custom
C implementation of Git, and no userland package build in NaSeed.

Use the real writable /tmp tmpfs for `git init`, local user config,
add/commit/rev-list/log/status, a real uncommitted diff, a second commit,
and creation/switching of a feature branch. A third commit contains a
branch-only file that must disappear on switch away and reappear on switch
back, with clean status and correct history on each branch.

No network stack, SMP or persistent filesystem is needed or implied.
An empty tmpfs Git template avoids depending on the optional distro
init-template package; the original Git implements all repository logic.

## Architecture and CI

Kernel Task/scheduler, syscall, VFS/tmpfs, ELF and MMU policy stay in the
same 29 shared core C TUs. There must be no ISA-specific or Git-specific
syscall shortcuts. Diagnose any observed failure through actual ELF/serial
evidence, then repair the owning common kernel mechanism.

M51 appends to foundation in the existing grouped three-ISA QEMU suite.
All M38–M49 checks (36 genuine QEMU acceptance cases) remain, plus M51
on each ISA (39 cases total). Still 12 grouped installer jobs. The
seven-job existing userspace suite remains mandatory; a Git-only
three-ISA diagnostic is available via manual workflow_dispatch.

This milestone remains a **candidate** until exact-head native Git and
all historical regression gates complete successfully.

## Genuine new Linux ABI failures

The exact three-ISA run found two common problems, not Git-specific ISA defects:
- Git init hit a tmpfs read validation bug on a zero-length read for MAP_PRIVATE.
- Git add needs getrandom: x86 number 318, generic64 number 278.

Provide real shared Linux getrandom via bootloader-entropy-seeded ChaCha20,
and upgrade ELF AT_RANDOM when the same stream is available. Limine's
best-effort feature explicitly does NOT guarantee cryptographically strong
entropy: NaSeed is an experimental kernel, not a production security boundary.
If no usable boot entropy is available, getrandom fails, rather than returning
fabricated random data. Host checks independently verify a ChaCha20 vector.

M0 architecture smoke kernels deliberately contain no Linux userspace,
DRBG or syscall service. Only full-kernel builds initialise the shared RNG;
this keeps the original minimal RV64/AArch64 M0 link intact.
