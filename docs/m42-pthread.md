# M42 genuine pthread runtime and shared-VM futex architecture

This milestone begins a genuine implementation of musl pthread clone on all
three kernels, not fake pthread success. A signed Alpine unmodified XZ Utils
ELF is staged on the host (no in-guest compiler) and requested to use exactly
two threads and multiple blocks; it must compress/decompress a 4 MiB fixture
byte-for-byte and kernel serial output must independently show CLONE_THREAD.

Kernel architecture changes: task-local TID and TLS with shared CLONE_VM
root; CLONE_FILES references the actual same FD table, not duplicated open
file descriptions; per-process VM metadata owner shared by the group; real
scheduler futex WAIT/WAKE, private flag, bitsets and timeouts, child clear-TID
wake; fork remains independent. Three ISA ABI decoders agree on canonical
syscall identity, TLS and child-TID register order. Unsupported clone variants
or PI futex remain explicit ENOSYS.

Limitations not yet promised: full POSIX robust-mutex owner-death behavior,
all CLONE_THREAD edge conditions (including exec from worker), and SMP;
verified acceptance is constrained to the original XZ workload plus common
regressions. NEVER merge solely because code compiles.
