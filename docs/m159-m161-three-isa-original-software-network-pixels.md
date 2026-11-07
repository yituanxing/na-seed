# M159–M161: native Alpine programs, authenticated network images and first real pixels

This is a single architectural slice across the same **x86_64, RISC-V64 and
AArch64** NaSeed kernel. The normal headless boot hardware profiles remain
untouched; graphics are an opt-in test device. Signed Linux applications run
as their original ISA-matched Alpine 3.23 ELF binaries, not host-recompiled
replacement commands.

## M159 — software used for real work

30 previously unadmitted signed Alpine ELF identities, exactly one native
QEMU semantic gate per program, across three independent upstream families:

* **8 Subversion CLI programs** create repositories, import and commit data,
  check out worktrees, inspect revisions, dump/verify filesystems and filter
  real SVN streams.
* **5 Mozilla NSS tools** create a real certificate DB, import an ephemeral
  certificate/PKCS#12 bundle, inspect crypto modules and create library
  integrity records.
* **17 Netpbm ELF programs** do actual image transformations, layout and
  pixel extraction. Nontrivial image consumers parse the results independently.

The new inventory scanner rejects paths from all historical `m*-original-elfs`
manifests. Signed APK staging verifies native ELF machine architecture and
produces a canonical CPIO archive from the package manager's rooted install.

## M160 — a real HTTPS picture rather than a test file

The independent host generates a nontrivial RGB PNG but puts neither the PNG
nor the TLS server private key into the guest archive. Two existing, previously
admitted original signed Alpine clients, `curl` and GNU `wget`, download it
through the guest VirtIO-Net / lwIP network path using CA validation, DNS and
IP SAN verification and TLS 1.2+. Guest SHA-256 and PNG signature must match
the host truth. Original `pngtopam`, `pamfile` and `pamtable` consume actual
pixel bytes; original `chafa` renders and emits real Unicode image art on the
serial console. The host TLS oracle checks the exact two GET requests.

These six *reused* original ELF identities are **not** counted as new M159
programs.

## M161 — actual display memory, still without GPU/DRM

Limine already exposes an optional UEFI framebuffer response. The kernel now
validates mode, row pitch, dimensions, RGB masks and bounded length, then
exposes a real character endpoint `/dev/fb0` only if the response is usable.
The optional device uses the **existing OpenFile/FileOps** machinery: read,
write, shared open-file offset and seek, stat, poll, and two Linux LP64 ioctls:

* `FBIOGET_VSCREENINFO` (`0x4600`, 160-byte structure)
* `FBIOGET_FSCREENINFO` (`0x4602`, 80-byte structure)

There is no duplicate devfs, no new physical-frame ownership scheme, and no
direct user pointer access from the driver. Ordinary serial-only QEMU boots
keep running without a framebuffer.

The M161 native QEMU acceptance enables the upstream firmware `ramfb` only
for its own test, then requires all of these independently observable facts:

1. Real Limine mode logging reports 32-bit RGB offsets R16/G8/B0 and 8-bit
   masks, with a width accommodating the chosen color-strip fixture.
2. Original signed Alpine `magick` decodes the host-generated RGB PPM to a
   512-pixel BGRA strip; unmodified BusyBox writes those actual bytes into
   `/dev/fb0`.
3. **New original Alpine `fbgrab` ELF** obtains fbdev mode metadata, reads
   the screen pixels and libpng-encodes `/tmp/m161-screen.png`. The guest uses
   original ImageMagick to sample four specific output pixels and checks exact
   red, green, blue, white values.
4. Independently, a host QEMU **QMP `screendump`** examines the virtual display
   output, verifies it is a valid full-size PPM, and checks those same four
   RGB pixels. The guest serial marker only triggers the screenshot; it is
   not the success oracle.

The framebuffer driver is shared source compiled on all three ISA ports;
host sanitizer and Linux `<linux/fb.h>` layout tests protect bounds and ABI.

### Why `/dev/fb0` `mmap` is intentionally unavailable

Current anonymous/file VM cleanup and fork/COW logic assumes user mappings
are backed by physical frames under the kernel PMM's ownership. Firmware
display MMIO and reserved framebuffer pages are not such frames. Mapping
them with the ordinary PTE/VMA APIs would let `munmap`, `exec` or process
exit incorrectly return them to the physical allocator. This round does not
silently corrupt those invariants to make a framebuffer viewer demo pass.
Original upstream `fbi` requires this form of mmap, so **`fbi` is not yet
accepted**. The original `fbgrab` requires only read and framebuffer ioctls.

Real GPU command submission, multi-head outputs, device hotplug, SMP and
desktop window management remain separate feature layers to add when genuine
software demands them. The physical pixel path is a foundation, not a claim
of a complete modern desktop.

## Evidence contract

The three new workflows each test `amd64`, `riscv64`, and `arm64`. Host-only
static checks or Alpine Linux-user execution alone are **not** native NaSeed
QEMU proof. Do not mark a target accepted until its exact-head workflow
uploads serial output and the native semantic assertions (and for M161, the
independent QMP screenshot) pass. Existing acceptance gates remain separate.
