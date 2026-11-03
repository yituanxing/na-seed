/* M175: the REAL core IRQ registry must preserve owner-controlled callbacks
 * across a shared level line, with one controller enable and final disable.
 * The host supplies only the architecture MMIO enable/disable operations. */
#include "../kernel/drivers/core/irq.h"
#include "../kernel/errno.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static unsigned enable_count, disable_count, last_line;
static int enable_error;
int nas_arch_irq_route_enable(unsigned hwirq) {
    enable_count++;
    last_line=hwirq;
    return enable_error;
}
void nas_arch_irq_route_disable(unsigned hwirq) {
    disable_count++;
    last_line=hwirq;
}

struct source {
    uint32_t status;
    uint32_t last_notified;
    unsigned reads, notifications;
};
static uint32_t ack(void *ctx) {
    struct source *s=ctx;
    s->reads++;
    uint32_t status=s->status;
    s->status=0; /* host models PCI ISR read-to-clear / MMIO ACK */
    return status;
}
static void notify(void *ctx,uint32_t flags) {
    struct source *s=ctx;
    s->last_notified=flags;
    s->notifications++;
}

int main(void) {
    struct nas_irq_action one={0},two={0},bad={0};
    struct source keyboard={0},mouse={0};
    const struct nas_irq_route pic11={NAS_IRQ_DOMAIN_X86_PIC,11};
    const struct nas_irq_route wrong={NAS_IRQ_DOMAIN_RISCV_PLIC,11};
    assert(nas_irq_bind(&bad,(struct nas_irq_route){0},ack,&keyboard,
                        notify,&keyboard)==-EINVAL);
    assert(enable_count==0);
    assert(nas_irq_bind(&one,pic11,ack,&keyboard,notify,&keyboard)==0);
    assert(enable_count==1 && last_line==11);
    assert(nas_irq_bind(&one,pic11,ack,&keyboard,notify,&keyboard)==-EEXIST);
    assert(nas_irq_bind(&bad,wrong,ack,&mouse,notify,&mouse)==-EINVAL);
    assert(nas_irq_bind(&two,pic11,ack,&mouse,notify,&mouse)==0);
    assert(enable_count==1); /* shared PIC level IRQ: never reenable */
    assert(!nas_irq_dispatch(12));
    assert(!nas_irq_dispatch(11)); /* no guest device claimed this IRQ */
    assert(keyboard.reads==1 && mouse.reads==1);
    assert(!keyboard.notifications && !mouse.notifications);
    keyboard.status=1;
    assert(nas_irq_dispatch(11));
    assert(keyboard.notifications==1 && keyboard.last_notified==1);
    assert(mouse.notifications==0);
    mouse.status=3;
    keyboard.status=2;
    assert(nas_irq_dispatch(11));
    assert(mouse.notifications==1 && mouse.last_notified==3);
    assert(keyboard.notifications==2 && keyboard.last_notified==2);
    assert(nas_irq_unbind(&one)==0);
    assert(disable_count==0);
    assert(!nas_irq_dispatch(11));
    assert(nas_irq_unbind(&two)==0);
    assert(disable_count==1 && last_line==11);
    assert(nas_irq_unbind(&two)==0); /* idempotent final owner teardown */
    assert(disable_count==1);
    enable_error=-EIO;
    assert(nas_irq_bind(&bad,pic11,ack,&keyboard,notify,&keyboard)==-EIO);
    assert(!bad.bound && disable_count==1);
    enable_error=0;
    assert(nas_irq_bind(&bad,pic11,ack,&keyboard,notify,&keyboard)==0);
    assert(enable_count==3);
    assert(nas_irq_unbind(&bad)==0 && disable_count==2);
    puts("[M175 IRQ CORE PASS] shared route, source-specific ACK, spurious, first enable, final mask, rollback");
    return 0;
}
