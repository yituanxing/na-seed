# M190 — original GUI editors persist a real ext4 file across reboot

M189 proved only one PTY session on writable tmpfs. The M178 disk is
normally recreated, and the M187 desktop boot deliberately detaches it.
This milestone therefore reuses the exact already verified **physical**
M179/M180 raw disk through **two additional kernel boots**, without
rebuilding CPIO, preparing another fixture or truncating any disk.

- M179's own deterministic `/async/guest-write.bin` may preexist on
  subsequent boots. Tolerate only its `-EEXIST`, then still write, fsync
  and re-read the exact original bytes; do not bypass its IRQ proof.
- M180's actual worker remounts the old ext4 partition at `/async`.
  The original M180 launcher continues its full M176 network and userspace
  checks in the FIRST boot; desktop `m187` explicitly bypasses it later.
- Original Alpine Vim creates `/async/m190-editor.txt` from real keyboard
  input. Original Nano opens the same file, appends another line and saves.
  A separately executed shell checks its exact 32 bytes.
- The kernel performs its normal M180 broker shutdown, upstream ext4 unmount
  and physical FLUSH. The HOST extracts the raw MBR partition, validates
  the file and historical M179/M180 payloads and runs e2fsck.
- With no disk modifications on the host, the NEXT full kernel boot reads
  the exact file before any editor runs, then original Vim reopens it.
  Clean shutdown and independent host byte comparison repeat.

This is clean-shutdown persistence, NOT proof against sudden power loss.
The rootfs and genuine signed Alpine ELF binaries are unchanged. No new
driver, filesystem, alternate shell or scheduler is introduced. Both real
QEMU boots per ISA are required; host-only or printed markers cannot pass.
