#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "../kernel/task.h"
#include "../kernel/task/sched_core.h"
#include "../kernel/task/scheduler_service.h"
#include "../kernel/errno.h"

/* The real domain-fairness policy and scheduler-service composition run here.
 * Only architecture time, worker C-stack execution and bus removal are
 * replaced: actual yield/wait lifetime remains covered by M95/M96/M97. */
static struct nas_task tasks[NAS_TASK_LIMIT];
static unsigned stage=3, worker_calls, worker_slices, timer_ticks, bus_calls;
static unsigned before_calls, after_calls;
static char order[16];
static unsigned order_count;

struct nas_task *nas_task_at(unsigned slot) {
    return slot<NAS_TASK_LIMIT ? &tasks[slot] : NULL;
}
bool nas_task_wait_ready(const struct nas_task *task) {
    return task && task->state==NAS_TASK_BLOCKED &&
           task->wait.futex_woken;
}
static void mark(char c) {
    assert(order_count<sizeof(order));
    order[order_count++]=c;
}
uint64_t nas_arch_monotonic_ns(void) {return (uint64_t)(timer_ticks+1)*10000000U;}
unsigned nas_kthread_timers_tick(uint64_t now) {
    assert(now==(uint64_t)(timer_ticks+1)*10000000U);
    ++timer_ticks;
    mark('T');
    return 0;
}
bool nas_kthread_runnable(void) {return stage<3;}
unsigned nas_kthread_run(unsigned budget) {
    /* The single native RX wake creates a dependent core wake, then a
     * Netconn wake. All three must advance within one bounded invocation. */
    assert(budget==4U);
    ++worker_calls;
    mark('K');
    unsigned done=0;
    while(done<budget && stage<3) {
        ++stage;
        ++worker_slices;
        ++done;
    }
    return done;
}
unsigned nas_bus_service_removals(unsigned budget) {
    assert(budget==1U);
    ++bus_calls;
    mark('D');
    return 0;
}
static void before(void *arg) {
    assert(!arg);
    ++before_calls;
    mark('B');
    assert(nas_sched_register_deferred(NAS_SCHED_AFTER_WORKERS,
                                       before,NULL)==-EBUSY);
}
static void after(void *arg) {
    assert(!arg);
    ++after_calls;
    mark('A');
}
static void boundary(const char *expected) {
    order_count=0;
    nas_sched_service_kernel();
    unsigned n=0;
    while(expected[n]) {
        assert(n<order_count && order[n]==expected[n]);
        ++n;
    }
    assert(n==order_count);
    assert(before_calls==timer_ticks && after_calls==timer_ticks);
    assert(bus_calls==timer_ticks);
}
int main(void) {
    assert(nas_sched_register_deferred(NAS_SCHED_BEFORE_WORKERS,
                                       before,NULL)==0);
    assert(nas_sched_register_deferred(NAS_SCHED_AFTER_WORKERS,
                                       after,NULL)==0);

    /* A real runnable user retains eligibility after a four-slice turn. */
    tasks[1].state=NAS_TASK_READY;
    tasks[1].resume_valid=true;
    stage=0;
    boundary("BTKAD");
    assert(stage==3 && worker_calls==1 && worker_slices==3);
    assert(nas_sched_has_ready_users());

    /* Even a freshly awakened pipeline cannot take two consecutive kernel
     * turns while a user is ready. The next scheduling boundary is user-only. */
    stage=0;
    boundary("BTAD");
    assert(stage==0 && worker_calls==1 && worker_slices==3);
    boundary("BTKAD");
    assert(stage==3 && worker_calls==2 && worker_slices==6);

    /* No worker readiness: never invoke a meaningless worker batch. */
    boundary("BTAD");
    assert(worker_calls==2);

    /* The kernel does not need to alternate with nonexistent user work. */
    tasks[1].state=NAS_TASK_STOPPED;
    stage=0;
    boundary("BTKAD");
    assert(stage==3 && worker_calls==3);

    /* A woken BLOCKED syscall continuation counts as user work, just as
     * a READY user does. M99's rotating Task scan remains independent. */
    tasks[1].state=NAS_TASK_BLOCKED;
    tasks[1].wait.futex_woken=true;
    stage=0;
    boundary("BTAD");
    assert(stage==0 && worker_calls==3);
    boundary("BTKAD");
    assert(stage==3 && worker_calls==4 && worker_slices==12);

    puts("[SCHED WORKER BURST HOST PASS] four-slice cap, RX/core/bridge chain, user/blocked-wait fairness, timer/deferred/bus order");
    return 0;
}
