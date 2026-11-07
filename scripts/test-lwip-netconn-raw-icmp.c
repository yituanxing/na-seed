/* Real upstream lwIP RAW ICMP echo over native Netconn bridge. The test does
 * not synthesize receive packets or replace lwIP's IP/ICMP implementation. */
#include "../ports/lwip/netconn_bridge.h"
#include "../kernel/fd.h"
#include "../kernel/task/kthread.h"
#include "../kernel/errno.h"
#include "lwip/tcpip.h"
#include "lwip/sys.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t clock_ns=1;
uint64_t nas_arch_monotonic_ns(void){return clock_ns;}
void nas_print(const char *s){fputs(s,stdout);}
__attribute__((noreturn)) void nas_arch_halt(void){abort();}
void *nas_heap_calloc(size_t n,size_t size){return calloc(n,size);}
bool nas_heap_free(void *p){free(p);return true;}
struct nas_open_file nas_open_files[NAS_OPEN_FILE_LIMIT];

static unsigned ready_count;
static void tcpip_ready(void *unused){(void)unused;ready_count++;}
static struct nas_lwip_bridge_result finish(struct nas_lwip_bridge_id id) {
    struct nas_lwip_bridge_result result={0};
    for(unsigned attempts=0;attempts<1000;attempts++) {
        int got=nas_lwip_bridge_take(id,&result);
        assert(got>=0);
        if(got==1)return result;
        clock_ns+=UINT64_C(1000000);
        (void)nas_kthread_run(1);
    }
    fputs("RAW native completion timed out\n",stderr);
    abort();
}
static struct nas_lwip_bridge_result command(struct nas_lwip_bridge_id id,
                      enum nas_lwip_bridge_op op,const uint8_t *bytes,
                      unsigned length,uint32_t addr) {
    const struct nas_lwip_bridge_request request={
        .op=op,.address={.ipv4=addr,.port=0},
        .bytes=bytes,.length=(uint16_t)length
    };
    assert(nas_lwip_bridge_submit(id,&request)==0);
    return finish(id);
}
static uint16_t checksum(const uint8_t *bytes,unsigned len) {
    uint32_t sum=0;
    for(unsigned i=0;i<len;i+=2U) {
        sum+=(uint16_t)((unsigned)bytes[i]<<8|
                          (i+1U<len ? bytes[i+1U]:0U));
    }
    while(sum>>16)sum=(sum&0xffffU)+(sum>>16);
    return (uint16_t)~sum;
}
int main(void) {
    nas_kthread_system_init();
    sys_init();
    tcpip_init(tcpip_ready,NULL);
    assert(nas_lwip_bridge_start());
    assert(nas_kthread_run(1)==1 && ready_count==1);

    struct nas_lwip_bridge_id id={0};
    assert(nas_lwip_bridge_open(7,NAS_LWIP_BRIDGE_RAW_ICMP,&id)==0);
    assert(id.generation && finish(id).status==0);
    const uint8_t echo_template[]={8,0,0,0,0x45,0x67,0,1,'N','a','S','e','e','d'};
    uint8_t echo[sizeof(echo_template)];
    memcpy(echo,echo_template,sizeof(echo));
    uint16_t check=checksum(echo,sizeof(echo));
    echo[2]=(uint8_t)(check>>8);echo[3]=(uint8_t)check;
    assert(checksum(echo,sizeof(echo))==0);

    const uint32_t loopback=UINT32_C(0x7f000001);
    struct nas_lwip_bridge_result sent=command(id,NAS_LWIP_BRIDGE_SEND,
                                             echo,sizeof(echo),loopback);
    assert(sent.status==0 && sent.length==sizeof(echo));
    bool reply=false;
    for(unsigned attempt=0;attempt<1000;attempt++) {
        struct nas_lwip_bridge_result recv=command(id,NAS_LWIP_BRIDGE_RECV,
                                                   NULL,256,0);
        if(recv.status==-EAGAIN || recv.status==-EINPROGRESS)continue;
        assert(recv.status==0 && !recv.truncated);
        assert(recv.peer.ipv4==loopback && recv.peer.port==0);
        assert(recv.length>=20U+sizeof(echo));
        assert((recv.bytes[0]>>4)==4 && recv.bytes[9]==1);
        const unsigned ihl=(recv.bytes[0]&15U)*4U;
        assert(ihl>=20 && recv.length>=ihl+sizeof(echo));
        const uint8_t *icmp=recv.bytes+ihl;
        if(icmp[0]!=0)continue; /* an outbound echo may also be visible */
        assert(icmp[1]==0 && !memcmp(icmp+4,echo+4,sizeof(echo)-4));
        assert(checksum(icmp,sizeof(echo))==0);
        reply=true;
        break;
    }
    assert(reply && "genuine loopback ICMP echo reply never reached RAW PCB");
    assert(nas_lwip_bridge_release(id)==0);
    for(unsigned i=0;i<1000;i++) {
        (void)nas_kthread_run(1);
        if(nas_lwip_bridge_slot_available(id.slot))break;
    }
    assert(nas_lwip_bridge_slot_available(id.slot));
    puts("[LWIP RAW ICMP PASS] IPv4 header, real ICMP echo reply, checksum, peer, asynchronous final close");
    return 0;
}
