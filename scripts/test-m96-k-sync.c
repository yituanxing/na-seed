#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "../kernel/task/kthread.h"
#include "../kernel/task/k_sync.h"
static struct nas_ksem sem;
static struct nas_kmailbox box;
static unsigned stage;
void nas_print(const char *s){fputs(s,stdout);}
__attribute__((noreturn)) void nas_arch_halt(void){abort();}
static void consumer(void *ignored){
    (void)ignored;
    assert(stage==0);
    stage=1;
    assert(nas_ksem_wait(&sem));
    assert(stage==2);
    void *message=0;
    assert(nas_kmailbox_recv(&box,&message));
    assert(message==(void *)(uintptr_t)0x9910);
    stage=3;
}
static void producer(void *ignored){
    (void)ignored;
    assert(stage==1);
    assert(nas_kmailbox_try_send(&box,(void *)(uintptr_t)0x9910));
    stage=2;
    assert(nas_ksem_post(&sem));
}
static void waiting_receiver(void *ignored){
    (void)ignored;
    assert(stage==3);
    stage=4;
    void *message=0;
    assert(nas_kmailbox_recv(&box,&message));
    assert(message==0);
    stage=5;
}
static void waiting_sender(void *ignored){
    (void)ignored;
    assert(stage==5);
    stage=6;
    assert(nas_kmailbox_send(&box,(void *)(uintptr_t)0xbeef));
    stage=7;
}
int main(void){
    nas_kthread_system_init();
    nas_ksem_init(&sem,0);
    nas_kmailbox_init(&box);
    assert(!nas_ksem_trywait(&sem) && !nas_ksem_wait(&sem));
    assert(!nas_kmailbox_try_recv(&box,0));
    assert(!nas_kmailbox_recv(&box,0));
    struct nas_kthread_handle a,b;
    assert(nas_kthread_spawn(consumer,0,&a));
    assert(nas_kthread_spawn(producer,0,&b));
    assert(nas_kthread_run(1)==1 && stage==1 && sem.waiters.count==1);
    assert(nas_kthread_run(1)==1 && stage==2 && sem.waiters.count==0);
    assert(nas_kthread_run(1)==1 && stage==3 && !nas_kthread_live());
    assert(nas_kthread_spawn(waiting_receiver,0,&a));
    assert(nas_kthread_run(1)==1 && stage==4 && box.readable.count==1);
    assert(nas_kmailbox_try_send(&box,0)); /* IRQ-style producer, NULL is data */
    assert(box.readable.count==0);
    assert(nas_kthread_run(1)==1 && stage==5 && !nas_kthread_live());
    for(unsigned i=0;i<NAS_KMAILBOX_CAP;i++)
        assert(nas_kmailbox_try_send(&box,(void *)(uintptr_t)(i+1)));
    assert(!nas_kmailbox_try_send(&box,0));
    assert(nas_kthread_spawn(waiting_sender,0,&b));
    assert(nas_kthread_run(1)==1 && stage==6 && box.writable.count==1);
    void *message=0;
    assert(nas_kmailbox_try_recv(&box,&message) && message==(void *)(uintptr_t)1);
    assert(box.writable.count==0);
    assert(nas_kthread_run(1)==1 && stage==7);
    for(unsigned i=2;i<=NAS_KMAILBOX_CAP;i++){
        assert(nas_kmailbox_try_recv(&box,&message));
        assert(message==(void *)(uintptr_t)i);
    }
    assert(nas_kmailbox_try_recv(&box,&message) && message==(void *)(uintptr_t)0xbeef);
    assert(!nas_kmailbox_try_recv(&box,&message));
    assert(!nas_kthread_live());
    nas_ksem_init(&sem,UINT32_MAX);
    assert(!nas_ksem_post(&sem));
    assert(nas_ksem_trywait(&sem));
    assert(nas_ksem_post(&sem));
    nas_kthread_system_init();
    puts("[M96 KSYNC PASS] semaphore sleep/wake, mailbox full/empty, FIFO, NULL payload and IRQ-like producers");
    return 0;
}
