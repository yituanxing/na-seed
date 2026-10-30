/* Link the ACTUAL MM owner, COW fault, usercopy and ptrace implementation.
 * Mock only physical frame allocator and three-ISA PT provider. This tests
 * ownership, fork rollback and cross-MM bytes, not a fabricated QEMU pass. */
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdalign.h>
#include "../kernel/task.h"
#include "../kernel/mm/mm.h"
#include "../kernel/mm/vm.h"
#include "../kernel/mm/fault.h"
#include "../kernel/mm/usercopy.h"
#include "../kernel/mm/frame.h"
#include "../kernel/arch/user_pt.h"
#define P 4096ULL
#define NFRAMES 128
#define NROOTS 6
#define A 0x20010000ULL
#define X 0x20020000ULL
struct mapping {uint64_t va,pa;unsigned prot;};
struct mock_root {uint64_t id;struct mapping leaves[32];};
static struct mock_root roots[NROOTS];
static alignas(4096) uint8_t memory[NFRAMES*P];
static uint32_t refs[NFRAMES];
static unsigned get_calls,fail_get_on;
struct nas_task *nas_current_task;
void nas_print(const char *msg){(void)msg;}
__attribute__((noreturn))void nas_arch_halt(void) {abort();}
uint64_t nas_hhdm_offset(void){return (uint64_t)(uintptr_t)memory;}
uint64_t nas_frame_alloc(void) {
    for(unsigned i=1;i<NFRAMES;i++)if(!refs[i]) {
        refs[i]=1;memset(memory+i*P,0,P);return i*P;
    }
    return 0;
}
bool nas_frame_get(uint64_t pa) {
    if(fail_get_on && ++get_calls==fail_get_on)return false;
    if(pa%P || pa/P>=NFRAMES || !refs[pa/P])return false;
    ++refs[pa/P];return true;
}
bool nas_frame_put(uint64_t pa) {
    if(!pa || pa%P || pa/P>=NFRAMES || !refs[pa/P])return false;
    --refs[pa/P];return true;
}
uint32_t nas_frame_refs(uint64_t pa) {
    return pa && !(pa%P) && pa/P<NFRAMES?refs[pa/P]:0;
}
static unsigned total_refs(void) {
    unsigned n=0;for(unsigned i=1;i<NFRAMES;i++)n+=refs[i];
    return n;
}
static struct mock_root *root(uint64_t id) {
    for(unsigned i=0;i<NROOTS;i++)if(roots[i].id==id)return &roots[i];
    return 0;
}
static struct mapping *entry(uint64_t id,uint64_t va,bool create) {
    struct mock_root *r=root(id);
    if(!r)return 0;
    for(unsigned i=0;i<32;i++)
        if(r->leaves[i].va==va)return &r->leaves[i];
    if(create)for(unsigned i=0;i<32;i++)if(!r->leaves[i].va) {
        r->leaves[i].va=va;return &r->leaves[i];
    }
    return 0;
}
uint64_t nas_mmu_alloc_root(void){
    for(unsigned i=0;i<NROOTS;i++)if(!roots[i].id) {
        roots[i].id=(i+1U)*P;return roots[i].id;
    }
    return 0;
}
bool nas_mmu_reclaim_root(uint64_t id) {
    struct mock_root *r=root(id);if(!r)return false;
    for(unsigned i=0;i<32;i++)if(r->leaves[i].pa) {
        if(!nas_frame_put(r->leaves[i].pa))return false;
    }
    *r=(struct mock_root){0};
    return true;
}
bool nas_mmu_prepare_leaf_at(uint64_t id,uint64_t va) {
    return root(id) && va && !(va&(P-1));
}
bool nas_mmu_exchange_leaf_at(uint64_t id,uint64_t va,uint64_t new_pa,
                              unsigned prot,uint64_t *old) {
    if(!old || va&(P-1))return false;
    struct mapping *m=entry(id,va,new_pa!=0);
    if(!m)return false;
    *old=m->pa;
    m->pa=new_pa;
    m->prot=prot?NAS_UP_READ|((prot&2)?NAS_UP_WRITE:0)|
                        ((prot&4)?NAS_UP_EXEC:0):0;
    return true;
}
bool nas_mmu_resolve_user_address(uint64_t id,uint64_t va,
                                  uint64_t *pa,unsigned *prot) {
    struct mapping *m=entry(id,va&~(P-1),false);
    if(!m || !m->pa)return false;
    if(pa)*pa=m->pa+(va&(P-1));
    if(prot)*prot=m->prot;
    return true;
}
bool nas_mmu_walk_user_mapping(uint64_t id,nas_mmu_user_visit cb,void *ctx) {
    struct mock_root *r=root(id);
    if(!r || !cb)return false;
    for(unsigned i=0;i<32;i++)if(r->leaves[i].pa &&
       !cb(r->leaves[i].va,r->leaves[i].prot,ctx))return false;
    return true;
}
uint8_t *nas_mmu_user_byte(uint64_t va) {
    if(!nas_current_task || !nas_current_task->mm)return 0;
    uint64_t pa=0;
    return nas_mmu_resolve_user_address(nas_current_task->mm->root_phys,
                                         va,&pa,0)?memory+pa:0;
}
bool nas_mmu_user_writable(uint64_t va) {
    if(!nas_current_task || !nas_current_task->mm)return false;
    unsigned access=0;
    return nas_mmu_resolve_user_address(nas_current_task->mm->root_phys,
                                        va,0,&access) && (access&NAS_UP_WRITE);
}
static void map(struct nas_mm *mm,uint64_t va,unsigned prot,uint8_t value) {
    uint64_t frame=nas_frame_alloc();assert(frame);
    memset(memory+frame,value,P);
    uint64_t prior=0;
    assert(nas_mmu_prepare_leaf_at(mm->root_phys,va));
    assert(nas_mmu_exchange_leaf_at(mm->root_phys,va,frame,prot,&prior)&&!prior);
}
static uint64_t phys(const struct nas_mm *mm,uint64_t va,unsigned *perm) {
    uint64_t pa=0;
    assert(nas_mm_resolve_user(mm,va,&pa,perm));
    return pa&~(P-1);
}
int main(void) {
    nas_mm_system_init();
    struct nas_mm *parent=nas_mm_new();assert(parent);
    parent->root_phys=nas_mmu_alloc_root();assert(parent->root_phys);
    assert(nas_vma_record(&parent->mmap_vmas,A,3*P,3,
                          NAS_VMA_ANON_PRIVATE,0));
    assert(nas_vma_record(&parent->mmap_vmas,X,P,5,
                          NAS_VMA_ELF_PRIVATE_SNAPSHOT,0));
    map(parent,A,3,0x11);
    map(parent,X,5,0x90);
    /* Real MADV_DONTFORK fork contract: excluded *resident* pages must be
     * absent in the child VMA and PT, while parent stays RW with one frame
     * ref. A neighboring ordinary VMA must still participate in COW. */
    map(parent,A+2*P,3,0x6a);
    uint64_t excluded=phys(parent,A+2*P,0);
    assert(nas_vma_set_dontfork(&parent->mmap_vmas,A+2*P,P,true));
    assert(nas_vma_find(&parent->mmap_vmas,A+P) &&
           !(nas_vma_find(&parent->mmap_vmas,A+P)->flags&NAS_VMA_DONTFORK));
    assert(nas_vma_find(&parent->mmap_vmas,A+2*P)->flags&NAS_VMA_DONTFORK);
    uint64_t initial=phys(parent,A,0);
    uint64_t code=phys(parent,X,0);
    struct nas_task original={0};
    original.mm=parent;original.state=NAS_TASK_RUNNING;
    nas_current_task=&original;
    struct nas_mm *child=nas_mm_fork_cow(parent);assert(child);
    unsigned access=0;
    uint64_t child_excluded=0;
    assert(!nas_vma_find(&child->mmap_vmas,A+2*P));
    assert(!nas_mm_resolve_user(child,A+2*P,&child_excluded,0));
    assert(phys(parent,A+2*P,&access)==excluded &&
           access==(NAS_UP_READ|NAS_UP_WRITE) && nas_frame_refs(excluded)==1);
    assert(phys(child,A,&access)==initial && access==NAS_UP_READ);
    assert(phys(parent,A,&access)==initial && access==NAS_UP_READ);
    assert(nas_frame_refs(initial)==2);
    assert(phys(parent,X,0)==phys(child,X,0) && nas_frame_refs(code)==2);
    assert(nas_vma_find(&child->mmap_vmas,A)->prot==3);
    assert(nas_user_address_writable(A) && !nas_user_address_writable(X));
    assert(nas_mm_handle_fault(parent,A,NAS_UP_WRITE,true)==NAS_MM_FAULT_RESOLVED);
    uint64_t parent_copy=phys(parent,A,&access);
    assert(parent_copy!=initial && access==(NAS_UP_READ|NAS_UP_WRITE));
    assert(nas_frame_refs(parent_copy)==1 && nas_frame_refs(initial)==1);
    struct nas_task kid={0};kid.mm=child;kid.state=NAS_TASK_STOPPED;
    nas_current_task=&kid;
    assert(nas_user_address_writable(A) && !nas_user_address_writable(X));
    uint8_t values[4]={1,2,3,4};
    assert(nas_user_write_bytes(A+9,values,sizeof(values)));
    assert(phys(child,A,&access)==initial && (access&NAS_UP_WRITE));
    assert(memory[initial+9]==1 && memory[parent_copy+9]==0x11);
    /* Ptrace must privately modify RX code, never changing its user PTE
     * permission nor the parent's executable bytes. */
    assert(nas_vm_write_task_bytes(&kid,X+3,values,4)==4);
    assert(phys(child,X,&access)!=code && access==(NAS_UP_READ|NAS_UP_EXEC));
    assert(memory[code+3]==0x90);
    uint8_t check[4]={0};
    assert(nas_vm_read_task_bytes(&kid,X+3,check,4)==4 &&
           !memcmp(check,values,4));
    /* Two-phase usercopy writes no bytes on a later VMA boundary failure. */
    uint8_t boundary[8]={8,8,8,8,8,8,8,8};
    assert(!nas_user_write_bytes(A+3*P-4,boundary,8));
    /* Lazy anonymous resident fill from a valid VMA via usercopy. */
    assert(nas_user_read_bytes(A+P,check,4));
    assert(!check[0]&&!check[1]&&!check[2]&&!check[3]);
    assert(phys(child,A+P,&access)!=0);
    nas_mm_put(child);kid.mm=0;
    assert(nas_frame_refs(initial)==0 && nas_frame_refs(code)==1);
    assert(nas_frame_refs(parent_copy)==1);
    nas_current_task=&original;
    struct nas_mm *rollback=nas_mm_new();assert(rollback);
    rollback->root_phys=nas_mmu_alloc_root();
    assert(nas_vma_record(&rollback->mmap_vmas,A,2*P,3,
                          NAS_VMA_ANON_PRIVATE,0));
    map(rollback,A,3,0x41);map(rollback,A+P,3,0x42);
    unsigned before=total_refs();
    get_calls=0;fail_get_on=2;
    assert(!nas_mm_fork_cow(rollback));
    fail_get_on=0;
    assert(total_refs()==before && nas_frame_refs(phys(rollback,A,&access))==1);
    assert(access==(NAS_UP_READ|NAS_UP_WRITE)); /* parent never downgraded */
    nas_mm_put(rollback);
    /* A physical leaf without a logical VMA was allowed by the old eager
     * fork fallback. It must now fail without leaking refs or write-
     * protecting the parent: there is no policy for a later COW fault. */
    struct nas_mm *orphan=nas_mm_new();assert(orphan);
    orphan->root_phys=nas_mmu_alloc_root();assert(orphan->root_phys);
    map(orphan,A,3,0x51);
    uint64_t orphan_frame=phys(orphan,A,0);
    before=total_refs();
    assert(!nas_mm_fork_cow(orphan));
    assert(total_refs()==before && nas_frame_refs(orphan_frame)==1);
    assert(phys(orphan,A,&access)==orphan_frame &&
           access==(NAS_UP_READ|NAS_UP_WRITE));
    nas_mm_put(orphan);
    nas_mm_put(parent);
    assert(total_refs()==0);
    puts("[M101 COW PASS] rollback, no orphan PTE fork, shared fork, COW write, singleton promotion, RX ptrace isolation and lazy usercopy");
}
