#ifndef NASEED_PORT_NET_H
#define NASEED_PORT_NET_H
#include <stdbool.h>
#include <stdint.h>

/* Ethernet frame boundary for a real, upstream NIC driver. This is NOT an
 * IP stack, TCP implementation, socket registry or VirtIO queue engine.
 * lwIP netif holds a reference while attached. The driver owns all RX/TX
 * DMA and must stop completions before unregistering the device.
 */
#define NAS_NETDEV_LIMIT 8U
#define NAS_NETDEV_MAC_BYTES 6U
typedef int (*nas_netdev_rx)(void *consumer, const uint8_t *frame,
                             uint32_t bytes);
/* IRQ-safe notification: only wake the worker, NEVER parse packets or
 * call lwIP here. A worker sets this after acquiring a device reference. */
typedef void (*nas_netdev_wake)(void *consumer);
struct nas_netdev_ops {
    /* On success copy/retain the frame in driver-owned storage BEFORE
     * returning: the caller's pbuf/temporary frame may be freed at once.
     * Return -EAGAIN for ring backpressure; do not silently drop as success.
     */
    int (*transmit)(void *backend, const uint8_t *frame, uint32_t bytes);
    /* Linux NAPI-like bounded completion owner, not a second packet stack:
     * retire TX completions as needed, deliver at most 'budget' RX frames.
     * Frame storage remains valid only during the callback; lwIP copies it.
     * Returns RX frames consumed or -errno; DMA ownership stays in driver.
     */
    int (*poll)(void *backend, uint32_t budget,
                nas_netdev_rx receive, void *consumer);
    bool (*link_up)(void *backend);
};
struct nas_netdev {
    const char *name; /* driver-owned, valid until successful unregister */
    uint8_t mac[NAS_NETDEV_MAC_BYTES];
    uint16_t mtu;     /* IP payload, Ethernet framing is driver-owned */
    void *backend;
    const struct nas_netdev_ops *ops;
    /* True only after the physical driver binds a real IRQ source. The
     * network worker may then use RX notifications instead of timer poll. */
    bool rx_irq_driven;
    /* One pinned stack consumer; IRQ notification is a tiny scheduling
     * signal. Synchronize listener install/remove with local IRQs masked. */
    nas_netdev_wake rx_wake;
    void *rx_wake_context;
    uint64_t rx_sequence;
    uint32_t refs;
    bool registered;
    bool removing; /* reject new pins while live consumers detach */
};
int nas_netdev_register(struct nas_netdev *device);
/* Linux-like two-phase removal: first make a device unavailable to new
 * consumers, notify the attached owner, then unregister after its final put.
 * Driver DMA memory remains alive until unregister succeeds. */
int nas_netdev_begin_remove(struct nas_netdev *device);
int nas_netdev_unregister(struct nas_netdev *device);
bool nas_netdev_is_removing(const struct nas_netdev *device);
/* One change-event subscriber, not another NIC registry. The callback only
 * wakes the network manager; it never runs lwIP or blocks in a driver. */
int nas_netdev_set_change_wake(nas_netdev_wake wake,void *consumer);
/* Acquires one device reference; a live netif must call put on removal. */
struct nas_netdev *nas_netdev_get(const char *name);
/* Enumerate any NIC driver through the same pinned registry, independent
 * of naming (e.g. eth0, en0, wlan0). Ordinal is among currently registered
 * devices, not a persistent identity; hold the returned reference. */
struct nas_netdev *nas_netdev_get_ordinal(unsigned ordinal);
int nas_netdev_put(struct nas_netdev *device);
int nas_netdev_transmit(struct nas_netdev *device,
                        const uint8_t *frame, uint32_t bytes);
int nas_netdev_poll(struct nas_netdev *device, uint32_t budget,
                   nas_netdev_rx receive, void *consumer);
int nas_netdev_set_rx_wake(struct nas_netdev *device,
                           nas_netdev_wake wake, void *consumer);
/* May be called by a device interrupt; never blocks or switches context. */
void nas_netdev_notify_rx(struct nas_netdev *device);
bool nas_netdev_link_up(const struct nas_netdev *device);
#endif
