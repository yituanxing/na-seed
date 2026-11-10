# M254 — original Debian/glibc shell process lifecycle on native NaSeed

M254 extends M252 **inside one and the same native system-QEMU guest boot per
ISA**. All original signed Debian 13 `grep`, `sed`, `find`, `dash`, coreutils,
glibc interpreter/DSO provenance, real VirtIO physical ext4, interactive UART,
and M252's 12-command proof remain. It does not replace Debian binaries with
host-made implementations, nor count a host unit test as an OS acceptance.

## New real software workload

A visibly identified test-only shell script is copied onto physical ext4 as
`/etc/m254-guest-workload.sh`. An authentic, unmodified Debian/glibc dynamic
`dash` PID1 receives the script filename over its **live UART prompt**, then
launches it as a second process. The script itself uses original signed Debian
`mkdir`, `printf`, `sed`, `grep`, `env`, and `find` programs, and standard dash:

- shell redirection, exported environment and command substitution;
- background subshell with a two-process real pipe, `$!`, `wait` and exit status;
- physical ext4 creation and subsequent independent signed `grep` readback;
- complete-line `M254-SCRIPT-PASS` plus `M254-EXIT-0` from interactive PID1.

Only after all script assertions succeed may the guest write the three unique
M254 output files. The native runner still must exit with QEMU status 0 and
the lwext4 upstream unmount/flush marker. The host must independently run
`e2fsck` and read back the exact script bytes and outputs from the physical
disk via debugfs. Before QEMU starts, those output paths **must be absent**.

The script is an explicit test workload, not an assertion that the full Debian
init/systemd boot sequence is already supported. Its separate signed `/usr/bin/dash`
and glibc/ELF load evidence prevent host-only shell execution being mistaken
for a guest PASS.

## Three-ISA acceptance

```sh
NAS_ENABLE_M254_DEBIAN_SHELL=1 sh scripts/m252-probe-debian-text-tools.sh amd64
NAS_ENABLE_M254_DEBIAN_SHELL=1 sh scripts/m252-probe-debian-text-tools.sh riscv64
NAS_ENABLE_M254_DEBIAN_SHELL=1 sh scripts/m252-probe-debian-text-tools.sh arm64
```

The `m254-debian-shell-lifecycle.yml` workflow uses the reviewed M252 setup as
a reusable workflow, but runs M254 and M252 together in **one** physical QEMU
boot for each architecture. Its three green native jobs and uploaded UART,
provenance, clean-unmount and disk oracle logs are required for a three-ISA
M254 PASS; offline Python/shell validation alone is insufficient.
