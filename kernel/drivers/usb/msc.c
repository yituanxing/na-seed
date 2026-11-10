#include "msc.h"
#include "xhci.h"
#include "../../errno.h"
#include <stddef.h>
#include <stdint.h>

/* USB-IF Mass Storage Bulk-Only Transport Rev 1.0: CBW 31 bytes; CSW
 * 13 bytes; LSB first. We encode the wire buffers byte by byte and never
 * send a compiler-layout-dependent C structure to a USB controller. */
#define MSC_CBW_BYTES 31U
#define MSC_CSW_BYTES 13U
#define MSC_CBW_SIGNATURE UINT32_C(0x43425355)
#define MSC_CSW_SIGNATURE UINT32_C(0x53425355)
#define MSC_INQUIRY 0x12U
#define MSC_TEST_UNIT_READY 0x00U
#define MSC_READ_CAPACITY10 0x25U
#define MSC_READ10 0x28U
#define MSC_SECTOR_BYTES 512U
#define MSC_MAX_SECTORS_PER_READ 8U

static void store_le32(uint8_t *p,uint32_t v) {
    p[0]=(uint8_t)v;
    p[1]=(uint8_t)(v>>8);
    p[2]=(uint8_t)(v>>16);
    p[3]=(uint8_t)(v>>24);
}
static uint32_t load_le32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|
           ((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static uint32_t load_be32(const uint8_t *p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|
           ((uint32_t)p[2]<<8)|(uint32_t)p[3];
}
static void store_be32(uint8_t *p,uint32_t v) {
    p[0]=(uint8_t)(v>>24);
    p[1]=(uint8_t)(v>>16);
    p[2]=(uint8_t)(v>>8);
    p[3]=(uint8_t)v;
}

/* One synchronous BOT command: CBW OUT -> optional payload IN -> CSW IN.
 * No unchecked CSW status/residue/tag can be considered successful. BOT
 * phase errors are terminal for this read-only session: no unbounded USB
 * reset/retry loops or UEFI-owned endpoint recovery behind the caller. */
static int bot_in(struct nas_usb_msc_ro_session *session,
                  const uint8_t *command,uint8_t command_bytes,
                  uint8_t *result,uint32_t result_bytes) {
    if(!session || !session->xhci || !command ||
       command_bytes==0U || command_bytes>16U ||
       (result_bytes && !result) || result_bytes>4096U)
        return -EINVAL;
    if(session->in_io)return -EBUSY;
    session->in_io=true;

    uint8_t cbw[MSC_CBW_BYTES]={0};
    uint8_t csw[MSC_CSW_BYTES]={0};
    const uint32_t tag=session->next_tag++;
    store_le32(cbw,MSC_CBW_SIGNATURE);
    store_le32(cbw+4U,tag);
    store_le32(cbw+8U,result_bytes);
    cbw[12]=(uint8_t)(result_bytes ? 0x80U : 0U);
    cbw[13]=0U; /* Single qualified LUN 0. */
    cbw[14]=command_bytes;
    for(uint8_t i=0;i<command_bytes;i++)cbw[15U+i]=command[i];

    int rc=nas_xhci_ro_bulk_out(session->xhci,cbw,MSC_CBW_BYTES);
    if(!rc && result_bytes)
        rc=nas_xhci_ro_bulk_in(session->xhci,result,result_bytes);
    if(!rc)
        rc=nas_xhci_ro_bulk_in(session->xhci,csw,MSC_CSW_BYTES);
    if(!rc && (load_le32(csw)!=MSC_CSW_SIGNATURE ||
               load_le32(csw+4U)!=tag ||
               load_le32(csw+8U)!=0U || csw[12]>2U))
        rc=-EPROTO;
    if(!rc && csw[12]!=0U)
        rc=-EIO; /* Nonzero command status or BOT phase error. */

    /* After any failed CBW/data/CSW transaction, including a command
     * failure or STALL, the endpoint phase may be desynchronized. Without
     * a proven BOT class reset / clear-halt recovery path, fail closed:
     * never submit a later disk request using that possibly stale CSW. */
    if(rc)session->ready=false;
    session->in_io=false;
    return rc;
}

int nas_usb_msc_ro_open(struct nas_xhci_ro_session *xhci,
                        struct nas_usb_msc_ro_session *session) {
    if(!xhci || !session || session->xhci || session->in_io ||
       session->ready)return -EINVAL;
    session->xhci=xhci;
    session->next_tag=1U;
    session->sector_bytes=0U;
    session->sectors=0U;

    uint8_t inquiry_cmd[6]={MSC_INQUIRY,0,0,0,36U,0};
    uint8_t inquiry[36]={0};
    int rc=bot_in(session,inquiry_cmd,6U,inquiry,sizeof(inquiry));
    if(!rc && ((inquiry[0]&0x1fU)!=0U || (inquiry[0]&0xe0U)!=0U))
        rc=-EOPNOTSUPP; /* Require directly addressable block disk. */
    if(!rc) {
        const uint8_t tur_cmd[6]={MSC_TEST_UNIT_READY,0,0,0,0,0};
        rc=bot_in(session,tur_cmd,6U,NULL,0U);
    }
    uint8_t capacity[8]={0};
    if(!rc) {
        const uint8_t capacity_cmd[10]={MSC_READ_CAPACITY10,0,0,0,0,0,0,0,0,0};
        rc=bot_in(session,capacity_cmd,10U,capacity,sizeof(capacity));
    }
    if(!rc) {
        const uint32_t last_lba=load_be32(capacity);
        const uint32_t sector_bytes=load_be32(capacity+4U);
        if(last_lba==UINT32_MAX || sector_bytes!=MSC_SECTOR_BYTES ||
           last_lba<16383U)rc=-EOPNOTSUPP;
        else {
            session->sectors=(uint64_t)last_lba+1U;
            session->sector_bytes=sector_bytes;
            session->ready=true;
        }
    }
    if(rc) {
        session->ready=false;
        session->sector_bytes=0U;
        session->sectors=0U;
        session->xhci=NULL;
    }
    return rc;
}

int nas_usb_msc_ro_read(struct nas_usb_msc_ro_session *session,
                        uint64_t lba,uint32_t sectors,void *kernel_dst) {
    if(!session || !session->ready || !session->xhci || !kernel_dst)
        return -ENODEV;
    if(!sectors || !session->sector_bytes ||
       session->sector_bytes!=MSC_SECTOR_BYTES ||
       lba>=session->sectors || (uint64_t)sectors>session->sectors-lba ||
       lba>UINT32_MAX || (uint64_t)sectors>UINT32_MAX-lba)
        return -EINVAL;
    if(session->in_io)return -EBUSY;

    uint8_t *destination=kernel_dst;
    uint32_t completed=0;
    while(completed<sectors) {
        const uint32_t remaining=sectors-completed;
        const uint16_t chunk=(uint16_t)(remaining>MSC_MAX_SECTORS_PER_READ?
                                        MSC_MAX_SECTORS_PER_READ:remaining);
        const uint32_t sector=(uint32_t)(lba+completed);
        uint8_t command[10]={MSC_READ10,0,0,0,0,0,0,0,0,0};
        store_be32(command+2U,sector);
        command[7]=(uint8_t)(chunk>>8);
        command[8]=(uint8_t)chunk;
        int rc=bot_in(session,command,10U,
                      destination+(size_t)completed*MSC_SECTOR_BYTES,
                      (uint32_t)chunk*MSC_SECTOR_BYTES);
        if(rc)return rc;
        completed+=chunk;
    }
    return 0;
}

int nas_usb_msc_ro_close(struct nas_usb_msc_ro_session *session) {
    if(!session || !session->xhci)return -ENODEV;
    if(session->in_io)return -EBUSY;
    /* No SCSI command here. Platform must first detach the block and then
     * have xHCI halt DMA/rings before releasing the controller MMIO claim. */
    session->ready=false;
    session->sector_bytes=0U;
    session->sectors=0U;
    session->xhci=NULL;
    return 0;
}
