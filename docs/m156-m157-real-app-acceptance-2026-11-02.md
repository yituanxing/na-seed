# M156–M157: original Alpine applications drive the next kernel contracts

**Branch:** `naseed-app-batch2`, Draft PR #148. On exact PR head `9874503`,
**M156 native x86 QEMU passed** ([Actions run
41418773480](https://github.com/yituanxing/na-seed/actions/runs/41418773480))
with real TLS traffic and certificate checks; **M157 native x86 QEMU passed
30/30 original image operations** ([Actions run
41418773344](https://github.com/yituanxing/na-seed/actions/runs/41418773344)).
The two other ISAs were not part of those particular runs. New three-ISA
CI matrices must establish RV64/AArch64 independently; a static pass is
never a substitute for guest execution.

The early M157 staging run rejected a script and a nonexistent Netpbm path.
The second run passed 29/30 and exposed an absent optional X11 color-name
dictionary in original `pamlevels`; the final test now uses numeric RGB
colors accepted by the unchanged upstream ELF, passing 30/30.

The architecture audit found no evidence that a second networking stack,
device registry, File table or independent CPIO builder is needed. Continue
with signed, unchanged Alpine Linux programs; fix kernel lifetime/ABI contracts
only when their **real behavior** exposes a specific fault.

## M156: verified HTTPS, not just connected TCP

- The `m156-https` staging profile reuses the signed `m143-batch` package and
  original-ELF inventories; **zero new program identities** are claimed.
  `stage-alpine-prebuilt-overlay.sh` remains the only APK importer and CPIO
  writer. The profile's host hook creates an ephemeral test CA and a separate
  signed server certificate, with both DNS and IP Subject Alternative Names.
  Private keys stay outside the guest filesystem and uploaded CI artifacts.
- An independent host HTTPS server accepts TLS 1.2 or later and records the
  actual `/curl` and `/wget` requests and response sizes. The original signed
  Alpine Curl and GNU Wget clients use the guest's public CA certificate;
  bypassing verification (`-k`, `--no-check-certificate`) is forbidden.
  A trusted CA chain, certificate identity, two guest response bodies and
  exactly one host-received GET per client are hard assertions.
- `scripts/probe-m156-https-wire-smoke.sh` requires a real PCI (x86-64) or
  MMIO (RV64/AArch64) VirtIO-Net,
  actual DHCP, the kernel's `[EXEC]` evidence for both ELF clients, guest
  semantic markers, per-client SHA-256 and the independent TLS host log. It
  stores the guest serial and optional frame capture. The test completed on
  the genuine x86-64 NaSeed QEMU guest with both clients and the independent
  host oracle passing. This is a **real signed Alpine HTTPS/TLS acceptance
  on x86-64**, with other-ISA acceptance separately gated by CI.

M156 deliberately keeps its deterministic host on QEMU slirp. This is real
certificate-verified encrypted traffic across the guest NIC, **not** proof of
arbitrary public Internet access or production-grade certificate handling.

## M157: 30 genuinely new image-processing binaries

- Four official Alpine 3.23 APKs (`netpbm`, `giflib-utils`, `tiff-tools`,
  `libpng-utils`) provide 30 historically unlisted executable paths: 20 PNM
  tools, five GIF tools, three TIFF tools and two PNG utilities.
- The canonical signed APK importer checks the target ISA of each ELF; the
  milestone stage independently rejects historic paths, BusyBox aliases and
  duplicate executable hashes. `check-m157-inventory.py` enforces exact
  identity parity between the 30-item inventory, actual guest case labels
  and 31 semantic markers (30 programs plus final summary).
- The host hook generates small deterministic PNM, GIF, PNG, paletted TIFF
  and fax inputs. The guest invokes **all 30 original ELF tools on real
  image data**, and each case checks a concrete output condition, not merely
  `--help` or an executable-loading trace. The QEMU oracle also requires
  every original `[EXEC]` line and precisely one expected semantic marker.
- The M157 CI matrix now requires native x86-64, RV64 and AArch64 QEMU on
  each affected PR update. All three use the same signed APK profile and
  guest semantic script. Only x86-64 has previously passed this new
  M157 workload; the other two remain **pending same-head guest proof**.

## Verification boundary

These local gates **do not** imply a successful QEMU boot:

```sh
python3 scripts/check-prebuilt-profile-catalog.py
python3 scripts/check-m157-inventory.py
python3 scripts/check-three-isa-core.py
python3 scripts/check-architecture-boundaries.py
sh -n scripts/probe-m156-https-wire-smoke.sh
sh -n scripts/probe-m157-batch.sh
sh -n tests/m156-https-run.sh
sh -n tests/m157-batch-run.sh
```

The new exact-head GitHub Actions jobs are
`.github/workflows/m156-https.yml` and `.github/workflows/m157-batch.yml`,
both now requiring all three ISAs.
If a guest fails, classify its actual syscall trace, guest output and host
oracle before changing `kernel/fs/socket_syscall.c`, `ports/lwip/socket_port.c`
or Task/File ownership. The existing code already proves ordinary HTTP,
DNS, UDP, dual-NIC DHCP and cross-ISA signed applications; do not regress
those working contracts to make one new client pass.
