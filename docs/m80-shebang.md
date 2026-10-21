# M8.0 — Linux execve shebang handling from authentic CPIO scripts

## Real software driver

Until this milestone `sys_execve` accepted only ELF64. A directly
executed executable shell script such as `/etc/naseed-stage.sh token`
therefore returned `ENOEXEC` instead of loading its interpreter.
A shell's own fallback would hide that missing Linux kernel capability,
so acceptance includes a standalone freestanding x86-64 ring3 caller
that has no such fallback.

## Implemented, bounded Linux semantics

- Read an executable read-only CPIO entry whose content starts with `#!`.
- Parse an absolute interpreter path and at most **one** optional
  interpreter argument from its first bounded (<=255 byte) line.
- Resolve interpreter and permissions through existing CPIO symlinks;
  only native verified ELF64 can be an interpreter in this stage.
- Replace old `argv[0]` with the interpreter, insert optional arg
  followed by the original script pathname, retain `argv[1..]` and
  all environment arguments. Overflow returns `E2BIG`.
- Pass the actual BusyBox ELF and unchanged musl PT_INTERP to the
  already transactional `execve` loader. Existing CLOEXEC, signal,
  TLS, and VM rollback logic remains in effect.
- Explicitly refuse malformed/nested `#!` with `ENOEXEC`, absent
  interpreter with `ENOENT`, and non-executable scripts with
  `EACCES`. No success return is fabricated.

The **real pinned Alpine BusyBox binary** acts as both shell and
interpreter, with /bin/sh as a CPIO symbolic applet link. No changes
to BusyBox sources or another interpreter emulator were introduced.

## Acceptance

Three independent QEMU boots with actual stdout and clean exit:
1. Direct freestanding ring3 ABI caller checks three negative errno
   codes, then successfully replaces itself via `execve` of
   `#!/bin/busybox sh` and checks argument `token`, redirect,
   pipeline and output.
2. Authentic BusyBox ash invokes `#!/bin/sh` script via direct command.
3. A real CPIO `#!/bin/sh` workflow executes mkdir/cp/mv/cat/wc/rm/
   rmdir and must return its exact output and exit 0.

All M0–M7.5 QEMU regressions, static GNU Bash, two distinct genuine
BusyBox executables, path-ABI probe and strict preflight still apply.

## Explicitly unsupported

Recursive interpreter scripts, binfmt_misc, shebang script bytes in
the volatile /tmp backend, scripts with invalid/nonabsolute
interpreter path, and Linux pathwalk corner cases. Full interactive
TTY and preemptive timer scheduling remain separate tasks.
