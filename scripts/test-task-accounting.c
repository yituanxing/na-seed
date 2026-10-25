/* Production common scheduler accounting vs controlled native clock. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../kernel/task.h"
static struct nas_task tasks[2];
static uint64_t now;
struct nas_task *nas_task_at(unsigned i){return i<2?&tasks[i]:0;}
uint64_t nas_arch_monotonic_ns(void){return now;}
int main(void){
    tasks[0].state=NAS_TASK_RUNNING;tasks[1].state=NAS_TASK_READY;
    now=1000000000ULL;nas_task_cpu_account_switch(&tasks[0]);
    now=2500000000ULL;nas_task_cpu_account_switch(&tasks[1]);
    now=4000000000ULL;nas_task_cpu_account_switch(0);
    now=6000000000ULL;
    uint64_t busy=0,idle=0,loads[3]={0},switches=0;
    nas_task_cpu_account_snapshot(&busy,&idle,loads,&switches);
    assert(busy==3000000000ULL && idle==2000000000ULL);
    assert(tasks[0].cpu_runtime_ns==1500000000ULL);
    assert(tasks[1].cpu_runtime_ns==1500000000ULL);
    assert(switches==3);
    assert(loads[0]>loads[1] && loads[1]>loads[2]);
    puts("[M66 ACCOUNT PASS] real monotonic 3s busy, 2s idle, Q16 loadavg");
    return 0;
}
