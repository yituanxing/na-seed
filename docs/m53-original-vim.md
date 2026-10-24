# M53 — genuine signed Alpine Vim with real editing and three-ISA UART

## Goal
Run the *unmodified signed Alpine v3.23 Vim* ELF, its original ncurses
dependencies/terminfo, and its normal Linux TTY behavior on x86-64, RV64 and
AArch64. Do not treat `--version` or `vim -es` alone as interactive
acceptance, or replace upstream Vim with fake editing logic.

The first guest QEMU boot uses normal Alpine BusyBox shell and Vim Ex to
perform true `:setline`, `:substitute`, `:write`, `:quit`, and `:append`
through shared NaSeed tmpfs, checking exact saved contents from *outside*
Vim and a second genuine Vim reopening the file.

The second native QEMU boot uses a small **standard Linux C PID1** linked
against the same Alpine musl target ABI. It obtains the genuine controlling
TTY, forks and execs the untouched original Vim. The host injects only
actual QEMU serial bytes *after it sees Vim render the real file*, including
normal/insert/escape/command-mode interactions. PID1 waits for Vim exit
then reopens the saved tmpfs file to validate actual user input made it
into the file. No custom kernel program-name branch or fake syscall success.

## Scope and architecture
Use already implemented shared TTY termios, line discipline, serial transport,
fork/wait/exec and VFS. No SMP, persistent disk, network or per-ISA Linux
semantic code. If the original program exposes unsupported noncanonical
time, poll, signals or filesystem semantics, fix the actual common owner.

The original 42 M38-M49 + M51 + M52 full QEMU checks remain mandatory.
M53 adds **six** new real boot sessions (Ex + interactive for each ISA), while
the existing 12 CI QEMU installer jobs remain unchanged in count. A single
focused three-ISA M53 workflow is optional workflow_dispatch; exact-head
six main workflows and all 12 QEMU job results must pass before merging.
