/* USB-IF BOT 1.0 / SCSI READ(10) wire-level host model. This fake USB
 * transport checks the actual NaSeed mass-storage driver; only real QEMU
 * xHCI + USB media + original Alpine programs can qualify H11 as complete.
 */
#include "../kernel/drivers/usb/msc.h"
#include "../kernel/drivers/usb/xhci.h"
#include "../kernel/errno.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "[H11 USB MSC HOST FAIL] line %d: %s\n", __LINE__, #x); \
    return 1; \
} } while (0)
#define CBW_BYTES 31U
#define CSW_BYTES 13U
#define TEST_SECTORS 262144U
#define TEST_SECTOR_BYTES 512U

struct bot_fake {
    unsigned cbws,csws,inq,tur,cap,reads,illegal,usb_errors;
    uint32_t last_tag,previous_tag,last_lba,last_sectors,expected_bytes;
    uint8_t opcode;
    bool expecting_data,expecting_csw;
    bool corrupt_signature,corrupt_tag,corrupt_residue,bad_status;
    bool bad_sector_size,small_media,bad_inquiry,interrupt_data;
};
static struct bot_fake fake;

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|
           ((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|
           ((uint32_t)p[2]<<8)|p[3];
}
static void set_le32(uint8_t *p,uint32_t x) {
    p[0]=(uint8_t)x;p[1]=(uint8_t)(x>>8);
    p[2]=(uint8_t)(x>>16);p[3]=(uint8_t)(x>>24);
}
static void set_be32(uint8_t *p,uint32_t x) {
    p[0]=(uint8_t)(x>>24);p[1]=(uint8_t)(x>>16);
    p[2]=(uint8_t)(x>>8);p[3]=(uint8_t)x;
}

int nas_xhci_ro_bulk_out(struct nas_xhci_ro_session *xhci,
                          const void *source,uint32_t bytes) {
    if(!xhci || !source || bytes!=CBW_BYTES || fake.expecting_data ||
       fake.expecting_csw){fake.illegal++;return -EPROTO;}
    const uint8_t *cbw=source;
    if(le32(cbw)!=UINT32_C(0x43425355) || cbw[13]!=0 ||
       cbw[14]==0 || cbw[14]>16 || cbw[12]>0x80U ||
       (fake.cbws && le32(cbw+4)<=fake.previous_tag)){
        fake.illegal++;return -EPROTO;
    }
    fake.last_tag=le32(cbw+4);
    fake.previous_tag=fake.last_tag;
    fake.expected_bytes=le32(cbw+8);
    fake.opcode=cbw[15];
    fake.cbws++;
    switch(fake.opcode) {
    case 0x12: /* SCSI INQUIRY */
        if(cbw[14]!=6 || fake.expected_bytes!=36U || cbw[12]!=0x80U ||
           cbw[19]!=36U){fake.illegal++;return -EPROTO;}
        fake.inq++;break;
    case 0x00: /* SCSI TEST UNIT READY */
        if(cbw[14]!=6 || fake.expected_bytes || cbw[12]!=0){
            fake.illegal++;return -EPROTO;
        }
        fake.tur++;break;
    case 0x25: /* SCSI READ CAPACITY (10) */
        if(cbw[14]!=10 || fake.expected_bytes!=8U || cbw[12]!=0x80U){
            fake.illegal++;return -EPROTO;
        }
        fake.cap++;break;
    case 0x28: /* SCSI READ (10), big-endian LBA and sector count */
        fake.last_lba=be32(cbw+17);
        fake.last_sectors=((uint32_t)cbw[22]<<8)|cbw[23];
        if(cbw[14]!=10 || fake.last_sectors==0U ||
           fake.last_sectors>8U || cbw[12]!=0x80U ||
           fake.expected_bytes!=fake.last_sectors*TEST_SECTOR_BYTES ||
           fake.last_lba>=TEST_SECTORS ||
           fake.last_sectors>TEST_SECTORS-fake.last_lba){
            fake.illegal++;return -EPROTO;
        }
        fake.reads++;break;
    default:
        fake.illegal++;return -EOPNOTSUPP; /* no SCSI WRITE! */
    }
    fake.expecting_data=fake.expected_bytes!=0U;
    fake.expecting_csw=true;
    return 0;
}

int nas_xhci_ro_bulk_in(struct nas_xhci_ro_session *xhci,
                         void *destination,uint32_t bytes) {
    if(!xhci || !destination || !fake.expecting_csw){
        fake.illegal++;return -EPROTO;
    }
    uint8_t *dst=destination;
    if(fake.expecting_data) {
        if(bytes!=fake.expected_bytes){fake.illegal++;return -EPROTO;}
        if(fake.interrupt_data){fake.usb_errors++;return -ETIMEDOUT;}
        memset(dst,0,bytes);
        if(fake.opcode==0x12U) {
            dst[0]=fake.bad_inquiry ? 0x05U : 0x00U;
            dst[4]=31U;
        } else if(fake.opcode==0x25U) {
            set_be32(dst,fake.small_media ? 8191U : TEST_SECTORS-1U);
            set_be32(dst+4U,fake.bad_sector_size ? 4096U : 512U);
        } else if(fake.opcode==0x28U) {
            for(uint32_t s=0;s<fake.last_sectors;s++)
                for(uint32_t k=0;k<TEST_SECTOR_BYTES;k++)
                    dst[s*TEST_SECTOR_BYTES+k]=
                        (uint8_t)((fake.last_lba+s)*13U+k);
        }
        fake.expecting_data=false;
        return 0;
    }
    if(bytes!=CSW_BYTES){fake.illegal++;return -EPROTO;}
    memset(dst,0,bytes);
    set_le32(dst,fake.corrupt_signature ? 0U : UINT32_C(0x53425355));
    set_le32(dst+4U,fake.last_tag+(fake.corrupt_tag ? 1U : 0U));
    set_le32(dst+8U,fake.corrupt_residue ? 1U : 0U);
    dst[12]=fake.bad_status ? 1U : 0U;
    fake.expecting_csw=false;
    fake.csws++;
    return 0;
}

static void reset_fake(void) {memset(&fake,0,sizeof(fake));}

static int test_reads(void) {
    struct nas_xhci_ro_session xhci={0};
    struct nas_usb_msc_ro_session msc={0};
    reset_fake();
    CHECK(nas_usb_msc_ro_open(&xhci,&msc)==0);
    CHECK(msc.ready && msc.xhci==&xhci);
    CHECK(msc.sectors==TEST_SECTORS && msc.sector_bytes==512U);
    CHECK(fake.inq==1U && fake.tur==1U && fake.cap==1U);
    CHECK(fake.cbws==3U && fake.csws==3U);

    uint8_t sector[TEST_SECTOR_BYTES];
    CHECK(nas_usb_msc_ro_read(&msc,111U,1U,sector)==0);
    for(uint32_t k=0;k<TEST_SECTOR_BYTES;k++)
        CHECK(sector[k]==(uint8_t)(111U*13U+k));
    CHECK(fake.reads==1U && fake.last_lba==111U);

    uint8_t many[17U*TEST_SECTOR_BYTES];
    CHECK(nas_usb_msc_ro_read(&msc,250U,17U,many)==0);
    CHECK(fake.reads==4U && fake.last_lba==266U && fake.last_sectors==1U);
    for(uint32_t s=0;s<17U;s++)
        for(uint32_t k=0;k<TEST_SECTOR_BYTES;k++)
            CHECK(many[s*TEST_SECTOR_BYTES+k]==(uint8_t)((250U+s)*13U+k));
    CHECK(fake.illegal==0U && fake.usb_errors==0U);

    unsigned count=fake.cbws;
    CHECK(nas_usb_msc_ro_read(&msc,0,0U,sector)<0);
    CHECK(nas_usb_msc_ro_read(&msc,TEST_SECTORS,1U,sector)<0);
    CHECK(nas_usb_msc_ro_read(&msc,TEST_SECTORS-1U,2U,sector)<0);
    CHECK(nas_usb_msc_ro_read(&msc,UINT64_MAX,1U,sector)<0);
    CHECK(nas_usb_msc_ro_read(&msc,0,1U,NULL)<0);
    CHECK(fake.cbws==count); /* no unvalidated USB I/O on rejected LBAs */
    CHECK(nas_usb_msc_ro_close(&msc)==0);
    CHECK(!msc.ready && !msc.xhci);
    CHECK(nas_usb_msc_ro_read(&msc,0,1U,sector)<0);
    return 0;
}

static int test_rejections(void) {
    struct nas_xhci_ro_session xhci={0};
    struct nas_usb_msc_ro_session msc={0};
    reset_fake();fake.bad_inquiry=true;
    CHECK(nas_usb_msc_ro_open(&xhci,&msc)==-EOPNOTSUPP);
    CHECK(!msc.xhci && !msc.ready && fake.cap==0U);

    reset_fake();memset(&msc,0,sizeof(msc));fake.small_media=true;
    CHECK(nas_usb_msc_ro_open(&xhci,&msc)==-EOPNOTSUPP);
    CHECK(!msc.xhci && !msc.ready);

    reset_fake();memset(&msc,0,sizeof(msc));fake.bad_sector_size=true;
    CHECK(nas_usb_msc_ro_open(&xhci,&msc)==-EOPNOTSUPP);
    CHECK(!msc.xhci && !msc.ready);

    reset_fake();memset(&msc,0,sizeof(msc));fake.corrupt_signature=true;
    CHECK(nas_usb_msc_ro_open(&xhci,&msc)==-EPROTO);
    CHECK(!msc.xhci && !msc.ready);

    reset_fake();memset(&msc,0,sizeof(msc));fake.corrupt_tag=true;
    CHECK(nas_usb_msc_ro_open(&xhci,&msc)==-EPROTO);
    CHECK(!msc.xhci && !msc.ready);

    reset_fake();memset(&msc,0,sizeof(msc));fake.corrupt_residue=true;
    CHECK(nas_usb_msc_ro_open(&xhci,&msc)==-EPROTO);
    CHECK(!msc.xhci && !msc.ready);

    reset_fake();memset(&msc,0,sizeof(msc));fake.bad_status=true;
    CHECK(nas_usb_msc_ro_open(&xhci,&msc)==-EIO);
    CHECK(!msc.xhci && !msc.ready);

    reset_fake();memset(&msc,0,sizeof(msc));fake.interrupt_data=true;
    CHECK(nas_usb_msc_ro_open(&xhci,&msc)==-ETIMEDOUT);
    CHECK(!msc.xhci && !msc.ready);
    CHECK(fake.usb_errors==1U);
    return 0;
}

static int test_post_open_transport_failure(void) {
    struct nas_xhci_ro_session xhci={0};
    struct nas_usb_msc_ro_session msc={0};
    uint8_t sector[512U];
    reset_fake();
    CHECK(nas_usb_msc_ro_open(&xhci,&msc)==0);
    fake.bad_status=true;
    CHECK(nas_usb_msc_ro_read(&msc,1U,1U,sector)==-EIO);
    CHECK(!msc.ready);
    unsigned before=fake.cbws;
    CHECK(nas_usb_msc_ro_read(&msc,2U,1U,sector)<0);
    CHECK(fake.cbws==before);
    CHECK(nas_usb_msc_ro_close(&msc)==0);

    memset(&msc,0,sizeof(msc));reset_fake();
    CHECK(nas_usb_msc_ro_open(&xhci,&msc)==0);
    fake.interrupt_data=true;
    CHECK(nas_usb_msc_ro_read(&msc,3U,1U,sector)==-ETIMEDOUT);
    CHECK(!msc.ready);
    before=fake.cbws;
    CHECK(nas_usb_msc_ro_read(&msc,4U,1U,sector)<0);
    CHECK(fake.cbws==before);
    CHECK(nas_usb_msc_ro_close(&msc)==0);
    return 0;
}

int main(void) {
    CHECK(test_reads()==0);
    CHECK(test_rejections()==0);
    CHECK(test_post_open_transport_failure()==0);
    puts("[H11 USB MSC HOST PASS] actual BOT CBW/data/CSW, SCSI INQUIRY/TUR/CAPACITY10/READ10, 8-sector chunk, BE LBA, safe bounds, no write, malformed tag/signature/residue/status/timeouts; real QEMU still required");
    return 0;
}
