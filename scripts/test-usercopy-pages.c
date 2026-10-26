/* Real shared usercopy contract regression: distinct noncontiguous backing
 * frames, page-boundary transitions, read-only pages and failure atomicity.
 * Linked to actual kernel/mm/usercopy.c, not a copy of its implementation.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/mm/usercopy.h"
#include "../kernel/mm/mm.h"
#include "../kernel/mm/fault.h"
#include "../kernel/task.h"
#include "../kernel/arch/user_pt.h"
enum { PGSZ = 4096, COUNT = 4 };
#define BASE 0x40000ULL
static uint8_t phys[COUNT][PGSZ];
static bool mapped[COUNT], writable[COUNT];
static unsigned lookups, checks;
/* The historical usercopy test owns a simulated isolated VM. New COW/lazy
 * logic is tested with actual MM/fault code in M101; this fixture retains
 * deliberately noncontiguous user-byte and permission-provider mocks. */
static struct nas_mm mock_mm={.users=1,.root_phys=0x7000};
static struct nas_task mock_task;
struct nas_task *nas_current_task=&mock_task;
const struct nas_anon_region *nas_vma_find(const struct nas_anon_regions *vmas,
                                            uint64_t va) {
    (void)vmas;(void)va;return 0;
}
bool nas_mm_resolve_user(const struct nas_mm *mm,uint64_t va,
                         uint64_t *pa,unsigned *access) {
    assert(mm==&mock_mm);
    if(va<BASE || va-BASE>=(uint64_t)COUNT*PGSZ)return false;
    unsigned index=(unsigned)((va-BASE)/PGSZ);
    if(!mapped[index])return false;
    if(pa)*pa=(index+1U)*PGSZ+(va&(PGSZ-1));
    if(access)*access=NAS_UP_READ|(writable[index]?NAS_UP_WRITE:0);
    return true;
}
enum nas_mm_fault_result nas_mm_handle_fault(struct nas_mm *mm,uint64_t va,
                                             unsigned access,bool protection) {
    (void)mm;(void)va;(void)access;(void)protection;
    return NAS_MM_FAULT_MAPERR;
}
uint32_t nas_frame_refs(uint64_t pa){(void)pa;return 1;}
bool nas_mm_break_cow(struct nas_mm *mm,uint64_t va,
                       unsigned prot,bool preserve_perm) {
    (void)mm;(void)va;(void)prot;(void)preserve_perm;
    assert(!"unique usercopy fixture page must not need COW");
    return false;
}

static int index_of(uint64_t va) {
    if (va < BASE || va - BASE >= (uint64_t)COUNT * PGSZ) return -1;
    return (int)((va - BASE) / PGSZ);
}
uint8_t *nas_mmu_user_byte(uint64_t va) {
    lookups++;
    int idx = index_of(va);
    if (idx < 0 || !mapped[idx]) return NULL;
    return phys[idx] + (va - BASE) % PGSZ;
}
bool nas_mmu_user_writable(uint64_t va) {
    checks++;
    int idx = index_of(va);
    return idx >= 0 && mapped[idx] && writable[idx];
}
static void reset(void) {
    mock_task.mm=&mock_mm;
    for (unsigned p = 0; p < COUNT; p++) {
        mapped[p] = writable[p] = true;
        memset(phys[p], 0xcc, PGSZ);
    }
    lookups = checks = 0;
}
int main(void) {
    reset();
    static uint8_t original[PGSZ + 151], observed[PGSZ + 151];
    for (unsigned n = 0; n < sizeof(original); n++)
        original[n] = (uint8_t)((n * 79U + 11U) & 255U);
    /* Start unaligned, cross two noncontiguous frames, and initialize RX. */
    writable[0] = false;
    assert(nas_user_copy_to_mapped(BASE + PGSZ - 100, original, sizeof(original)));
    assert(lookups == 3);  /* 100 bytes + one whole page + 51 bytes */
    lookups = 0;
    assert(nas_user_read_bytes(BASE + PGSZ - 100, observed, sizeof(observed)));
    assert(lookups == 3);
    assert(memcmp(original, observed, sizeof(original)) == 0);
    /* A one-byte range is one mapping lookup, not a whole-page walk. */
    lookups = 0;
    assert(nas_user_read_bytes(BASE + PGSZ - 1, observed, 1));
    assert(lookups == 1 && observed[0] == original[99]);

    reset();
    uint8_t small[32];
    memset(small, 0x4e, sizeof(small));
    /* Any read-only page must reject the ENTIRE write before mutation. */
    writable[1] = false;
    assert(!nas_user_write_bytes(BASE + PGSZ - 16, small, sizeof(small)));
    assert(checks == 2 && lookups == 0);
    for (unsigned i = 0; i < 16; i++) {
        assert(phys[0][PGSZ - 16 + i] == 0xcc);
        assert(phys[1][i] == 0xcc);
    }
    writable[1] = true;
    checks = lookups = 0;
    assert(nas_user_write_bytes(BASE + PGSZ - 16, small, sizeof(small)));
    assert(checks == 2 && lookups == 2);
    for (unsigned i = 0; i < 16; i++) {
        assert(phys[0][PGSZ - 16 + i] == 0x4e);
        assert(phys[1][i] == 0x4e);
    }
    /* Missing second page also must not partially mutate the first. */
    reset();
    mapped[1] = false;
    assert(!nas_user_write_bytes(BASE + PGSZ - 16, small, sizeof(small)));
    assert(phys[0][PGSZ - 1] == 0xcc);

    reset();
    const uint64_t number = UINT64_C(0x0123456789abcdef);
    assert(nas_user_write_u64(BASE + 2 * PGSZ - 4, number));
    uint64_t got = 0;
    assert(nas_user_read_u64(BASE + 2 * PGSZ - 4, &got));
    assert(got == number);
    assert(phys[1][PGSZ-4] == 0xef && phys[2][3] == 0x01);
    assert(!nas_user_copy_to_mapped(UINT64_MAX - 1, small, 16));
    assert(!nas_user_read_bytes(UINT64_MAX, observed, 1));
    assert(!nas_user_write_bytes(UINT64_MAX, small, 1));
    assert(nas_user_copy_to_mapped(UINT64_MAX, small, 0));
    printf("[USERCOPY PAGE PASS] genuine shared kernel memory-copy module: cross-page, RX, atomic write rejection, u64, overflow, 4KiB lookup batching\n");
    return 0;
}
