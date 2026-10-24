#include <stdint.h>
#include <stdbool.h>
#include "errno.h"
#include "random.h"
#include "mm/usercopy.h"
#include "arch/mmu.h"

/* 20-round ChaCha20 stream seeded from Limine's best-effort boot entropy.
 * No fixed clock/PID/test seed is silently accepted as getrandom success.
 * This experimental kernel does NOT guarantee hardware-grade entropy.
 * Single CPU with non-preemptive kernel C serializes the stream. */
static uint32_t key[8];
static uint64_t counter;
static uint8_t block[64];
static unsigned used=64;
static bool ready;
static uint32_t rol(uint32_t v,unsigned n) {
    return (v<<n) | (v>>(32U-n));
}
#define QR(a,b,c,d) do { \
    x[a]+=x[b]; x[d]=rol(x[d]^x[a],16); \
    x[c]+=x[d]; x[b]=rol(x[b]^x[c],12); \
    x[a]+=x[b]; x[d]=rol(x[d]^x[a],8); \
    x[c]+=x[d]; x[b]=rol(x[b]^x[c],7); \
} while(0)
static bool next_block(void) {
    if(counter==UINT64_MAX)return false;
    uint32_t initial[16]={
        0x61707865U,0x3320646eU,0x79622d32U,0x6b206574U,
        key[0],key[1],key[2],key[3],key[4],key[5],key[6],key[7],
        (uint32_t)counter,(uint32_t)(counter>>32),0,0
    };
    uint32_t x[16];
    for(unsigned i=0;i<16;i++)x[i]=initial[i];
    for(unsigned i=0;i<10;i++) {
        QR(0,4,8,12); QR(1,5,9,13); QR(2,6,10,14); QR(3,7,11,15);
        QR(0,5,10,15); QR(1,6,11,12); QR(2,7,8,13); QR(3,4,9,14);
    }
    for(unsigned i=0;i<16;i++) {
        uint32_t v=x[i]+initial[i];
        for(unsigned j=0;j<4;j++)block[i*4+j]=(uint8_t)(v>>(j*8U));
    }
    counter++;
    used=0;
    return true;
}
#undef QR
bool nas_random_seed(const uint64_t *values,uint64_t count) {
    ready=false;
    if(!values || count<4)return false;
    uint64_t any=0;
    for(unsigned i=0;i<4;i++)any|=values[i];
    if(!any)return false;
    for(unsigned i=0;i<4;i++) {
        key[i*2]=(uint32_t)values[i];
        key[i*2+1]=(uint32_t)(values[i]>>32);
    }
    counter=0;
    used=64;
    ready=true;
    return true;
}
bool nas_random_bytes(uint8_t *dst,uint64_t len) {
    if(!ready || (len && !dst))return false;
    for(uint64_t i=0;i<len;i++) {
        if(used==64 && !next_block())return false;
        dst[i]=block[used++];
    }
    return true;
}
uint64_t nas_random_getrandom(uint64_t addr,uint64_t len,uint64_t flags) {
    /* GRND_NONBLOCK=1, GRND_RANDOM=2; reject unknown flags. */
    if(flags & ~3ULL)return (uint64_t)-EINVAL;
    if(!len)return 0;
    if(!ready)return (uint64_t)-ENOSYS;
    if(addr>=USER_LIMIT || len>USER_LIMIT-addr)return (uint64_t)-EFAULT;
    /* Atomic short results, bounded positive partial count for huge asks. */
    uint64_t permitted=len>(1ULL<<20) ? (1ULL<<20):len;
    uint8_t bytes[256];
    uint64_t done=0;
    while(done<permitted) {
        uint64_t n=permitted-done;
        if(n>sizeof(bytes))n=sizeof(bytes);
        if(!nas_random_bytes(bytes,n))return done?done:(uint64_t)-ENOSYS;
        if(!nas_user_write_bytes(addr+done,bytes,n))
            return done?done:(uint64_t)-EFAULT;
        done+=n;
    }
    for(unsigned i=0;i<sizeof(bytes);i++)bytes[i]=0;
    return done;
}
