/* Exercise the REAL kernel/mm/vm.c truncate walker on hosted fake page
 * tables. Unused kernel functions are discarded at link by --gc-sections.
 * Only the page-table, MM enumeration and inode identity boundary are faked. */
#include "../kernel/mm/vm.h"
#include "../kernel/mm/mm.h"
#include "../kernel/mm/frame.h"
#include "../kernel/mm/file_backing.h"
#include "../kernel/arch/mmu.h"
#include "../kernel/fs/vfs.h"
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VA UINT64_C(0x40000000)
#define PA UINT64_C(0x1000)
struct nas_mapped_file { int marker; };
static struct nas_mapped_file backing={1}, other={2};
/* VFS node is opaque to VM: identity is never dereferenced by this test. */
static _Alignas(16) unsigned char inode_storage[16];
static const struct nas_fs_node *const inode =
    (const struct nas_fs_node *)(const void *)inode_storage;
static struct nas_anon_region vma;
static struct nas_mm mm;
static _Alignas(PAGE) uint8_t physical[PAGE];
static unsigned exchanges, releases, matches, resolves;
static bool resident;

bool nas_mm_walk_live(nas_mm_live_visit fn,void *ctx) {
    return fn(&mm,ctx);
}
bool nas_fs_mmap_matches(struct nas_mapped_file *file,
                         const struct nas_fs_node *node) {
    ++matches;
    return file==&backing && node==inode;
}
bool nas_mm_resolve_user(const struct nas_mm *owner,uint64_t addr,
                         uint64_t *pa,unsigned *access) {
    assert(owner==&mm && addr==VA);
    ++resolves;
    if(!resident)return false;
    *pa=PA;
    if(access)*access=3;
    return true;
}
bool nas_mmu_exchange_leaf_at(uint64_t root,uint64_t va,
                              uint64_t frame,unsigned prot,uint64_t *old) {
    assert(root==mm.root_phys && va==VA && frame==0 && prot==0);
    assert(old && resident);
    ++exchanges;
    *old=PA;
    resident=false;
    return true;
}
bool nas_frame_put(uint64_t frame) {
    assert(frame==PA);
    ++releases;
    return true;
}
uint64_t nas_hhdm_offset(void) {
    return (uint64_t)(uintptr_t)physical-PA;
}
void nas_arch_halt(void) {
    abort();
}
static void reset(unsigned kind,struct nas_mapped_file *file) {
    memset(physical,0xa5,sizeof physical);
    vma=(struct nas_anon_region){
        .start=VA,.end=VA+PAGE,.kind=kind,.prot=3,.backing=file
    };
    mm=(struct nas_mm){.users=1,.root_phys=PA,
        .mmap_vmas={.slots=&vma,.capacity=1}};
    exchanges=releases=matches=resolves=0;
    resident=true;
}
static void private_copy_survives_truncate(void) {
    reset(NAS_VMA_FILE_PRIVATE_LAZY,&backing);
    nas_vm_invalidate_file(inode,1000);
    assert(physical[999]==0xa5 && physical[1000]==0xa5 &&
           physical[PAGE-1]==0xa5);
    assert(resident && exchanges==0 && releases==0);
    reset(NAS_VMA_FILE_PRIVATE_LAZY,&backing);
    nas_vm_invalidate_file(inode,0);
    assert(resident && physical[0]==0xa5 && physical[PAGE-1]==0xa5);
    assert(exchanges==0 && releases==0);
}
static void shared_mapping_tracks_new_eof(void) {
    reset(NAS_VMA_FILE_MUTABLE_SHARED,&backing);
    nas_vm_invalidate_file(inode,1000);
    assert(resident && exchanges==0 && releases==0 && matches==1);
    assert(physical[999]==0xa5 && physical[1000]==0 &&
           physical[PAGE-1]==0);
    reset(NAS_VMA_FILE_MUTABLE_SHARED,&backing);
    nas_vm_invalidate_file(inode,0);
    assert(!resident && exchanges==1 && releases==1);
}
static void unrelated_inode_is_untouched(void) {
    reset(NAS_VMA_FILE_MUTABLE_SHARED,&other);
    nas_vm_invalidate_file(inode,0);
    assert(resident && matches==1 && resolves==0 &&
           exchanges==0 && releases==0);
}
int main(void) {
    private_copy_survives_truncate();
    shared_mapping_tracks_new_eof();
    unrelated_inode_is_untouched();
    puts("PASS: truncate preserves private COW snapshots, invalidates only shared cache aliases");
    return 0;
}
