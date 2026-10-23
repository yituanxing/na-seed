# M39: From authentic GCC execution to compiling real projects in NaSeed

## Goal and scope

The M38 cross-ISA kernel accepts original APK-signed Alpine GCC 15.2,
musl, binutils and GNU Make. M39 independently verifies it can build
upstream libraries and executables inside the NaSeed guest using the
original build systems, not only compile small stand-alone ABI fixtures.

One source and one guest script are used on all three ISAs:
- **zlib 1.3.1**: exact existing M36 pinned commit,
  `51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf`.
  Run the upstream `./configure --static`, `make -j2`, then `make test`.
  This exercises the zlib link/decompress oracle, GNU `ar`, shell
  processing, multi-file source compilation, temporary inodes and EOF.
- **Lua 5.4.8**: SHA256-pinned original tar.gz already used by NaSeed's
  independently validated upstream Lua acceptance.
  Run upstream `make -j2 linux`, building `liblua.a`, `lua` and `luac`;
  exercise VM/GC/coroutines and standard stdio file I/O scripts.

Copies of release-pinned unmodified sources are CPIO read-only at
`/tests/src`. Runtime BusyBox copies those sources into writable `/tmp`.
The *entire* compile and link happens inside NaSeed using original
Alpine GNU Make/GCC/binutils/musl. No target binary or generated objects
are staged from the Linux host. All host activity is source
authentication and signed package extraction.

## Acceptance and architecture constraints

Each guest QEMU must exit with status 0 and produce all five markers,
including upstream zlib and upstream Lua test results. Existing
M38 GCC, M37 GNU Make/TinyCC, BusyBox and Lua regressions are not
removed or weakened.

The first native run is diagnostic. If upstream software finds a missing
standard Linux syscall, review the common Task/VM/VFS architecture and
resolve *the whole operation class*, not a per-package shim. Preserve
the same implementation for x86-64, RV64 and AArch64. M39 merges only
when **all three actual QEMU hosts** pass the same workload.
