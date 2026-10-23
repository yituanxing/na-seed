# M39: Original prebuilt Alpine user programs, no in-guest compilation

User-directed scope correction: M38 already proves that stock Alpine GCC
can compile a normal C program inside NaSeed. Making NaSeed compile whole
upstream projects in QEMU is much too slow and conflates system compatibility
with compilation performance. **Do not use native Make/zlib/Lua builds for
the next OS capability gate.**

Instead, the host downloads signed genuine Alpine v3.23 APK packages for
the target ISA and assembles the unchanged original executables and their
package-managed musl/shared-library dependencies into CPIO. The guest
executes the **prebuilt** binaries directly. No cross-compiler, patched
test compiler, or ahead-of-time generated guest application is used.

The minimal runtime profile retains only `musl busybox lua5.4 jq sqlite`;
cross-ISA signed APK keyring verification is shared with M38's toolchain
profile but GCC/binutils/GNU Make are NOT installed in the runtime image.

The exact same `/tests/run-packages.sh` drives:
- `/usr/bin/lua5.4`: existing verified Lua VM/GC/coroutine and stdio tests.
- `/usr/bin/jq`: genuine package and dynamic dependency processing a
  real read-only CPIO JSON file; require selected-score sum = 42.
- `/usr/bin/sqlite3`: arithmetic in memory; then create, commit,
  close, and re-open a real tmpfs database in a fresh SQLite process,
  requiring the same 42 sum.

Each ISA must produce all four stage PASS markers and exit zero under
real QEMU. Failure is evidence of a concrete shared Linux ABI / process /
VFS / VM gap. Fix such gaps by capability class in the common kernel, not
per package and not by weakening verification.

The earlier M39 upstream zlib/Lua build experiment timed out after six
minutes while GCC was still making progress. That experiment is abandoned,
not accepted. M38 GCC, original TinyCC, Lua, BusyBox, ARM64/RV64 regression
workflows remain mandatory and unchanged.
