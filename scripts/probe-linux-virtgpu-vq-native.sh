#!/bin/sh
# Diagnostic only. Never treat this as a passing native driver build.
# Compiles the ORIGINAL, unmodified Linux v6.14 virtgpu_vq.c against only
# NaSeed's actual port headers. Prints precisely the first missing API.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT HUP INT TERM
source_file=third_party/linux-virtgpu/drivers/gpu/drm/virtio/virtgpu_vq.c
clang --target=x86_64-unknown-none-elf -std=c11 -ffreestanding \
    -fno-builtin -Wall -Wextra -Werror -ferror-limit=4 \
    -I kernel -I ports/linux-virtgpu/include \
    -I third_party/linux-virtgpu/include/uapi \
    -I ports/unikraft/include -I third_party/unikraft/include \
    -c "$source_file" -o "$scratch/original-virtgpu-vq.o" \
    > "$scratch/build.log" 2>&1 && result=0 || result=$?
if [ "$result" -eq 0 ]; then
    echo 'NATIVE ORIGINAL virtgpu_vq.c: COMPILED (still requires link+QEMU)'
else
    echo 'NATIVE ORIGINAL virtgpu_vq.c: NOT COMPILED — exact first compiler diagnostics:'
    head -n 35 "$scratch/build.log"
    echo 'This is a dependency diagnostic; GPU 3D was NOT enabled.'
fi
# In CI, render the missing header/dependency clearly. This optional probe
# must NOT be misrepresented as a success criterion: the exit code is
# always zero because this script is only an inventory of porting blockers.
if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
    {
        echo '### Original Linux v6.14 virtgpu_vq.c build readiness'
        if [ "$result" -eq 0 ]; then
            echo 'Original source compiled as an object. **Not linked or 3D-tested.**'
        else
            echo '**NOT COMPILED.** Diagnostic only (not a passing test).'
            echo '~~~'
            head -n 18 "$scratch/build.log"
            echo '~~~'
        fi
    } >> "$GITHUB_STEP_SUMMARY"
fi
