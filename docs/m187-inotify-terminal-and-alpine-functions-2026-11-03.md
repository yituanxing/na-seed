# M187: functional original Alpine tooling, Linux inotify and one VT terminal

## Scope and ownership

M187 advances the existing NaSeed **single signed rootfs/CPIO per ISA**. The
normal M179/M180 QEMU boot retains its historic desktop+ext4 proof; an
additional QMP boot selects M187's VT interaction using that **same staged
CPIO**, without a second software profile or filesystem image. It preserves
M174's historical, immutable 20 signed Alpine
candidate executables and 13-package baseline, and preserves M184–M186's
functional oracles. Running `--help` or `--version` is an entry-point smoke
test, not evidence that a utility's operating-system feature works.

The coherent changes are:

1. Give the existing `sar`, `sadc`, and `sadf` original Alpine binaries
   a functional CPU-sample, binary capture, binary replay, and decoded
   numerical-record oracle. The CPU records must come from the guest's
   real `/proc` counters; samples are taken one second apart.
2. Provide Linux-compatible inotify file descriptors through the existing
   **shared OpenFile** and FileOps subsystem. Namespace/write notifications
   originate at successful VFS operations. An inotify watch tracks inode
   identity without extending inode lease ownership, so observation must not
   turn legitimate `unlink` into `EBUSY`. Reads must preserve complete
   variable-length events, avoid leaking padding, and cooperate with the
   existing poll and epoll wakeup path.
3. Give the existing compositor's PTY terminal one ANSI/VT screen model,
   keyboard modifier routing and truthful terminal dimensions. Userspace
   applications, not the GUI compositor, remain the owners of text/VT
   escape interpretation and TUI application behavior.
4. Add additional original signed Alpine binaries only when their actual
   distribution paths and unique binary identity can be demonstrated for
   the three target ISAs. The old 20-entry historical manifest is never
   rewritten or silently recounted.

The signed package delta is `util-linux-misc` plus
`ncurses-terminfo-base` (required to run the original Alpine `dialog` with
`TERM=vt100`) added to the existing M174 overlay recipe. The 26 candidate
executable paths contributed by `util-linux-misc` are recorded in
`scripts/m187-original-elfs.txt` and checked against earlier milestone
inventories; this does **not** change the M174 baseline check or duplicate
an earlier ELF path. Per-ISA staging requires the APK-installed manifest,
native ELF machine headers, executable type and 46 distinct SHA-256 hashes
across the earlier 20 and new 26. Only successful native staging can promote
those 26 *candidates* to verified signed ELF additions. Their guest `--help`
gate is labeled **entry-only**; unsupported privileged tools are not claimed
to be fully functional.

The kernel remains nonpreemptible while user tasks may preempt. Existing
mouse, keyboard, framebuffer, U-Boot VirtIO block/network and lwext4
providers remain in use; M187 adds no homemade device driver.

## Guest-side functional checks

`tests/m187-software-functions.sh` is included in the same CPIO staged for
M174 and runs after M186's task-level `pidstat` oracle. It performs:

| Original Alpine tool | Required action | Evidence |
| --- | --- | --- |
| `sar` | Live two-interval `-u` sampling | Two numeric CPU rows |
| `sar`/`sadc` | Save `-u -o` data | Nonempty binary SA file |
| `sar` | Replay `-u -f` | Numeric CPU data read from saved file |
| `sadf` | Decode `-d ... -- -u` | Semicolon-delimited numeric CPU data |
| `inotifywait` | Watch guest tmpfs | Exact `CREATE` and `MODIFY` basename events and persisted writes |

Five unique `M187_SOFTWARE_*_FUNCTION_PASS` markers are asserted exactly
once by the existing guest probe. The host check
`scripts/check-m187-software-functions.sh` guards the old signed manifest,
the old functional test chain, the single-CPIO staging seam and the new
guest markers. The additional util-linux guest entry gate has its own
`M187_SOFTWARE_26_NEW_SIGNED_ELF_ENTRY_ONLY_PASS` marker. Neither substitutes
for QEMU or turns entry-only checks into functional claims.

## Verification and limitations

The M179/M180 pull-request workflows are path-filtered so changes to
userspace desktop code, original signed package catalogs, manifests and
M187 scripts trigger their **native amd64, riscv64 and arm64 matrix**. Both
still require genuine ext4 physical disk persistence and read-only host
filesystem checks; no emulated or expected-only PASS is acceptable. The
subsequent M187 mode uses the original `naseed-m174-devfs-launch` preflight,
then the same dynamic compositor with an original Alpine `dialog` TUI.
QMP must see actual original ELF `execve`, physical framebuffer changes
including arrow selection, ESC returning to the still-live PTY shell, and
an explicit Ctrl+F12 desktop exit. The M179/M180 ext4 disk is independently
verified first and is not reused or overwritten by the M187 UI boot.

The milestone is complete only when source-level/host checks and all three
native QEMU guests pass at the **same remote commit SHA**. Compiler/runtime
limitations, particularly missing host Docker, QEMU or `e2fsprogs`, must be
reported explicitly and cannot be recast as a runtime PASS.

### Inotify compatibility boundary

M187's initial provider targets `IN_CREATE` and directory-named `IN_MODIFY`
used by the signed `inotifywait` oracle, plus bounded queue, remove/ignore,
delete, move, rename cookie, poll/epoll and proper shared-FD lifetime. This
is not the full Linux inotify event vocabulary: `IN_OPEN`, `IN_ACCESS`, and
`IN_CLOSE_*` emission, `IN_EXCL_UNLINK` behavior, and certain scatter/gather
read boundary cases remain outside the current verified contract. The VFS
can also emit `MODIFY` for multiple cached hardlink names instead of just
the exact pathname used by a write. A later File-path owner refinement is
needed for strict multi-hardlink fidelity.
