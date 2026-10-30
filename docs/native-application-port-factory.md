# Native Linux application port factory: boundaries and evidence

NaSeed's purpose here is to execute **unmodified, authentic Linux user-space
software**, and let independent programs expose general kernel defects.  A
program passing is evidence about the paths it exercised, not a license to
add an application-name special case to the kernel or claim complete Linux
compatibility.

## Source and ownership boundaries

| Owner | Input | Output / responsibility | Must not own |
| --- | --- | --- | --- |
| `scripts/prebuilt-profile-catalog.tsv` | Profile ID | Signed APK package list, original ELF paths, rootfs copy manifest, official Alpine repository selection | QEMU arguments, kernel workarounds, test pass status |
| `scripts/stage-alpine-prebuilt-overlay.sh` | Target ISA + validated catalog profile | Host-installed signature-verified APK dependency closure and target-ISA-checked original ELF/CPIO | Workload-specific acceptance logic, guest compilation |
| `scripts/stage-mXX-prebuilt.sh` | Target ISA | Stable historical entry point; additional source/DWARF build where genuinely required (M76) | A second copy of catalog metadata |
| `scripts/native-qemu-target.sh` | Validated target ISA from an M38+ probe | One real system-QEMU runner and raw guest-serial directory | Program-specific assertions, fake runner results or boot-time tuning |
| `scripts/probe-mXX-*.sh` and `tests/mXX-*` | Original ELF and supplied input | Real guest execution and exact behavior/exit evidence | Kernel implementation rules or invented successful return values |
| Kernel VFS + filesystem adapters | File operation and backend capabilities | Common file objects, resource ownership and correct provider-specific semantics | Knowledge of the application under test |

The package catalog migration replaces the old 14-profile hard-coded whitelist
and redundant three-manifest argument lists.  Both the canonical two-argument
installer and the five-argument compatibility entry point resolve the **same
catalog record**; a legacy call with conflicting manifests must fail.  The
static checker `scripts/check-prebuilt-profile-catalog.py` rejects duplicate or
orphaned profiles, bad repository names, missing manifests, malformed guest
paths and conflicting wrapper identities.  Catalog changes are listed in the
relevant workflow path filters.

The M38–M55 original 17-workload matrix (FAST 12, FULL 17) and later M58+
guest probes retain their own historical acceptance contracts.  The migration
does not rewrite the genuinely different cJSON, BusyBox,
SQLite and DWARF build algorithms into a fake common build step.

All 40 validated original-program probes (seven historical M31/M33–M37/Lua
entrypoints, 17 original M50 roster entries, two M56/M57 manual source-build
probes and fourteen M58+ signed-APK probes) now source the **same**
target helper for the exact `amd64`, `riscv64` and `arm64` system-QEMU
system-QEMU runner. The 33 M38+ probes also share its serial-log directory;
M31 deliberately keeps its separate default-BusyBox non-source log path.
M34–M36 retain their Linux QEMU-user emulator oracles. Their manifest selection,
resource budgets (including M55 RTC seeding), runner failure handling and
original guest markers remain owned by each probe.  The original 19
M38–M57 private case statements are deleted, not retained as an alternative.
`scripts/check-native-qemu-target.py` resolves the three real runners,
guards all 40 consumers (the seven earlier probes are discovered from their
existing CI workflow entries) and deliberately mutates fixtures to ensure
stale private mappings, missing boot calls or missing serial checks fail. The M50
matrix reuses this checker, while the twelve M58+ focused workflows track
helper changes and run the same host contract before native QEMU.

## Compatibility is a matrix, not a single percentage

The future machine-readable result should identify:

1. **Program provenance**: exact upstream source or signed distribution
   package, version, architecture, and native ELF/dependency identity.
2. **Workload**: original command, inputs, expected exit code, output/file
   assertions, FAST/FULL budget, and equivalent Linux control result.
3. **Execution target**: exact NaSeed commit, ISA, QEMU/boot settings, guest
   image and filesystem mount topology.
4. **Capability outcome**: `PASS`, `FAIL` or a **declared unsupported backend
   capability**.  A skipped test is never counted as a pass.
5. **Failure classification**: supply chain, ELF/ABI, syscall, shared VFS,
   provider semantics, task/signal, network, test environment or performance.

The existing `scripts/native-perf-report.py` already defines strict JSON
input-manifest fields for exact QEMU settings, source/rootfs hashes, toolchain
and cache-state comparisons.  A future machine-readable outcome record must
reuse that provenance contract where applicable, not create a competing
identity for the same execution.  Its `PASS` is valid only after the original
probe validates genuine guest markers and exit; automatic FAST deferrals
remain `DEFERRED`, never a manufactured success.

Filesystem differences are real.  `tmpfs`, `lwext4`, `littlefs` and imported
RAM filesystems must not be forced to advertise identical semantics.  The
common VFS defines stable object/operation ownership; each backend declares
which operations and guarantees it can actually provide.  For example,
POSIX hard-link, permission, timestamp and durability expectations must be
tested separately where a foreign embedded filesystem does not implement
them natively.  Never turn an unsupported operation into a synthetic success.

One practical matrix is `(program, workload, filesystem, ISA, capability)`.
Running an existing real program such as `rsync` or `SQLite` across different
backends is a stronger adapter test than inventing a different toy file-copy
test for each backend.

## Next architectural batches

1. **Package catalog (implemented)**: one data owner
   for the 14 signed-APK overlay profiles, stable old wrappers and strict
   validation.  No new native application is claimed to pass as a result.
2. **Execution target mapping (implemented)**: one sourced ISA-to-system-QEMU
   runner contract for all 40 validated probes, with common serial-log
   directory for M38+ and the original non-source M31 directory preserved.
3. **Workload catalog and unified outcome record**: migrate existing FAST/
   FULL cases without deleting the historical markers or allowing hidden
   skips; retain exact-head original-guest serial artifacts.
4. **Additional independent programs and filesystem combinations**: stage
   signed programs such as `bsdtar`, `ripgrep`, `7zip`, `pkgconf`; extend
   already-present `rsync`/`SQLite` across real VFS backends.  These are
   candidates, not current NaSeed pass claims.

Every real-program failure first receives an exact reproduction and a Linux
comparison.  Fix the owning common layer, then rerun affected programs and
all prior three-ISA regressions.  This is how real software develops the OS
without turning its architecture into a list of application exceptions.
