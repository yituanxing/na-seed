#ifndef NASEED_FS_PACKET_SOCKET_H
#define NASEED_FS_PACKET_SOCKET_H
#include <stdbool.h>
#include <stdint.h>
#include "../mm/device_backing.h"

/* Actual Linux AF_PACKET/SOCK_RAW capture on the registered Ethernet NIC.
 * The Linux syscall layer owns usercopy, endian ABI and FD validation.
 * All helpers below consume only kernel-owned values and byte buffers. */
struct nas_file_ops;
struct nas_packet_ring_request {
    uint32_t block_size,block_nr,frame_size,frame_nr;
};
struct nas_packet_stats {uint32_t packets,drops;};
const struct nas_file_ops *nas_packet_socket_ops(void);
bool nas_packet_socket_is_slot(unsigned slot);
int nas_packet_socket_open(unsigned slot,unsigned type,uint16_t protocol);
int nas_packet_socket_bind(unsigned slot,unsigned ifindex,uint16_t protocol);
int nas_packet_socket_name(unsigned slot,unsigned *ifindex,uint16_t *protocol);
int nas_packet_socket_version(unsigned slot,unsigned *version,bool change);
int nas_packet_socket_reserve(unsigned slot,unsigned *reserve,bool change);
int nas_packet_socket_ring(unsigned slot,const struct nas_packet_ring_request *request);
int nas_packet_socket_mmap_pin(unsigned slot,struct nas_vm_device **device);
int nas_packet_socket_stats(unsigned slot,struct nas_packet_stats *stats);
int nas_packet_socket_detach_filter(unsigned slot);
#endif
