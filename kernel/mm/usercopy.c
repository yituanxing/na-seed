#include <stdint.h>
#include <stdbool.h>
#include "../errno.h"
#include "../arch/mmu.h"
#include "../lib/string.h"
#include "usercopy.h"
#include "mm.h"
#include "fault.h"
#include "frame.h"
#include "../task.h"
#include "../arch/user_pt.h"
#include "prot.h"
/* Every NaSeed user page is a 4-KiB base page across all three backends.
 * A resolved HHDM pointer is physically contiguous only until the page
 * boundary; do not confuse adjacent user VAs with adjacent frames. */
#define NAS_USERCOPY_PAGE_BYTES 4096ULL
static uint64_t page_chunk(uint64_t va, uint64_t remaining) {
    uint64_t room = NAS_USERCOPY_PAGE_BYTES -
                    (va & (NAS_USERCOPY_PAGE_BYTES - 1));
    return remaining < room ? remaining : room;
}
/* Return the kernel's alias of a readable user page, resolving a legitimate
 * nonresident VMA once. Never resolve an unmapped address by inventing a PTE. */
static uint8_t *read_page(uint64_t at) {
    uint8_t *src = nas_mmu_user_byte(at);
    if (src) return src;
    if (!nas_current_task || !nas_current_task->mm ||
        nas_mm_handle_fault(nas_current_task->mm,at,NAS_UP_READ,false)!=
            NAS_MM_FAULT_RESOLVED)
        return 0;
    return nas_mmu_user_byte(at);
}
/* This kernel-origin copy may initialize RX code pages: intentionally
 * require a valid user mapping, NOT user-writable permission. */
bool nas_user_copy_to_mapped(uint64_t va, const uint8_t *data, uint64_t length) {
    if (length && va > UINT64_MAX - (length - 1)) return false;
    for (uint64_t done = 0; done < length;) {
        uint64_t addr = va + done;
        uint8_t *dst = nas_mmu_user_byte(addr);
        if (!dst) return false;
        uint64_t n = page_chunk(addr, length - done);
        memcpy(dst, data + done, (size_t)n);
        done += n;
    }
    return true;
}
bool nas_user_read_bytes(uint64_t addr, uint8_t *dst, uint64_t len) {
    if (addr >= USER_LIMIT || len > USER_LIMIT - addr) return false;
    for (uint64_t done = 0; done < len;) {
        uint64_t at = addr + done;
        uint8_t *src = read_page(at);
        if (!src) return false;
        uint64_t n = page_chunk(at, len - done);
        memcpy(dst + done, src, (size_t)n);
        done += n;
    }
    return true;
}
int nas_user_read_cstr(uint64_t addr,char *dst,uint64_t capacity) {
    if(!dst || addr>=USER_LIMIT)return -EFAULT;
    if(!capacity)return -ENAMETOOLONG;
    for(uint64_t done=0;done<capacity;) {
        const uint64_t at=addr+done;
        if(at>=USER_LIMIT)return -EFAULT;
        uint8_t *src=read_page(at);
        if(!src)return -EFAULT;
        uint64_t n=page_chunk(at,capacity-done);
        if(n>USER_LIMIT-at)n=USER_LIMIT-at;
        /* Do not touch the next byte after NUL, even when it shares a page. */
        uint64_t i=0;
        while(i<n && src[i])i++;
        if(i<n) {
            memcpy(dst+done,src,(size_t)(i+1));
            return 0;
        }
        memcpy(dst+done,src,(size_t)n);
        done+=n;
    }
    return -ENAMETOOLONG;
}
bool nas_user_address_writable(uint64_t addr) {
    if(addr>=USER_LIMIT || !nas_current_task || !nas_current_task->mm)
        return false;
    const struct nas_mm *mm=nas_current_task->mm;
    const struct nas_anon_region *v=nas_vma_find(&mm->mmap_vmas,addr);
    if(v)return (v->prot&NAS_VM_WRITE)!=0;
    unsigned hw=0;
    /* Legacy resident mappings without a canonical VMA may only pass if
     * truly writable; never invent a permission for an unmapped user VA. */
    uint64_t pa=0;
    return nas_mm_resolve_user(mm,addr,&pa,&hw) && (hw&NAS_UP_WRITE);
}
bool nas_user_prepare_write(uint64_t addr,uint64_t len) {
    if (addr >= USER_LIMIT || len > USER_LIMIT - addr) return false;
    /* Resolve/validate the whole destination without writing any bytes. */
    for (uint64_t checked = 0; checked < len;) {
        uint64_t at = addr + checked;
        if(!nas_current_task || !nas_current_task->mm)return false;
        struct nas_mm *mm=nas_current_task->mm;
        const struct nas_anon_region *v=nas_vma_find(&mm->mmap_vmas,at);
        if(v && !(v->prot&NAS_VM_WRITE))return false;
        uint64_t pa=0;
        unsigned access=0;
        if(!nas_mm_resolve_user(mm,at,&pa,&access)) {
            if(nas_mm_handle_fault(mm,at,NAS_UP_WRITE,false)!=
               NAS_MM_FAULT_RESOLVED)return false;
        } else if(!(access&NAS_UP_WRITE)) {
            if(nas_mm_handle_fault(mm,at,NAS_UP_WRITE,true)!=
               NAS_MM_FAULT_RESOLVED)return false;
        } else if(nas_frame_refs(pa&~UINT64_C(4095))>1 &&
                  !nas_mm_break_cow(mm,at,access,false))return false;
        if(!nas_mmu_user_writable(at))return false;
        checked += page_chunk(at, len - checked);
    }
    return true;
}
bool nas_user_write_bytes(uint64_t addr, const uint8_t *src, uint64_t len) {
    /* Keep original two-phase semantics: reject any unmapped/read-only page
     * before modifying ANY byte of the target range. */
    if(!nas_user_prepare_write(addr,len))return false;
    for (uint64_t done = 0; done < len;) {
        uint64_t at = addr + done;
        uint8_t *dest = nas_mmu_user_byte(at);
        if (!dest) return false;
        uint64_t n = page_chunk(at, len - done);
        memcpy(dest, src + done, (size_t)n);
        done += n;
    }
    return true;
}
bool nas_user_read_u64(uint64_t addr, uint64_t *value) {
    uint8_t bytes[8];
    if (!nas_user_read_bytes(addr, bytes, 8)) return false;
    *value = 0;
    for (unsigned n = 0; n < 8; n++) *value |= (uint64_t)bytes[n] << (8 * n);
    return true;
}
bool nas_user_write_u64(uint64_t addr, uint64_t value) {
    uint8_t bytes[8];
    for (unsigned n = 0; n < 8; n++) bytes[n] = (uint8_t)(value >> (8 * n));
    return nas_user_write_bytes(addr, bytes, 8);
}
