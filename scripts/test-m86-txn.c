/* M86: link REAL vm.c/anon_regions.c; simulate only arch PTEs and PMM.
 * Verify MAP_FIXED errors preserve the original VMA, contents and owners.
 * No NaSeed guest PASS is fabricated by the host fixture. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdalign.h>
#include "../kernel/task.h"
#include "../kernel/mm/vm.h"
#include "../kernel/mm/frame.h"
#define P 4096ULL
#define NP 96U
#define MAP_FIXED_PRIVATE_ANON 0x32ULL
#define BASE 0x20010000ULL
static alignas(4096) uint8_t ram[NP*P];
static unsigned owners[NP],alloc_count,alloc_fail_at,prepare_count,prepare_fail_at;
static bool read_fail;
struct mapping {uint64_t va,pa;unsigned prot;};
static struct mapping maps[96];
static struct nas_task current;
static struct nas_mm space;
#define SELECTED_ROOT 0xabc000ULL
struct nas_task *nas_current_task=&current;
uint64_t nas_hhdm_offset(void){return (uint64_t)(uintptr_t)ram;}
uint64_t nas_frame_alloc(void){
    if(alloc_fail_at && ++alloc_count==alloc_fail_at)return 0;
    for(unsigned i=1;i<NP;i++)if(!owners[i]) {
        owners[i]=1;memset(ram+i*P,0,P);return i*P;
    }
    return 0;
}
bool nas_frame_put(uint64_t pa){
    if(!pa || (pa&(P-1)) || pa/P>=NP || !owners[pa/P])return false;
    owners[pa/P]--;return true;
}
bool nas_frame_free(uint64_t pa){return nas_frame_put(pa);}
uint32_t nas_frame_refs(uint64_t pa){return pa/P<NP?owners[pa/P]:0;}
static unsigned living(void){
    unsigned n=0;for(unsigned i=1;i<NP;i++)n+=owners[i];return n;
}
static struct mapping *slot(uint64_t va,bool create){
    for(unsigned i=0;i<96;i++)if(maps[i].va==va)return &maps[i];
    if(create)for(unsigned i=0;i<96;i++)if(!maps[i].va){
        maps[i].va=va;return &maps[i];
    }
    return 0;
}
bool nas_mmu_prepare_leaf_at(uint64_t root_phys,uint64_t va){
    (void)va;
    if(root_phys!=SELECTED_ROOT)return false;
    return !(prepare_fail_at && ++prepare_count==prepare_fail_at);
}
bool nas_mmu_exchange_leaf_at(uint64_t root_phys,uint64_t va,
                              uint64_t pa,unsigned prot,uint64_t *old){
    if(root_phys!=SELECTED_ROOT)return false;
    struct mapping *p=slot(va,pa!=0);
    if(!old || !p)return false;
    *old=p->pa;p->pa=pa;p->prot=prot;
    return true;
}
uint8_t *nas_mmu_user_byte(uint64_t va){
    struct mapping *p=slot(va&~(P-1),false);
    return p && p->pa ? ram+p->pa+(va&(P-1)) : 0;
}
bool nas_mmu_map_prot_page(uint64_t va,uint64_t prot){
    struct mapping *p=slot(va,true);
    if(!p || p->pa)return false;
    p->pa=nas_frame_alloc();p->prot=(unsigned)prot;return p->pa!=0;
}
bool nas_mmu_unmap_page(uint64_t va){
    struct mapping *p=slot(va,false);
    if(!p || !p->pa)return true;
    uint64_t pa=p->pa;p->pa=0;return nas_frame_put(pa);
}
bool nas_mmu_resolve_user_address(uint64_t root,uint64_t va,
                                   uint64_t *physical,unsigned *access){
    if(root!=SELECTED_ROOT || !physical)return false;
    struct mapping *p=slot(va&~(P-1),false);
    if(!p || !p->pa)return false;
    *physical=p->pa+(va&(P-1));
    if(access)*access=p->prot;
    return true;
}
bool nas_mmu_protect_pages(uint64_t va,uint64_t pages,uint64_t prot){
    for(uint64_t i=0;i<pages;i++){
        struct mapping *p=slot(va+i*P,false);
        if(!p || !p->pa)return false;
    }
    for(uint64_t i=0;i<pages;i++)slot(va+i*P,false)->prot=(unsigned)prot;
    return true;
}
bool nas_arch_nx_enabled(void){return true;}
__attribute__((noreturn)) void nas_arch_halt(void){abort();}
int64_t nas_fs_mmap_read(uint64_t fd,uint64_t off,uint8_t *dst,uint64_t n){
    if(fd!=4)return -9;
    if(!dst && !n)return 0;
    if(read_fail && off>=P)return -5;
    if(!dst)return -14;
    for(uint64_t i=0;i<n;i++)dst[i]=(uint8_t)((off+i+7)&255);
    return (int64_t)n;
}
static void initialize(void){
    current.mm=&space;space.users=1;space.root_phys=SELECTED_ROOT;
    assert(nas_vma_record(&space.mmap_vmas,BASE,2*P,3,
                          NAS_VMA_ANON_PRIVATE,0));
    for(unsigned i=0;i<2;i++){
        uint64_t pa=nas_frame_alloc();
        assert(pa);
        struct mapping *m=slot(BASE+i*P,true);
        assert(m);m->pa=pa;m->prot=3;
        memset(ram+pa,(int)(0xa0+i),P);
    }
}
int main(void){
    initialize();
    uint64_t original_pa0=slot(BASE,false)->pa;
    uint64_t original_pa1=slot(BASE+P,false)->pa;
    unsigned baseline=living();
    alloc_count=0;alloc_fail_at=3;
    assert((int64_t)nas_vm_anon_mmap(BASE,2*P,3,MAP_FIXED_PRIVATE_ANON,
                                      UINT64_MAX,0)==-12);
    alloc_fail_at=0;
    assert(slot(BASE,false)->pa==original_pa0 &&
           slot(BASE+P,false)->pa==original_pa1 &&
           living()==baseline &&
           nas_vma_find(&space.mmap_vmas,BASE)->prot==3 &&
           ram[original_pa0]==0xa0 && ram[original_pa1]==0xa1);
    prepare_count=0;prepare_fail_at=2;
    assert((int64_t)nas_vm_anon_mmap(BASE,2*P,1,MAP_FIXED_PRIVATE_ANON,
                                      UINT64_MAX,0)==-12);
    prepare_fail_at=0;
    assert(slot(BASE,false)->pa==original_pa0 &&
           slot(BASE+P,false)->pa==original_pa1 && living()==baseline);
    read_fail=true;
    assert((int64_t)nas_vm_file_mmap(BASE,2*P,1,0x12,4,0)==-5);
    read_fail=false;
    assert(slot(BASE,false)->pa==original_pa0 &&
           slot(BASE+P,false)->pa==original_pa1 &&
           living()==baseline && ram[original_pa0]==0xa0);
    assert(nas_vm_file_mmap(BASE,2*P,1,0x12,4,0)==BASE);
    const struct nas_anon_region *v=nas_vma_find(&space.mmap_vmas,BASE);
    assert(v && v->prot==1 && v->kind==NAS_VMA_FILE_PRIVATE_SNAPSHOT);
    assert(slot(BASE,false)->pa!=original_pa0 &&
           slot(BASE+P,false)->pa!=original_pa1 &&
           !owners[original_pa0/P] && !owners[original_pa1/P] &&
           living()==baseline);
    assert(*nas_mmu_user_byte(BASE)==7 &&
           *nas_mmu_user_byte(BASE+P+1)==8);
    assert(nas_vm_anon_mmap(BASE,2*P,0,MAP_FIXED_PRIVATE_ANON,
                             UINT64_MAX,0)==BASE);
    assert(!nas_mmu_user_byte(BASE) && !nas_mmu_user_byte(BASE+P));
    assert(nas_vma_find(&space.mmap_vmas,BASE)->prot==0);
    assert(living()==0);
    /* A mixed resident + reserved range must NOT partially change the
     * existing page's permissions when the new page fails to allocate. */
    assert(nas_mmu_map_prot_page(BASE,1));
    uint64_t retained=slot(BASE,false)->pa;
    assert(retained);
    ram[retained]=0x7b;
    alloc_count=0;alloc_fail_at=1;
    assert((int64_t)nas_vm_mprotect(BASE,2*P,3)==-12);
    alloc_fail_at=0;
    assert(slot(BASE,false)->pa==retained &&
           slot(BASE,false)->prot==1 &&
           !nas_mmu_user_byte(BASE+P) &&
           nas_vma_find(&space.mmap_vmas,BASE)->prot==0 &&
           ram[retained]==0x7b && living()==1);
    prepare_count=0;prepare_fail_at=1;
    assert((int64_t)nas_vm_mprotect(BASE,2*P,3)==-12);
    prepare_fail_at=0;
    assert(slot(BASE,false)->pa==retained &&
           slot(BASE,false)->prot==1 &&
           !nas_mmu_user_byte(BASE+P) && living()==1);
    assert(nas_vm_mprotect(BASE,2*P,3)==0);
    assert(slot(BASE,false)->pa==retained &&
           slot(BASE,false)->prot==3 &&
           slot(BASE+P,false)->pa &&
           nas_vma_find(&space.mmap_vmas,BASE)->prot==3 &&
           ram[retained]==0x7b && living()==2);
    assert(nas_vm_munmap(BASE,2*P)==0);
    assert(living()==0 && !nas_vma_find(&space.mmap_vmas,BASE));
    assert(nas_vm_file_mmap(0,P,1,2,4,0)>0);
    assert(living()==1); /* one resident data frame; staging metadata freed */
    puts("[M87 MM TXN PASS] MAP_FIXED and mprotect OOM/PT faults preserve VMA/PTE and frame ownership");
    return 0;
}
