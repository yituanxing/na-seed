#ifndef NASEED_E1000_QEMU_H
#define NASEED_E1000_QEMU_H

#include "../port/dma.h"
#include "../port/net.h"
#include <stdbool.h>
#include <stdint.h>

/* Intel 82540EM/QEMU-only bounded Ethernet MAC. "readonly" is the
 * historical H-series filename, NOT a claim that Ethernet has no TX.
 * Platform code must qualify the exact emulated PCI function and BAR0
 * before supplying these callbacks. There is no physical E1000 support. */
#define NAS_E1000_MMIO_BYTES 0x20000U
#define NAS_E1000_RX_DESCRIPTORS 8U
#define NAS_E1000_TX_DESCRIPTORS 8U
#define NAS_E1000_RX_BUFFER_PAGES 4U
#define NAS_E1000_DMA_BUFFER_BYTES 2048U
#define NAS_E1000_FRAME_MAX 1518U

struct nas_e1000_io {
    void *context;
    uint32_t mmio_bytes;
    int (*read32)(void *,uint32_t,uint32_t *);
    int (*write32)(void *,uint32_t,uint32_t);
};

struct nas_e1000_session {
    const struct nas_e1000_io *io;
    const struct nas_dma_provider *dma;
    struct nas_dma_lease lease;
    struct nas_dma_page rx_ring,tx_ring;
    struct nas_dma_page rx_pages[NAS_E1000_RX_BUFFER_PAGES];
    struct nas_dma_page tx_page;
    uint8_t mac[6];
    uint8_t rx_head,tx_head;
    bool tx_pending,ready,may_dma;
    bool in_rx_poll,in_tx,in_irq,closing;
};

int nas_e1000_open(const struct nas_e1000_io *,
                   const struct nas_dma_provider *,
                   struct nas_e1000_session *);
int nas_e1000_close(struct nas_e1000_session *);
int nas_e1000_transmit(void *,const uint8_t *,uint32_t);
int nas_e1000_poll(void *,uint32_t,nas_netdev_rx,void *);
bool nas_e1000_link_up(void *);
/* IRQ caller must own a live device reference; this only reads/acks ICR.
 * It never performs RX delivery or calls lwIP from the interrupt handler. */
int nas_e1000_irq_ack(struct nas_e1000_session *);
#endif
