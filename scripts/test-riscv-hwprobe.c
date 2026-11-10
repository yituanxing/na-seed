#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Host fixture for Linux RV64 riscv_hwprobe user memory and return ABI.
 * The fake user VMA has an intentionally nonzero base to catch null access. */
#undef __x86_64__
#define __riscv 1
#define __riscv_xlen 64
#include "../kernel/arch/riscv64/hwprobe.h"
#include "../kernel/arch/linux_syscall_abi.h"

enum { USER_BASE=0x1000, USER_SIZE=4096 };
static uint8_t user_memory[USER_SIZE];
static bool user_range(uint64_t address,uint64_t len) {
    return address>=USER_BASE &&
        len <= USER_SIZE && address-USER_BASE<=USER_SIZE-len;
}
bool nas_user_read_bytes(uint64_t address,uint8_t *dest,uint64_t length) {
    if(!user_range(address,length))return false;
    memcpy(dest,user_memory+(address-USER_BASE),(size_t)length);
    return true;
}
bool nas_user_prepare_write(uint64_t address,uint64_t length) {
    return user_range(address,length);
}
bool nas_user_write_bytes(uint64_t address,const uint8_t *src,uint64_t length) {
    if(!user_range(address,length))return false;
    memcpy(user_memory+(address-USER_BASE),src,(size_t)length);
    return true;
}
int main(void) {
    assert(nas_linux_decode_syscall(258)==NAS_SC_riscv_hwprobe);
    assert(nas_linux_decode_syscall(259)==NAS_SC_NONE);
    struct nas_riscv_hwprobe_pair input[]={{3,UINT64_MAX},{4,UINT64_MAX},
                                           {5,UINT64_MAX},{99,UINT64_MAX}};
    memcpy(user_memory,input,sizeof(input));
    assert(nas_riscv_hwprobe(USER_BASE,4,0,0,0)==0);
    struct nas_riscv_hwprobe_pair output[4];
    memcpy(output,user_memory,sizeof(output));
    assert(output[0].key==3 && output[0].value==1);
    assert(output[1].key==4 && output[1].value==3);
    assert(output[2].key==5 && output[2].value==0);
    assert(output[3].key==-1 && output[3].value==0);
    assert(nas_riscv_hwprobe(0,0,0,0,0)==0);
    assert((int64_t)nas_riscv_hwprobe(0,1,0,0,0)==-EFAULT);
    assert((int64_t)nas_riscv_hwprobe(USER_BASE+USER_SIZE-8,1,0,0,0)==-EFAULT);
    assert((int64_t)nas_riscv_hwprobe(USER_BASE,65,0,0,0)==-E2BIG);
    assert((int64_t)nas_riscv_hwprobe(USER_BASE,1,0,0,2)==-EINVAL);
    assert((int64_t)nas_riscv_hwprobe(USER_BASE,1,8,USER_BASE,0)==-EOPNOTSUPP);
    assert((int64_t)nas_riscv_hwprobe(USER_BASE,1,8,USER_BASE,1)==-EOPNOTSUPP);
    return 0;
}
