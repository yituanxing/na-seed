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
#include "../kernel/fs/io.h"
#include "../kernel/fs/lookup.h"
#include "../kernel/arch/abi.h"
#include "../kernel/task/user_stack.h"
/* The existing M86 runner links vm.c but not fault.c. Include the REAL
 * common fault implementation here so this host-only high-VA check runs
 * production demand-zero policy against the same mocked native PTEs. */
#include "../kernel/mm/fault.c"
#define P 4096ULL
#define NP 96U
#define MAP_FIXED_PRIVATE_ANON 0x32ULL
#define BASE 0x20010000ULL
static alignas(4096) uint8_t ram[NP*P];
static unsigned owners[NP],alloc_count,alloc_fail_at,prepare_count,prepare_fail_at;
static bool read_fail;
/* A model of one inode-scoped tmpfs page owner. The actual mapping and VMA
 * implementation under test are production vm.c/fault.c/anon_regions.c;
 * only FD/inode+frame provision is mocked to control OOM and EOF. */
struct nas_mapped_file {unsigned refs;bool writable;};
static struct nas_mapped_file shared_file={.refs=1,.writable=true};
static struct nas_mapped_file immutable_file={.refs=1,.writable=false};
static unsigned immutable_reads;
static uint64_t shared_pages[2],shared_size=2*P;
static bool shared_regrown;
static unsigned shared_dirty,shared_sync,shared_rearm,internal_sync;
int nas_fs_mmap_pin(uint64_t fd,bool writable,struct nas_mapped_file **out){
    if(!out)return -22;
    *out=0;
    if(fd!=5)return -9;
    if(writable && !shared_file.writable)return -13;
    shared_file.refs++;
    *out=&shared_file;
    return 0;
}
int nas_fs_mmap_pin_immutable(uint64_t fd,struct nas_mapped_file **out){
    if(!out)return -22;
    *out=0;
    if(fd!=4)return fd==5?-95:-9;
    immutable_file.refs++;
    *out=&immutable_file;
    return 0;
}
int64_t nas_fs_mmap_read_immutable(struct nas_mapped_file *file,
                                    uint64_t off,uint8_t *dst,uint64_t len){
    if(file!=&immutable_file || !file->refs || (!dst && len))return -9;
    immutable_reads++;
    for(uint64_t i=0;i<len;i++)dst[i]=(uint8_t)((off+i+7)&255);
    return (int64_t)len;
}
bool nas_fs_mmap_get(struct nas_mapped_file *file){
    if((file!=&shared_file && file!=&immutable_file) || !file->refs)return false;
    file->refs++;
    return true;
}
void nas_fs_mmap_put(struct nas_mapped_file *file){
    assert((file==&shared_file || file==&immutable_file) && file->refs>1);
    file->refs--;
}
bool nas_fs_mmap_writable(const struct nas_mapped_file *file){
    return file==&shared_file && file->writable;
}
int nas_fs_mmap_dirty(struct nas_mapped_file *file){
    if(!nas_fs_mmap_writable(file))return -13;
    shared_dirty++;
    return 0;
}
int nas_fs_mmap_sync(struct nas_mapped_file *file,bool rearm){
    if(file!=&shared_file)return -9;
    shared_sync++;
    if(rearm){
        struct nas_fs_node node={.ino=5};
        shared_rearm++;
        nas_vm_rearm_file(&node);
    }else internal_sync++;
    return 0;
}
bool nas_fs_mmap_matches(struct nas_mapped_file *file,
                         const struct nas_fs_node *node){
    return file==&shared_file && node && node->ino==5;
}
/* The real file-descriptor/VFS layer authorizes immutable file mappings.
 * fd=4 is a read-only bootfs file; fd=5 is a mutable tmpfs regular file.
 * A snapshot of fd=5 would NOT satisfy Linux MAP_SHARED coherence. */
int nas_fs_mmap_immutable_readonly(uint64_t fd){
    if(fd==4)return 1;
    if(fd==5)return 0;
    return -9;
}
struct mapping {uint64_t va,pa;unsigned prot;};
static struct mapping maps[96];
static struct nas_task current;
static struct nas_mm space;
bool nas_mm_walk_live(nas_mm_live_visit visitor,void *opaque){
    return visitor(&space,opaque);
}
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
bool nas_frame_get(uint64_t pa){
    if(!pa || (pa&(P-1)) || pa/P>=NP || !owners[pa/P] ||
       owners[pa/P]==UINT32_MAX)return false;
    owners[pa/P]++;
    return true;
}
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
bool nas_mm_resolve_user(const struct nas_mm *mm,uint64_t va,
                         uint64_t *pa,unsigned *access){
    return mm==&space && nas_mmu_resolve_user_address(mm->root_phys,va,pa,access);
}
bool nas_mm_break_cow(struct nas_mm *mm,uint64_t va,
                      unsigned prot,bool preserve_perm){
    (void)mm;(void)va;(void)prot;(void)preserve_perm;
    /* This fixture intentionally does not claim to exercise COW. */
    abort();
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
    if(fd!=4 && fd!=5)return -9;
    if(!dst && !n)return 0;
    if(read_fail && off>=P)return -5;
    if(!dst)return -14;
    for(uint64_t i=0;i<n;i++)dst[i]=(uint8_t)((off+i+7)&255);
    return (int64_t)n;
}
int nas_fs_mmap_page(struct nas_mapped_file *file,uint64_t off,
                     uint64_t *pa_owned){
    if(!pa_owned)return -22;
    *pa_owned=0;
    if(file!=&shared_file || (off&(P-1)))return -22;
    if(off>=shared_size)return -14;
    unsigned idx=(unsigned)(off/P);
    if(idx>=2)return -22;
    if(!shared_pages[idx]) {
        shared_pages[idx]=nas_frame_alloc();
        if(!shared_pages[idx])return -12;
        memset(ram+shared_pages[idx],idx && !shared_regrown?0x42:0x41,P);
        if(idx && shared_regrown)memset(ram+shared_pages[idx],0,P);
    }
    if(!nas_frame_get(shared_pages[idx]))return -12;
    *pa_owned=shared_pages[idx];
    return 0;
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
    /* A mixed resident + reserved range must change only VMA policy and
     * existing PTE permissions. A protection change must NOT fault in an
     * unused page, even if the physical allocator would reject a request. */
    assert(nas_mmu_map_prot_page(BASE,1));
    uint64_t retained=slot(BASE,false)->pa;
    assert(retained);
    ram[retained]=0x7b;
    alloc_count=0;alloc_fail_at=1;
    assert(nas_vm_mprotect(BASE,2*P,3)==0);
    assert(alloc_count==0);
    alloc_fail_at=0;
    assert(slot(BASE,false)->pa==retained &&
           slot(BASE,false)->prot==3 &&
           !nas_mmu_user_byte(BASE+P) &&
           nas_vma_find(&space.mmap_vmas,BASE)->prot==3 &&
           ram[retained]==0x7b && living()==1);
    /* OOM is deferred to the real first write fault, which leaves the
     * intended VMA permission and original resident frame intact. */
    alloc_count=0;alloc_fail_at=1;
    assert(nas_mm_handle_fault(&space,BASE+P+1,NAS_UP_WRITE,false)==
           NAS_MM_FAULT_OOM);
    alloc_fail_at=0;
    assert(living()==1 && !nas_mmu_user_byte(BASE+P));
    prepare_count=0;prepare_fail_at=1;
    assert(nas_vm_mprotect(BASE,2*P,7)==0);
    assert(prepare_count==0);
    prepare_fail_at=0;
    assert(slot(BASE,false)->pa==retained &&
           slot(BASE,false)->prot==7 &&
           !nas_mmu_user_byte(BASE+P) && living()==1);
    /* A fork-shared resident frame must not become hardware writable when
     * a private VMA is promoted to RWX: the eventual write must take the
     * existing COW fault path, and the same physical owner must survive. */
    owners[retained/P]++;
    assert(nas_vm_mprotect(BASE,2*P,7)==0);
    assert(slot(BASE,false)->pa==retained &&
           slot(BASE,false)->prot==5 &&
           nas_vma_find(&space.mmap_vmas,BASE)->prot==7 &&
           living()==2 && ram[retained]==0x7b);
    assert(nas_frame_put(retained));
    assert(nas_vm_mprotect(BASE,2*P,3)==0);
    assert(slot(BASE,false)->pa==retained &&
           slot(BASE,false)->prot==3 &&
           !nas_mmu_user_byte(BASE+P) &&
           nas_vma_find(&space.mmap_vmas,BASE)->prot==3 &&
           ram[retained]==0x7b && living()==1);
    assert(nas_mm_handle_fault(&space,BASE+P+1,NAS_UP_WRITE,false)==
           NAS_MM_FAULT_RESOLVED);
    assert(slot(BASE+P,false)->pa && living()==2);
    assert(nas_vm_munmap(BASE,2*P)==0);
    assert(living()==0 && !nas_vma_find(&space.mmap_vmas,BASE));
    uint64_t transient_file=nas_vm_file_mmap(0,P,1,2,4,0);
    assert(transient_file>0);
    assert(living()==1); /* one resident data frame; staging metadata freed */
    /* A hardware-only leaf must not regain write permission through the
     * old mprotect bypass when no corresponding VMA owns the address. */
    assert(nas_mmu_map_prot_page(BASE+4*P,1));
    uint64_t orphan=slot(BASE+4*P,false)->pa;
    assert((int64_t)nas_vm_mprotect(BASE+4*P,P,3)==-12);
    assert(slot(BASE+4*P,false)->pa==orphan &&
           slot(BASE+4*P,false)->prot==1);
    assert(nas_mmu_unmap_page(BASE+4*P));
    assert(nas_vm_munmap(transient_file,P)==0);
    /* M124 immutable CPIO can share snapshots. M111 mutable tmpfs MUST use
     * the same provider-owned frame under two independently mapped VMAs;
     * this host test executes real VM ledger, fault, MSYNC and teardown. */
    uint64_t immutable=nas_vm_file_mmap(0,P,1,1,4,0);
    assert((int64_t)immutable>0);
    assert(*nas_mmu_user_byte(immutable)==7 &&
           nas_vma_find(&space.mmap_vmas,immutable)->kind==
               NAS_VMA_FILE_IMMUTABLE_SHARED &&
           slot(immutable,false)->prot==1);
    const unsigned immutable_baseline=living();
    const uint64_t immutable_pa=slot(immutable,false)->pa;
    shared_file.writable=false;
    assert((int64_t)nas_vm_file_mmap(immutable,P,3,0x11,5,0)==-13);
    shared_file.writable=true;
    assert((int64_t)nas_vm_file_mmap(0,P,3,1,4,0)==-13);
    assert((int64_t)nas_vm_mprotect(immutable,P,3)==-13);
    assert(nas_vma_find(&space.mmap_vmas,immutable)->prot==1 &&
           slot(immutable,false)->prot==1 &&
           slot(immutable,false)->pa==immutable_pa &&
           *nas_mmu_user_byte(immutable)==7 &&
           living()==immutable_baseline);
    uint64_t live=nas_vm_file_mmap(0,2*P,3,1,5,0);
    assert((int64_t)live>0 && shared_file.refs==2);
    assert(!nas_mmu_user_byte(live)); /* live file pages remain lazy */
    assert(nas_vm_msync(live,P,0)==0 && shared_sync==1);
    assert(nas_mm_handle_fault(&space,live,NAS_UP_READ,false)==
           NAS_MM_FAULT_RESOLVED);
    assert(slot(live,false)->pa==shared_pages[0] &&
           slot(live,false)->prot==1 && nas_frame_refs(shared_pages[0])==2);
    assert(nas_mm_handle_fault(&space,live,NAS_UP_WRITE,true)==
           NAS_MM_FAULT_RESOLVED);
    assert(slot(live,false)->prot==3 && shared_dirty>0);
    uint64_t alias=nas_vm_file_mmap(0,P,3,1,5,0);
    assert((int64_t)alias>0 && alias!=live && shared_file.refs==3);
    assert(nas_mm_handle_fault(&space,alias,NAS_UP_WRITE,false)==
           NAS_MM_FAULT_RESOLVED);
    assert(slot(alias,false)->pa==slot(live,false)->pa &&
           nas_frame_refs(shared_pages[0])==3);
    *nas_mmu_user_byte(live)=0x91;
    assert(*nas_mmu_user_byte(alias)==0x91);
    *nas_mmu_user_byte(alias)=0x73;
    assert(*nas_mmu_user_byte(live)==0x73);
    assert(nas_vm_msync(live,P,1)==0);
    /* User msync starts a new dirty epoch for ALL resident inode aliases:
     * the next write must trap and account for a fresh mtime update. */
    assert(shared_rearm==2 && slot(live,false)->prot==1 &&
           slot(alias,false)->prot==1);
    unsigned dirty_after_sync=shared_dirty;
    assert(nas_mm_handle_fault(&space,alias,NAS_UP_WRITE,true)==
           NAS_MM_FAULT_RESOLVED && slot(alias,false)->prot==3 &&
           shared_dirty==dirty_after_sync+1);
    *nas_mmu_user_byte(alias)=0x73;
    assert(nas_vm_msync(live,P,4)==0);
    assert((int64_t)nas_vm_msync(live,P,5)==-22);
    assert((int64_t)nas_vm_msync(live+1,P,1)==-22);
    assert((int64_t)nas_vm_msync(live,4*P,1)==-12);
    assert((int64_t)nas_vm_msync(live,P,2)==-95);
    /* ftruncate operates on the VFS inode, not the open FD or the current
     * Task alone. A partial tail must zero in all aliases; fully discarded
     * pages lose their PTE, then fault SIGBUS until the inode grows again. */
    assert(nas_mm_handle_fault(&space,live+P,NAS_UP_READ,false)==
           NAS_MM_FAULT_RESOLVED);
    uint64_t former_second=slot(live+P,false)->pa;
    assert(former_second==shared_pages[1]);
    ram[former_second+17]=0x9d;
    struct nas_fs_node resized={.ino=5};
    shared_size=P+1;
    nas_vm_invalidate_file(&resized,P+1);
    assert(slot(live+P,false)->pa==former_second &&
           ram[former_second]==0x42 && ram[former_second+17]==0);
    shared_size=P;
    assert(nas_frame_put(shared_pages[1]));shared_pages[1]=0;
    nas_vm_invalidate_file(&resized,P);
    assert(!nas_mmu_user_byte(live+P));
    assert(nas_mm_handle_fault(&space,live+P,NAS_UP_READ,false)==
           NAS_MM_FAULT_NEEDS_BACKING);
    shared_size=2*P;
    shared_regrown=true;
    assert(nas_mm_handle_fault(&space,live+P,NAS_UP_READ,false)==
           NAS_MM_FAULT_RESOLVED);
    assert(*nas_mmu_user_byte(live+P)==0 &&
           *(nas_mmu_user_byte(live+P)+17)==0);
    assert(nas_vm_mprotect(alias,P,1)==0 && slot(alias,false)->prot==1);
    assert(nas_vm_mprotect(alias,P,3)==0 && slot(alias,false)->prot==1);
    assert(nas_mm_handle_fault(&space,alias,NAS_UP_WRITE,true)==
           NAS_MM_FAULT_RESOLVED && slot(alias,false)->prot==3);
    uint64_t shared_before_guard=slot(alias,false)->pa;
    assert(nas_vm_mprotect(alias,P,0)==0 && !nas_mmu_user_byte(alias));
    assert(nas_vma_find(&space.mmap_vmas,alias)->prot==0);
    assert(nas_vm_mprotect(alias,P,3)==0);
    assert(nas_mm_handle_fault(&space,alias,NAS_UP_WRITE,false)==
           NAS_MM_FAULT_RESOLVED);
    assert(slot(alias,false)->pa==shared_before_guard &&
           *nas_mmu_user_byte(alias)==0x73);
    unsigned before_internal=internal_sync,before_rearm=shared_rearm;
    assert(nas_vm_munmap(alias,P)==0 && shared_file.refs==2);
    assert(internal_sync>before_internal && shared_rearm==before_rearm);
    assert(nas_vm_munmap(live,2*P)==0 && shared_file.refs==1);
    assert(nas_frame_refs(shared_pages[0])==1); /* cache owns final ref */
    assert(nas_frame_put(shared_pages[0]));shared_pages[0]=0;
    assert(nas_frame_put(shared_pages[1]));shared_pages[1]=0;
    uint64_t split=nas_vm_file_mmap(0,3*P,3,1,5,0);
    assert((int64_t)split>0 && shared_file.refs==2);
    assert(nas_vm_munmap(split+P,P)==0 && shared_file.refs==3);
    const struct nas_anon_region *split_right=
        nas_vma_find(&space.mmap_vmas,split+2*P);
    assert(split_right && split_right->file_offset==2*P &&
           split_right->backing==&shared_file);
    assert((int64_t)nas_vm_msync(split,3*P,1)==-12);
    assert(nas_vm_munmap(split,P)==0 && shared_file.refs==2);
    assert(nas_vm_munmap(split+2*P,P)==0 && shared_file.refs==1);
    assert(living()==immutable_baseline);
    assert(nas_vm_munmap(immutable,P)==0);
    /* M138: a >64-MiB CPIO MAP_PRIVATE is an immutable, VMA-owned lazy
     * snapshot. It must pin the original file but allocate zero data frames
     * until touched. Splits/fork keep independently owned backing refs. */
    const uint64_t lib_bytes=(128ULL<<20)+P;
    unsigned before_lib=living();
    unsigned before_reads=immutable_reads;
    uint64_t lib=nas_vm_file_mmap(0,lib_bytes,3,2,4,0);
    assert((int64_t)lib>0);
    const struct nas_anon_region *lib_vma=nas_vma_find(&space.mmap_vmas,lib);
    assert(lib_vma && lib_vma->kind==NAS_VMA_FILE_PRIVATE_LAZY &&
           lib_vma->backing==&immutable_file && immutable_file.refs==2 &&
           living()==before_lib && !nas_mmu_user_byte(lib+2*P));
    assert(nas_mm_handle_fault(&space,lib+2*P,NAS_UP_READ,false)==
           NAS_MM_FAULT_RESOLVED);
    assert(immutable_reads==before_reads+1 && living()==before_lib+1 &&
           *nas_mmu_user_byte(lib+2*P)==7);
    *nas_mmu_user_byte(lib+2*P)=0x8f;
    /* MADV_DONTNEED of a private file page discards the private edit, then
     * reads original immutable bytes again, never demand-zero bytes. */
    assert(nas_vm_madvise(lib+2*P,P,4)==0);
    assert(!nas_mmu_user_byte(lib+2*P) && living()==before_lib);
    assert(nas_mm_handle_fault(&space,lib+2*P,NAS_UP_READ,false)==
           NAS_MM_FAULT_RESOLVED);
    assert(immutable_reads==before_reads+2 &&
           *nas_mmu_user_byte(lib+2*P)==7);
    *nas_mmu_user_byte(lib+2*P)=0x8f;
    assert(nas_vm_mprotect(lib,P,0)==0);
    assert(nas_mm_handle_fault(&space,lib,NAS_UP_READ,false)==
           NAS_MM_FAULT_ACCERR);
    assert((int64_t)nas_vm_mprotect(lib+2*P,P,0)==-38);
    assert(*nas_mmu_user_byte(lib+2*P)==0x8f);
    assert(nas_vm_mprotect(lib,P,3)==0);
    assert(nas_mm_handle_fault(&space,lib,NAS_UP_READ,false)==
           NAS_MM_FAULT_RESOLVED);
    assert(*nas_mmu_user_byte(lib)==7 && living()==before_lib+2);
    struct nas_anon_regions forked_lib={0};
    unsigned before_fork_refs=immutable_file.refs;
    assert(nas_vma_clone(&forked_lib,&space.mmap_vmas));
    assert(immutable_file.refs>before_fork_refs);
    nas_anon_reset(&forked_lib);
    assert(immutable_file.refs==before_fork_refs);
    assert(nas_vm_munmap(lib+P,P)==0);
    const struct nas_anon_region *lib_right=
        nas_vma_find(&space.mmap_vmas,lib+2*P);
    assert(lib_right && lib_right->file_offset==2*P &&
           lib_right->backing==&immutable_file);
    assert(nas_vm_munmap(lib,lib_bytes)==0 && immutable_file.refs==1 &&
           living()==before_lib);
    assert((int64_t)nas_vm_file_mmap(0,lib_bytes,3,2,5,0)==-95);
    puts("[M138 IMMUTABLE PRIVATE LAZY HOST PASS] 128MiB reservation, page faults, MADV_DONTNEED re-read, COW ownership, split/fork and cleanup");
    /* XZ's original Linux encoder can reserve 64 MiB plus one extra page
     * in a single anonymous mmap. The VMA window, not an arbitrary 64-MiB
     * per-call limit, is the authority for anonymous PROT_NONE space. */
    const uint64_t large_request=(64ULL<<20)+P;
    uint64_t large=nas_vm_anon_mmap(0,large_request,0,0x22,UINT64_MAX,0);
    assert((int64_t)large>0);
    const struct nas_anon_region *large_vma=nas_vma_find(&space.mmap_vmas,large);
    assert(large_vma && large_vma->start==large &&
           large_vma->end-large==large_request &&
           large_vma->prot==0 && large_vma->kind==NAS_VMA_ANON_PRIVATE);
    assert(nas_vm_munmap(large,large_request)==0);
    assert(!nas_vma_find(&space.mmap_vmas,large));
    /* Low-window pressure spills into the disjoint high window, but a
     * request larger than the whole high window still returns real ENOMEM. */
    uint64_t low_width=NAS_ANON_CEILING-NAS_ANON_FLOOR;
    assert(nas_vm_anon_mmap(0,low_width,0,0x22,UINT64_MAX,0)==NAS_ANON_FLOOR);
    uint64_t high=nas_vm_anon_mmap(0,3*P,3,0x22,UINT64_MAX,0);
    assert(high>=NAS_ANON_EXTENDED_FLOOR &&
           high<=NAS_ANON_EXTENDED_CEILING-3*P);
    assert(nas_vma_find(&space.mmap_vmas,high) &&
           !nas_mmu_user_byte(high) && !nas_mmu_user_byte(high+P));
    unsigned high_baseline=living();
    /* Execute the genuine common demand-zero fault resolver at >2GiB,
     * including physical ownership and writable byte accessibility. */
    assert(nas_mm_handle_fault(&space,high+31,NAS_UP_WRITE,false)==
           NAS_MM_FAULT_RESOLVED);
    assert(living()==high_baseline+1 && nas_mmu_user_byte(high+31));
    assert(*nas_mmu_user_byte(high+31)==0);
    *nas_mmu_user_byte(high+31)=0x83;
    alloc_count=0;alloc_fail_at=1;
    assert(nas_mm_handle_fault(&space,high+2*P,NAS_UP_WRITE,false)==
           NAS_MM_FAULT_OOM);
    alloc_fail_at=0;
    assert(!nas_mmu_user_byte(high+2*P) && living()==high_baseline+1);
    /* An untouched high guard becomes PROT_NONE without backing; an
     * already-dirty page must not lose its original physical data. */
    assert(nas_vm_mprotect(high+P,P,0)==0);
    assert(nas_mm_handle_fault(&space,high+P,NAS_UP_WRITE,false)==
           NAS_MM_FAULT_ACCERR);
    assert((int64_t)nas_vm_mprotect(high,P,0)==-38);
    assert(*nas_mmu_user_byte(high+31)==0x83);
    alloc_count=0;alloc_fail_at=1;
    assert(nas_vm_mprotect(high+P,P,3)==0);
    assert(alloc_count==0);
    alloc_fail_at=0;
    assert(nas_vma_find(&space.mmap_vmas,high+P)->prot==3 &&
           !nas_mmu_user_byte(high+P));
    alloc_count=0;alloc_fail_at=1;
    assert(nas_mm_handle_fault(&space,high+P,NAS_UP_WRITE,false)==
           NAS_MM_FAULT_OOM);
    alloc_fail_at=0;
    assert(nas_mm_handle_fault(&space,high+P,NAS_UP_WRITE,false)==
           NAS_MM_FAULT_RESOLVED);
    assert(nas_mmu_user_byte(high+P) && *nas_mmu_user_byte(high+P)==0);
    *nas_mmu_user_byte(high+P)=0x49;
    assert(nas_vm_munmap(high+P,P)==0);
    assert(!nas_vma_find(&space.mmap_vmas,high+P) &&
           nas_mm_handle_fault(&space,high+P,NAS_UP_READ,false)==
           NAS_MM_FAULT_MAPERR && *nas_mmu_user_byte(high+31)==0x83);
    assert(nas_vm_munmap(high,3*P)==0);
    assert(!nas_vma_find(&space.mmap_vmas,high) && living()==high_baseline);
    assert(nas_vm_munmap(NAS_ANON_FLOOR,low_width)==0);
    /* Real V8/Node CodeRange: reserving and mprotecting 512 MiB RWX does
     * not eagerly consume 131072 physical pages, and both first and last
     * pages fault in with the declared Linux permissions. */
    const uint64_t code_bytes=512ULL<<20;
    const uint64_t code=nas_vm_anon_mmap(0,code_bytes,0,0x22,UINT64_MAX,0);
    assert((int64_t)code>0);
    unsigned before_code=living();
    alloc_count=0;alloc_fail_at=1;
    assert(nas_vm_mprotect(code,code_bytes,7)==0);
    assert(alloc_count==0 && living()==before_code &&
           !nas_mmu_user_byte(code) &&
           !nas_mmu_user_byte(code+code_bytes-P));
    alloc_fail_at=0;
    assert(nas_mm_handle_fault(&space,code,NAS_UP_EXEC,false)==
           NAS_MM_FAULT_RESOLVED);
    assert(nas_mm_handle_fault(&space,code+code_bytes-P,NAS_UP_WRITE,false)==
           NAS_MM_FAULT_RESOLVED);
    assert(slot(code,false)->prot==7 &&
           slot(code+code_bytes-P,false)->prot==7 &&
           living()==before_code+2);
    /* Linux MADV_DONTFORK is VMA policy, not a 'success' with no effect.
     * Partial interval changes must preserve adjacent pages/permissions. */
    assert(nas_vm_madvise(code+P,P,10)==0);
    assert(!(nas_vma_find(&space.mmap_vmas,code)->flags&NAS_VMA_DONTFORK));
    assert(nas_vma_find(&space.mmap_vmas,code+P)->flags&NAS_VMA_DONTFORK);
    assert(!(nas_vma_find(&space.mmap_vmas,code+2*P)->flags&NAS_VMA_DONTFORK));
    assert(nas_vm_madvise(code+P,P,11)==0);
    assert(!(nas_vma_find(&space.mmap_vmas,code+P)->flags&NAS_VMA_DONTFORK));
    /* V8's real 512-MiB MADV_DONTNEED must release only resident frames;
     * preserve the virtual RWX reservation, then refault a fresh zero page. */
    assert(nas_vm_madvise(code,code_bytes,4)==0);
    assert(living()==before_code && !nas_mmu_user_byte(code) &&
           !nas_mmu_user_byte(code+code_bytes-P));
    assert(nas_vma_find(&space.mmap_vmas,code)->prot==7);
    assert(nas_mm_handle_fault(&space,code,NAS_UP_WRITE,false)==
           NAS_MM_FAULT_RESOLVED);
    assert(*nas_mmu_user_byte(code)==0 && living()==before_code+1);
    assert((int64_t)nas_vm_madvise(code+1,P,4)==-22);
    assert((int64_t)nas_vm_madvise(code,P,99)==-22);
    assert((int64_t)nas_vm_madvise(code+code_bytes,P,4)==-12);
    assert(nas_vm_munmap(code,code_bytes)==0);
    assert(living()==before_code);
    /* A private file snapshot is eager and lacks a durable backing ref.
     * MADV_DONTNEED must report unsupported, not silently lose data. */
    uint64_t snap=nas_vm_file_mmap(0,P,1,2,4,0);
    assert((int64_t)snap>0);
    uint64_t snap_pa=slot(snap,false)->pa;
    assert(snap_pa && *nas_mmu_user_byte(snap)==7);
    assert((int64_t)nas_vm_madvise(snap,P,4)==-95);
    assert(slot(snap,false)->pa==snap_pa && *nas_mmu_user_byte(snap)==7);
    assert(nas_vm_munmap(snap,P)==0);
    /* Simulate an external COW owner: discard the calling MM's PTE and
     * decrement only its physical ownership, preserving sibling bytes. */
    uint64_t shared=nas_vm_anon_mmap(0,P,3,0x22,UINT64_MAX,0);
    assert((int64_t)shared>0);
    assert(nas_mm_handle_fault(&space,shared,NAS_UP_WRITE,false)==
           NAS_MM_FAULT_RESOLVED);
    uint64_t shared_pa=slot(shared,false)->pa;
    *nas_mmu_user_byte(shared)=0x5b;
    owners[shared_pa/P]++;
    assert(nas_vm_madvise(shared,P,4)==0);
    assert(!nas_mmu_user_byte(shared) && nas_frame_refs(shared_pa)==1 &&
           ram[shared_pa]==0x5b);
    assert(nas_frame_put(shared_pa));
    assert(nas_mm_handle_fault(&space,shared,NAS_UP_READ,false)==
           NAS_MM_FAULT_RESOLVED);
    assert(*nas_mmu_user_byte(shared)==0);
    assert(nas_vm_munmap(shared,P)==0);
    assert(nas_vm_anon_mmap(NAS_ANON_EXTENDED_FLOOR+P,P,0,
                            0x32,UINT64_MAX,0)==NAS_ANON_EXTENDED_FLOOR+P);
    assert(nas_vm_munmap(NAS_ANON_EXTENDED_FLOOR+P,P)==0);
    assert((int64_t)nas_vm_anon_mmap(0,
          NAS_ANON_EXTENDED_CEILING-NAS_ANON_EXTENDED_FLOOR+P,
          0,0x22,UINT64_MAX,0)==-12);
    assert((int64_t)nas_vm_anon_mmap(0x6ff00000,P,
                                    0,0x32,UINT64_MAX,0)==-22);
    /* M127 LAME: original encoder faulted at 0x6feffd58, 680 bytes below
     * the old 1-MiB eager stack. Reserve 8 MiB in VMA only; confirm the
     * genuine common fault resolver lazily populates the lower stack,
     * rejects the guard below its hard limit, and cannot write the RX
     * signal trampoline living outside the stack reservation. */
    const uint64_t stack_bottom=NAS_USER_STACK_TOP-
                                NAS_USER_STACK_RESERVE_PAGES*P;
    const uint64_t eager_bottom=NAS_USER_STACK_TOP-NAS_USER_STACK_PAGES*P;
    const uint64_t lame_fault=eager_bottom-0x2a8ULL;
    assert(NAS_USER_STACK_RESERVE_PAGES>NAS_USER_STACK_PAGES);
    assert(eager_bottom>stack_bottom+P);
    assert(NAS_USER_SIGNAL_TRAMPOLINE+P<stack_bottom);
    assert(nas_vma_record(&space.mmap_vmas,stack_bottom,
                          NAS_USER_STACK_RESERVE_PAGES*P,3,NAS_VMA_STACK,0));
    assert(nas_vma_record(&space.mmap_vmas,NAS_USER_SIGNAL_TRAMPOLINE,
                          P,5,NAS_VMA_SIGNAL_TRAMPOLINE,0));
    assert(!nas_mmu_user_byte(lame_fault));
    unsigned before_stack=living();
    assert(nas_mm_handle_fault(&space,lame_fault,NAS_UP_READ,false)==
           NAS_MM_FAULT_RESOLVED);
    assert(nas_mmu_user_byte(lame_fault) && *nas_mmu_user_byte(lame_fault)==0 &&
           living()==before_stack+1);
    *nas_mmu_user_byte(lame_fault)=0x7a;
    assert(nas_mm_handle_fault(&space,stack_bottom,NAS_UP_WRITE,false)==
           NAS_MM_FAULT_RESOLVED);
    assert(nas_mmu_user_byte(stack_bottom) &&
           *nas_mmu_user_byte(stack_bottom)==0 && living()==before_stack+2);
    assert(nas_mm_handle_fault(&space,stack_bottom-1,NAS_UP_READ,false)==
           NAS_MM_FAULT_MAPERR);
    assert(nas_mm_handle_fault(&space,NAS_USER_SIGNAL_TRAMPOLINE,
                                NAS_UP_WRITE,false)==NAS_MM_FAULT_ACCERR);
    assert(!nas_vma_find(&space.mmap_vmas,stack_bottom-1) &&
           nas_vma_find(&space.mmap_vmas,lame_fault)->kind==NAS_VMA_STACK &&
           nas_vma_find(&space.mmap_vmas,NAS_USER_SIGNAL_TRAMPOLINE)->kind==
               NAS_VMA_SIGNAL_TRAMPOLINE);
    assert(nas_mmu_unmap_page(lame_fault&~(P-1)));
    assert(nas_mmu_unmap_page(stack_bottom));
    assert(living()==before_stack);
    puts("[M80 LARGE ANON MMAP HOST PASS] 64MiB+4KiB, high 64-bit VMA, honest total-window ENOMEM");
    puts("[M124 IMMUTABLE MAP_SHARED HOST PASS] bootfs RO snapshot and write escalation rejected");
    puts("[A005 VM SHARED ALIAS HOST PASS] alias, VMA refs/split, truncate SIGBUS/regrow, write fault, mprotect and msync");
    puts("[M110 NODE VM PASS] 512MiB RWX reservation, lazy mprotect, demand-faulted executable pages");
    puts("[M110 NODE MADVISE HOST PASS] 512MiB DONTNEED refault zero, DONTFORK VMA policy, snapshot honesty");
    puts("[M87 MM TXN PASS] MAP_FIXED rollback and mprotect lazy protection preserve VMA/PTE and frame ownership");
    puts("[M127 STACK LAZY HOST PASS] 8MiB reservation; LAME boundary and lower-limit guards; signal trampoline isolation");
    return 0;
}
