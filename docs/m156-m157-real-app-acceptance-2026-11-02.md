# M156–M157: original Alpine applications drive the next kernel contracts

**Branch:** `naseed-app-batch2`, Draft PR #148. **M156 native x86 QEMU
passed** on exact PR head `a9cd5df` ([Actions run
41417005258](https://github.com/yituanxing/na-seed/actions/runs/41417005258)).
M157's initial run reached signed APK staging and correctly rejected
two unsuitable Netpbm paths before guest boot; the replacement 30-program
batch still requires new native-QEMU acceptance. A host/static pass is not
a guest application pass.

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
- `scripts/probe-m156-https-wire-smoke.sh` requires a real PCI VirtIO-Net,
  actual DHCP, the kernel's `[EXEC]` evidence for both ELF clients, guest
  semantic markers, per-client SHA-256 and the independent TLS host log. It
  stores the guest serial and optional frame capture. The test completed on
  the genuine x86-64 NaSeed QEMU guest with both clients and the independent
  host oracle passing. This is a **real signed Alpine HTTPS/TLS acceptance**.

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
- The default M157 job is native x86-64 QEMU; an explicit
  `run-m157-multiarch` label or manual dispatch requests RV64/AArch64 as
  well. All three use the same signed APK profile and guest semantic script.

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
`.github/workflows/m156-https.yml` and `.github/workflows/m157-batch.yml`.
If a guest fails, classify its actual syscall trace, guest output and host
oracle before changing `kernel/fs/socket_syscall.c`, `ports/lwip/socket_port.c`
or Task/File ownership. The existing code already proves ordinary HTTP,
DNS, UDP, dual-NIC DHCP and cross-ISA signed applications; do not regress
those working contracts to make one new client pass.
