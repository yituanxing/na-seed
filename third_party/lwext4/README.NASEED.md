# Vendored lwext4

Unmodified upstream source selection from https://github.com/gkostka/lwext4
at exactly commit `58bcf89a121b72d4fb66334f1693d3b30e4cb9c5`.

Sources kept under upstream `include/`, `src/`, `blockdev/`; NaSeed
specific callbacks belong in `ports/lwext4/lwext4_block.[ch]`.

To refresh, review upstream changes and licenses separately, copy the full
source set, update `UPSTREAM_REVISION`, and run
`sh scripts/check-lwext4-ramdisk.sh`. CI no longer fetches source.

## License and attribution

Upstream's original `LICENSE` applies. Several source files are BSD-3
clause, while `src/ext4_xattr.c` and `src/ext4_extent.c` explicitly permit
**GPL version 2 or (at your option) any later version** in their original
file headers. They must not be characterized as GPLv2-only; the broader
upstream `LICENSE` wording does not supersede those file-level grants.

Review the chosen license and distribution obligations for the **combined**
NaSeed kernel before publishing binaries or other distributions. NaOS's
GPLv3-only sources have not been copied into this port; any future copying
must be reviewed against the actual per-file grants and provenance rather
than an assumed GPLv2-only versus GPLv3 conflict. See
`docs/distribution-license-review.md`.
