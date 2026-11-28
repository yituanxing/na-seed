/* SPDX-License-Identifier: MIT
 * C-facing allocator and upstream C-ABI probe used ONLY by the opt-in
 * cross-language experiment. No 3D implementation, no shadow DRM ioctl.
 * Ownership: NaSeed TLSF. The DRM Rust source remains unchanged. */
#include "../../kernel/mm/heap.h"
#include "../../kernel/errno.h"
#include "../../kernel/drivers/serial.h"
#include <stdint.h>
#include <stddef.h>
#include <stdalign.h>

static const uintptr_t nas_tgos_cookie=(uintptr_t)UINT64_C(0x44524d54474f534b);

/* Temporary, serial-only failure localization; no allocations or fake APIs. */
void nas_tgos_probe_stage(uint32_t stage) {
    nas_print("[TGOS ABI STAGE] ");
    nas_dec(stage);
    nas_print("\n");
}

void *nas_tgos_alloc_aligned(size_t bytes,size_t alignment) {
    /* Rust layout alignment may exceed C max_align_t; never silently
     * return an under-aligned pointer. This heap is process-context only. */
    if (!bytes || !alignment || (alignment & (alignment-1U)) ||
        alignment > (1U<<20))return NULL;
    if (alignment < alignof(void *))alignment=alignof(void *);
    size_t overhead=2U*sizeof(uintptr_t)+alignment-1U;
    if (bytes>SIZE_MAX-overhead)return NULL;
    void *raw=nas_heap_alloc(bytes+overhead);
    if(!raw)return NULL;
    uintptr_t p=(uintptr_t)raw;
    if(p>UINTPTR_MAX-(2U*sizeof(uintptr_t)+alignment-1U)) {
        (void)nas_heap_free(raw);
        return NULL;
    }
    uintptr_t aligned=(p+2U*sizeof(uintptr_t)+alignment-1U)&
                      ~((uintptr_t)alignment-1U);
    uintptr_t *head=(uintptr_t *)aligned;
    head[-2]=(uintptr_t)raw;
    head[-1]=nas_tgos_cookie^aligned;
    return (void *)aligned;
}
void nas_tgos_free_aligned(void *buffer) {
    if(!buffer)return;
    uintptr_t p=(uintptr_t)buffer;
    if(p<2U*sizeof(uintptr_t))__builtin_trap();
    uintptr_t *head=(uintptr_t *)buffer;
    if(head[-1]!=(nas_tgos_cookie^p))__builtin_trap();
    void *base=(void *)head[-2];
    if(!nas_heap_free(base))__builtin_trap();
}
extern int nas_tgos_original_drm_ffi_selftest(void);
/* Actual upstream Rust functions are public extern "C" no_mangle symbols.
 * Resolve BOTH from NaSeed C code as well, so LTO cannot make the whole
 * test pass by inlining them and discarding the original export. */
extern uint32_t drm_mode_legacy_fb_format(uint32_t bpp,uint32_t depth);
/* This is the ABI-visible PREFIX of the original Linux DrmFormatInfo.
 * Rust upstream has compile-time assertions of the full 24-byte layout;
 * NaSeed reads only these first three fields, not a guessed C substitute. */
struct nas_tgos_drm_format_prefix {
    uint32_t format;
    uint8_t depth,num_planes;
};
extern const struct nas_tgos_drm_format_prefix *drm_format_info(uint32_t);

int nas_tgos_drm_check_original_abi(void) {
    /* An ABI mismatch MUST refuse the original /dev/dri/card0 endpoint. */
    nas_tgos_probe_stage(1U);
    uint32_t fmt=drm_mode_legacy_fb_format(32U,24U);
    nas_tgos_probe_stage(2U);
    if(fmt!=UINT32_C(0x34325258))return -EINVAL;
    const struct nas_tgos_drm_format_prefix *info=drm_format_info(fmt);
    nas_tgos_probe_stage(3U);
    if(!info || info->format!=fmt || info->depth!=24U ||
       info->num_planes!=1U)return -EINVAL;

    /* Real upstream Rust KAPI/provider-slot and ioctl contract also run. */
    nas_tgos_probe_stage(4U);
    int result=nas_tgos_original_drm_ffi_selftest();
    nas_tgos_probe_stage(5U);
    return result==0 ? 0 : -EINVAL;
}
