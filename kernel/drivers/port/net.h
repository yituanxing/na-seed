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
struct nas_netdev_ops {
    /* On success copy/retain the frame in driver-owned storage BEFORE
     * returning: the caller's pbuf/temporary frame may be freed at once.
     * Return -EAGAIN for ring backpressure; do not silently drop as success.
     */
    int (*transmit)(void *backend, const uint8_t *frame, uint32_t bytes);
    /* Invoke receive synchronously up to 'budget' times. Each frame buffer
     * remains valid only during its callback; the stack copies into pbufs.
     * Returns frames consumed or -errno; never transfers RX DMA ownership.
     */
    int (*poll_receive)(void *backend, uint32_t budget,
                        nas_netdev_rx receive, void *consumer);
    bool (*link_up)(void *backend);
};
struct nas_netdev {
    const char *name; /* driver-owned, valid until successful unregister */
    uint8_t mac[NAS_NETDEV_MAC_BYTES];
    uint16_t mtu;     /* IP payload, Ethernet framing is driver-owned */
    void *backend;
    const struct nas_netdev_ops *ops;
    uint32_t refs;
    bool registered;
};
int nas_netdev_register(struct nas_netdev *device);
int nas_netdev_unregister(struct nas_netdev *device);
/* Acquires one device reference; a live netif must call put on removal. */
struct nas_netdev *nas_netdev_get(const char *name);
int nas_netdev_put(struct nas_netdev *device);
int nas_netdev_transmit(struct nas_netdev *device,
                        const uint8_t *frame, uint32_t bytes);
int nas_netdev_poll_receive(struct nas_netdev *device, uint32_t budget,
                            nas_netdev_rx receive, void *consumer);
bool nas_netdev_link_up(const struct nas_netdev *device);
#endif
