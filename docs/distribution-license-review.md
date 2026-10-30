# NaSeed upstream-source and distribution-license review

Status: **open review, not legal sign-off** (2026-10-30, `main@f531b5d`).
This checklist distinguishes a technically accepted source merge from the
permissions and notices required when conveying source or a linked kernel.
The repository is already public: distribution of source, not only a future
binary release, must respect the licenses of included third-party files.

## Evidence actually present in this checkout

| Imported source | Observed source terms | Evidence / qualification |
| --- | --- | --- |
| `third_party/lwext4` | Its `LICENSE` contains GPLv2 and defers to individual file notices; many files have BSD-3-Clause notices | `src/ext4_extent.c` and `src/ext4_xattr.c` explicitly state **GPL version 2 or any later version**. Do not summarize these two as `GPL-2.0-only`. |
| `third_party/uboot/drivers/virtio` | `virtio_blk.c`: `GPL-2.0+`; `virtio_blk.h`: `BSD-3-Clause` | Retained original SPDX headers; driver is actually compiled into the kernel's opt-in physical-disk path. |
| `third_party/jkent_ramfs` | `MPL-2.0` | Original `LICENSE.txt` and source notices retained. No Exhibit B incompatible notice was observed in the pinned covered files. MPL 2.0 §3.3 secondary-license conditions require a separate check; this is not automatic blanket relicensing. |
| `third_party/littlefs` | `BSD-3-Clause` | `LICENSE.md`, source headers and immutable upstream revision retained. |
| `third_party/unikraft` | Generally `BSD-3-Clause` for currently imported vring headers | `COPYING.md` itself warns that individual files may differ; check the actually linked files. |
| NaOS drivers / NuttX / Zephyr | Design references only | No copied NaOS or NuttX/Zephyr driver source identified in the inspected NaSeed integration. Do not infer a grant to copy it from an architecture comparison. |

NaSeed's own kernel, ports and tools currently have **no root-level
`LICENSE`**. No project-wide license, contributor authorization, compatibility
conclusion or redistribution permission for first-party code is asserted by
this document. GPLv2-only/GPLv3 incompatibility must be assessed against the
**actual linked file-level terms**; the earlier shorthand that both lwext4
files were GPLv2-only was factually wrong.

## Required before shipping or redistributing a packaged kernel

- [ ] Decide and record the first-party source license with authorization
      from all relevant contributors, including file-level exceptions if
      necessary. Do not silently relicense third-party or contributor files.
- [ ] For each of x86-64, RV64 and AArch64 and each shipping feature set,
      collect the **actual linked object/source manifest**, not merely a
      repository-wide list of vendored files. Confirm compatibility of the
      combined work using the pinned source headers and license texts.
- [ ] Confirm the exact lwext4 file-by-file grants, U-Boot GPL-2.0+ and BSD
      notice obligations, Unikraft per-file terms, and BSD-3-Clause notice
      retention; keep source hashes and upstream revision links auditable.
- [ ] For the MPL-2.0 RamFS code, reconfirm Exhibit B status and the
      §3.3 conditions for any secondary-license distribution. Preserve the
      covered-source license and access to its corresponding source, including
      modifications if any; do not treat mere static linking as permission to
      remove MPL notices.
- [ ] Prepare source-conveyance instructions and the required copyright,
      license, disclaimer and offer/access notices for each distribution
      channel (repository/source archive, binary kernel image, appliance).
- [ ] Recheck any newly borrowed upstream code **before import**. NaOS GPLv3
      source remains a reference unless specifically reviewed and granted.
- [ ] Obtain the project's chosen release/legal review sign-off. This file
      is an engineering inventory, not legal advice.

**Separate acceptance dimensions:** a passing exact-HEAD three-ISA QEMU
test proves runtime behavior for the exercised configuration; it does not
prove license compatibility. Conversely, incomplete distribution paperwork
does not negate the passing technical evidence or justify rewriting a
correctly working driver. Track the two gates independently.
