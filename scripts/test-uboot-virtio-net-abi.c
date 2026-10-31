/* Compile the unmodified original U-Boot VirtIO-Net driver through the
 * SAME minimal device-model headers as the existing original block driver.
 * ABI only: this is not yet an active NIC or Ethernet acceptance test. */
#include <stddef.h>
#include <stdint.h>
#include "../ports/uboot/compat/virtio_types.h"
#include "../third_party/uboot/drivers/virtio/virtio_net.h"
_Static_assert(sizeof(struct virtio_net_hdr)==10,"wire legacy header");
_Static_assert(sizeof(struct virtio_net_hdr_v1)==12,"wire modern header");
_Static_assert(sizeof(struct virtio_net_config)==17,"wire packed config");
_Static_assert(offsetof(struct virtio_net_config,status)==6,"MAC/config offset");
_Static_assert(offsetof(struct virtio_net_config,mtu)==10,"MTU/config offset");
_Static_assert(VIRTIO_NET_F_MAC==5,"feature bit");
int main(void){return 0;}
