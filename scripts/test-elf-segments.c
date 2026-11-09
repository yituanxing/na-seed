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
/* Deliberately deny the ELF loader a whole-image pointer. This mirrors
 * the sparse tmpfs read_at contract in all three kernel architectures. */
static bool paged_read(const void *context, uint64_t offset,
                       uint8_t *dst, uint64_t len) {
    const uint8_t *image = (const uint8_t *)context;
    if (offset > 4096 || len > 4096 - offset) return false;
    memcpy(dst, image + offset, (size_t)len);
    return true;
}
static void interpreter_contract(void) {
    union { uint64_t align; uint8_t b[4096]; } bytes={0};
    struct elf_hdr *h=(struct elf_hdr *)(void *)bytes.b;
    struct elf_phdr *p=(struct elf_phdr *)(void *)(bytes.b+sizeof(*h));
    static const char glibc[]="/lib64/ld-linux-x86-64.so.2";
    memcpy(h->ident,"\177ELF",4);
    h->ident[4]=2;h->ident[5]=1;h->ident[6]=1;
    h->type=2;h->machine=62;h->version=1;h->entry=0x10000;
    h->phoff=sizeof(*h);h->phnum=2;h->phentsize=sizeof(*p);
    p[0].type=1;p[0].flags=5;p[0].vaddr=0x10000;
    p[0].filesz=0x100;p[0].memsz=0x100;
    p[1].type=3;p[1].offset=0x300;p[1].filesz=sizeof(glibc);
    memcpy(bytes.b+p[1].offset,glibc,sizeof(glibc));
    struct elf_image contiguous={.data=bytes.b,.size=sizeof(bytes.b)};
    struct elf_image sparse={.size=sizeof(bytes.b),
                             .read_at=paged_read,.context=bytes.b};
    char path[256]={0};
    bool has=false;
    assert(nas_elf_interpreter_path(&contiguous,62,path,sizeof(path),&has));
    assert(has && strcmp(path,glibc)==0);
    memset(path,0,sizeof(path));
    assert(nas_elf_interpreter_path(&sparse,62,path,sizeof(path),&has));
    assert(has && strcmp(path,glibc)==0);
    struct nas_elf_segment_policy opt={
        .machine=62,.max_phnum=64,.min_va=0x10000,.max_va=0x14000,
        .max_segment=0x4000,.interpreter=path,
        .require_page_congruence=true
    };
    struct nas_elf_segment_ops ops={.map_page=map,.copy_bytes=copy};
    struct nas_elf_segment_result out={0};
    memset(mapped,0,sizeof(mapped));calls=0;
    assert(nas_elf_segments_load(&sparse,&opt,&ops,&out));
    assert(out.has_interp && calls==1);
    memset(mapped,0,sizeof(mapped));calls=0;
    opt.interpreter="/lib/ld-musl-x86_64.so.1";
    assert(!nas_elf_segments_load(&sparse,&opt,&ops,&out) && calls==0);
    opt.interpreter=path;
    p[2]=p[1];h->phnum=3;
    assert(!nas_elf_interpreter_path(&sparse,62,path,sizeof(path),&has));
    assert(!nas_elf_segments_load(&sparse,&opt,&ops,&out) && calls==0);
    h->phnum=2;
    bytes.b[p[1].offset+2]=0;
    assert(!nas_elf_interpreter_path(&sparse,62,path,sizeof(path),&has));
    memcpy(bytes.b+p[1].offset,glibc,sizeof(glibc));
    bytes.b[p[1].offset]='.';
    assert(!nas_elf_interpreter_path(&sparse,62,path,sizeof(path),&has));
    memcpy(bytes.b+p[1].offset,glibc,sizeof(glibc));
    p[1].filesz=sizeof(glibc)-1;
    assert(!nas_elf_interpreter_path(&sparse,62,path,sizeof(path),&has));
    p[1].filesz=sizeof(glibc);
    p[1].offset=4090;
    assert(!nas_elf_interpreter_path(&sparse,62,path,sizeof(path),&has));
    p[1].type=0;
    assert(nas_elf_interpreter_path(&sparse,62,path,sizeof(path),&has));
    assert(!has && path[0]==0);
    assert(!nas_elf_interpreter_path(&sparse,243,path,sizeof(path),&has));
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
    memset(memory,0,sizeof(memory));
    /* Header, PHDR and 1700 bytes of PT_LOAD all cross the read_at seam. */
    struct elf_image paged={.size=sizeof(image.b),.read_at=paged_read,
                            .context=image.b};
    p->filesz=1700;p->memsz=1900;
    assert(nas_elf_segments_load(&paged,&opt,&ops,&out));
    assert(calls==1 && memory[0]==42 && memory[1699]==0 &&
           memory[1899]==0);
    p->filesz=4;p->memsz=12;
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
    interpreter_contract();
    return 0;
}
