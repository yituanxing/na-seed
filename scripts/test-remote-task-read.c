/* Actual production remote_copy.c linked; fake ONLY physical frame
 * allocation and arch translation, both with multiple nonadjacent frames. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/mm/vm.h"
#include "../kernel/task.h"
#define PAGE 4096ULL
static uint8_t physical[0x10000];
static uint64_t last_root;
uint64_t nas_hhdm_offset(void){return (uint64_t)(uintptr_t)physical;}
bool nas_mmu_resolve_user_address(uint64_t root,uint64_t va,
                                  uint64_t *pa,unsigned *access) {
    last_root=root;
    if(root!=0x7000 || va<0x10000 || va>=0x12000)return false;
    *pa=(va<0x11000?0x2000:0x9000)+(va&4095ULL);
    if(access)*access=1;
    return true;
}
int main(void){
    struct nas_task main_task={0},thread={0},dead={0};
    struct nas_mm mm={.users=1,.root_phys=0x7000};
    struct nas_mm unrelated_mm={.users=1,.root_phys=0x8888};
    main_task.pid=22;main_task.mm=&mm;
    main_task.state=NAS_TASK_RUNNING;
    thread.pid=23;thread.mm=&mm; /* CLONE_VM shares MM, not leader Task */
    thread.vm_owner=&main_task;thread.state=NAS_TASK_BLOCKED;
    dead.state=NAS_TASK_FREE;dead.mm=&unrelated_mm;
    for(unsigned n=0;n<PAGE;n++){
        physical[0x2000+n]=(uint8_t)(n*13U+7U);
        physical[0x9000+n]=(uint8_t)(n*11U+3U);
    }
    uint8_t data[80]={0};
    assert(nas_vm_read_task_bytes(&thread,0x10fe1,data,80)==80);
    assert(last_root==main_task.mm->root_phys);
    assert(!memcmp(data,physical+0x2000+PAGE-31,31));
    assert(!memcmp(data+31,physical+0x9000,49));
    uint8_t saved[80];memcpy(saved,data,80);
    assert(nas_vm_read_task_bytes(&main_task,0x12000,data,4)==-14);
    assert(!memcmp(saved,data,80));
    assert(nas_vm_read_task_bytes(&main_task,0x11ff8,data,16)==8);
    assert(nas_vm_read_task_bytes(&thread,0x10000,data,0)==0);
    assert(nas_vm_read_task_bytes(&dead,0x10000,data,8)==-3);
    assert(nas_vm_read_task_bytes(&main_task,UINT64_MAX,data,8)==-14);
    assert(nas_vm_read_task_bytes(&main_task,0x10000,0,8)==-14);

    /* Debugger breakpoint writes must cross noncontiguous frames and
     * preserve every byte when any later page fails preflight. */
    uint8_t patch[80];
    for(unsigned i=0;i<80;i++)patch[i]=(uint8_t)(i+0x30U);
    assert(nas_vm_write_task_bytes(&thread,0x10fe1,patch,80)==80);
    assert(last_root==main_task.mm->root_phys);
    assert(!memcmp(physical+0x2000+PAGE-31,patch,31));
    assert(!memcmp(physical+0x9000,patch+31,49));
    uint8_t original[8];
    memcpy(original,physical+0x9000+PAGE-8,8);
    assert(nas_vm_write_task_bytes(&thread,0x11ff8,patch,16)==-14);
    assert(!memcmp(physical+0x9000+PAGE-8,original,8));
    assert(nas_vm_write_task_bytes(&thread,0x11000,patch,8)==8);
    assert(nas_vm_write_task_bytes(&dead,0x10000,patch,8)==-3);
    assert(nas_vm_write_task_bytes(&thread,UINT64_MAX,patch,8)==-14);
    assert(nas_vm_write_task_bytes(&thread,0x11000,0,8)==-14);
    puts("[M75 REMOTE WRITE PASS] 3 ISA common Task VM, nonadjacent frames, atomic preflight and EFAULT");
    puts("[M71 REMOTE VM PASS] selected Task owner, 4K noncontiguous frames, partial reads, EFAULT ESRCH");
    return 0;
}
