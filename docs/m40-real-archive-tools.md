# M40 — original archive utilities, no compiler inside the guest

M38 proved native GCC and M39 validated SQLite, jq, and Lua on x86-64,
RV64 and AArch64. The ongoing **OS implementation** mainline now runs
already-downloaded signed Alpine v3.23 packages. Do not reintroduce
an in-guest source compiler/Make loop for M40.

The host resolves original `tar` (GNU tar) and `xz` (XZ Utils), together
with their true Alpine package dependency closure, from the signed APK
repository for each ISA. Host-side `readelf` verifies that the two executables
really target the requested architecture; the guest also verifies both
projects' own version banners. The CPIO source fixture has a nested
directory plus original file bytes. Entire lifecycle occurs in kernel
userspace: original GNU tar creates tar in tmpfs; original xz compresses
and decompresses; the original GNU tar extracts into new tmpfs paths;
real BusyBox cmp verifies byte-for-byte source parity for two independent
files. It exercises ordinary open/seek/read/write, parent directories,
metadata and time semantics without faking file contents.

Identical APK package names and identical guest shell script on all three
ISAs. No custom userspace test binary is compiled, and none is injected
as a substitute for tar/xz. M38+M39 existing CI gates are retained.

If a real GNU tar or xz invocation fails, diagnose the relevant complete
Linux syscall/FS operation family and address the *shared* kernel code in
one coherent batch before all three QEMU gates are rerun. No software
specific compatibility shim. Merge only after original CI and M40 all green.
