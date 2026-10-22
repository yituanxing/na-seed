# GNU nano on NaSeed: first interactive editor experiment

We choose GNU nano because having a genuine editor inside our own OS is
fun and useful. It is not a disguised syscall checklist.

This small x86 milestone uses the exact original GNU nano 9.3 source
(verified SHA256, configure --enable-tiny, no patches), static Linux
libc/ncurses ELF, compiled native Linux terminfo, and a normal Linux C
TTY-owning PID1 launcher. The initial selected note lives in real
read-only CPIO. Only the compiler/terminal library's usual Linux
userspace ABI is used. We do not invent an in-kernel editor.

The Linux PTY oracle first opens and displays that note, then sends Ctrl+X.
The native NaSeed QEMU experiment should visibly render original nano's
curses screen from that same real file and exit with genuine COM1 Ctrl+X.
No claiming success based only on a --version output. No stage counts as
accepted unless those actual observations complete.

Once this works, **next** is modifying a /tmp file and saving through
the normal TTY, VFS and process lifecycle. NaSeed deliberately does not
wait for full Nano support or all ISAs before trying another program.

A build or behavior failure is valid evidence of what's missing; fix
the relevant *general* OS subsystem in batches, not one bypass per app.
