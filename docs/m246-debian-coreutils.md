# M246: one-boot original Debian glibc coreutils batch

M246 is independent of the proven M245 original Debian GNU printf gate.
It does **not** claim that Debian boots as a full distribution.

## Scope and provenance

- x86-64 real NaSeed system-QEMU, not Linux user-mode QEMU or host execution.
- Debian 13 trixie signed InRelease and authenticated Packages index, with
  exact package versions pinned in the M245 Debian versions manifest.
- Four original byte-identical ELF files from signed coreutils:
  **/usr/bin/true**, **/usr/bin/cat**, **/usr/bin/wc**, **/usr/bin/printf**.
- Original glibc libc.so.6 and ld-linux-x86-64.so.2 from signed libc6.
- Pinned *static Debian BusyBox* solely launches the actual dynamic programs.
  It cannot satisfy any per-program EXEC marker.

Every program is checked for ELF64 x86-64 PIE, exact glibc PT_INTERP, and a
reviewed DT_NEEDED closure containing **only libc.so.6**. Original bytes,
merged-/usr symlinks, and the test file are verified after extracting the
final boot CPIO.

## One guest, four execution transitions

The command uses absolute paths and short-circuit AND, in this order:

1. true: successful dynamic startup and exit.
2. cat: reads the exact M246-CAT-OK line from /etc/m246-input.
3. wc: reports 12 bytes in that same file.
4. printf: emits M246-GLIBC-BATCH-OK followed by newline and exits zero.

The acceptance **requires**, from the actual QEMU serial:

- Four distinct EXEC markers identifying the original /usr/bin paths.
- At least four DYN real ELF interpreter loaded markers.
- Exact cat, wc, and printf outputs.
- Normal guest exit status zero and no kernel FAIL.

CI executes the native x86 CPIO parity harness only once. A separate offline
host contract suite deliberately does **not** claim native PASS.

## Status interpretation

The gate's success marker appears only after real QEMU serial checks pass.
Before a successful workflow run, this is a **planned test**, not proven
compatibility. Alpine/musl and full Debian/glibc boot coverage still require
their own independent acceptance.

Entry point: sh scripts/m246-probe-debian-coreutils.sh amd64.
