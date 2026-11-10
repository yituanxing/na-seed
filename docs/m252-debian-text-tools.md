# M252 — original Debian `grep`, `sed`, `find` on three native NaSeed kernels

The M251 baseline already runs twelve *unmodified* Debian 13 glibc programs
in actual x86-64, RV64 and AArch64 NaSeed system-QEMU guests using a physical
VirtIO ext4 root. M252 extends this acceptance **without weakening or replacing
M251** by executing the original Debian 13 text-search programs `grep`, `sed`
and `find` (from their official `grep`, `sed`, and `findutils` packages).

## Provenance

Signed Debian `InRelease` and its authenticated `Packages` index provide the
exact architecture, version, SHA256, size and pool path of each original `.deb`
and every additionally required shared library. Stage the exact ELF bytes,
verify their class, machine type, genuine glibc `PT_INTERP`, `DT_NEEDED`
dependency closure and SHA. Never introduce a host-built replacement, a
generic host glibc library, or a CPIO shell as a substitute.

The physical ext4 root retains all M248–M251 original Debian files and the
clearly identified test-only static M250 controlling-TTY launcher. The launcher
is replaced by the authentic `dash -i` in PID1. An independent manifest for
M252 packages/ELF and signed dependencies supplements the preserved M248 and
M251 metadata.

## Execution and durability

Use only live QEMU UART commands sent after a genuine interactive shell
prompt. Verify `grep` selection from files/pipes, `sed` textual transformations
and `find` traversal over an ext4 directory created by the guest. Use exact
guest stdout lines to reject command echoes, inspect `[EXEC]` and real ELF
interpreter events for each of the three new programs, then exit the original
PID1. Finally, require clean upstream ext4 unmount and native QEMU exit 0.

The post-guest host oracle must run `e2fsck`, independently read back every
signed program and DSO from the actual disk, and observe the guest-created
`/var/tmp/m252-dir/result` with exact `M252-DURABLE\n` content. This marker
must be absent before boot; a preseeded value is a failure.

## Three-ISA acceptance

```sh
sh scripts/m252-probe-debian-text-tools.sh amd64
sh scripts/m252-probe-debian-text-tools.sh riscv64
sh scripts/m252-probe-debian-text-tools.sh arm64
```

The corresponding workflow is `.github/workflows/m252-debian-text-tools-ext4.yml`.
Host unit tests or signed package preparation **do not count as a guest PASS**.
M252 only passes when all three real system-QEMU guests execute their programs,
perform real filesystem operations and satisfy the post-shutdown disk oracle.
