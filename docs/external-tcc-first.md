# First native TinyCC capability: compiler runs *inside* NaSeed

NaSeed deliberately chooses real software because we want to use it, not
because we are chasing a Linux syscall checklist. The first TinyCC
milestone is x86-64 only: the official unchanged TinyCC 0.9.27 source
builds a static Linux ELF and runs on NaSeed as /bin/tcc.

Two useful real-program scenarios are exercised:
1. Preprocess source read from the readonly CPIO into generated C text.
2. Compile original C source into a real ELF relocatable object at /tmp.

The exact same compiler + source first runs on Linux; its output is
verified with readelf and the object symbol table. On NaSeed, acceptance
requires a genuine QEMU boot and syscall-level exit=0 for each case.

**The guest-generated /tmp/first.o byte validity is not yet checked**;
exit=0 proves the compiler completed, not yet that its guest output
matches Linux. Do not advertise native linking, executing generated
code, or GCC compatibility until tested. This is a deliberately useful,
limited first milestone, not a fake `tcc --version` acceptance.

The original BusyBox matrix stays separate. We do not make ARM/RV ports
a prerequisite for using a C compiler on x86, and we do not expand a
large kernel feature before a real TCC workload demonstrates the need.
