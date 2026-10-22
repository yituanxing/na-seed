#include <stdint.h>
#include <stdbool.h>
#include "user_pt.h"
#include "../mm/frame.h"
#if defined(__riscv) && __riscv_xlen == 64
#include "riscv64/mmu.h"
#include "riscv64/cpu.h"
static struct nas_rv_page_table native_pt(const struct nas_user_space *s) {
    return (struct nas_rv_page_table){s->root_pa,s->levels};
}
unsigned nas_arch_pt_active_levels(void) {
    uint64_t mode=nas_riscv_satp_read()>>60;
    return mode>=8 && mode<=10 ? (unsigned)(mode-5) : 0;
}
bool nas_arch_pt_new(struct nas_user_space *s, unsigned levels) {
    if (!s || levels != nas_arch_pt_active_levels()) return false;
    struct nas_rv_page_table pt={0};
    if (!nas_rv_pt_init(&pt, levels)) return false;
    /* Inherit ONLY Limine supervisor upper half. Never free its tables. */
    uint64_t satp=nas_riscv_satp_read();
    uint64_t *src=(uint64_t *)(uintptr_t)(nas_hhdm_offset()+
                                            ((satp & ((1ULL<<44)-1))<<12));
    uint64_t *dst=(uint64_t *)(uintptr_t)(nas_hhdm_offset()+pt.root_pa);
    for(unsigned i=256;i<512;i++) dst[i]=src[i];
    s->root_pa=pt.root_pa; s->levels=levels;
    return true;
}
void nas_arch_pt_destroy(struct nas_user_space *s) {
    struct nas_rv_page_table pt=native_pt(s);
    /* Remove the BORROWED kernel entries so pt_destroy does not free them. */
    uint64_t *root=(uint64_t *)(uintptr_t)(nas_hhdm_offset()+pt.root_pa);
    for(unsigned i=256;i<512;i++) root[i]=0;
    nas_rv_pt_destroy(&pt);
    s->root_pa=pt.root_pa; s->levels=pt.levels;
}
bool nas_arch_pt_map(const struct nas_user_space *s,uint64_t va,
                     uint64_t pa,unsigned access) {
    struct nas_rv_page_table p=native_pt(s);
    uint64_t flags=NAS_RV_PTE_U |
       ((access & NAS_UP_READ)?NAS_RV_PTE_R:0) |
       ((access & NAS_UP_WRITE)?NAS_RV_PTE_W:0) |
       ((access & NAS_UP_EXEC)?NAS_RV_PTE_X:0);
    return nas_rv_pt_map(&p,va,pa,flags);
}
bool nas_arch_pt_lookup(const struct nas_user_space *s,uint64_t va,
                        uint64_t *pa,unsigned *access) {
    struct nas_rv_page_table p=native_pt(s);
    uint64_t address=0,flags=0;
    if (!nas_rv_pt_translate(&p,va,&address,&flags)) return false;
    if (pa) *pa=address;
    if (access) *access =
      ((flags & NAS_RV_PTE_R)?NAS_UP_READ:0) |
      ((flags & NAS_RV_PTE_W)?NAS_UP_WRITE:0) |
      ((flags & NAS_RV_PTE_X)?NAS_UP_EXEC:0);
    return true;
}
bool nas_arch_pt_unmap(const struct nas_user_space *s,uint64_t va) {
    struct nas_rv_page_table p=native_pt(s);
    return nas_rv_pt_unmap(&p,va);
}
bool nas_arch_pt_protect(const struct nas_user_space *s,uint64_t va,
                         unsigned access) {
    struct nas_rv_page_table p=native_pt(s);
    return nas_rv_pt_protect(&p,va,access);
}
bool nas_arch_pt_walk(const struct nas_user_space *s,
                      nas_user_pt_visit visitor,void *ctx) {
    struct nas_rv_page_table p=native_pt(s);
    return nas_rv_pt_walk(&p,visitor,ctx);
}
bool nas_arch_pt_install(const struct nas_user_space *s) {
    if (!s || !s->root_pa || s->levels!=nas_arch_pt_active_levels()) return false;
    uint64_t mode=nas_riscv_satp_read() & (15ULL<<60);
    uint64_t new_satp=mode | (s->root_pa>>12); /* ASID 0 */
    __asm__ volatile("csrw satp,%0\n sfence.vma zero,zero\n fence.i"
                     : : "r"(new_satp) : "memory");
    return true;
}
void nas_arch_pt_flush(uint64_t va) {
    __asm__ volatile("sfence.vma %0,zero" : : "r"(va) : "memory");
}
#elif defined(__aarch64__)
#include "aarch64/mmu.h"
#include "aarch64/cpu.h"
static struct nas_arm_pt native_pt(const struct nas_user_space *s) {
    return (struct nas_arm_pt){s->root_pa,s->levels};
}
unsigned nas_arch_pt_active_levels(void) {
    uint64_t tcr=nas_arm64_read_tcr();
    if ((tcr & (3ULL<<14))!=0) return 0; /* TG0 must be 4K */
    unsigned sz=(unsigned)(tcr & 63U);
    if (sz==25) return 3; /* 39-bit TTBR0 */
    if (sz==16) return 4; /* 48-bit TTBR0 */
    return 0;
}
bool nas_arch_pt_new(struct nas_user_space *s,unsigned levels) {
    struct nas_arm_pt pt={0};
    if (!s || !nas_arch_pt_active_levels() ||
        levels!=nas_arch_pt_active_levels() ||
        !nas_arm_pt_init(&pt,levels)) return false;
    /* EL1 kernel lives in separate TTBR1; no kernel entries to copy. */
    s->root_pa=pt.root_pa; s->levels=pt.levels;
    return true;
}
void nas_arch_pt_destroy(struct nas_user_space *s) {
    struct nas_arm_pt pt=native_pt(s);
    nas_arm_pt_destroy(&pt);
    s->root_pa=pt.root_pa; s->levels=pt.levels;
}
bool nas_arch_pt_map(const struct nas_user_space *s,uint64_t va,
                     uint64_t pa,unsigned access) {
    struct nas_arm_pt p=native_pt(s);
    return nas_arm_pt_map(&p,va,pa,NAS_ARM_USER|NAS_ARM_R|
       ((access & NAS_UP_WRITE)?NAS_ARM_W:0) |
       ((access & NAS_UP_EXEC)?NAS_ARM_X:0));
}
bool nas_arch_pt_lookup(const struct nas_user_space *s,uint64_t va,
                        uint64_t *pa,unsigned *access) {
    struct nas_arm_pt p=native_pt(s);
    uint64_t address=0,flags=0;
    if (!nas_arm_pt_translate(&p,va,&address,&flags)) return false;
    if (pa) *pa=address;
    if (access) *access=NAS_UP_READ|
      ((flags & NAS_ARM_W)?NAS_UP_WRITE:0) |
      ((flags & NAS_ARM_X)?NAS_UP_EXEC:0);
    return true;
}
bool nas_arch_pt_unmap(const struct nas_user_space *s,uint64_t va) {
    struct nas_arm_pt p=native_pt(s);
    return nas_arm_pt_unmap(&p,va);
}
bool nas_arch_pt_protect(const struct nas_user_space *s,uint64_t va,
                         unsigned access) {
    struct nas_arm_pt p=native_pt(s);
    return nas_arm_pt_protect(&p,va,access);
}
bool nas_arch_pt_walk(const struct nas_user_space *s,
                      nas_user_pt_visit visitor,void *ctx) {
    struct nas_arm_pt p=native_pt(s);
    return nas_arm_pt_walk(&p,visitor,ctx);
}
bool nas_arch_pt_install(const struct nas_user_space *s) {
    if (!s || !s->root_pa || s->levels!=nas_arch_pt_active_levels()) return false;
    uint64_t pa=s->root_pa; /* TTBR0 (ASID 0) */
    __asm__ volatile("dsb ishst\n msr ttbr0_el1,%0\n isb\n"
                     "tlbi vmalle1is\n dsb ish\n isb" : : "r"(pa) : "memory");
    return true;
}
void nas_arch_pt_flush(uint64_t va) {
    uint64_t v=va>>12;
    __asm__ volatile("dsb ishst\n tlbi vae1is,%0\n dsb ish\n isb"
                     : : "r"(v) : "memory");
}
#else
#error "Shared user PTE provider only supports RV64 and AArch64"
#endif
