# GNU Make in NaSeed — the beginning of building on our own OS

We want to be able to **develop software inside NaSeed**. Real GNU Make
is useful in its own right: read a Makefile, resolve a target, and start
ordinary Linux shell processes from recipes.

The code is **unmodified** GNU Make 4.4.1 from the published release
tarball (SHA256 dd16fb1d67bfab79a72f5e8390735c49e3e8e70b4945a15ab1f81ddb78658fb3)
compiled as a static Linux x86-64 ELF. It and the exact Makefile first
run on native Linux with two shell recipes. Next, the same ELF reads a
CPIO-backed /tests/Makefile inside the real NaSeed native QEMU guest.

GNU Make chooses and invokes the shell as a separate process. The guest
uses original Debian static BusyBox ash as /bin/sh; it has no fake or
NaSeed-specific callbacks. The real kernel must implement the genuine
fork/exec/wait/pipe/read/stat syscalls demanded by GNU Make and the shell.
The test requires two distinct output lines from the recipes and status
zero from GNU Make. If only GNU Make --version works, the milestone fails.

This is a single limited stage, **not** full GNU Make compliance. Next,
we may run Make recipes that invoke the existing original TinyCC to
compile C and eventually create and run a new binary within NaSeed.
That next stage is driven by wanting to write C on our own machine,
not by a synthetic syscall coverage target.
