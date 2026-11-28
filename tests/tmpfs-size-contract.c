/* Production tmpfs data/size contract, using a refcounted fake PMM.
 * Real tmpfs.c handles indexing, sparse I/O, growth, shared-page pinning,
 * EOF tail zeroing, shrink and release. No fake filesystem logic. */
#include "../kernel/tmpfs.h"
#include "../kernel/mm/frame.h"
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

/* Hosted architecture and VFS ownership boundaries. */
void nas_arch_halt(void) { abort(); }
static const void *leased_inode;
bool nas_vfs_inode_is_pinned(const void *inode) {
    return leased_inode && inode==leased_inode;
}

#define P 4096U
#define NFRAMES 64U
static _Alignas(P) uint8_t frames[NFRAMES][P];
static uint32_t refs[NFRAMES];
static uint64_t timestamp;
static unsigned slot_for(uint64_t pa) {
    assert(pa>=P && (pa%P)==0 && pa/P<=NFRAMES);
    return (unsigned)(pa/P)-1;
}
uint64_t nas_hhdm_offset(void) {
    return (uint64_t)(uintptr_t)&frames[0][0]-P;
}
uint64_t nas_frame_alloc(void) {
    for(unsigned i=0;i<NFRAMES;i++)
        if(!refs[i]) {
            refs[i]=1;
            memset(frames[i],0xf7,P);
            return (uint64_t)(i+1)*P;
        }
    return 0;
}
bool nas_frame_get(uint64_t pa) {
    unsigned i=slot_for(pa);
    if(!refs[i] || refs[i]==UINT32_MAX)return false;
    ++refs[i];
    return true;
}
bool nas_frame_put(uint64_t pa) {
    unsigned i=slot_for(pa);
    if(!refs[i])return false;
    --refs[i];
    return true;
}
uint32_t nas_frame_refs(uint64_t pa) { return refs[slot_for(pa)]; }
static unsigned frames_used(void) {
    unsigned count=0;
    for(unsigned i=0;i<NFRAMES;i++)count+=refs[i]!=0;
    return count;
}
bool nas_time_realtime_ns(uint64_t *now) {
    *now=++timestamp;
    return true;
}
uint64_t nas_arch_monotonic_ns(void) { return timestamp; }
static uint8_t *bytes(uint64_t pa) { return frames[slot_for(pa)]; }
static struct nas_tmp_file fresh(void) {
    return (struct nas_tmp_file){.used=true,.mode=0100644,.ino=1};
}
static void sparse_growth_and_held_shared_page(void) {
    struct nas_tmp_file f=fresh();
    assert(tmp_resize(&f,100)==0 && f.size==100 && !f.index_root);
    uint64_t pa=0;
    assert(tmp_acquire_shared_page(&f,0,&pa)==0 && pa &&
           nas_frame_refs(pa)==2);
    uint8_t *page=bytes(pa);
    page[99]=0x41;
    page[110]=0xae; /* a MAP_SHARED store beyond EOF is not file data */
    assert(tmp_resize(&f,120)==0 && f.size==120);
    assert(page[99]==0x41 && page[100]==0 && page[110]==0 &&
           page[119]==0);
    page[140]=0xbf;
    const uint8_t value=0x62;
    assert(tmp_write_at(&f,150,&value,1)==1 && f.size==151);
    assert(page[139]==0 && page[140]==0 && page[149]==0 &&
           page[150]==value);
    uint8_t result[52];
    memset(result,0xff,sizeof result);
    assert(tmp_read_at(&f,99,result,sizeof result)==52);
    assert(result[0]==0x41 && result[41]==0 && result[51]==value);
    uint64_t old_time=f.mtime_ns;
    assert(tmp_resize(&f,151)==0 && f.mtime_ns>old_time);
    assert(nas_frame_refs(pa)==2 && page[150]==value);
    assert(tmp_resize(&f,90)==0 && page[89]==0 && page[99]==0 &&
           page[150]==0);
    assert(tmp_resize(&f,151)==0 && page[150]==0);
    assert(tmp_resize(&f,0)==0 && f.index_root==0 &&
           nas_frame_refs(pa)==1);
    assert(nas_frame_put(pa) && nas_frame_refs(pa)==0);
}
static void sparse_extent_and_reservation(void) {
    struct nas_tmp_file f=fresh();
    assert(tmp_resize(&f,4*P+13)==0 && f.size==4*P+13 &&
           !f.index_root);
    uint8_t buf[16];
    memset(buf,0xff,sizeof buf);
    assert(tmp_read_at(&f,4*P,buf,13)==13);
    for(unsigned i=0;i<13;i++)assert(buf[i]==0);
    assert(!f.index_root && tmp_page_resident(&f,0)==0);
    assert(tmp_preallocate(&f,6*P,P,1)==0);
    assert(f.size==4*P+13 && tmp_page_resident(&f,6*P)==0);
    /* KEEP_SIZE may allocate pages beyond i_size, but never expose them. */
    assert(f.index_root);
    assert(tmp_preallocate(&f,4*P,2*P+13,0)==0 &&
           f.size==6*P+13);
    assert(tmp_page_resident(&f,6*P)==1);
    memset(buf,0xff,sizeof buf);
    assert(tmp_read_at(&f,6*P,buf,13)==13);
    for(unsigned i=0;i<13;i++)assert(buf[i]==0);
    assert(tmp_resize(&f,0)==0 && f.index_root==0);
}
static void quota_cannot_publish_missing_pages(void) {
    struct nas_tmp_quota q={.max_pages=1,.max_inodes=4};
    struct nas_tmp_file f=fresh();
    f.quota=&q;
    assert(tmp_resize(&f,2*P)==0);
    assert(tmp_preallocate(&f,0,2*P,1)==-ENOSPC && q.used_pages==1);
    assert(f.size==2*P);
    /* A new indirect region must not consume metadata pages while full.
     * The same existing data page must remain mappable under full quota. */
    assert(tmp_resize(&f,(TMP_PAGE_ENTRIES+1)*P)==0);
    unsigned before=frames_used();
    uint64_t shared=0;
    assert(tmp_acquire_shared_page(&f,TMP_PAGE_ENTRIES*P,&shared)==-ENOSPC);
    assert(!shared && frames_used()==before);
    assert(tmp_preallocate(&f,TMP_PAGE_ENTRIES*P,P,1)==-ENOSPC);
    assert(frames_used()==before);
    assert(tmp_acquire_shared_page(&f,0,&shared)==0 && shared);
    assert(nas_frame_refs(shared)==2 && nas_frame_put(shared));
    assert(tmp_resize(&f,0)==0 && q.used_pages==0 && frames_used()==0);

    struct nas_tmp_quota zero={.max_pages=0,.max_inodes=4};
    struct nas_tmp_file empty=fresh();
    empty.quota=&zero;
    assert(tmp_resize(&empty,P)==0);
    assert(tmp_acquire_shared_page(&empty,0,&shared)==-ENOSPC);
    assert(!empty.index_root && frames_used()==0);
}
static void shrink_releases_keep_size_reservations(void) {
    struct nas_tmp_quota q={.max_pages=8,.max_inodes=8};
    struct nas_tmp_file f=fresh();
    f.quota=&q;
    /* One live low page, then KEEP_SIZE pages at two distant offsets:
     * allocations must not change i_size but must count against quota. */
    assert(tmp_resize(&f,P)==0);
    const uint8_t b=0x6d;
    assert(tmp_write_at(&f,17,&b,1)==1);
    assert(tmp_preallocate(&f,5*P,P,1)==0);
    assert(tmp_preallocate(&f,(TMP_PAGE_ENTRIES+3)*P,P,1)==0);
    assert(f.size==P && q.used_pages==3 && frames_used()==6);
    assert(f.highest_allocated_data_page_plus_one==TMP_PAGE_ENTRIES+4);
    /* Shrink below EOF: both out-of-EOF reserved pages must go, including
     * the indirect index at 512+, but the retained low page stays alive. */
    assert(tmp_resize(&f,P/2)==0);
    assert(f.size==P/2 && q.used_pages==1 && frames_used()==3);
    assert(f.highest_allocated_data_page_plus_one==1);
    uint8_t readback=0;
    assert(tmp_read_at(&f,17,&readback,1)==1 && readback==b);
    assert(tmp_resize(&f,0)==0 && q.used_pages==0 &&
           frames_used()==0 && f.index_root==0);
}
/* Keep the REAL tmpfs orphan reclaimer while faking only the VFS lease
 * predicate. Open File/VMA owners preserve the inode after final unlink;
 * inode data dies only on final unpin, and an independent PTE frame ref
 * survives the inode's own storage put until its mapping is destroyed. */
static void orphaned_inode_lifetime(void) {
    assert(frames_used()==0);
    struct nas_tmp_quota q={.max_pages=3,.max_inodes=2};
    struct nas_tmp_file *f=tmp_inode_alloc_quota(false,0644,&q);
    assert(f && f->used && q.used_inodes==1);
    tmp_inode_link(f);
    tmp_inode_link(f); /* two names, then both unlinked */
    uint8_t payload=0x77;
    assert(tmp_write_at(f,10,&payload,1)==1);
    uint64_t pa=0;
    assert(tmp_acquire_shared_page(f,0,&pa)==0 && pa &&
           nas_frame_refs(pa)==2);
    leased_inode=f;
    tmp_inode_unlink(f);
    assert(f->nlink==1 && f->used);
    tmp_inode_unlink(f);
    assert(!f->nlink && f->used && q.used_pages==1);
    tmp_inode_discard(f); /* even an explicit orphan probe cannot free it */
    uint8_t got=0;
    assert(tmp_read_at(f,10,&got,1)==1 && got==payload);
    assert(nas_frame_refs(pa)==2 && q.used_inodes==1);
    leased_inode=NULL;
    tmp_inode_discard(f); /* models VFS final inode lease release */
    assert(!f->used && q.used_pages==0 && q.used_inodes==0);
    assert(nas_frame_refs(pa)==1); /* independent shared PTE ownership */
    assert(nas_frame_put(pa) && nas_frame_refs(pa)==0);
    /* One reusable inode-slab metadata frame remains intentionally cached. */
    assert(frames_used()==1);
}
int main(void) {
    shrink_releases_keep_size_reservations();
    sparse_growth_and_held_shared_page();
    sparse_extent_and_reservation();
    quota_cannot_publish_missing_pages();
    orphaned_inode_lifetime();
    puts("PASS: real tmpfs EOF tail, sparse growth, quota and orphan inode/PTE ownership");
    return 0;
}
