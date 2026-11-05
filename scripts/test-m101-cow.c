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
#include "../kernel/mm/device_backing.h"
#include "../kernel/fs/io.h"
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
static bool fail_prepare,fail_exchange;
/* Real MM/COW code over one shared inode-owned cache frame; only FS page
 * acquisition is mocked, so a fork that accidentally COWs the page fails. */
struct nas_mapped_file { unsigned refs; };
static struct nas_mapped_file file_owner={.refs=1};
static uint64_t file_pages[2];
static uint64_t file_size=2*P;
static unsigned mapped_dirty;
/* Device page() only BORROWS a PMM frame.  The provider holds its original
 * reference and MM must acquire one more for EACH resident PTE. */
static uint64_t device_pages[4];
static unsigned device_pins=1,device_page_calls;
static bool device_get(void *context) {
    assert(context==device_pages && device_pins);
    ++device_pins;
    return true;
}
static void device_put(void *context) {
    assert(context==device_pages && device_pins>1);
    --device_pins;
}
static int device_page(void *context,uint64_t offset,uint64_t *pa) {
    assert(context==device_pages && pa);
    ++device_page_calls;
    *pa=0;
    if(offset==2*P)return -5; /* device error: no MM leaf or new ref */
    if(offset%P || offset>=4*P)return -22;
    *pa=device_pages[offset/P];
    return *pa ? 0 : -5;
}
static struct nas_vm_device device_owner={
    .context=device_pages,.get=device_get,
    .put=device_put,.page=device_page
};
/* This fixture covers mutable-file and device shared VMAs. Immutable lazy
 * private reads are verified by the real VM/fault path in test-m86-txn.c. */
int64_t nas_fs_mmap_read_immutable(struct nas_mapped_file *file,
                                    uint64_t off,uint8_t *dst,uint64_t len) {
    (void)file;(void)off;(void)dst;(void)len;
    return -22;
}
bool nas_fs_mmap_get(struct nas_mapped_file *file) {
    if(file!=&file_owner || !file->refs)return false;
    file->refs++;return true;
}
void nas_fs_mmap_put(struct nas_mapped_file *file) {
    assert(file==&file_owner && file->refs>1);file->refs--;
}
int nas_fs_mmap_page(struct nas_mapped_file *file,uint64_t off,
                     uint64_t *pa_owned) {
    if(!pa_owned)return -22;
    *pa_owned=0;
    if(file!=&file_owner || (off&(P-1)) || off>=file_size)return -14;
    unsigned index=(unsigned)(off/P);
    if(!file_pages[index])file_pages[index]=nas_frame_alloc();
    if(!file_pages[index] || !nas_frame_get(file_pages[index]))return -12;
    *pa_owned=file_pages[index];return 0;
}
int nas_fs_mmap_dirty(struct nas_mapped_file *file) {
    if(file!=&file_owner)return -9;
    mapped_dirty++;return 0;
}
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
    return !fail_prepare && root(id) && va && !(va&(P-1));
}
bool nas_mmu_exchange_leaf_at(uint64_t id,uint64_t va,uint64_t new_pa,
                              unsigned prot,uint64_t *old) {
    if(fail_exchange || !old || va&(P-1))return false;
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
    /* MAP_SHARED is an inode alias, unlike the preceding private anon/ELF
     * test. Both children and parents retain WRITE and identical frame PA;
     * kernel usercopy, ptrace and lazy page faults must never privatize it. */
    struct nas_mm *shared=nas_mm_new();assert(shared);
    shared->root_phys=nas_mmu_alloc_root();assert(shared->root_phys);
    assert(nas_vma_record_shared(&shared->mmap_vmas,X,2*P,3,0,&file_owner));
    file_pages[0]=nas_frame_alloc();assert(file_pages[0]);
    memset(memory+file_pages[0],0x46,P);
    assert(nas_frame_get(file_pages[0]));
    uint64_t prior=0;
    assert(nas_mmu_exchange_leaf_at(shared->root_phys,X,file_pages[0],3,&prior));
    assert(!prior && file_owner.refs==2);
    struct nas_mm *shared_child=nas_mm_fork_cow(shared);
    assert(shared_child && file_owner.refs==3);
    assert(phys(shared,X,&access)==file_pages[0] &&
           access==(NAS_UP_READ|NAS_UP_WRITE));
    assert(phys(shared_child,X,&access)==file_pages[0] &&
           access==(NAS_UP_READ|NAS_UP_WRITE) &&
           nas_frame_refs(file_pages[0])==3);
    struct nas_task shared_task={.mm=shared_child,.state=NAS_TASK_STOPPED};
    nas_current_task=&shared_task;
    uint8_t different=0x93;
    assert(nas_user_write_bytes(X+1,&different,1));
    assert(memory[file_pages[0]+1]==0x93 &&
           phys(shared,X,0)==phys(shared_child,X,0));
    different=0x5a;
    assert(nas_vm_write_task_bytes(&shared_task,X+2,&different,1)==1);
    assert(memory[file_pages[0]+2]==0x5a && mapped_dirty>=2);
    assert(nas_mm_handle_fault(shared_child,X+P,NAS_UP_READ,false)==
           NAS_MM_FAULT_RESOLVED);
    assert(nas_mm_handle_fault(shared,X+P,NAS_UP_READ,false)==
           NAS_MM_FAULT_RESOLVED);
    assert(phys(shared,X+P,0)==phys(shared_child,X+P,0) &&
           phys(shared,X+P,0)==file_pages[1]);
    nas_mm_put(shared_child);
    nas_mm_put(shared);
    assert(file_owner.refs==1 && nas_frame_refs(file_pages[0])==1 &&
           nas_frame_refs(file_pages[1])==1);
    assert(nas_frame_put(file_pages[0]));file_pages[0]=0;
    assert(nas_frame_put(file_pages[1]));file_pages[1]=0;
    /* M200: device-borrowed frames are not file-cache pages or private
     * copies.  Fault, fork, usercopy and ptrace must all preserve PA. */
    for(unsigned i=0;i<4;i++)if(i!=2) {
        device_pages[i]=nas_frame_alloc();assert(device_pages[i]);
        memset(memory+device_pages[i],0x61+i,P);
    }
    struct nas_mm *device_mm=nas_mm_new();assert(device_mm);
    device_mm->root_phys=nas_mmu_alloc_root();assert(device_mm->root_phys);
    assert(nas_vma_record_device(&device_mm->mmap_vmas,X,4*P,3,
                                 0,&device_owner));
    assert(nas_vma_record_device(&device_mm->mmap_vmas,A,P,1,
                                 3*P,&device_owner));
    assert(device_pins==3);
    unsigned dirty_before=mapped_dirty;
    assert(nas_mm_handle_fault(device_mm,X,NAS_UP_READ,false)==
           NAS_MM_FAULT_RESOLVED);
    assert(phys(device_mm,X,&access)==device_pages[0] &&
           access==(NAS_UP_READ|NAS_UP_WRITE) &&
           nas_frame_refs(device_pages[0])==2);
    /* Even an unusually read-only hardware leaf must upgrade in-place,
     * without treating another device owner reference as COW. */
    uint64_t same=0;
    assert(nas_mmu_exchange_leaf_at(device_mm->root_phys,X,
           device_pages[0],1,&same) && same==device_pages[0]);
    assert(nas_mm_handle_fault(device_mm,X,NAS_UP_WRITE,true)==
           NAS_MM_FAULT_RESOLVED);
    assert(phys(device_mm,X,&access)==device_pages[0] &&
           (access&NAS_UP_WRITE) && nas_frame_refs(device_pages[0])==2);
    assert(nas_mm_handle_fault(device_mm,A,NAS_UP_READ,false)==
           NAS_MM_FAULT_RESOLVED);
    assert(phys(device_mm,A,&access)==device_pages[3] &&
           access==NAS_UP_READ);
    struct nas_mm *device_child=nas_mm_fork_cow(device_mm);
    assert(device_child && device_pins==5 &&
           phys(device_child,X,&access)==device_pages[0] &&
           (access&NAS_UP_WRITE) && nas_frame_refs(device_pages[0])==3);
    assert(phys(device_child,A,&access)==device_pages[3] &&
           access==NAS_UP_READ);
    struct nas_task device_task={.mm=device_child,.state=NAS_TASK_STOPPED};
    nas_current_task=&device_task;
    different=0xb7;
    assert(nas_user_write_bytes(X+7,&different,1));
    assert(memory[device_pages[0]+7]==0xb7 &&
           phys(device_child,X,0)==phys(device_mm,X,0));
    different=0xc8;
    assert(nas_vm_write_task_bytes(&device_task,X+8,&different,1)==1 &&
           memory[device_pages[0]+8]==0xc8);
    assert(nas_vm_write_task_bytes(&device_task,A,&different,1)==-EFAULT &&
           memory[device_pages[3]]==0x64); /* shared, but logically RO */
    assert(nas_user_read_bytes(X+P,check,4) &&
           check[0]==0x62 && nas_frame_refs(device_pages[1])==2);
    assert(nas_mm_handle_fault(device_mm,X+P,NAS_UP_READ,false)==
           NAS_MM_FAULT_RESOLVED);
    assert(phys(device_child,X+P,0)==phys(device_mm,X+P,0) &&
           nas_frame_refs(device_pages[1])==3);
    unsigned pages_before=device_page_calls;
    unsigned refs_before=total_refs();
    assert(nas_mm_handle_fault(device_mm,X+2*P,NAS_UP_READ,false)==
           NAS_MM_FAULT_NEEDS_BACKING);
    assert(device_page_calls==pages_before+1 &&
           total_refs()==refs_before);
    get_calls=0;fail_get_on=1;
    assert(nas_mm_handle_fault(device_mm,X+3*P,NAS_UP_READ,false)==
           NAS_MM_FAULT_INVARIANT && total_refs()==refs_before);
    fail_get_on=0;
    fail_prepare=true;
    assert(nas_mm_handle_fault(device_mm,X+3*P,NAS_UP_READ,false)==
           NAS_MM_FAULT_OOM && total_refs()==refs_before);
    fail_prepare=false;fail_exchange=true;
    assert(nas_mm_handle_fault(device_mm,X+3*P,NAS_UP_READ,false)==
           NAS_MM_FAULT_INVARIANT && total_refs()==refs_before);
    fail_exchange=false;
    assert(nas_mm_handle_fault(device_mm,X+3*P,NAS_UP_READ,false)==
           NAS_MM_FAULT_RESOLVED &&
           nas_frame_refs(device_pages[3])==4);
    assert(mapped_dirty==dirty_before);
    nas_mm_put(device_child);
    nas_mm_put(device_mm);
    assert(device_pins==1 && nas_frame_refs(device_pages[0])==1 &&
           nas_frame_refs(device_pages[1])==1 &&
           nas_frame_refs(device_pages[3])==1);
    for(unsigned i=0;i<4;i++)if(device_pages[i]) {
        assert(nas_frame_put(device_pages[i]));device_pages[i]=0;
    }
    nas_mm_put(parent);
    assert(total_refs()==0);
    puts("[M101 COW PASS] rollback, no orphan PTE fork, shared fork, COW write, singleton promotion, RX ptrace isolation and lazy usercopy");
    puts("[A005 SHARED FORK PASS] same inode frame across fork, lazy alias, usercopy and ptrace write");
    puts("[M200 DEVICE SHARED PASS] PMM borrow/pin/ref rollback, fork aliases, usercopy/ptrace no-COW or file dirty");
}
