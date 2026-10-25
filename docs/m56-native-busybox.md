# M56 — pinned upstream BusyBox builds itself inside NaSeed (draft)

- Reuse exact verified BusyBox **1.37.0** source archive SHA256
  `3311dff32e746499f4df0d5df04d7eb396382d7e108bb9250e7b519b837043a4`
  and the existing `scripts/source-busybox.config` used by independent host
  cross-build Linux-user oracles.
- Host installs original signed Alpine GCC/GNU Make/binutils/musl and **stages
  source only** into immutable initramfs. No host built program, no rewritten
  source or host-generated Kconfig/Makefile.
- Guest copies the **original** source tree into tmpfs; runs original
  `allnoconfig`, three `oldconfig` passes, GNU `make -j2 busybox`
  with target-native GCC/AS/LD, checks static ELF and executes that exact
  newly built binary with ash and applets.
- Must run with actual NaSeed x86-64, RV64, ARM64 QEMU system/kernel;
  Linux-user QEMU/native Linux host runs do not count as M56 acceptance.
- Keep M38–M55 grouped 12/12 unchanged while M56 is a focused three-ISA
  **draft** gate. Promote it to the shared native matrix only after all three
  M56 gates and exact-head old regressions pass.
- Fix only genuine generic missing Linux ABI interfaces in shared FS/MMU/task
  architecture. Neither synthetic Kbuild outputs nor skipped original stages
  are acceptable. Guest output and failure markers retained as CI artifacts.
