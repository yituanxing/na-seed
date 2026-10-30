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
| `scripts/probe-mXX-*.sh` and `tests/mXX-*` | Original ELF and supplied input | Real guest execution and exact behavior/exit evidence | Kernel implementation rules or invented successful return values |
| Kernel VFS + filesystem adapters | File operation and backend capabilities | Common file objects, resource ownership and correct provider-specific semantics | Knowledge of the application under test |

The first catalog migration replaces the old 14-profile hard-coded whitelist
and redundant three-manifest argument lists.  Both the canonical two-argument
installer and the five-argument compatibility entry point resolve the **same
catalog record**; a legacy call with conflicting manifests must fail.  The
static checker `scripts/check-prebuilt-profile-catalog.py` rejects duplicate or
orphaned profiles, bad repository names, missing manifests, malformed guest
paths and conflicting wrapper identities.  Catalog changes are listed in the
relevant workflow path filters.

The M38–M55 original 17-workload matrix (FAST 12, FULL 17) and later M58+
guest probes retain their own historical acceptance contracts.  The first
catalog phase does not rewrite the genuinely different cJSON, BusyBox,
SQLite and DWARF build algorithms into a fake common build step.

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

1. **Package catalog (implemented locally in first batch)**: one data owner
   for the 14 signed-APK overlay profiles, stable old wrappers and strict
   validation.  No new native application is claimed to pass as a result.
2. **Execution target catalog**: factor the repeated `ISA -> QEMU runner /
   guest serial log` mapping out of the M58+ probes, preserving their
   independent guest assertion logic.
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
