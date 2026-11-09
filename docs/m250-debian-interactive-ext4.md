# M250 — original Debian glibc `dash -i` from physical ext4, real UART

M249 proved the original authenticated Debian 13 trixie `dash` plus `true`,
`cat`, `wc`, `printf` and original glibc loader/libc run as **direct PID1 from
physical VirtIO ext4** with host `e2fsck` and guest writes. M250 separately
proves the **interactive, controlling-terminal** path and does not modify or
replace any upstream Debian executable.

## Guest provenance and actual PID1

1. Fetch the same signed Debian source packages and record the independently
   verified seven original ELF SHA-256 digests (M248); stage as genuine files
   under ext4 `/usr/bin` and `/usr/lib` (M249). Original `dash` uses its
   authentic PT_INTERP glibc dynamic loader; it is not statically recompiled.
2. Add **one clearly distinguished non-Debian test-only ELF** built from
   `tests/m250-debian-tty-launch.c` (`-static -no-pie`) as the actual on-ext4
   initial process `/sbin/m250-tty-launch --console`. This file is checked for
   the native ISA and for the **absence** of a PT_INTERP. It attaches the real
   kernel-provided serial console with `TIOCSCTTY`, opens `/dev/tty`, wires all
   three standard descriptors to it, establishes canonical/ECHO/ICRNL, then
   calls `execve("/usr/bin/dash", {"/usr/bin/dash", "-i"}, ...)`.
3. The `execve` replaces the **same PID1**, from the **same physical ext4**.
   This is NOT M249's direct-from-boot original-dash PID1 case and the static
   launcher is never mislabeled an original Debian distro executable.
4. The Limine CPIO is a separate rescue archive with **no signed Debian ELF
   and no M250 launcher**; a CPIO-only acceptance success is excluded.

## Real interactive UART oracle

`scripts/m250-debian-uart.py` reuses M193's `NAS_QEMU_SERIAL_FIFO` transport,
drives one real byte at a time through QEMU PL011/SBI serial input, and waits
for **each actual `M250> ` interactive `dash -i` prompt** before sending the next
command. Its evidence distinguishes a command echo from a standalone program
output line. Eight commands prove:

- `/usr/bin/printf` prints a distinct output line;
- `/usr/bin/cat` reads the immutable physical-ext4 input;
- `/usr/bin/cat | /usr/bin/wc -c` uses a genuine kernel pipe and reports 16;
- `/usr/bin/printf > /var/tmp/m250-persisted` writes guest-owned ext4 data;
- `/usr/bin/cat` reads that write during the same guest boot;
- `/usr/bin/wc -c < /var/tmp/m250-persisted` uses input redirection (15 bytes);
- `/usr/bin/true && /usr/bin/printf` executes signed Debian coreutils;
- `exit` terminates the original interactive dash PID1 normally.

The oracle additionally requires original `/usr/bin/dash` and each coreutils
`execve` from lwext4, at least five real dynamic glibc interpreter loads,
kernel `exit 0`/native debug-exit, **successful native QEMU runner exit 0**,
and clean upstream lwext4 unmount. It then extracts the *same physical* ext4
partition, checks the MBR and clean e2fsck, confirms signed Debian ELF bytes
were unmodified, checks the test launcher still matches its hash, and checks
`/var/tmp/m250-persisted` has exactly the bytes written live from UART.

**Only the native-QEMU run + physical disk post-QEMU oracle is a PASS.** Local
host negative/ABI tests only verify contracts and cannot substitute for a
real guest boot. Unclean host-terminated shells, QEMU-user runs, an echoed
command string, an ext4 image assembled entirely on the host, or a green CI
job without serial and disk proof do not count.

## Reproduce

`sh scripts/m250-probe-debian-interactive.sh riscv64` and
`sh scripts/m250-probe-debian-interactive.sh arm64`, under the same native-QEMU,
cross-GCC, Limine seed, signed Debian package, and e2fsprogs dependencies as
the M249 CI. `.github/workflows/m250-debian-interactive-ext4.yml` runs both
architecture cases and publishes the real serial, runner, and source manifest.

The x86 native runner has the same explicit M250 300-second watchdog; x86 is
available for a separate target profile and is not claimed as passing in the
initial RV64/ARM64 M250 matrix.
