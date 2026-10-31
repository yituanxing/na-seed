#ifndef NASEED_ANON_REGIONS_H
#define NASEED_ANON_REGIONS_H
#include <stdbool.h>
#include <stdint.h>
#define NAS_ANON_REGION_INITIAL_CAPACITY 16U
#define NAS_ANON_FIXED_FLOOR 0x00010000ULL
#define NAS_ANON_FLOOR 0x20000000ULL
#define NAS_ANON_CEILING 0x60000000ULL
/* Keep the established low mmap window for old binaries and Linux libc;
 * reserve the 0x60000000..0x80000000 gap for the main user stack and signal
 * trampoline (0x70000000/0x6f700000). On pressure, use a disjoint higher
 * window for large real-program VMAs, e.g. V8's >1-GiB aggregate CodeRange
 * reservations. The 64-GiB ceiling is below the smallest 64-bit user-VA
 * hardware regime currently supported: RV64 Sv39=256 GiB, ARM64=512 GiB,
 * x86-64=128 TiB. No 32-bit ELF ABI is exposed by this kernel. */
#define NAS_ANON_EXTENDED_FLOOR 0x80000000ULL
#define NAS_ANON_EXTENDED_CEILING 0x1000000000ULL
#define NAS_VMA_ADDRESS_LIMIT NAS_ANON_EXTENDED_CEILING
/* Policy records for mmap-owned user VMAs, independent of resident
 * pages and PTE permissions. PROT_NONE is a VMA even with no resident PTE.
 * File snapshots are eager copies; immutable shared snapshots are safe only
 * if the underlying provider guarantees its contents cannot ever change.
 * ELF, heap, stack and signal helper ranges use this same MM-owned map.
 * Eager snapshot mappings have no live backing file references. Mutable
 * shared mappings own a reference to their pinned inode/page-cache owner,
 * independent of the original open FD and its lifetime. */
enum nas_vma_kind {
    NAS_VMA_ANON_PRIVATE=0,
    NAS_VMA_FILE_PRIVATE_SNAPSHOT=1,
    NAS_VMA_ELF_PRIVATE_SNAPSHOT=2,
    NAS_VMA_HEAP=3,
    NAS_VMA_STACK=4,
    NAS_VMA_SIGNAL_TRAMPOLINE=5,
    NAS_VMA_FILE_IMMUTABLE_SHARED=6,
    NAS_VMA_FILE_MUTABLE_SHARED=7
};
#define NAS_VMA_DONTFORK 1U
struct nas_mapped_file;
struct nas_anon_region {
    uint64_t start,end;
    uint64_t file_offset; /* file offset corresponding to this VMA's start */
    uint32_t prot;        /* logical permissions; NOT hardware PTE state */
    uint32_t kind;
    uint32_t flags;       /* Linux MADV_DONTFORK, MM-owned fork policy */
    /* One owned ref per live VMA slot, including split fragments and forks.
     * NULL for anonymous, ELF and eager snapshot policies. */
    struct nas_mapped_file *backing;
};
/* The MM owns this heap-backed table. A zeroed table is empty; cloning an MM
 * must use nas_vma_clone rather than copying the pointer. Capacity grows with
 * the actual workload, and transactional copies never live on a 16 KiB
 * kernel entry stack. */
struct nas_anon_regions {
    struct nas_anon_region *slots;
    unsigned capacity;
};
typedef bool (*nas_vma_visit)(const struct nas_anon_region *,void *);
const struct nas_anon_region *nas_vma_find(const struct nas_anon_regions *,uint64_t va);
bool nas_vma_walk(const struct nas_anon_regions *,nas_vma_visit,void *);
bool nas_vma_record(struct nas_anon_regions *,uint64_t addr,uint64_t length,
                    unsigned prot,unsigned kind,uint64_t file_offset);
bool nas_vma_record_shared(struct nas_anon_regions *,uint64_t addr,
                           uint64_t length,unsigned prot,uint64_t file_offset,
                           struct nas_mapped_file *backing);
bool nas_vma_replace(struct nas_anon_regions *,uint64_t addr,uint64_t length,
                     unsigned prot,unsigned kind,uint64_t file_offset);
bool nas_vma_replace_shared(struct nas_anon_regions *,uint64_t addr,
                            uint64_t length,unsigned prot,uint64_t file_offset,
                            struct nas_mapped_file *backing);
bool nas_vma_protect(struct nas_anon_regions *,uint64_t addr,uint64_t length,
                     unsigned prot);
bool nas_vma_set_dontfork(struct nas_anon_regions *,uint64_t addr,
                           uint64_t length,bool enabled);
/* M11 reservation APIs are retained as compatibility wrappers. These and
 * the typed VMA routines manipulate the SAME canonical MM-owned slots. */
/* Reset releases table storage. Clone preserves the destination on OOM;
 * both operands must be zero-initialized or already own valid VMA tables.
 * Commit transfers ownership of staged slots, zeroing the staged handle. */
void nas_anon_reset(struct nas_anon_regions *);
bool nas_vma_clone(struct nas_anon_regions *destination,
                   const struct nas_anon_regions *source);
void nas_vma_commit(struct nas_anon_regions *destination,
                    struct nas_anon_regions *staged);
bool nas_anon_find_gap(const struct nas_anon_regions *,uint64_t length,uint64_t *);
/* Whole MAP_FIXED interval must stay in the historical low VA domain or the
 * extended mmap domain; never overwrite the intervening user stack/gap. */
bool nas_anon_fixed_range_allowed(uint64_t addr,uint64_t length);
bool nas_anon_record(struct nas_anon_regions *,uint64_t addr,uint64_t length);
bool nas_anon_forget(struct nas_anon_regions *,uint64_t addr,uint64_t length);
bool nas_anon_covered(const struct nas_anon_regions *,uint64_t addr,uint64_t length);
bool nas_anon_intersects(const struct nas_anon_regions *,uint64_t addr,uint64_t length);
bool nas_anon_overlay(struct nas_anon_regions *,uint64_t addr,uint64_t length);
#endif
