# Original Alpine Linux fixtures: pinned bytes and mirror availability

The three-ISA package-managed M38 GCC/musl acceptance gate is fully green at
commit 3f94db1, including native GCC and three different GCC-built ELF
programs on x86-64, RV64 and AArch64. The remaining historical x86 VFS gate
failed before boot at `scripts/fetch-alpine.sh` with a CDN HTTP 404 after
its first BusyBox package verified; another x86 job fetched and verified the
identical archived musl package successfully in the same CI run. This is an
external dependency availability failure, not an ELF/VM/FS regression.

## Shared transport policy

- Source packages and exact versions remain unchanged (v3.23).
- Maintain their existing SHA256-pinned checksums; no untrusted replacement.
- Reuse cached packages only after validating SHA256.
- If a transport is unavailable or serves different bytes, try listed Alpine
  mirrors and accept ONLY the original expected digest; otherwise fail closed.
- Write to temporary files before atomic rename so interrupted downloads
  cannot masquerade as authenticated cache contents.
- Shared helper handles x86 and RV64/AArch64 fixture staging. It does not
  affect in-guest ABI, CPIO files, compiler behavior, or native OS execution.
- Run an offline host regression for mirror 404, checksum mismatch, valid
  fallback, cached reuse, and refusal of corrupted cache.

Do not merge M38 until the original BusyBox x86 VFS gate and all concurrent
three-ISA jobs are green. This staging change is test infrastructure, not a
reason to lower the gate or rewrite expected output.
