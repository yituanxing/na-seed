#include <stdint.h>
#include <stdbool.h>
#include "errno.h"
#include "random.h"
#include "mm/usercopy.h"
#include "arch/mmu.h"
#include "../third_party/chacha20/crypto/chacha20/chacha.h"

/* 20-round ChaCha20 stream seeded from Limine's best-effort boot entropy.
 * No fixed clock/PID/test seed is silently accepted as getrandom success.
 * This experimental kernel does NOT guarantee hardware-grade entropy.
 * Single CPU with non-preemptive kernel C serializes the stream. */
static struct chacha_ctx stream;
static uint8_t block[CHACHA_BLOCKLEN];
static unsigned used=CHACHA_BLOCKLEN;
static bool ready;
static const uint8_t zero[CHACHA_BLOCKLEN];
static void clear_secret(void *p, unsigned size) {
    volatile uint8_t *b=p;
    while(size--) *b++=0;
}
static bool next_block(void) {
    /* Preserve the previous 64-bit counter exhaustion policy. */
    if(stream.input[12]==UINT32_MAX && stream.input[13]==UINT32_MAX)
        return false;
    chacha_encrypt_bytes(&stream,zero,block,sizeof(block));
    used=0;
    return true;
}
bool nas_random_seed(const uint64_t *values,uint64_t count) {
    ready=false;
    clear_secret(&stream,sizeof(stream));
    clear_secret(block,sizeof(block));
    used=sizeof(block);
    if(!values || count<4)return false;
    uint64_t any=0;
    for(unsigned i=0;i<4;i++)any|=values[i];
    if(!any)return false;
    uint8_t key[32];
    for(unsigned i=0;i<4;i++)
        for(unsigned j=0;j<8;j++)key[i*8+j]=(uint8_t)(values[i]>>(j*8U));
    chacha_keysetup(&stream,key,256);
    chacha_ivsetup(&stream,zero,zero);
    clear_secret(key,sizeof(key));
    ready=true;
    return true;
}
bool nas_random_bytes(uint8_t *dst,uint64_t len) {
    if(!ready || (len && !dst))return false;
    for(uint64_t i=0;i<len;i++) {
        if(used==sizeof(block) && !next_block())return false;
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
    clear_secret(bytes,sizeof(bytes));
    return done;
}
