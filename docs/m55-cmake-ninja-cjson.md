# M55 — Native CMake + real Ninja, entire upstream cJSON 1.7.19 tests

## Actual Linux compatibility finding
CMake 4/libuv is the first accepted program requiring Linux epoll, not a
niche-specific test shim. On all 3 ISAs the unchanged CMake emitted
epoll_create1 ENOSYS (x86 nr=291, generic64 nr=20) and hung waiting on a
real pipe. Implement a bounded shared open-file-descriptor epoll backend,
including create/ctl/wait/pwait, checked 12-byte x86 versus 16-byte generic
epoll_event ABI, real pipe/TTY readiness, task-owned timeout, CLOEXEC and
last-close lifetime. Add a focused original target GCC-compiled native
event-loop smoke before the expensive full CMake upstream build.

After the first real epoll/pipe test passed, CMake 4 still hung on all ISAs.
Guest logs show missing eventfd2 (x86 nr=290, generic64 nr=19). Add a
shared eventfd object with real EFD_NONBLOCK, EFD_SEMAPHORE and EFD_CLOEXEC,
validated counter read/write and integration into poll, epoll and task-wake.
io_uring_setup (nr=425) remains deliberately ENOSYS for normal Linux
fallback. Eventfd is a likely missing prerequisite, not a proven root cause
until real CMake passes. Validate with a GCC guest-compiled eventfd test
before the full upstream configure to get rapid fail-closed evidence.

Goal: move past toy CMake samples. Fetch an authentic immutable source
snapshot from DaveGamble/cJSON commit
`c859b25da02955fef659d658b8f324b5cde87be3` (upstream v1.7.19)
and independently verify the true Git blobs of upstream CMakeLists, cJSON.c,
cJSON.h, test.c, Unity sources and tests/CMakeLists. The signed original
Alpine v3.23 *cmake*, *ctest*, *ninja-build*, GNU GCC/binutils and musl
execute only in the NaSeed guest, never on the host.

On each native x86-64/RV64/AArch64 QEMU boot:
- Copy pristine pinned original cJSON into writable /tmp.
- Real CMake configures Ninja, compiling and linking test programs with
  real Alpine GCC/musl and CMake's compiler discovery.
- Build the complete upstream CTest-enabled cJSON, its Unity test program
  corpus, static libcjson.a and the real cJSON_test.
- Execute the upstream CTest suite through real Alpine CTest, without
  replacing upstream tests or defining fake success markers.
- Verify Ninja no-op incremental build and rebuild when the source's mtime
  changes; execute the complete CTest suite again.

Architectural invariant: no application-name kernel branches or ISA-specific
CMake adaptations. Any problems surfaced by CMake, Ninja, GCC or the source
tests should be assigned to generic Linux process, VFS, FD, VM or clock
semantics and fixed there, not worked around in cJSON tests.

CI remains 12 existing grouped QEMU jobs. All M38–M54 tests stay mandatory;
one additional real upstream workload on each ISA. Focused M55
workflow_dispatch is manual, not an extra PR job. Do NOT merge until all
native QEMU M55 tests and existing six CI workflows finish green.

## Subsequent real Ninja kernel fault
After the generic FIONBIO (0x5421) and FIOCLEX/FIONCLEX ioctls were
implemented at the shared descriptor layer, genuine CMake progressed through
GCC 15 compiler identification and Ninja ABI try-compile. Ninja then produced
a reproducible user stack page fault on x86-64 and ARM64 near 0x6ffdfxxx,
below the former 0x6ffe0000 mapped floor. All boot/exec/auxv code had three
duplicated 32-page (128-KiB) policies. Consolidate the per-process main stack
mapping in one helper and provide a bounded 256-page (1-MiB) eager stack on
all three ISAs. A separate target-GCC-built guard touches >128 KiB of real
volatile stack before authentic CMake is run. This is **not** claimed to be
Linux's on-demand grow-down stack; the latter requires deliberate exception
handling, VM-area metadata, and per-architecture page-fault recovery and is
tracked as future architecture work. The shared policy does not branch on
program name or ISA.

## Mandatory signal restorer layout invariant
The 1-MiB main stack revealed an ABI layout collision: the old RV64/ARM64
rt_sigreturn RX trampoline was pinned at 0x6ff00000, exactly the new bottom
stack page. On both native ISAs boot failed to install the signal helper
before userspace entered. Move the two providers onto one common constant
NAS_USER_SIGNAL_TRAMPOLINE=0x6fe00000, outside the entire main stack with
an unmapped gap. Keep the x86 libc-provided SA_RESTORER unchanged. Do not
weaken signal, TinyCC, Lua or other historical acceptance gates.

## Full-build acceptance budget and diagnostic policy
After shared FIONBIO, eventfd/epoll and 1-MiB main-stack fixes, the new
three-ISA guest test reached real upstream CMake configure (x86-64 and ARM64)
and Ninja's complete 45-action CTest-enabled build. The original 180/200s
single-QEMU smoke budget cut off ordinary compiler subprocesses after only a
few actions; the recorded logs show continuing GCC/ld/ar/ranlib progress, not
a fatal kernel exception. RV64 was likewise still running GCC try_compile.

M55 is now a *full guest build and test*, not a short smoke test: use a
maximum 2400s per ISA (overridable with NAS_M55_QEMU_TIMEOUT), bounded
within the existing 50-minute grouped CI job. The hardware marker monitor
returns immediately on genuine successful completion and fails closed on
[FAIL], QEMU termination and timeout. Run with NAS_SYSCALL_TRACE=0 in regular
CI to avoid unnecessary UART spam; opt in with NAS_SYSCALL_TRACE=1 for a
focused diagnosis. This change does not weaken expected upstream CTest,
rebuild, or earlier acceptance markers.

## Latest exact-head matrix evidence (24706a4)
All six PR workflows finished; five passed and grouped native acceptance
reached 11/12 QEMU jobs. Crucially, M55 FULL cJSON acceptance passed on
both x86-64 and ARM64, including real CTest + Ninja no-op + true rebuild.
The only failing job was RV64 foundation: actual guest CMake configured,
Ninja completed all 45 compile/link actions, and original CTest finished
**19/19 PASS**. Its first dry-run Ninja returned exit 0 but the shell
assertion did NOT find 'no work to do' in redirected stdout, so the test
stopped before the source-touch rebuild. This is **not** a QEMU timeout
or a CTest failure. Preserve the strict no-op criterion. The next native
RV64 run captures original Ninja '-n -d explain' output and filesystem
mtimes in the serial log so the actual dirty-edge cause can be fixed.

## RV64 incremental-build root cause and UTC bootstrap
On exact head c631528 the genuine RV64 guest built all 45 upstream Ninja
actions and passed **19/19 CTests**, yet Ninja correctly found every object
older than `/usr/include/stdc-predef.h`. The unmodified Alpine header
mtime was **2026-10-21 16:00:02 UTC**, whereas new guest compiler outputs
were dated **2026-09-28**: virtual RV64 firmware's Limine RTC calendar was
stale. This was NOT a syscall, cJSON or CTest defect.

Fix the general Linux CLOCK_REALTIME bootstrap, not Ninja or package files:
provide optional `naseed.realtime=<actual UTC epoch>` before `naseed.argv`
in the trusted Limine cmdline; remove it before normal user argv parsing,
preserving all existing serial/argv checks. The unified Linux timekeeper
continues to use architecture-specific monotonic clocks for durations.
Without the optional parameter it still falls back to Limine RTC.
All three actual QEMU launch scripts now provide the host's real UTC clock.
No dependency edges, CTest requirements or up-to-date semantics are bypassed.
