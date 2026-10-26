/* Link the ACTUAL TTY and line discipline; mock only console/input provider
 * and logical user memory. COW sets hardware PTE W=0 without revoking
 * logical VMA W=1. A forked interactive ash must keep reading TTY. */
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/task.h"
#include "../kernel/drivers/tty.h"
#include "../kernel/mm/usercopy.h"
#include "../kernel/errno.h"
#define USER_ADDR 0x20010000ULL
static struct nas_task current;
struct nas_task *nas_current_task=&current;
static char userbuf[128];
static bool user_logical_write=true,physical_write=false;
static unsigned checks,bytes_written;
static const char *rx;
void nas_putchar(char c){(void)c;}
void nas_arch_console_putc(char c){(void)c;}
bool nas_arch_console_has_rx(void){return true;}
bool nas_arch_console_getc(uint8_t *out){
    if(!rx || !*rx)return false;
    *out=(uint8_t)*rx++;return true;
}
bool nas_user_address_writable(uint64_t addr){
    ++checks;
    return user_logical_write && addr>=USER_ADDR &&
           addr<USER_ADDR+sizeof(userbuf);
}
bool nas_mmu_user_writable(uint64_t addr){(void)addr;return physical_write;}
bool nas_user_write_bytes(uint64_t addr,const uint8_t *src,uint64_t n){
    if(!nas_user_address_writable(addr) || n>sizeof(userbuf) ||
       addr-USER_ADDR>sizeof(userbuf)-n)return false;
    /* Emulate the real MM copy-on-write break: before publication PTE W=0,
     * after nas_user_write_bytes the private frame becomes hardware W=1. */
    physical_write=true;
    memcpy(userbuf+(addr-USER_ADDR),src,(size_t)n);
    bytes_written+=(unsigned)n;
    return true;
}
bool nas_user_read_bytes(uint64_t addr,uint8_t *dst,uint64_t n){
    (void)addr;(void)dst;(void)n;return false;
}
struct nas_task *nas_task_at(unsigned slot){(void)slot;return 0;}
uint64_t nas_arch_monotonic_ns(void){return 0;}
uint64_t nas_time_add_deadline(uint64_t base,uint64_t delta){
    return base+delta;
}
bool nas_time_deadline_expired(uint64_t now,uint64_t target){
    return now>=target;
}
int main(void){
    current.pid=current.sid=current.pgid=1;
    nas_tty_init();
    assert(nas_tty_ioctl(0x540e,0)==0); /* real TIOCSCTTY */
    assert(nas_tty_enabled());
    rx="echo one\r";
    assert(nas_tty_read(USER_ADDR,sizeof(userbuf),false)==9);
    assert(!memcmp(userbuf,"echo one\n",9));
    assert(bytes_written==9 && physical_write);
    /* fork: PTE write is removed while mmap/VMA still grants WRITE */
    physical_write=false;
    rx="pwd\r";
    assert(nas_tty_read(USER_ADDR,sizeof(userbuf),false)==4);
    assert(!memcmp(userbuf,"pwd\n",4));
    assert(bytes_written==13 && physical_write);
    /* Permission really removed, not just COW: fail EFAULT and do NOT
     * consume queued terminal input before a future valid read. */
    rx="test\r"; physical_write=false;user_logical_write=false;
    assert((int64_t)nas_tty_read(USER_ADDR,sizeof(userbuf),false)==-EFAULT);
    user_logical_write=true;
    assert(nas_tty_read(USER_ADDR,sizeof(userbuf),false)==5);
    assert(!memcmp(userbuf,"test\n",5));
    assert(bytes_written==18 && checks>=6);
    puts("[M102 TTY COW PASS] real kernel TTY continues reading logically writable buffers after fork; true RO is EFAULT without data loss");
    return 0;
}
