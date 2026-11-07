# M189 — repeated genuine editors in one desktop session

## Goal

Prove the existing single-core nonpreemptive-kernel / preemptible-userspace
NaSeed can keep an actual interactive PTY, desktop compositor and original
Alpine apps alive across **multiple foreground process lifetimes**.

This batch does **not** add new ELF paths or APK counts. M188's previously
verified original Vim and historical Nano are reused, on the same M174 signed
CPIO, as distinct real child processes.

## Required runtime chain

- The unchanged M187 `dialog` QMP physical-key path returns to the Shell.
- M188 continues to edit a fresh regular tmpfs file with original Vim,
  save it and verify exact 17 bytes from a separately executed shell script.
- M189 launches **original Alpine Nano** in that still-running PTY, appends
  a second line, saves with Ctrl+O and exits with Ctrl+X. Actual PID-tagged
  kernel exec, exit and parent wait4 records must match.
- An independent script opens the same file and rejects any content other
  than the exact 35 bytes: `m188vimrealinput\nm189nanoroundtrip\n`.
- Original Alpine Vim is launched **again**, reads that modified file,
  displays a settled physical full-screen frame and exits without saving.
  Its distinct PID must also be reaped.
- The same interactive shell executes a separate `echo 189` after all
  editors, then the old desktop exits through physical Ctrl+F12.

The host-only check tests exact bytes and negative cases, not a fabricated
guest success. QMP must capture the actual 800×600 framebuffer from the
original dynamic compositor and physical keyboard path; it cannot synthesize
terminal frame pixels or infer a PASS merely from `execve`.

## Architecture and validation

No change to ISA, VFS, terminal driver, WorkQueue, scheduler, or original
Alpine editor binaries is required. The existing terminal output oracle gains
two additive M189 one-shot markers; historical M170–M188 checks remain.

Both M179 and M180 PR workflows must pass their host contract tests and
three independent x86-64, RV64 and AArch64 *native QEMU* jobs at the same
commit SHA. The editor file is on tmpfs: only the separate existing M179/M180
tests prove ext4 durable writes; no persistence across reboot is claimed
for this editor file.
