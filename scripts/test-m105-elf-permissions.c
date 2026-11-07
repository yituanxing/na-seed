/* M105: actual common ELF engine, host physical-page/perms fixture.
 * Covers both PT_LOAD orders, BSS overwrites, read_at-only sources, R-only
 * mapping, malformed W+X rollback and large-ELF bounded VMA registration. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../kernel/task/elf_segments.h"
#include "../kernel/mm/anon_regions.h"
#include "../kernel/mm/file_backing.h"
/* PT_LOAD only emits private snapshots; shared-backend refs are forbidden. */
bool nas_fs_mmap_get(struct nas_mapped_file *f) {(void)f;assert(0);return false;}
void nas_fs_mmap_put(struct nas_mapped_file *f) {(void)f;assert(0);}
#define P 4096ULL
#define BASE 0x10000ULL
#define IMAGE_SIZE 0x6000U
#define MAX_PAGES 384U
static uint8_t bytes[MAX_PAGES*P],perm[MAX_PAGES],seen[MAX_PAGES];
static unsigned mapped, regions;
static struct nas_anon_regions ledger;
struct interval {uint64_t first,last;unsigned flags;};
static struct interval spans[16];
static uint8_t image[IMAGE_SIZE];
static void clear_target(void) {
    memset(bytes,0,sizeof(bytes));memset(perm,0,sizeof(perm));
    memset(seen,0,sizeof(seen));memset(spans,0,sizeof(spans));
    mapped=regions=0;
    nas_anon_reset(&ledger);
}
static bool mapped_page(void *ctx,uint64_t va,unsigned flags) {
    (void)ctx;
    if (va<BASE || va>=BASE+MAX_PAGES*P || va%P)return false;
    unsigned i=(unsigned)((va-BASE)/P);
    if(seen[i]++)return false;
    perm[i]=(uint8_t)flags;mapped++;
    return true;
}
static bool copied(void *ctx,uint64_t va,const uint8_t *src,uint64_t n) {
    (void)ctx;
    if(va<BASE || n>BASE+MAX_PAGES*P-va)return false;
    /* A real HHDM write only reaches user pages with present PTEs. */
    for(uint64_t at=va;at<va+n;) {
        if(!seen[(at-BASE)/P])return false;
        uint64_t room=P-((at-BASE)%P);
        if(room>va+n-at)room=va+n-at;
        memcpy(bytes+at-BASE,src+(at-va),(size_t)room);
        at+=room;
    }
    return true;
}
static bool recorded(void *ctx,uint64_t start,uint64_t length,
                     unsigned flags,uint64_t off) {
    (void)ctx;(void)off;
    if(!length || start%P || length%P || regions>=16)return false;
    unsigned prot=1U|((flags&2U)?2U:0U)|((flags&1U)?4U:0U);
    if(!nas_vma_replace(&ledger,start,length,prot,
                        NAS_VMA_ELF_PRIVATE_SNAPSHOT,off))return false;
    spans[regions++]=(struct interval){start,start+length,flags};
    return true;
}
static bool image_read(const void *context,uint64_t offset,
                       uint8_t *destination,uint64_t count) {
    const uint8_t *src=context;
    if(offset>IMAGE_SIZE || count>IMAGE_SIZE-offset)return false;
    memcpy(destination,src+offset,(size_t)count);
    return true;
}
static struct elf_hdr *header(void) {
    memset(image,0,sizeof(image));
    struct elf_hdr *h=(struct elf_hdr *)(void *)image;
    memcpy(h->ident,"\177ELF",4);
    h->ident[4]=2;h->ident[5]=1;h->ident[6]=1;
    h->type=2;h->machine=62;h->version=1;h->entry=BASE;
    h->phoff=sizeof(*h);h->phentsize=sizeof(struct elf_phdr);
    return h;
}
static struct elf_phdr *phdr(unsigned i) {
    struct elf_hdr *h=(struct elf_hdr *)(void *)image;
    return (struct elf_phdr *)(void *)(image+h->phoff+
                                      i*sizeof(struct elf_phdr));
}
static struct nas_elf_segment_policy policy(void) {
    return (struct nas_elf_segment_policy){
        .machine=62,.max_phnum=64,.min_va=BASE,
        .max_va=BASE+MAX_PAGES*P,.max_segment=MAX_PAGES*P,
        .max_pages=MAX_PAGES,.forbid_wx=false,
        .require_page_congruence=true
    };
}
static struct nas_elf_segment_ops operations(void) {
    return (struct nas_elf_segment_ops){
        .map_page=mapped_page,.copy_bytes=copied,.record_region=recorded
    };
}
static void verify_spans(void) {
    for(unsigned i=0;i<MAX_PAGES;i++)if(seen[i]) {
        unsigned matching=0;
        const struct nas_anon_region *v=nas_vma_find(&ledger,BASE+i*P);
        assert(v && v->prot==(1U|((perm[i]&2U)?2U:0U)|
                                    ((perm[i]&1U)?4U:0U)));
        for(unsigned j=0;j<regions;j++)
            if(spans[j].first<=BASE+i*P && BASE+i*P<spans[j].last) {
                assert(spans[j].flags==perm[i]);matching++;
            }
        assert(matching==1);
    }
}
static void overlap(bool reverse,bool callback_source) {
    clear_target();
    struct elf_hdr *h=header();h->phnum=3;
    struct elf_phdr rx={.type=1,.flags=5,.offset=0x1000,
                         .vaddr=BASE,.filesz=0x1800,.memsz=0x1800};
    struct elf_phdr rw={.type=1,.flags=6,.offset=0x3000,
                         .vaddr=BASE+P,.filesz=0x100,.memsz=P};
    struct elf_phdr ro={.type=1,.flags=4,.offset=0x4000,
                         .vaddr=BASE+2*P,.filesz=0x100,.memsz=P};
    *phdr(0)=reverse?rw:rx;
    *phdr(1)=reverse?rx:rw;
    *phdr(2)=ro;
    memset(image+0x1000,0x51,0x1800);
    memset(image+0x3000,0x62,0x100);
    memset(image+0x4000,0x73,0x100);
    struct elf_image e={.data=callback_source?NULL:image,.size=IMAGE_SIZE,
                         .context=image,.read_at=callback_source?image_read:0};
    struct nas_elf_segment_policy p=policy();
    struct nas_elf_segment_ops ops=operations();
    struct nas_elf_segment_result out={0};
    assert(nas_elf_segments_load(&e,&p,&ops,&out));
    assert(out.entry==BASE && mapped==3 && regions==3);
    assert(perm[0]==5 && perm[1]==7 && perm[2]==4);
    verify_spans();
    assert(bytes[0]==0x51 && bytes[0xfff]==0x51);
    if(!reverse) {
        assert(bytes[P]==0x62 && bytes[P+0xff]==0x62);
        assert(bytes[P+0x100]==0 && bytes[P+0x7ff]==0);
    } else {
        /* P1 was copied after P2, therefore its file data wins. */
        assert(bytes[P]==0x51 && bytes[P+0x7ff]==0x51);
        assert(bytes[P+0x800]==0);
    }
    assert(bytes[2*P]==0x73 && bytes[2*P+0x100]==0);
    /* W+X in one PT_LOAD is not a legitimate overlap exception. The
     * preflight fails before creating *any* page or logical region. */
    clear_target();phdr(0)->flags=7;
    assert(!nas_elf_segments_load(&e,&p,&ops,&out));
    assert(mapped==0 && regions==0);
    clear_target();phdr(0)->flags=reverse?6:5;
    phdr(0)->offset++;
    assert(!nas_elf_segments_load(&e,&p,&ops,&out));
    assert(mapped==0 && regions==0);
}
static void large_elf(void) {
    clear_target();
    struct elf_hdr *h=header();h->phnum=1;
    *phdr(0)=(struct elf_phdr){.type=1,.flags=5,.offset=0x5000,
                             .vaddr=BASE,.filesz=1,.memsz=256*P};
    image[0x5000]=0xcc;
    struct elf_image e={.data=NULL,.size=IMAGE_SIZE,
                        .read_at=image_read,.context=image};
    struct nas_elf_segment_policy p=policy();
    struct nas_elf_segment_ops ops=operations();
    struct nas_elf_segment_result out={0};
    assert(nas_elf_segments_load(&e,&p,&ops,&out));
    assert(mapped==256 && regions==1);
    unsigned ledger_count=0;
    for(unsigned i=0;i<ledger.capacity;i++)
        if(ledger.slots[i].end)ledger_count++;
    assert(ledger_count==1);
    assert(spans[0].first==BASE && spans[0].last==BASE+256*P);
    assert(bytes[0]==0xcc && bytes[256*P-1]==0);
    verify_spans();
}
int main(void) {
    overlap(false,false);
    overlap(true,false);
    overlap(false,true);
    overlap(true,true);
    large_elf();
    puts("[M105 ELF PASS] PT_LOAD both orders, PTE/VMA union, BSS, read_at, PF_RWX rollback, >128 pages in one VMA");
    return 0;
}
