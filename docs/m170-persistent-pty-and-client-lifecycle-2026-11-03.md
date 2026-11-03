# M170 — Persistent PTY and recoverable client windows

This batch extends the already accepted M167–M169 user-space desktop without
moving compositor state into the kernel.

- An independently executable terminal app opens a real PTY and forks a
  persistent, interactive BusyBox Shell. It polls the PTY master and the
  compositor socket concurrently, translating Linux evdev keys to bytes and
  painting bounded scrolling text into its own shared memfd surface.
  Guest acceptance requires two distinct actual Shell command outputs,
  not two inputs or a fabricated success string.
- The compositor retains stable client role slots while compacting only its
  active z-order. Browser-initiated MSG_CLOSE, titlebar-initiated MSG_QUIT and
  unexpected client death all converge on one process/FD/mmap retirement
  path. Focus falls to the next live window; other clients remain intact.
- The M167/M168 named cases preserve their original behavior. The regular
  no-argument desktop now starts the persistent terminal and permits
  client retirement. M170 has its own named case and QMP oracle.
- Reuse M168's signed Alpine Magick/Lynx/Midnight Commander programs and
  exact guest filesystem. No signed Alpine ELF is counted again as new.
  The new three-ISA M170 workflow verifies real shell output, five hardware
  screenshots, two independent window closes and framebuffer pixel changes.

LVGL 9 is a potential next client: its display flush callback should copy
dirty rectangles to the client's memfd and acknowledge completion, then use
the existing userspace MSG_UPDATED mechanism. No LVGL source or runtime is
linked here, so a real LVGL GUI has *not* yet been accepted.

Limits: one fixed bootstrap list of trusted clients, simple ASCII terminal
glyphs rather than full ANSI/Unicode handling, and no SMP/GPU or device
hotplug changes. Kernel syscall architecture remains unchanged.
