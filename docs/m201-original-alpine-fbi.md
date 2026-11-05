# M201 — original signed Alpine `fbida-fbi` on physical NaSeed framebuffer

## Target and acceptance boundary

Run the unmodified `/usr/bin/fbi` from Alpine 3.23 `community` on all three
native NaSeed system-QEMU targets (`amd64`, `riscv64`, `arm64`). This gate
requires **both** of the following, never just a guest print:

1. Original Alpine `/sbin/init` starts OpenRC on physical ext4, with an
   authenticated original getty/login/ash shell. A signed, package-database
   visible `fbida-fbi` executable runs in the foreground and actually exits
   with status `0` after showing one PNG image.
2. Independently, host QMP `screendump` of the **real QEMU VirtIO-GPU**
   contains exactly four pixels at `(100,300)=(255,0,0)`,
   `(300,300)=(0,255,0)`, `(500,300)=(0,0,255)`,
   `(700,300)=(255,255,255)` from that original 800×600 PNG. Its captured
   PPM and original guest UART, not program-emitted pixels, are CI artifacts.

No locally compiled stand-in image viewer, fake framebuffer, synthetic QMP,
modified Alpine `fbi`, `LD_PRELOAD`, test-specific guest kernel shortcut,
or guest output-only `PASS` is accepted.

## Minimal actual execution chain

```sh
sh scripts/probe-m201-alpine-fbi.sh amd64
sh scripts/probe-m201-alpine-fbi.sh riscv64
sh scripts/probe-m201-alpine-fbi.sh arm64
```

`stage-m201-alpine-fbi.sh` builds M191's checked original official Alpine
3.23.7 ext4 host root, but **does not invoke `probe-m191-alpine-root.sh`**:
there is no duplicate first-QEMU smoke boot. It exports the same exact
Alpine BusyBox/musl rescue CPIO required by the physical ext4 root selector.
In a disposable overlay, the existing M193 helper supplies an actual original
login prompt and the M198-proven `docker alpine:3.23 apk --arch --root
--keys-dir --no-scripts` mechanism fetches and signature-verifies original
`fbida-fbi-2.14-r6` from the v3.23 **community** repository plus its original
shared-library dependencies and official `font-dejavu` from main. Installed
APK DB, original ELF SHA256, native ELF architecture and musl interpreter are
preserved in CI; `/usr/bin/fbi` is never rewritten.

Host Python only builds a test **input PNG** (800×600 pure RGB color bars),
which is copied into Alpine's disposable ext4 root. The original fbi command
is executed by the real original Alpine authenticated ash:

```sh
/usr/bin/fbi --nointeractive -d /dev/fb0 -t 15 -1 \
    /root/m201-four-bars.png >/root/m201-fbi.log 2>&1
```

Upstream `--nointeractive` avoids terminal input dependence; `-t 15 -1`
displays one frame for 15 seconds then permits real process exit. An
independent QMP watcher connects before this invocation and captures physical
pixels before the original fbi cleanup/exit; an Alpine shell step after the
original exit is held until that QMP watcher has succeeded. Timeouts,
original `fbi` nonzero status, missing fonts, missing real GPU pixels and
stale success markers are hard failures.

One real per-ISA kernel is built with the established ext4/VirtIO block and
VirtIO-GPU feature gates, then **one** original Alpine/OpenRC QEMU instance
starts. M197's much slower guest-side public HTTPS APK/Git/Bash/Lua/SQLite
transaction is not invoked. `m201-fbi` is explicitly added to the existing
strict native runners' accepted case names; historical runner assertions
remain unchanged for all prior cases.

## Evidence and runtime

- `.github/workflows/m201-original-alpine-fbi.yml`: one independent native
  case per ISA plus a fast deterministic PNG/script host contract.
- `build/external/m201-alpine-fbi/$ISA/m201-original-fbi.sha256`: untouched
  actual package ELF, with original signed APK install log and installed DB.
- `.../m201-original-fbi-screen.ppm`: physical-QMP screenshot retained.
- `.../m201-original-fbi-qmp.log`: full physical pixel oracle outcome.
- `build/*/m201-fbi.serial.log` and `.clean.log`: original Alpine OpenRC,
  original `/usr/bin/fbi` exec and actual `M201_FBI_EXEC_RC=0`.

Time is deliberately bounded by a 240-second native-QEMU cap and a separate
235-second independent QMP cap, plus host Alpine staging, package installation
and one feature-correct kernel build. These are budgets, **not measured
completion times**. Previous M197 public-WAN work is completely skipped.

Optional reuse of an explicitly existing and *fully verified* M191 Alpine
root is `NAS_M201_REUSE_SIGNED_ROOT=1`; the host source/disk check must pass.
It does not silently reuse an unknown/stale image.
