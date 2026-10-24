#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "../kernel/random.h"
#include "../kernel/errno.h"
/* Host adapter only: the true kernel checks the native MMU on every write. */
bool nas_user_write_bytes(uint64_t addr,const uint8_t *src,uint64_t n) {
    if(!addr && n)return false;
    memcpy((void *)(uintptr_t)addr,src,(size_t)n);
    return true;
}
int main(void) {
    uint8_t out[64]={0};
    assert((int64_t)nas_random_getrandom((uint64_t)(uintptr_t)out,1,0)==-ENOSYS);
    assert(!nas_random_seed(0,0));
    const uint64_t zero[4]={0};
    assert(!nas_random_seed(zero,4));
    const uint64_t key[4]={
        UINT64_C(0x0706050403020100),UINT64_C(0x0f0e0d0c0b0a0908),
        UINT64_C(0x1716151413121110),UINT64_C(0x1f1e1d1c1b1a1918)
    };
    assert(nas_random_seed(key,4));
    static const uint8_t known[64]={
        0x39,0xfd,0x2b,0x7d,0xd9,0xc5,0x19,0x6a,
        0x8d,0xbd,0x03,0x77,0xb8,0xdc,0x4a,0x49,
        0x8a,0x35,0xd8,0x6f,0xbc,0xde,0x6a,0xcc,
        0xb2,0xcc,0x7d,0x4c,0xd8,0xea,0x24,0x92,
        0x2b,0x23,0xcc,0xe7,0xa2,0x60,0x23,0xab,
        0x3f,0x0e,0xef,0x69,0x3a,0xc8,0x7f,0x64,
        0x25,0x82,0x35,0xea,0xb1,0xf7,0xa3,0x2d,
        0xc2,0x27,0x62,0xa0,0x48,0x5b,0x41,0x0c
    };
    assert((int64_t)nas_random_getrandom((uint64_t)(uintptr_t)out,64,0)==64);
    assert(memcmp(out,known,64)==0);
    uint8_t next[64];
    assert(nas_random_bytes(next,64));
    assert(memcmp(out,next,64)!=0);
    assert((int64_t)nas_random_getrandom(0,8,0)==-EFAULT);
    assert((int64_t)nas_random_getrandom((uint64_t)(uintptr_t)out,8,8)==-EINVAL);
    assert(nas_random_getrandom(0,0,0)==0);
    puts("[PASS] shared ChaCha20 known-answer, getrandom flags and stream");
    return 0;
}
