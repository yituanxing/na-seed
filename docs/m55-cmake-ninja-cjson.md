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
