#include "irq.h"
#include "../../errno.h"
#include <stddef.h>

/* Intrusive owner-controlled actions avoid another allocation/registry and
 * preserve bus->class->transport lifetime across asynchronous unbind. All
 * registration/removal runs in the IRQ-masked single-CPU kernel context.
 * Dispatch only reads this list; callbacks cannot register or unregister. */
static struct nas_irq_action *actions;
static uint64_t hardware_irq_total;

void nas_irq_note_hardware(void) {
    __atomic_fetch_add(&hardware_irq_total,1U,__ATOMIC_RELAXED);
}
uint64_t nas_irq_total_snapshot(void) {
    return __atomic_load_n(&hardware_irq_total,__ATOMIC_RELAXED);
}

static bool valid_route(struct nas_irq_route route) {
    return route.hwirq &&
           (route.domain==NAS_IRQ_DOMAIN_X86_PIC ||
            route.domain==NAS_IRQ_DOMAIN_RISCV_PLIC ||
            route.domain==NAS_IRQ_DOMAIN_ARM_GIC);
}

int nas_irq_bind(struct nas_irq_action *action,struct nas_irq_route route,
                 nas_irq_source_ack_fn source_ack,void *source_context,
                 nas_irq_notify_fn notify,void *notify_context) {
    if(!action || !valid_route(route) || !source_ack || !notify)
        return -EINVAL;
    if(action->bound)return -EEXIST;
    bool first=true;
    for(struct nas_irq_action *p=actions;p;p=p->next) {
        if(p==action)return -EEXIST;
        if(p->route.hwirq!=route.hwirq)continue;
        if(p->route.domain!=route.domain)return -EINVAL;
        first=false;
    }
    /* Install before unmask so an interrupt arriving at enable time can
     * already find an owner. A failed controller hook is fully rolled back. */
    action->route=route;
    action->source_ack=source_ack;
    action->source_context=source_context;
    action->notify=notify;
    action->notify_context=notify_context;
    action->next=actions;
    action->bound=true;
    actions=action;
    if(first) {
        int rc=nas_arch_irq_route_enable(route.hwirq);
        if(rc) {
            actions=action->next;
            *action=(struct nas_irq_action){0};
            return rc;
        }
    }
    return 0;
}

int nas_irq_unbind(struct nas_irq_action *action) {
    if(!action)return -EINVAL;
    if(!action->bound)return 0;
    struct nas_irq_action **link=&actions;
    while(*link && *link!=action)link=&(*link)->next;
    if(!*link)return -EINVAL;
    struct nas_irq_route route=action->route;
    *link=action->next;
    bool last=true;
    for(struct nas_irq_action *p=actions;p;p=p->next) {
        if(p->route.hwirq==route.hwirq) {last=false;break;}
    }
    if(last)nas_arch_irq_route_disable(route.hwirq);
    *action=(struct nas_irq_action){0};
    return 0;
}

bool nas_irq_dispatch(unsigned hwirq) {
    /* Each call follows one actual PIC/PLIC/GIC interrupt-controller claim.
     * An edge whose class has already deasserted is still a hardware IRQ. */
    nas_irq_note_hardware();
    bool claimed=false;
    for(struct nas_irq_action *p=actions;p;p=p->next) {
        if(p->route.hwirq!=hwirq)continue;
        uint32_t source=p->source_ack(p->source_context);
        if(!source)continue;
        claimed=true;
        p->notify(p->notify_context,source);
    }
    return claimed;
}
