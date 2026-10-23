# M41 real GNU userspace — one multi-program Linux ABI gate

Signed Alpine v3.23 packages, original ELF, no guest compiler. Stage
GNU coreutils, findutils, grep, sed, diffutils and native musl into
CPIO on host. All three native NaSeed kernels (amd64, RV64, ARM64)
run the identical shell and signed packages, not per-ISA app patches.

Verify native GNU --version banners; GNU cp copies CPIO bytes into
nested writable tmpfs; GNU find enumerates and launches GNU grep
using -exec ... {} +; the GNU grep output crosses Unix pipes through
GNU sed and GNU sort, with GNU diff comparing CPIO golden data.
GNU mv renames the result, real GNU sha256sum validates bytes,
and GNU diff must return exactly exit status 1 for a real difference.

This specifically probes fork/exec/wait, pipes, cwd/path, getdents,
metadata, read/write, temporary inode lifetime, atomic rename and
process error propagation in one end-to-end workload. Fix kernel
failures in common Linux semantics, never tool-specific stubs.
Retain M38/M39/M40 and all existing CI. Merge only after 3-ISA
real QEMU and regression gates are green. Threads, disks, network
and LoongArch are out of scope.
