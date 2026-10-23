#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "../kernel/task/elf_segments.h"
static uint8_t memory[0x4000];
static bool mapped[4];
static unsigned calls;
static bool map(void *ctx,uint64_t va,unsigned flags) {
    (void)ctx;
    if(va<0x10000 || va>=0x14000) return false;
    unsigned page=(unsigned)((va-0x10000)/4096);
    if(mapped[page]) return false;
    mapped[page]=true;
    assert((flags&4U)!=0);
    calls++;
    return true;
}
static bool copy(void *ctx,uint64_t va,const uint8_t *src,uint64_t size) {
    (void)ctx;
    if(va<0x10000 || size>0x14000-va) return false;
    memcpy(memory+va-0x10000,src,(size_t)size);
    return true;
}
int main(void) {
    union { uint64_t align; uint8_t b[4096]; } image={0};
    struct elf_hdr *h=(struct elf_hdr *)(void *)image.b;
    struct elf_phdr *p=(struct elf_phdr *)(void *)(image.b+sizeof(*h));
    memcpy(h->ident,"\177ELF",4);
    h->ident[4]=2;h->ident[5]=1;h->ident[6]=1;
    h->type=2;h->machine=243;h->version=1;h->entry=0x10000;
    h->phoff=sizeof(*h);h->phnum=1;h->phentsize=sizeof(*p);
    p->type=1;p->flags=5;p->offset=0x200;p->vaddr=0x10000;
    p->filesz=4;p->memsz=12;
    image.b[0x200]=42;
    struct elf_image elf={.data=image.b,.size=sizeof(image.b)};
    struct nas_elf_segment_policy opt={
        .machine=243,.max_phnum=16,.min_va=0x10000,
        .max_va=0x14000,.max_segment=0x4000,.max_pages=4,
        .forbid_wx=true
    };
    struct nas_elf_segment_ops ops={.map_page=map,.copy_bytes=copy};
    struct nas_elf_segment_result out={0};
    assert(nas_elf_segments_load(&elf,&opt,&ops,&out));
    assert(calls==1 && out.entry==0x10000 && out.phnum==1);
    assert(memory[0]==42 && memory[7]==0 && memory[11]==0);
    calls=0;memset(mapped,0,sizeof(mapped));
    opt.machine=62;
    assert(!nas_elf_segments_load(&elf,&opt,&ops,&out) && calls==0);
    opt.machine=243;
    p->flags=7;
    assert(!nas_elf_segments_load(&elf,&opt,&ops,&out) && calls==0);
    p->flags=5;
    p->filesz=p->memsz+1;
    assert(!nas_elf_segments_load(&elf,&opt,&ops,&out) && calls==0);
    p->filesz=4;
    p->offset=UINT64_MAX-2;
    assert(!nas_elf_segments_load(&elf,&opt,&ops,&out) && calls==0);
    return 0;
}
