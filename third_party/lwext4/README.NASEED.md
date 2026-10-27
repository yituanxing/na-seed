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
clause, while `src/ext4_xattr.c` and `src/ext4_extent.c` carry GPLv2.
The overall unmodified compiled upstream library therefore has GPLv2 terms;
review compatibility with NaSeed's final chosen distribution license before
merging into a distributable kernel. Do not mix code copied from GPLv3-only
NaOS with GPLv2-only files.
