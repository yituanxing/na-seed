/* Focused host test of the real VirtIO input consumer's IRQ gate and
 * deferred per-device fairness. Reuse M166's transport/DMA/vring fixture
 * without copying its fake device implementation. Hardware wiring and IRQ
 * controller dispatch are separately checked by the core IRQ host gate. */
#define main m166_legacy_main
#include "test-m166-virtio-input-port.c"
#undef main

static struct nas_irq_action *irq_actions[3];
static struct fake_input *irq_keyboard,*irq_pointer;
static uint32_t keyboard_status,pointer_status;
static bool pass_marker;

void nas_print(const char *str) {
    if (!strcmp(str,"\n[M175 INPUT IRQ ONLY PASS]\n")) pass_marker=true;
    fputs(str,stdout);
}
void nas_dec(uint64_t number) { printf("%llu",(unsigned long long)number); }

int nas_irq_bind(struct nas_irq_action *action,struct nas_irq_route route,
                 nas_irq_source_ack_fn ack,void *ack_context,
                 nas_irq_notify_fn notify,void *notify_context) {
    assert(action && ack && notify && route.hwirq>=1 && route.hwirq<=2);
    assert(route.domain==NAS_IRQ_DOMAIN_X86_PIC);
    assert(!irq_actions[route.hwirq]);
    action->route=route;
    action->source_ack=ack;
    action->source_context=ack_context;
    action->notify=notify;
    action->notify_context=notify_context;
    action->bound=true;
    irq_actions[route.hwirq]=action;
    return 0;
}
int nas_irq_unbind(struct nas_irq_action *action) {
    if (!action->bound) return 0;
    unsigned hwirq=action->route.hwirq;
    assert(hwirq>=1 && hwirq<=2 && irq_actions[hwirq]==action);
    irq_actions[hwirq]=NULL;
    action->bound=false;
    return 0;
}
static uint32_t read_ack_irq(void *context) {
    uint32_t *status=context==irq_keyboard ? &keyboard_status :
                     context==irq_pointer ? &pointer_status : NULL;
    assert(status);
    uint32_t result=*status;
    *status=0;
    return result;
}
static void fire_irq(unsigned hwirq) {
    struct nas_irq_action *action=irq_actions[hwirq];
    assert(action && action->bound);
    uint32_t bits=action->source_ack(action->source_context);
    if (bits) action->notify(action->notify_context,bits);
}

int main(void) {
    struct fake_input keyboard={.features=NAS_VIRTIO_F_VERSION_1};
    struct fake_input tablet={.features=NAS_VIRTIO_F_VERSION_1,.tablet=true};
    irq_keyboard=&keyboard;
    irq_pointer=&tablet;
    struct nas_virtio_transport_ops irq_ops=mock_ops;
    irq_ops.read_ack_irq=read_ack_irq;
    struct nas_virtio_transport ktr={.context=&keyboard,.ops=&irq_ops,
                                    .dma=&nas_dma_qemu_coherent};
    struct nas_virtio_transport ptr={.context=&tablet,.ops=&irq_ops,
                                    .dma=&nas_dma_qemu_coherent};
    struct nas_bus_device kdev={.bus=NAS_BUS_PCI,.device_id=18,
        .resource={.kind=NAS_BUS_RESOURCE_PCI,
            .irq={NAS_IRQ_DOMAIN_X86_PIC,1},
            .view.pci={.config=pci_config,
                       .config_bytes=NAS_BUS_PCI_CONFIG_BYTES,
                       .mapped_bar_mask=1}},.virtio=&ktr};
    struct nas_bus_device pdev={.bus=NAS_BUS_VIRTIO_MMIO,.device_id=18,
        .resource={.kind=NAS_BUS_RESOURCE_VIRTIO_MMIO,
            .irq={NAS_IRQ_DOMAIN_X86_PIC,2},
            .view.mmio={mmio_registers,sizeof(mmio_registers)}},.virtio=&ptr};
    assert(nas_virtio_input_driver_register()==0);
    /* A hardware route with no source ACK cannot quietly become a polling
     * device; failed probe must also release input registration and DMA. */
    struct nas_virtio_transport missing_ack=ktr;
    missing_ack.ops=&mock_ops;
    kdev.virtio=&missing_ack;
    /* A reader can pin InputDevice between its publication and late
     * native IRQ binding failure. The unbound bus probe must keep that
     * class serviceable until the reader drops its pin; next probe then
     * finishes rollback before publishing the replacement instance. */
    pin_new_registration=true;
    assert(bound_driver->probe(&kdev)==-EOPNOTSUPP);
    assert(!irq_actions[1] && !nas_virtio_input_count() && registered==1);
    assert(received[0] && received[0]->registered &&
           received[0]->readers==1 && nas_virtio_input_poll(1)==0);
    assert(bound_driver->probe(&kdev)==-EBUSY);
    assert(registered==1 && received[0]->registered);
    received[0]->readers=0;
    pin_new_registration=false;
    kdev.virtio=&ktr;
    assert(bound_driver->probe(&kdev)==0);
    assert(registered==1 && nas_virtio_input_count()==1);
    assert(bound_driver->probe(&pdev)==0);
    assert(irq_actions[1] && irq_actions[2]);

    /* Probe allows one pending drain to close the pre-bind race, but no
     * queue read is allowed after that without a fresh hardware IRQ. */
    assert(nas_virtio_input_poll(64U)==0);
    for (unsigned i=0;i<12;i++) {
        complete(&keyboard,i,NAS_INPUT_EV_KEY,30U,(int32_t)(i&1U));
        complete(&tablet,i,NAS_INPUT_EV_ABS,0U,(int32_t)i);
    }
    assert(nas_virtio_input_poll(64U)==0);
    assert(event_count==0);
    keyboard_status=NAS_VIRTIO_IRQ_USED_BUFFER;
    pointer_status=NAS_VIRTIO_IRQ_USED_BUFFER;
    fire_irq(1U);
    fire_irq(2U);
    /* One keyboard must never use the pointer's remaining eight-event
     * allowance. A coalesced single IRQ must eventually drain all 12. */
    assert(nas_virtio_input_poll(64U)==16);
    assert(event_count==16);
    assert(nas_virtio_input_poll(64U)==8);
    assert(event_count==24);
    assert(nas_virtio_input_poll(64U)==0);
    nas_virtio_input_irq_report();
    assert(pass_marker);

    received[0]->readers=1;
    assert(bound_driver->remove(&kdev)==-EBUSY && irq_actions[1]);
    received[0]->readers=0;
    assert(bound_driver->remove(&kdev)==0);
    assert(!irq_actions[1] && irq_actions[2]);
    assert(bound_driver->remove(&pdev)==0);
    assert(!irq_actions[2]);
    for(unsigned i=0;i<12;i++) assert(!page_used[i]);
    assert(nas_virtio_input_count()==0);
    puts("[M175 HOST INPUT IRQ PASS] two routed devices, no speculative poll, "
         "12+12 events drained across quota, IRQ teardown after EBUSY");
    return 0;
}
