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
