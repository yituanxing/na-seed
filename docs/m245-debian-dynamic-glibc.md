# M245 — real Debian 13 dynamic glibc on NaSeed, not an Alpine substitute

**Status: independent x86-64 gate prepared; no M245 native guest PASS has yet
been observed.** Neither an Ubuntu host binary, a Linux-user control, a
signed Debian download, nor prior *static* Debian BusyBox acceptance counts.

## One genuine original executable, one closure

The starting executable is original Debian 13 `coreutils` **`/usr/bin/printf`**
(9.7-3, amd64), a true ELF64 DYN/PIE with
`PT_INTERP=/lib64/ld-linux-x86-64.so.2` and `DT_NEEDED=libc.so.6`. Its loader
and libc are exact Debian 13 `libc6` (2.41-12+deb13u4) bytes, not host
libraries, Alpine musl, a static rebuild, a shim, or an emulator in the guest.

`scripts/m245-debian-versions.tsv` fixes both package versions. The separate
`scripts/m245-fetch-debian-dynamic.py` does an **isolated** `apt-get update`
against `trixie main` with Debian archive's `signed-by` keyring. Apt must
authenticate signed `InRelease` before `apt-cache show` may supply the exact
architecture/version `Packages` records. Each official .deb is fetched from
its signed-index pool path, and both its authenticated size and SHA256 must
match. `dpkg-deb` extracts untouched files, ELF ISA/PIE/interpreter/DT_NEEDED
are independently checked, and actual original loader/libc run original
`printf` on the Linux host as a control. Only after all checks does the
fetcher publish `HOST PREPARED, NOT GUEST PASS` and a JSON evidence manifest.

No Debian keyring, unsigned metadata, missing exact version, wrong ISA,
wrong package SHA, extra dynamic dependencies, or wrong `PT_INTERP` may
silently fall back to another package or the runner's libraries. The exact
version requirement deliberately fails closed on a future Debian update;
refresh it only after reviewing the signed repository and guest impact.

## Native guest gate

`sh scripts/m245-probe-debian-glibc.sh amd64` reuses the verified original
Debian **static** BusyBox as a boot shell *only*, stages authentic dynamic
Debian `printf` with real `libc.so.6` and `ld-linux` into one CPIO (including
the Debian merged-usr symlink paths), then independently extracts the CPIO
to compare all three original ELF files byte-for-byte with their verified
source. A standalone workload executes `sh -c 'exec /usr/bin/printf ...'`
inside real NaSeed x86-64 system-QEMU, using the preexisting native BusyBox
parity harness with its 90s `m105-*` dynamic-VFS case budget. This does not
change the Alpine M244 lane or any shared kernel/harness policy.

The independent M245 oracle requires **all** of:

1. Authenticated Debian package metadata, original ELF SHA256 and host Linux
   printf output before boot; exact CPIO byte comparison and interpreter link.
2. Actual kernel `[EXEC] replaced process with CPIO ELF: /usr/bin/printf`.
3. Actual `[DYN] real ELF interpreter loaded, entry=...` (GNU, not musl).
4. Guest's exact `M245-DEBIAN-DYNAMIC-GLIBC-OK` output and natural user
   `exit(0)`; any `[FAIL]`, timeout, missing evidence or static shell-only
   marker rejects the run.

The inherited BusyBox harness's own `static BusyBox PASS` string is **not**
the acceptance. An official glibc binary failing the guest is a genuine
kernel/runtime compatibility finding, not license to print a substitute
PASS or weaken the ELF program.

## Fast versus expensive and follow-up

Run `python3 tests/m245-debian-dynamic-host.py` and
`sh -n scripts/m245-probe-debian-glibc.sh` for subsecond offline negative
contracts; they cannot be reported as guest support. The separate automatic
scoped x86 workflow `.github/workflows/m245-debian-dynamic-glibc.yml` handles
the first native proof and preserves its real serial and authenticated host
metadata. It triggers only on relevant M245/loader changes on the feature
branch, not on every unrelated NaSeed commit.

**Not included yet:** RISC-V64 or AArch64 dynamic Debian fixture/proof,
glibc Bash, full Debian `/sbin/init`, complete Debian rootfs, networking or
desktop. Extend those only after this minimal unmodified dynamic ELF runs
on the real kernel. The existing musl Alpine 3/3 acceptance remains a
separate mandatory nonregression gate for loader changes.
