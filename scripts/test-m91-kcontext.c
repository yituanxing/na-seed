/* Call real x86 assembly on three distinct kernel stacks; nested C locals
 * must survive switching away and returning from saved call frames. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "../kernel/task/kernel_context.h"
__attribute__((noreturn)) void nas_arch_halt(void){abort();}
void nas_print(const char *str){fputs(str,stdout);}
static struct nas_kernel_context main_context,a_context,b_context;
static _Alignas(16) unsigned char a_stack[8192],b_stack[8192];
static volatile unsigned phase;
static void a(void){
    volatile uint64_t token=UINT64_C(0x123456789a);
    assert(++phase==1);
    nas_arch_kernel_swap(&a_context,&b_context);
    assert(token==UINT64_C(0x123456789a) && ++phase==3);
    nas_arch_kernel_swap(&a_context,&main_context);
    nas_fail("A resumed after finished");
}
static void b(void){
    volatile uint64_t token=UINT64_C(0xfedcba9876);
    assert(++phase==2);
    nas_arch_kernel_swap(&b_context,&a_context);
    assert(token==UINT64_C(0xfedcba9876) && ++phase==4);
    nas_arch_kernel_swap(&b_context,&main_context);
    nas_fail("B resumed after finished");
}
int main(void){
    assert(!nas_kernel_context_prepare(0,a_stack,sizeof(a_stack),a));
    assert(!nas_kernel_context_prepare(&a_context,a_stack,1,a));
    assert(nas_kernel_context_prepare(&a_context,a_stack,sizeof(a_stack),a));
    assert(nas_kernel_context_prepare(&b_context,b_stack,sizeof(b_stack),b));
    nas_kernel_context_reset(&main_context);
    nas_arch_kernel_swap(&main_context,&a_context);
    assert(phase==3);
    nas_arch_kernel_swap(&main_context,&b_context);
    assert(phase==4);
    nas_kernel_context_boot_selftest();
    puts("[M91 HOST PASS] two nested C continuations across three stacks");
    return 0;
}
