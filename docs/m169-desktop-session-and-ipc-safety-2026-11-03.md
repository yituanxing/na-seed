# M169: durable desktop session and bounded client surface capabilities

M167/M168 have already proven three native QEMU ports with a multi-process
userspace image, terminal and Lynx/MC document viewer. M169 hardens the **same
protocol and compositor** without introducing a kernel window manager.

- The ordinary no-argument desktop mode now runs the three client processes
  without milestone-dependent keyboard routing or a 30-second idle deadline.
  `Esc` requests orderly shutdown. M167/M168 named case selectors still
  produce their exact guest acceptance markers and Q-to-exit behavior.
- The compositor validates each received SCM_RIGHTS descriptor with
  `fstat` **before** mapping. Only a regular file of exactly the advertised
  width × height × four XRGB32 bytes is accepted. Invalid geometry, missing
  capabilities, truncated ancillary data and a short/oversized backing FD
  fail closed; no pixel is dereferenced before this gate.
- Client updates are processed by their registered window and permitted
  key regardless of the QMP acceptance stage. The browser may receive
  repeated B keys. The milestone PTY Shell intentionally remains one-shot:
  additional A keys are not sent to its already-exited child. A general
  interactive terminal emulator is separate future work.
- `scripts/check-m168-desktop.sh` now additionally runs the real M169
  host oracle: four malformed SCM_RIGHTS deliveries across actual Unix
  sockets plus stage-independent browser/terminal focus routing.
- The existing three-ISA M168 QEMU workflow is extended, not copied:
  after the initial physical-frame screenshot it **idles for 32 seconds**,
  captures an eighth frame and then proves the compositor woke by clicking
  the image and inspecting the new framebuffer pixels and serial marker.
  Existing seven frame signatures remain unchanged.

This does **not** add arbitrary clients, asynchronous damaged-rectangle
tracking, full Wayland, multi-seat, persistent PTY commands or protection
against a malicious client truncating its file *after* validation.
Those need independent lifecycle/protocol work and validation.

Gate: the PR head's M168 workflow must pass actual QEMU amd64, riscv64 and
arm64 with eight hardware screenshot stages and M169 idle-wake evidence.
