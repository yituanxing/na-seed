#!/bin/sh
# SPDX-License-Identifier: MIT
# Fetch ONE audited source revision; build unchanged upstream DRM crates as
# dependencies of a thin C ABI staticlib. Failure is a HARD build failure.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
pin=0e7907141d5580d1d9a4524b765e86fe17d334d8
repo=https://github.com/rcore-os/tgoskits.git
src=build/tgos-upstream
target=build/tgos-rust-target
archive="$target/x86_64-unknown-none/release/libnaseed_tgoskits_drm_ffi.a"

if [ ! -d "$src/.git" ]; then
    mkdir -p "$src"
    git -C "$src" init -q
    git -C "$src" remote add origin "$repo"
fi
if [ "$(git -C "$src" rev-parse HEAD 2>/dev/null || true)" != "$pin" ]; then
    git -C "$src" -c protocol.version=2 fetch -q --depth=1 --filter=blob:none origin "$pin"
    git -C "$src" checkout -q --detach "$pin"
fi
test "$(git -C "$src" rev-parse HEAD)" = "$pin" || {
    echo 'ERROR: upstream TGOSKits source revision mismatch' >&2; exit 1;
}
test -f "$src/components/kdrm-core/src/drm_fourcc_upstream.rs"
test -f "$src/components/drm-kapi/src/kernel_services.rs"
echo "ORIGINAL UPSTREAM source: $repo @ $pin"
echo 'ORIGINAL UPSTREAM kdrm-core/drm-kapi are not edited or copied into NaSeed.'
# This requires rustup nightly-2026-11-13 with the original upstream target.
# Do not silently switch Rust toolchains or rewrite source on failure.
export CARGO_TARGET_DIR="$root/$target"
export RUSTFLAGS="-C panic=abort -C relocation-model=static -C code-model=kernel"
cargo +nightly-2026-11-13 build \
    --manifest-path ports/tgoskits-drm-ffi/Cargo.toml \
    --release --target x86_64-unknown-none
test -s "$archive"
# Validate the ORIGINAL DRM export and the NaSeed wrapper export are both
# present; the full NaSeed kernel link is a DIFFERENT required CI gate.
nm "$archive" | grep -Eq ' [TW] drm_mode_legacy_fb_format$'
nm "$archive" | grep -Eq ' [TW] nas_tgos_original_drm_ffi_selftest$'
echo "ORIGINAL DRM static archive READY: $archive"
