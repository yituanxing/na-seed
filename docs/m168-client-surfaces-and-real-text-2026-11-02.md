# M168: three real desktop clients, dimensioned surfaces and program text

## Why this batch follows M167

M167 proved that two independently exec'ed processes could share a physical
screen without acquiring framebuffer or input devices. M168 expands that
contract in a single batch, keeping the Linux-kernel boundary unchanged:

- `naseed-desktop` remains the **only** `/dev/fb0` and evdev owner. It now
  keeps a bounded table of clients, z-order and focus, and polls 3+N FDs
  instead of assuming precisely two windows or a fixed render order.
- A client advertises **width and height** in its bounded READY packet,
  alongside the same real memfd capability transferred by `SCM_RIGHTS`.
  The compositor validates the dimensions and screen placement before
  mapping. It owns placement and input arbitration; the client owns pixels.
- The same executable supports M167's original two-app session and M168's
  three-app session, so the earlier successful hardware proof stays a real
  compatibility gate rather than being replaced by a similar test.
- An independent `naseed-browser` process executes two additional original
  Alpine APK-signed ELFs (`lynx` and `mc`). Lynx parses actual staged HTML;
  Midnight Commander identifies its version. Their captured output is
  drawn into a third memfd surface, with a separate focused keyboard update.
- The native `naseed-terminal` gains an opt-in text mode that renders the
  actual PTY byte stream via a tiny userspace 5×7 glyph renderer. The M167
  original color fixture is preserved when the mode is not selected.

The small hand-drawn glyph set is sufficient to prove that Linux program
text reaches *client-owned visible pixels*. It is not a full terminal
emulator, font/layout engine or LVGL replacement. There is no special
kernel desktop protocol, PS/2 backend, new framebuffer mapping, hotplug
policy, GPU driver, or per-application display driver.

## Actual programs and acceptance contracts

The four independently compiled native ELFs are `naseed-desktop`,
`naseed-image`, `naseed-terminal`, `naseed-browser`. The two newly counted
original signed Alpine ELFs are `usr/bin/lynx` and `usr/bin/mc`, distinct
from earlier signed-program manifests. Previously signed ImageMagick and
`dialog` are also re-executed, so the image and PTY branches advance with
the new browser branch rather than being omitted.

- `sh scripts/check-m168-desktop.sh` links all four static native host ELFs
  with strict warnings and runs the production helpers with three dimensioned
  surfaces, real memfd, focus/overlap/z-order and pixel glyph checks.
- `sh scripts/check-m167-desktop.sh` verifies the unchanged M167 public
  behavior through its production self-test after protocol generalization.
- `.github/workflows/m168-batch.yml` runs native QEMU on amd64, riscv64 and
  arm64. QMP injects physical absolute pointer, buttons and keyboard input
  and independently validates **seven actual framebuffer screenshots**:
  initial, image focus, image drag, browser focus, browser key, terminal
  focus and PTY key output. The shell runner requires 18 unique guest
  markers, actual `exec` evidence for Magick/dialog/Lynx/MC and correct ELF
  ISA signatures. Serial markers synchronize; screenshots prove pixels.

## Deliberately not asserted

M168's launch policy is still a small table of three trusted startup apps.
External runtime connection acceptance, unbounded windows, crash recovery,
international text, real terminal escape-sequence interpretation, and an
LVGL client rendering adapter remain separate follow-ups. Those should be
added **on top of this userspace surface protocol**, not by giving each
application direct access to the framebuffer or teaching the kernel
about window identities.

Three-ISA QEMU/physical input/real signed ELF evidence, rather than host
simulation, decides whether the batch is accepted.
