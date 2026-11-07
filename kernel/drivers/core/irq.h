#ifndef NASEED_DRIVER_IRQ_H
#define NASEED_DRIVER_IRQ_H
#include <stdbool.h>
#include <stdint.h>

/* A device carries the platform's resolved interrupt wire, not a raw PCI
 * interrupt pin or an ISA-specific guess in its class driver. `hwirq` is
 * the PIC IRQ, PLIC source ID or absolute GIC INTID as appropriate. Only
 * one interrupt domain is active in a given NaSeed kernel image. */
enum nas_irq_domain {
    NAS_IRQ_DOMAIN_NONE = 0,
    NAS_IRQ_DOMAIN_X86_PIC = 1,
    NAS_IRQ_DOMAIN_RISCV_PLIC = 2,
    NAS_IRQ_DOMAIN_ARM_GIC = 3
};
struct nas_irq_route {
    enum nas_irq_domain domain;
    unsigned hwirq;
};

/* `source_ack` MUST check and acknowledge this particular interrupt
 * source before returning nonzero (VirtIO: bit 0 used ring, bit 1 config).
 * A shared IRQ can interrupt without this device asserting its source;
 * zero is a legitimate, unclaimed interrupt, and must not call notify.
 * `notify` runs in hard IRQ context: no polling, blocking, allocation,
 * device teardown or scheduling. Wake an existing worker/set a pending bit.
 * The owner retains this action until nas_irq_unbind succeeds. */
typedef uint32_t (*nas_irq_source_ack_fn)(void *context);
typedef void (*nas_irq_notify_fn)(void *context,uint32_t source_status);
struct nas_irq_action {
    struct nas_irq_route route;
    nas_irq_source_ack_fn source_ack;
    nas_irq_notify_fn notify;
    void *source_context;
    void *notify_context;
    struct nas_irq_action *next;
    bool bound;
};

int nas_irq_bind(struct nas_irq_action *action,struct nas_irq_route route,
                 nas_irq_source_ack_fn source_ack,void *source_context,
                 nas_irq_notify_fn notify,void *notify_context);
/* Idempotent for never-bound/already-unbound actions. Caller owns the
 * IRQ-masked kernel context; once returned, the action can be freed. */
int nas_irq_unbind(struct nas_irq_action *action);
/* Called after the interrupt controller has claimed hwirq and before its
 * controller-level EOI/complete. Returns true iff any source asserted. */
bool nas_irq_dispatch(unsigned hwirq);
/* Count hardware interrupt entries (including timer interrupts) rather
 * than inventing Linux /proc/stat `intr` totals from elapsed wall time.
 * Architecture timer handlers call note_hardware directly; external IRQs
 * are counted by nas_irq_dispatch after the controller claims a source. */
void nas_irq_note_hardware(void);
uint64_t nas_irq_total_snapshot(void);

/* Platform IRQ controller hooks: first binder unmasks, last unbinder masks.
 * Controller claim/EOI is architecture-owned, not performed by this core. */
int nas_arch_irq_route_enable(unsigned hwirq);
void nas_arch_irq_route_disable(unsigned hwirq);
#endif
