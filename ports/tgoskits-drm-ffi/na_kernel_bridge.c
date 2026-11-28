/* SPDX-License-Identifier: MIT
 * C-facing allocator and upstream C-ABI probe used ONLY by the opt-in
 * cross-language experiment. No 3D implementation, no shadow DRM ioctl.
 * Ownership: NaSeed TLSF. The DRM Rust source remains unchanged. */
#include "../../kernel/mm/heap.h"
#include "../../kernel/errno.h"
#include <stdint.h>
#include <stddef.h>
#include <stdalign.h>

static const uintptr_t nas_tgos_cookie=(uintptr_t)UINT64_C(0x44524d54474f534b);

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

int nas_tgos_drm_check_original_abi(void) {
    /* This actual upstream call is required during opt-in DRM node init.
     * An incompatible module *blocks* node registration, never pretends
     * accelerated GPU ioctls succeeded. */
    int result=nas_tgos_original_drm_ffi_selftest();
    return result==0 ? 0 : -EINVAL;
}
