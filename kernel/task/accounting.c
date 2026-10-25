/* One kernel-owned CPU occupancy and loadavg sampler. Sample true
 * monotonic elapsed time at syscall/scheduler boundaries, not fabricated
 * hardware user/system cycle accounting. */
#include "../task.h"
#include "../arch/arch.h"
#include <stdint.h>
#include <stdbool.h>

static struct nas_task *cpu_account_active;
static uint64_t cpu_last_ns,cpu_busy_ns,cpu_idle_ns,cpu_switches;
static uint64_t load_q16[3];
static bool cpu_clock_started;
static uint64_t load_last_ns;
static void cpu_account_sample(uint64_t now) {
    if(!cpu_clock_started) {
        cpu_last_ns=load_last_ns=now;
        cpu_clock_started=true;
        return;
    }
    if(now<cpu_last_ns)return;
    uint64_t delta=now-cpu_last_ns;
    if(cpu_account_active && cpu_account_active->state!=NAS_TASK_FREE) {
        struct nas_task *t=cpu_account_active;
        if(UINT64_MAX-t->cpu_runtime_ns<delta)t->cpu_runtime_ns=UINT64_MAX;
        else t->cpu_runtime_ns+=delta;
        if(UINT64_MAX-cpu_busy_ns<delta)cpu_busy_ns=UINT64_MAX;
        else cpu_busy_ns+=delta;
    } else {
        if(UINT64_MAX-cpu_idle_ns<delta)cpu_idle_ns=UINT64_MAX;
        else cpu_idle_ns+=delta;
    }
    cpu_last_ns=now;
    if(now>=load_last_ns && now-load_last_ns>=1000000ULL) {
        uint64_t run=0;
        for(unsigned i=0;i<NAS_TASK_LIMIT;i++) {
            const struct nas_task *t=nas_task_at(i);
            if(t && (t->state==NAS_TASK_RUNNING || t->state==NAS_TASK_READY))run++;
        }
        const uint64_t windows[3]={60000000000ULL,300000000000ULL,900000000000ULL};
        uint64_t dt=now-load_last_ns;
        if(dt>900000000000ULL)dt=900000000000ULL;
        for(unsigned i=0;i<3;i++) {
            uint64_t target=run*65536ULL;
            if(target>=load_q16[i])
                load_q16[i]+=(target-load_q16[i])*dt/(windows[i]+dt);
            else
                load_q16[i]-=(load_q16[i]-target)*dt/(windows[i]+dt);
        }
        load_last_ns=now;
    }
}
void nas_task_cpu_account_switch(struct nas_task *next) {
    cpu_account_sample(nas_arch_monotonic_ns());
    if(next!=cpu_account_active)cpu_switches++;
    cpu_account_active=next;
}
void nas_task_cpu_account_snapshot(uint64_t *busy,uint64_t *idle,
                                   uint64_t load[3],uint64_t *switches) {
    cpu_account_sample(nas_arch_monotonic_ns());
    if(busy)*busy=cpu_busy_ns;
    if(idle)*idle=cpu_idle_ns;
    if(load)for(unsigned i=0;i<3;i++)load[i]=load_q16[i];
    if(switches)*switches=cpu_switches;
}
