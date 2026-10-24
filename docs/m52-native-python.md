# M52 — Original Alpine CPython 3, three-ISA native runtime acceptance

Run the unmodified signed native CPython ELF plus its unmodified upstream
stdlib and dependency closure on x86-64, riscv64 and AArch64. Do not run host
Python as a substitute for guest acceptance. The Python script checks real
bytecode, JSON/hashlib/zlib, pathlib/tmpfs, SQLite file transaction lifecycle,
Python threading, and subprocess with real guest BusyBox.

No network, SMP, persistent filesystem or app-specific kernel syscall
shortcuts. Diagnose guest failures via actual syscall/serial evidence;
fix the owning shared Linux ABI/VFS/VM mechanism and retest all three ISAs.

One additional foundation workload/ISA; keep all M38–M49 and M51 tests,
same 12 bundled QEMU jobs, 7 historical userspace regression shards. Git
success is not Python success. No merge until exact-head CI is green.

## Exact three-ISA first Python startup fault

The untouched signed Python ELF starts but cannot resolve the filesystem
encoding during import initialization: `LookupError: unknown encoding: utf-8`.
Before modifying the shared VFS, assert the signed encoding sources exist
in the staged CPIO, assert BusyBox can read them inside the native guest,
then enable original Python's verbose import messages on the actual failing
execution. Never replace stdlib modules or special-case Python in the kernel.

## Root cause confirmed by real three-ISA guest traces

CPython FileFinder uses directory enumeration to discover encodings/utf_8.py,
not a direct open. All three ISAs successfully opened encodings/__init__.py
and aliases.py, but never attempted opening utf_8.py: common
sys_getdents64 forcibly truncated every read-only archive directory after
96 direct children, despite BootFS storing 65,536 entries and its host
scale test verifying 1,200 children. The encodings directory is larger
than this old arbitrary cap, so CPython's directory cache omitted utf_8.

Remove the 96-entry truncate rather than insert program-specific imports;
also index CPIO direct children once after mount in original archive order,
avoiding O(archive-entry-count * directory-size) repeated scans. The common
VFS remains ISA-neutral; no new syscall or kernel-lock requirement.
